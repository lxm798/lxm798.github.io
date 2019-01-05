## tcp 层数据
### tcp_sendmsg
返回的数据是实际copy到sk_buffer中的数据
```c
int tcp_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		size_t size) {
	struct iovec *iov;
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	int iovlen, flags;
	int mss_now, size_goal;
	int sg, err, copied;
	long timeo;
 
	//首先对sock加锁防止下半段中断访问
	lock_sock(sk);
	TCP_CHECK_TIMER(sk);
 
	//对于阻塞的发送模式还需设置超时时间
	flags = msg->msg_flags;
	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
 
	//只有在ESTABLISHED和CLOSE_WAIT状态下对方才能够接收数据，尝试等待连接的建立
	if ((1 << sk->sk_state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT))
		if ((err = sk_stream_wait_connect(sk, &timeo)) != 0)
			goto out_err;
 
	clear_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);
 
	//获取当前的MSS大小
	mss_now = tcp_send_mss(sk, &size_goal, flags);
 
	/*iov可以看做是一个数组，iovlen表示数组的长度。每一项都是一个数据段。*/
 /*struct msghdr和struct iovec在内核的消息通信机制里很常见*/
	iovlen = msg->msg_iovlen;
	iov = msg->msg_iov;//获取第一个iovec
	copied = 0;
 
	err = -EPIPE;
	if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN))
		goto out_err;
 
	sg = sk->sk_route_caps & NETIF_F_SG;
 
	/*循环操作iov，TCP是面向字节流而不是面向数据报的，可以发生粘包现象*/
	/*如果前一个SKB的小于MSS，新的数据可以将部分数据填入旧的SKB中*/
	while (--iovlen >= 0) {
		size_t seglen = iov->iov_len;					//读取该段数据的长度
		unsigned char __user *from = iov->iov_base;<span style="white-space:pre">			</span>//指向数据区域
 
		iov++;
 
		while (seglen > 0) {
			int copy = 0;
			int max = size_goal;
 
			//得到发送队列的尾部的skb，因为尾部才可能有剩余空间
			skb = tcp_write_queue_tail(sk);				
			if (tcp_send_head(sk)) {				//判断是否还有未发送的数据，即sk_send_head是否非空，
				if (skb->ip_summed == CHECKSUM_NONE)
					max = mss_now;
				copy = max - skb->len;
			}
 
			//不能够填充到sk_send_head指向的SKB的话，新建一个SKB
			if (copy <= 0) {
new_segment:
				//首先判断发送队列总长度是否超过发送缓冲区上限，即sk->sk_wmem_queued < sk->sk_sndbuf
				if (!sk_stream_memory_free(sk))
					goto wait_for_sndbuf;		//设置SOCK_NOSPACE标志位后等待空间
 
 
				//分配一个新的SKB封装新的数据，失败的话也要等待空间,select_size返回一个skb连续数据域的长度，在2.6.38中返回的是0，表示数据全部存储在skb尾部的frags中
				skb = sk_stream_alloc_skb(sk,select_size(sk, sg),sk->sk_allocation);
				if (!skb)
					goto wait_for_memory;
 
				if (sk->sk_route_caps & NETIF_F_ALL_CSUM)
					skb->ip_summed = CHECKSUM_PARTIAL;
 
				skb_entail(sk, skb);					//将新的SKB插入发送队列尾部
				copy = size_goal;
				max = size_goal;
			}
 
			if (copy > seglen)
				copy = seglen;
 
			if (skb_tailroom(skb) > 0) {				//判断skb的连续数据存储区是否还有空间
				if (copy > skb_tailroom(skb))<span style="white-space:pre">			</span>//不够长的话放入部分数据
					copy = skb_tailroom(skb);
				if ((err = skb_add_data(skb, from, copy)) != 0)
					goto do_fault;
			} else {
				//这一段是将数据复制到skb尾部的skb_shared_info中的frags，比较复杂，这样做的目的是支持物理上不连续的数据，减少合并操作提高效率
				int merge = 0;
				int i = skb_shinfo(skb)->nr_frags;
				struct page *page = TCP_PAGE(sk);//返回sk最近操作的page，每个frag的数据都是放在page中
				int off = TCP_OFF(sk);
 
				if (skb_can_coalesce(skb, i, page, off) &&
					off != PAGE_SIZE) {
					 /* We can extend the last page
					  * fragment. */
					 merge = 1;
				 } else if (i == MAX_SKB_FRAGS || !sg) {
					 /* Need to add new fragment and cannot
					  * do this because interface is non-SG,
					  * or because all the page slots are
					  * busy. */
					 tcp_mark_push(tp, skb);
					 goto new_segment;
				 } else if (page) {
					 if (off == PAGE_SIZE) { //PAGE被填满了，重置
					 put_page(page);
					 TCP_PAGE(sk) = page = NULL;
					 off = 0;
					}
				 } else {
					 off = 0;
 
					 if (copy > PAGE_SIZE - off) //不够空间的话先拷贝部分数据
					 copy = PAGE_SIZE - off;
 
					 if (!sk_wmem_schedule(sk, copy))
					 goto wait_for_memory;
 
					 if (!page) {
					 /* Allocate new cache page. */
					 if (!(page = sk_stream_alloc_page(sk)))
					 goto wait_for_memory;
				 }
 
				 //具体的数据拷贝过程
				 err = skb_copy_to_page(sk, from, skb, page,
						off, copy);
				 if (err) {
					 /* If this page was new, give it to the
					  * socket so it does not get leaked.
					  */
					 if (!TCP_PAGE(sk)) {
					 TCP_PAGE(sk) = page;
					 TCP_OFF(sk) = 0;
					}
					goto do_error;
				 }
 
 
				 /* Update the skb. */
				 if (merge) {
					 skb_shinfo(skb)->frags[i - 1].size +=
					 copy;
				 } else {
					 skb_fill_page_desc(skb, i, page, off, copy);
					 if (TCP_PAGE(sk)) {
					 get_page(page);
					 } else if (off + copy < PAGE_SIZE) {
					 get_page(page);
					 TCP_PAGE(sk) = page;
					 }
				}
				TCP_OFF(sk) = off + copy;
			}	
			//如果这一次的数据超过了最大窗口的一半，设置PUSH标志调用__tcp_push_pending_frames发送sk_send_head的所有数据
			if (forced_push(tp)) {
				tcp_mark_push(tp, skb);
				__tcp_push_pending_frames(sk, mss_now, TCP_NAGLE_PUSH);
			} else if (skb == tcp_send_head(sk))	//否则如果只有当前的SKB未发送过,调用tcp_push_one发送sk_send_head的第一个数据,即当前SKB
				tcp_push_one(sk, mss_now);
				
			//上面两个判断的结果最终都会调用tcp_write_xmit函数，这个函数会从传入的sock得到sk_send_head，发送该队列上的所有数据
			//因为第二个判断只有一个SKB，所以只发送一个SKB.但是两者传入tcp_write_xmit的参数不一样。前者会发送MTU探测包，后者只发送一个数据包
			continue;
 
wait_for_sndbuf:
			set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
wait_for_memory:
			if (copied)
				tcp_push(sk, flags & ~MSG_MORE, mss_now, TCP_NAGLE_PUSH);
 
			if ((err = sk_stream_wait_memory(sk, &timeo)) != 0)	//等待内存分配
				goto do_error;
 
			mss_now = tcp_send_mss(sk, &size_goal, flags);
		}
	}
 
out:
	if (copied)
		tcp_push(sk, flags, mss_now, tp->nonagle);
	TCP_CHECK_TIMER(sk);
	release_sock(sk);
	return copied;
 
do_fault:
	if (!skb->len) {
		tcp_unlink_write_queue(skb, sk);
 
		tcp_check_send_head(sk, skb);
		sk_wmem_free_skb(sk, skb);
	}
 
do_error:
	if (copied)
		goto out;
out_err:
	err = sk_stream_error(sk, flags, err);
	TCP_CHECK_TIMER(sk);
	release_sock(sk);
	return err;
}

```
## tcp_push
tcp_sendmsg()中，在sock发送缓存不足、系统内存不足或应用层的数据都拷贝完毕等情况下，

都会调用tcp_push()来把已经拷贝到发送队列中的数据给发送出去。

 

tcp_push()主要做了以下事情：

1. 检查是否有未发送过的数据。

2. 检查是否需要设置PSH标志。

3. 检查是否使用了紧急模式。

4. 检查是否需要使用自动阻塞。

5. 尽可能地把发送队列中的skb给发送出去。

```c
static void tcp_push(struct sock *sk, int flags, int mss_now, int nonagle, int size_goal)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct sk_buff *skb;
 
    /* 如果没有未发送过的数据 */
    if (! tcp_send_head(sk))
        return;
 
    /* 发送队列的最后一个skb */
    skb = tcp_write_queue_tail(sk);
 
    /* 如果接下来没有更多的数据需要发送，或者距离上次PUSH后又有比较多的数据，
     * 那么就需要设置PSH标志，让接收端马上把接收缓存中的数据提交给应用程序。
     */
    if (! (flags & MSG_MORE) || forced_push(tp))
        tcp_mark_push(tp, skb);
 
    /* 如果设置了MSG_OOB标志，就记录紧急指针 */
    tcp_mark_urg(tp, flags);
 
    /* 如果需要自动阻塞小包 */
    if (tcp_should_autocork(sk, skb, size_goal)) {
        /* avoid atomic op if TSQ_THROTTED bit is already set, 设置阻塞标志位 */
        if (! test_bit(TSQ_THROTTLED, &tp->tsq_flags)) {
            NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTOCORKING);
            set_bit(TSQ_THROTTLED, &tp->tsq_flags);
        }
       
        /* It is possible TX completion already happened before we set TSQ_THROTTED.
         * 我的理解是，当提交给IP层的数据包都发送出去后，sk_wmem_alloc的值就会变小，
         * 此时这个条件就为假，之后可以发送被阻塞的数据包了。
         */
        if (atomic_read(&sk->sk_wmem_alloc) > skb->truesize)
            return;
    }
 
    /* 如果之后还有更多的数据，那么使用TCP CORK，显式地阻塞发送 */
    if (flags & MSG_MORE)
        nonagle = TCP_NAGLE_CORK;
 
    /* 尽可能地把发送队列中的skb发送出去。
     * 如果发送失败，检查是否需要启动零窗口探测定时器。
     */
    __tcp_push_pending_frames(sk, mss_now, nonagle);
}

```

tcp_push_pending_frames()和__tcp_push_pending_frames()简单的封装了下tcp_write_xmit()。

从tcp_write_xmit()开始，TCP层才真正开始发送数据。 
```c
/* Push out any pending frames which were held back due to TCP_CORK
 * or attempt at coalescing tiny packets.
 * The socket must be locked by the caller.
 */
void __tcp_push_pending_frames(struct sock *sk, unsigned int cur_mss, int nonagle)
{
    /* If we are closed, the bytes will have to remain here.
     * In time closedown will finish, we empty the write queue and
     * all will be happy.
     */
    if (unlikely(sk->sk_state == TCP_CLOSE))
        return;
 
    /* 如果发送失败 */
    if (tcp_write_xmit(sk, cur_mss, nonagle, 0, sk_gfp_atomic(sk, GFP_ATOMIC)))
        tcp_check_probe_timer(sk); /* 检查是否需要启用0窗口探测定时器*/
} 

```
## tcp_write_xmit
https://blog.csdn.net/ctthuangcheng/article/details/42202477
一、tcp_write_xmit()将发送队列上的SBK发送出去，返回值为0表示发送成功。函数执行过程如下：
1、检测拥塞窗口的大小。
2、检测当前报文是否完全处在发送窗口内。
3、检测报文是否使用nagle算法进行发送。
4、通过以上检测后将该SKB发送出去。
5、循环检测发送队列上所有未发送的SKB。

如果不在发送或者接受窗口内，或者受到nagle/塞子的限制，写到sk_buffer后就返回了
```c
static int tcp_write_xmit(struct sock *sk, unsigned int mss_now, int nonagle,
             int push_one, gfp_t gfp)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct sk_buff *skb;
    unsigned int tso_segs, sent_pkts;
    int cwnd_quota;
    int result;
 
    /*sent_pkts用来统计函数中已发送报文总数。*/
    sent_pkts = 0;
 
    if (!push_one) {
        /* Do MTU probing. */
        result = tcp_mtu_probe(sk);
        if (!result) {
            return 0;
        } else if (result > 0) {
            sent_pkts = 1;
        }
    }
    /*13~21首先初始化为0，接着发送一个路径MTU探测报文，如果成功则发送报文数加1。*/
 
    /*如果发送队列不为空，则准备开始发送报文*/
    while ((skb = tcp_send_head(sk))) {
        unsigned int limit;
 
      /*设置有关TSO的信息，包括GSO类型，GSO分段的大小等等。这些信息是准备给软件TSO分段使用的。
        如果网络设备不支持TSO，但又使用了TSO功能，则报文在提交给网络设备之前，需进行软分段，即由代码实现TSO分段。*/
        tso_segs = tcp_init_tso_segs(sk, skb, mss_now);
        BUG_ON(!tso_segs);
 
      /*检测拥塞窗口的大小，如果为0，则说明拥塞窗口已满，目前不能发送。
        拿拥塞窗口和正在网络上传输的包数目相比，如果拥塞窗口还大，则返回拥塞窗口减掉正在网络上传输的包数目剩下的大小。
        该函数目的是判断正在网络上传输的包数目是否超过拥塞窗口，如果超过了，则不发送。
        tcp_cwnd_test()源代码见段二*/
        cwnd_quota = tcp_cwnd_test(tp, skb);
        if (!cwnd_quota)
            break;
       
        /*检测当前报文是否完全处于发送窗口内，如果是则可以发送，否则不能发送*/
        if (unlikely(!tcp_snd_wnd_test(tp, skb, mss_now)))
            break;
 
        /*tso_segs=1表示无需tso分段*/
        if (tso_segs == 1) {
            /*根据nagle算法，计算是否需要发送数据*/
            if (unlikely(!tcp_nagle_test(tp, skb, mss_now,
                         (tcp_skb_is_last(sk, skb) ?
                         nonagle : TCP_NAGLE_PUSH))))
                break;
        } else {
          /*如果需要TSO分段，则检测该报文是否应该延时发送。tcp_tso_should_defer()用来检测GSO段是否需要延时发送。
            在段中有FIN标志，或者不处于open拥塞状态，或者TSO段延时超过2个时钟滴答，或者拥塞窗口和发送窗口的最小值大于64K或三倍的当前有效MSS，
            在这些情况下会立即发送，而其他情况下会延时发送，这样主要是为了减少软GSO分段的次数，以提高性能。*/
            if (!push_one && tcp_tso_should_defer(sk, skb))
                break;
        }
 
        /*limit为再次分段的段长，初始化为当前MSS*/
        limit = mss_now; 
      /*在TSO分片大于1并且不是URG模式下，通过mss_now计算limit的值
        以发送窗口和拥塞窗口的最小值作为分段段长*/
        if (tso_segs > 1 && !tcp_urg_mode(tp))
            limit = tcp_mss_split_point(sk, skb, mss_now,
                         cwnd_quota);
 
        /*如果SKB中的数据长度大于分段段长，则调用tso_fragment()根据该段长进行分段，如果分段失败则暂不发送*/
        if (skb->len > limit &&
         unlikely(tso_fragment(sk, skb, limit, mss_now, gfp)))
            break;
      /*line61~71：根据条件，可能需要对SKB中的报文进行分段处理，分段的报文包括两种：一种是普通的用MSS分段的报文，另一种则是TSO分段的报文。
        能否发送报文主要取决于两个条件：一是报文需完全在发送窗口中，而是拥塞窗口未满。第一种报文，应该不会再分段了，因为在tcp_sendmsg()中创建报文的SKB时已经根据MSS处理了，
        而第二种报文，则一般情况下都会大于MSS，因为通过TSO分段的段有可能大于拥塞窗口的剩余空间，如果是这样，就需要以发送窗口和拥塞窗口的最小值作为段长对报文再次分段。*/
 
      /*更新TCP时间戳，记录此报文发送的时间，
        #define tcp_time_stamp	 ((__u32)(jiffies))*/
        TCP_SKB_CB(skb)->when = tcp_time_stamp;
 
        /*调用tcp_transmit_skb()发送TCP段，其中第三个参数1表示是否需要克隆被发送的报文，详见后续对此函数的分析*/
        if (unlikely(tcp_transmit_skb(sk, skb, 1, gfp)))
            break;
 
        /* Advance the send_head. This one is sent out.
         * This call will increment packets_out.
         */
      /*调用tcp_event_new_data_sent()-->tcp_advance_send_head()更新sk_send_head，即取发送队列中的下一个SKB。
        同时更新snd_nxt，即等待发送的下一个TCP段的序号，然后统计发出但未得到确认的数据报个数。
        最后如果发送该报文前没有需要确认的报文，则复位重传定时器，对本次发送的报文做重传超时计时。*/
        tcp_event_new_data_sent(sk, skb);
 
      /*更新struct tcp_sock中的snd_sml字段。snd_sml表示最近发送的小包(小于MSS的段)的最后一个字节序号，
        在发送成功后，如果报文小于MSS，即更新该字段，主要用来判断是否启动nagle算法*/
        tcp_minshall_update(tp, mss_now, skb);
        sent_pkts++;//更新已发送报文总数
 
        if (push_one)
            break;
    }
 
    /*如果本次有数据发送，则对TCP拥塞窗口进行检查确认。*/
    if (likely(sent_pkts)) {
        tcp_cwnd_validate(sk);
        return 0;
    }
 
    /*如果本次没有数据发送，则根据已发送但未确认的报文数packets_out和sk_send_head返回，packets_out不为零或sk_send_head为空都视为有数据发出，因此返回成功。*/
    return !tp->packets_out && tcp_send_head(sk);
}
```
### tcp_init_tso_segs()函数
该函数根据当前mss的值重新设置数据包中的struct skb_shared_info内的关于GSO的内容项。
```c
static int tcp_init_tso_segs(struct sock *sk, struct sk_buff *skb,
             unsigned int mss_now)
{
    int tso_segs = tcp_skb_pcount(skb);
 
    if (!tso_segs || (tso_segs > 1 && tcp_skb_mss(skb) != mss_now)) {
        tcp_set_skb_tso_segs(sk, skb, mss_now);
        tso_segs = tcp_skb_pcount(skb);
    }
    return tso_segs;
}

```
```c
static inline unsigned int tcp_cwnd_test(struct tcp_sock *tp,
                     struct sk_buff *skb)
{
    u32 in_flight, cwnd;
 
    /* Don't be strict about the congestion window for the final FIN. */
    /*对FIN包不检测，让他通过*/
    if ((TCP_SKB_CB(skb)->flags & TCPHDR_FIN) && tcp_skb_pcount(skb) == 1)
        return 1;
 
    /*计算正在网络上传输的包数目*/
    in_flight = tcp_packets_in_flight(tp);
    /*获取当前拥塞窗口的大小，snd_cwnd表示当前拥塞窗口的大小*/
    cwnd = tp->snd_cwnd;
    if (in_flight < cwnd)
        return (cwnd - in_flight);
 
    return 0;
}
```

## tcp_transmit_skb
https://blog.csdn.net/ctthuangcheng/article/details/42202927
```c
static int tcp_transmit_skb(struct sock *sk, struct sk_buff *skb, int clone_it,
             gfp_t gfp_mask)
{
    const struct inet_connection_sock *icsk = inet_csk(sk);
    struct inet_sock *inet;
    struct tcp_sock *tp;
    struct tcp_skb_cb *tcb;
    struct tcp_out_options opts;
    unsigned tcp_options_size, tcp_header_size;
    struct tcp_md5sig_key *md5;
    struct tcphdr *th;
    int err;
 
    BUG_ON(!skb || !tcp_skb_pcount(skb));
 
    /* If congestion control is doing timestamping, we must
     * take such a timestamp before we potentially clone/copy.
     */
  /*如果拥塞控制需要做时间才有，则必须在克隆或者拷贝报文之前设置一个时间戳。
    linux支持了多达十种拥塞控制算法，但并不是每种算中都需要做时间采样的，
    因此在设置时间戳前先判断当前的拥塞算法是否需要做时间采样。*/
    if (icsk->icsk_ca_ops->flags & TCP_CONG_RTT_STAMP)
        __net_timestamp(skb);
 
    /*根据传递进来的clone_it参数来确定是否需要克隆待发送的报文。*/
    if (likely(clone_it)) {
        /*如果skb已经被clone，则只能复制该skb的数据到新分配的skb中*/
        if (unlikely(skb_cloned(skb)))
            skb = pskb_copy(skb, gfp_mask);
        else
        /*clone新的skb*/
            skb = skb_clone(skb, gfp_mask);
        if (unlikely(!skb))
            return -ENOBUFS;
    }
 
    /*获取INET层和TCP层的传输控制块、skb中的TCP私有数据块。*/
    inet = inet_sk(sk);
    tp = tcp_sk(sk);
    tcb = TCP_SKB_CB(skb);
    memset(&opts, 0, sizeof(opts));
 
    /*根据TCP选项重新调整TCP首部的长度。*/
    /*判断当前TCP报文是否是SYN段，因为有些选项只能出现在SYN报文中，需做特别处理。*/
    if (unlikely(tcb->flags & TCPHDR_SYN))
        tcp_options_size = tcp_syn_options(sk, skb, &opts, &md5);
    else
        tcp_options_size = tcp_established_options(sk, skb, &opts, &md5);
 
    /*tcp首部的总长度等于可选长度加上struct tcphdr。*/
    tcp_header_size = tcp_options_size + sizeof(struct tcphdr);
 
    /*如果已发出但未确认的数据包数目为零，则只初始化拥塞控制，并开始跟踪该连接的RTT。*/
    if (tcp_packets_in_flight(tp) == 0)
        tcp_ca_event(sk, CA_EVENT_TX_START);
    
    /*调用skb_push()在数据部分的头部添加TCP首部，长度即为之前计算得到的那个tcp_header_size，实际上是把data指针往上移。*/
    skb_push(skb, tcp_header_size);
    skb_reset_transport_header(skb);
    /*SKB已添加到发送队列中，但是从SKB的角度去看还不知道他是属于哪个传输控制块，因此调用skb_set_owner_w设置该SKB的宿主。*/
    skb_set_owner_w(skb, sk);
 
    /* Build TCP header and checksum it. */
    /*填充TCP首部中的源端口source、目的端口dest、TCP报文的序号seq、确认序号ack_seq以及各个标志位*/
    th = tcp_hdr(skb);
    th->source        = inet->inet_sport;
    th->dest        = inet->inet_dport;
    th->seq            = htonl(tcb->seq);
    th->ack_seq        = htonl(tp->rcv_nxt);
    *(((__be16 *)th) + 6)    = htons(((tcp_header_size >> 2) << 12) |
                    tcb->flags);
 
    /*分两种情况设置TCP首部的接收窗口的大小*/
    if (unlikely(tcb->flags & TCPHDR_SYN)) {
        /* RFC1323: The window in SYN & SYN/ACK segments
         * is never scaled.
         */
        /*如果是SYN段，则设置接收窗口初始值为rcv_wnd*/
        th->window    = htons(min(tp->rcv_wnd, 65535U));
    } else {
        /*如果是其他的报文，则调用tcp_select_window()计算当前接收窗口的大小。*/
        th->window    = htons(tcp_select_window(sk));
    }
    /*初始化TCP首部的校验码和紧急指针，具体请参考TCP协议中的首部定义。*/
    th->check        = 0;
    th->urg_ptr        = 0;
 
    /* The urg_mode check is necessary during a below snd_una win probe */
    if (unlikely(tcp_urg_mode(tp) && before(tcb->seq, tp->snd_up))) {
        if (before(tp->snd_up, tcb->seq + 0x10000)) {
            th->urg_ptr = htons(tp->snd_up - tcb->seq);
            th->urg = 1;
        } else if (after(tcb->seq + 0xFFFF, tp->snd_nxt)) {
            th->urg_ptr = htons(0xFFFF);
            th->urg = 1;
        }
    }
 
    tcp_options_write((__be32 *)(th + 1), tp, &opts);
    if (likely((tcb->flags & TCPHDR_SYN) == 0))
        TCP_ECN_send(sk, skb, tcp_header_size);
 
#ifdef CONFIG_TCP_MD5SIG
    /* Calculate the MD5 hash, as we have all we need now */
    if (md5) {
        sk_nocaps_add(sk, NETIF_F_GSO_MASK);
        tp->af_specific->calc_md5_hash(opts.hash_location,
                     md5, sk, NULL, skb);
    }
#endif
 
    icsk->icsk_af_ops->send_check(sk, skb);
 
    if (likely(tcb->flags & TCPHDR_ACK))
        tcp_event_ack_sent(sk, tcp_skb_pcount(skb));
 
    if (skb->len != tcp_header_size)
        tcp_event_data_sent(tp, skb, sk);
 
    if (after(tcb->end_seq, tp->snd_nxt) || tcb->seq == tcb->end_seq)
        TCP_ADD_STATS(sock_net(sk), TCP_MIB_OUTSEGS,
             tcp_skb_pcount(skb));
    /*调用发送接口queue_xmit发送报文，进入到ip层，如果失败返回错误码。在TCP中该接口实现函数为ip_queue_xmit()*/
    err = icsk->icsk_af_ops->queue_xmit(skb);
    if (likely(err <= 0))
        return err;
 
    tcp_enter_cwr(sk, 1);
 
    return net_xmit_eval(err);
}
```
http://www.linuxtcpipstack.com/634.html
```c
static int tcp_transmit_skb(struct sock *sk, struct sk_buff *skb, int clone_it,
			    gfp_t gfp_mask)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_sock *inet;
	struct tcp_sock *tp;
	struct tcp_skb_cb *tcb;
	struct tcp_out_options opts;
	unsigned int tcp_options_size, tcp_header_size;
	struct tcp_md5sig_key *md5;
	struct tcphdr *th;
	int err;

	BUG_ON(!skb || !tcp_skb_pcount(skb));
	tp = tcp_sk(sk);

    /* 需要克隆 */
	if (clone_it) {
		skb_mstamp_get(&skb->skb_mstamp);
		TCP_SKB_CB(skb)->tx.in_flight = TCP_SKB_CB(skb)->end_seq
			- tp->snd_una;
		tcp_rate_skb_sent(sk, skb);

        /* 如果skb已经是被克隆过的，那么只能复制 */
		if (unlikely(skb_cloned(skb)))
			skb = pskb_copy(skb, gfp_mask);
        /* 未被克隆过，则克隆之 */
		else
			skb = skb_clone(skb, gfp_mask);

        /* 复制或者克隆失败 */
		if (unlikely(!skb))
			return -ENOBUFS;
	}

	inet = inet_sk(sk);
	tcb = TCP_SKB_CB(skb);
	memset(&opts, 0, sizeof(opts));

    /* 计算syn包tcp选项长度 */
	if (unlikely(tcb->tcp_flags & TCPHDR_SYN))
		tcp_options_size = tcp_syn_options(sk, skb, &opts, &md5);
    /* 计算已连接状态tcp选项长度 */
	else
		tcp_options_size = tcp_established_options(sk, skb, &opts,
							   &md5);
    /* 计算tcp头部长度 */
	tcp_header_size = tcp_options_size + sizeof(struct tcphdr);

	/* if no packet is in qdisc/device queue, then allow XPS to select
	 * another queue. We can be called from tcp_tsq_handler()
	 * which holds one reference to sk_wmem_alloc.
	 *
	 * TODO: Ideally, in-flight pure ACK packets should not matter here.
	 * One way to get this would be to set skb->truesize = 2 on them.
	 */
	skb->ooo_okay = sk_wmem_alloc_get(sk) < SKB_TRUESIZE(1);

	/* If we had to use memory reserve to allocate this skb,
	 * this might cause drops if packet is looped back :
	 * Other socket might not have SOCK_MEMALLOC.
	 * Packets not looped back do not care about pfmemalloc.
	 */
	skb->pfmemalloc = 0;

    /* 加入tcp头 */
	skb_push(skb, tcp_header_size);
	skb_reset_transport_header(skb);

    /* 与控制块解除关联 */
	skb_orphan(skb);

    /* 与控制块建立关联 */
	skb->sk = sk;
	skb->destructor = skb_is_tcp_pure_ack(skb) ? __sock_wfree : tcp_wfree;
	skb_set_hash_from_sk(skb, sk);

    /* 增加分配的内存 */
	atomic_add(skb->truesize, &sk->sk_wmem_alloc);

	skb_set_dst_pending_confirm(skb, sk->sk_dst_pending_confirm);

	/* Build TCP header and checksum it. */
    /* 构造tcp头 */
	th = (struct tcphdr *)skb->data;
	th->source		= inet->inet_sport;
	th->dest		= inet->inet_dport;
	th->seq			= htonl(tcb->seq);
	th->ack_seq		= htonl(tp->rcv_nxt);
	*(((__be16 *)th) + 6)	= htons(((tcp_header_size >> 2) << 12) |
					tcb->tcp_flags);

	th->check		= 0;
	th->urg_ptr		= 0;

	/* The urg_mode check is necessary during a below snd_una win probe */
    /* 紧急模式 */
	if (unlikely(tcp_urg_mode(tp) && before(tcb->seq, tp->snd_up))) {
		if (before(tp->snd_up, tcb->seq + 0x10000)) {
			th->urg_ptr = htons(tp->snd_up - tcb->seq);
			th->urg = 1;
		} else if (after(tcb->seq + 0xFFFF, tp->snd_nxt)) {
			th->urg_ptr = htons(0xFFFF);
			th->urg = 1;
		}
	}

    /* 写入tcp选项 */
	tcp_options_write((__be32 *)(th + 1), tp, &opts);
	skb_shinfo(skb)->gso_type = sk->sk_gso_type;

    /* syn需要选择通告窗口 */
	if (likely(!(tcb->tcp_flags & TCPHDR_SYN))) {
		th->window      = htons(tcp_select_window(sk));
		tcp_ecn_send(sk, skb, th, tcp_header_size);
	} 
    /* 其他需要设置接收窗口 */
    else {
		/* RFC1323: The window in SYN & SYN/ACK segments
		 * is never scaled.
		 */
		th->window	= htons(min(tp->rcv_wnd, 65535U));
	}
#ifdef CONFIG_TCP_MD5SIG
	/* Calculate the MD5 hash, as we have all we need now */
	if (md5) {
		sk_nocaps_add(sk, NETIF_F_GSO_MASK);
		tp->af_specific->calc_md5_hash(opts.hash_location,
					       md5, sk, skb);
	}
#endif
    /* 计算校验和 */
	icsk->icsk_af_ops->send_check(sk, skb);

    /* ack处理，快速模式数量-以及定时器清除 */
	if (likely(tcb->tcp_flags & TCPHDR_ACK))
		tcp_event_ack_sent(sk, tcp_skb_pcount(skb));

    /* 有数据要发送 */
	if (skb->len != tcp_header_size) {
		tcp_event_data_sent(tp, sk);
		tp->data_segs_out += tcp_skb_pcount(skb);
	}

    /* 统计分段数 */
	if (after(tcb->end_seq, tp->snd_nxt) || tcb->seq == tcb->end_seq)
		TCP_ADD_STATS(sock_net(sk), TCP_MIB_OUTSEGS,
			      tcp_skb_pcount(skb));

    /* 发送的总分段数统计 */
	tp->segs_out += tcp_skb_pcount(skb);
    
	/* OK, its time to fill skb_shinfo(skb)->gso_{segs|size} */
    /* skb中分段数统计 */
	skb_shinfo(skb)->gso_segs = tcp_skb_pcount(skb);
	skb_shinfo(skb)->gso_size = tcp_skb_mss(skb);

	/* Our usage of tstamp should remain private */
	skb->tstamp = 0;

	/* Cleanup our debris for IP stacks */
    /* 清空tcb，ip层要使用 */
	memset(skb->cb, 0, max(sizeof(struct inet_skb_parm),
			       sizeof(struct inet6_skb_parm)));

    /* 发送skb */
	err = icsk->icsk_af_ops->queue_xmit(sk, skb, &inet->cork.fl);

    /* 发送成功或失败 */
	if (likely(err <= 0))
		return err;

    /* 拥塞控制 */

    /* 进入cwr */
	tcp_enter_cwr(sk);

    /* 根据err返回成功与否 */
	return net_xmit_eval(err);
}
```

## ip层数据传输
### ip_queue_xmit
```c
/* Note: skb->sk can be different from sk, in case of tunnels */
int ip_queue_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl)
{
	struct inet_sock *inet = inet_sk(sk);
	struct net *net = sock_net(sk);
	struct ip_options_rcu *inet_opt;
	struct flowi4 *fl4;
	struct rtable *rt;
	struct iphdr *iph;
	int res;

	/* Skip all of this if the packet is already routed,
	 * f.e. by something like SCTP.
	 */
	rcu_read_lock();
	inet_opt = rcu_dereference(inet->inet_opt);
	fl4 = &fl->u.ip4;

    /* 获取skb中的路由缓存 */
	rt = skb_rtable(skb);

    /* skb中有缓存则跳转处理 */
	if (rt)
		goto packet_routed;

	/* Make sure we can route this packet. */
    /* 检查控制块中的路由缓存 */
	rt = (struct rtable *)__sk_dst_check(sk, 0);
    /* 缓存过期 */
	if (!rt) {
		__be32 daddr;

		/* Use correct destination address if we have options. */
        /* 目的地址 */
		daddr = inet->inet_daddr;

        /* 严格路由选项 */
		if (inet_opt && inet_opt->opt.srr)
			daddr = inet_opt->opt.faddr;

		/* If this fails, retransmit mechanism of transport layer will
		 * keep trying until route appears or the connection times
		 * itself out.
		 */
		/* 查找路由缓存 */
		rt = ip_route_output_ports(net, fl4, sk,
					   daddr, inet->inet_saddr,
					   inet->inet_dport,
					   inet->inet_sport,
					   sk->sk_protocol,
					   RT_CONN_FLAGS(sk),
					   sk->sk_bound_dev_if);
        /* 失败 */
		if (IS_ERR(rt))
			goto no_route;

        /* 设置控制块的路由缓存 */
		sk_setup_caps(sk, &rt->dst);
	}

    /* 将路由设置到skb中 */
	skb_dst_set_noref(skb, &rt->dst);

packet_routed:
    /* 严格路由选项    &&使用网关，无路由 */
	if (inet_opt && inet_opt->opt.is_strictroute && rt->rt_uses_gateway)
		goto no_route;

	/* OK, we know where to send it, allocate and build IP header. */
    /* 加入ip头 */
	skb_push(skb, sizeof(struct iphdr) + (inet_opt ? inet_opt->opt.optlen : 0));
	skb_reset_network_header(skb);

    /* 构造ip头 */
    iph = ip_hdr(skb);
	*((__be16 *)iph) = htons((4 << 12) | (5 << 8) | (inet->tos & 0xff));
	if (ip_dont_fragment(sk, &rt->dst) && !skb->ignore_df)
		iph->frag_off = htons(IP_DF);
	else
		iph->frag_off = 0;
	iph->ttl      = ip_select_ttl(inet, &rt->dst);
	iph->protocol = sk->sk_protocol;
	ip_copy_addrs(iph, fl4);

	/* Transport layer set skb->h.foo itself. */
    /* 构造ip选项 */
	if (inet_opt && inet_opt->opt.optlen) {
		iph->ihl += inet_opt->opt.optlen >> 2;
		ip_options_build(skb, &inet_opt->opt, inet->inet_daddr, rt, 0);
	}

    /* 设置id */
	ip_select_ident_segs(net, skb, sk,
			     skb_shinfo(skb)->gso_segs ?: 1);

	/* TODO : should we use skb->sk here instead of sk ? */
    /* QOS等级 */
	skb->priority = sk->sk_priority;
	skb->mark = sk->sk_mark;

    /* 输出 */
	res = ip_local_out(net, sk, skb);
	rcu_read_unlock();
	return res;

no_route:
    /* 无路由处理 */
	rcu_read_unlock();
	IP_INC_STATS(net, IPSTATS_MIB_OUTNOROUTES);
	kfree_skb(skb);
	return -EHOSTUNREACH;
}
```

```c
int ip_build_and_send_pkt(struct sk_buff *skb, const struct sock *sk,
			  __be32 saddr, __be32 daddr, struct ip_options_rcu *opt)
{
	struct inet_sock *inet = inet_sk(sk);
	struct rtable *rt = skb_rtable(skb);
	struct net *net = sock_net(sk);
	struct iphdr *iph;

	/* Build the IP header. */
    /* 构造ip头 */
	skb_push(skb, sizeof(struct iphdr) + (opt ? opt->opt.optlen : 0));
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	iph->version  = 4;
	iph->ihl      = 5;
	iph->tos      = inet->tos;
	iph->ttl      = ip_select_ttl(inet, &rt->dst);
	iph->daddr    = (opt && opt->opt.srr ? opt->opt.faddr : daddr);
	iph->saddr    = saddr;
	iph->protocol = sk->sk_protocol;

    /* 分片与否 */
	if (ip_dont_fragment(sk, &rt->dst)) {
		iph->frag_off = htons(IP_DF);
		iph->id = 0;
	} else {
		iph->frag_off = 0;
		__ip_select_ident(net, iph, 1);
	}

    /* 选项 */
	if (opt && opt->opt.optlen) {
		iph->ihl += opt->opt.optlen>>2;
		ip_options_build(skb, &opt->opt, daddr, rt, 0);
	}

    /* QOS优先级 */
	skb->priority = sk->sk_priority;
	skb->mark = sk->sk_mark;

	/* Send it out. */
    /* 输出 */
	return ip_local_out(net, skb->sk, skb);
}
```
```c
void ip_send_unicast_reply(struct sock *sk, struct sk_buff *skb,
			   const struct ip_options *sopt,
			   __be32 daddr, __be32 saddr,
			   const struct ip_reply_arg *arg,
			   unsigned int len)
{
	struct ip_options_data replyopts;
	struct ipcm_cookie ipc;
	struct flowi4 fl4;
	struct rtable *rt = skb_rtable(skb);
	struct net *net = sock_net(sk);
	struct sk_buff *nskb;
	int err;
	int oif;

    /* 获取ip选项 */
	if (__ip_options_echo(&replyopts.opt.opt, skb, sopt))
		return;

	ipc.addr = daddr;
	ipc.opt = NULL;
	ipc.tx_flags = 0;
	ipc.ttl = 0;
	ipc.tos = -1;

    /* 选项存在 */
	if (replyopts.opt.opt.optlen) {
		ipc.opt = &replyopts.opt;

        /* 源路由存在，设置下一跳ip地址为目的地址 */
		if (replyopts.opt.opt.srr)
			daddr = replyopts.opt.opt.faddr;
	}

    /* 输出接口设置 */
	oif = arg->bound_dev_if;
	if (!oif && netif_index_is_l3_master(net, skb->skb_iif))
		oif = skb->skb_iif;

    /* 查路由 */
	flowi4_init_output(&fl4, oif,
			   IP4_REPLY_MARK(net, skb->mark),
			   RT_TOS(arg->tos),
			   RT_SCOPE_UNIVERSE, ip_hdr(skb)->protocol,
			   ip_reply_arg_flowi_flags(arg),
			   daddr, saddr,
			   tcp_hdr(skb)->source, tcp_hdr(skb)->dest,
			   arg->uid);
	security_skb_classify_flow(skb, flowi4_to_flowi(&fl4));
	rt = ip_route_output_key(net, &fl4);
	if (IS_ERR(rt))
		return;


    /* 根据skb更新sk的属性 */
	inet_sk(sk)->tos = arg->tos;

	sk->sk_priority = skb->priority;
	sk->sk_protocol = ip_hdr(skb)->protocol;
	sk->sk_bound_dev_if = arg->bound_dev_if;
	sk->sk_sndbuf = sysctl_wmem_default;
	sk->sk_mark = fl4.flowi4_mark;
    /* 数据追加到前一个skb或者新建skb后添加到发送队列 */
	err = ip_append_data(sk, &fl4, ip_reply_glue_bits, arg->iov->iov_base,
			     len, 0, &ipc, &rt, MSG_DONTWAIT);
	if (unlikely(err)) {
		ip_flush_pending_frames(sk);
		goto out;
	}

    /* 如果发送队列有skb，则计算校验和，发送 */
	nskb = skb_peek(&sk->sk_write_queue);
	if (nskb) {
		if (arg->csumoffset >= 0)
			*((__sum16 *)skb_transport_header(nskb) +
			  arg->csumoffset) = csum_fold(csum_add(nskb->csum,
								arg->csum));
		nskb->ip_summed = CHECKSUM_NONE;

        /* 发送数据包 */
		ip_push_pending_frames(sk, &fl4);
	}
out:
	ip_rt_put(rt);
}
```

### ip_push_pending_frames
个函数会被icmp_push_reply，ip_send_reply，raw_sendmsg，和udp_push_pending_frames调用。该函数用于将该socket上的所有pending的IP分片，组成一个IP报文发送出去。
```c
int ip_push_pending_frames(struct sock *sk)
{
    struct sk_buff *skb, *tmp_skb;
    struct sk_buff **tail_skb;
    struct inet_sock *inet = inet_sk(sk);
    struct net *net = sock_net(sk);
    struct ip_options *opt = NULL;
    struct rtable *rt = (struct rtable *)inet->cork.dst;
    struct iphdr *iph;
    __be16 df = 0;
    __u8 ttl;
    int err = 0;
     /* 发送队列可能为空 */
    if ((skb = __skb_dequeue(&sk->sk_write_queue)) == NULL)
        goto out;
    /* 获得分片链表 */
    tail_skb = &(skb_shinfo(skb)->frag_list);

    /* move skb->data to ip header from ext header */
    /* 调整data指针位置 */
    if (skb->data skb_network_header(skb))
        __skb_pull(skb, skb_network_offset(skb));
     /* 调整所有发送缓冲中的sk_buff的data指针位置，并更新第一个sk_buff的数据长度 */
    while ((tmp_skb = __skb_dequeue(&sk->sk_write_queue)) != NULL) {
        __skb_pull(tmp_skb, skb_network_header_len(skb));
        *tail_skb = tmp_skb;
        tail_skb = &(tmp_skb->next);
        skb->len = tmp_skb->len;
        skb->data_len = tmp_skb->len;
        skb->truesize = tmp_skb->truesize;
        tmp_skb->destructor = NULL;
        tmp_skb->sk = NULL;
    }

    /* Unless user demanded real pmtu discovery (IP_PMTUDISC_DO), we allow
     * to fragment the frame generated here. No matter, what transforms
     * how transforms change size of the packet, it will come out.
     */
    /* 允许本地分片 */
    if (inet->pmtudisc IP_PMTUDISC_DO)
        skb->local_df = 1;

    /* DF bit is set when we want to see DF on outgoing frames.
     * If local_df is set too, we still allow to fragment this frame
     * locally. */
    /* 不允许分片，或者不需要分片 */
    if (inet->pmtudisc >= IP_PMTUDISC_DO ||
     (skb->len = dst_mtu(&rt->dst) &&
     ip_dont_fragment(sk, &rt->dst)))
        df = htons(IP_DF);
      /* ip option 保存在cork中， 则使用cork中的option */
    if (inet->cork.flags & IPCORK_OPT)
        opt = inet->cork.opt;
      /* 选择合适的TTL值 */
    if (rt->rt_type == RTN_MULTICAST)
        ttl = inet->mc_ttl;
    else
        ttl = ip_select_ttl(inet, &rt->dst);
     /* 得到IP报文头的地址 */ 
    iph = (struct iphdr *)skb->data;
    /* 初始化IP报文头的内容 */
    iph->version = 4;
    iph->ihl = 5;
    if (opt) {
        /* 
        填充IP option
        看到这里，可以发现opt只可能从cork中获得
         */
        iph->ihl = opt->optlen>>2;
        ip_options_build(skb, opt, inet->cork.addr, rt, 0);
    }
    iph->tos = inet->tos;
    iph->frag_off = df;
    ip_select_ident(iph, &rt->dst, sk);
    iph->ttl = ttl;
    iph->protocol = sk->sk_protocol;
    iph->saddr = rt->rt_src;
    iph->daddr = rt->rt_dst;

    skb->priority = sk->sk_priority;
    skb->mark = sk->sk_mark;
    /*
     * Steal rt from cork.dst to avoid a pair of atomic_inc/atomic_dec
     * on dst refcount
     */
    inet->cork.dst = NULL;
    skb_dst_set(skb, &rt->dst);

    if (iph->protocol == IPPROTO_ICMP)
        icmp_out_count(net, ((struct icmphdr *)
            skb_transport_header(skb))->type);

    /* Netfilter gets whole the not fragmented skb. */
    /* 发送数据 */
    err = ip_local_out(skb);
    if (err) {
        if (err > 0)
            err = net_xmit_errno(err);
        if (err)
            goto error;
    }

out:
    ip_cork_release(inet);
    return err;

error:
    IP_INC_STATS(net, IPSTATS_MIB_OUTDISCARDS);
    goto out;
}
```
# ip_output
```c
int ip_output(struct sock *sk, struct sk_buff *skb)
{
    struct net_device *dev = skb_dst(skb)->dev;
 
 
    IP_UPD_PO_STATS(dev_net(dev), IPSTATS_MIB_OUT, skb->len);
 
 
    skb->dev = dev;
    skb->protocol = htons(ETH_P_IP);   //设置报文协议为IPV4
 
 
    return NF_HOOK_COND(NFPROTO_IPV4, NF_INET_POST_ROUTING, sk, skb, 
         NULL, dev,
         ip_finish_output,   //报文发送netfilter处理，如果允许则调用ip_finish_output
         !(IPCB(skb)->flags & IPSKB_REROUTED));
}
```

```c
static inline int ip_finish_output2(struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct rtable *rt = (struct rtable *)dst;
	struct net_device *dev = dst->dev;
	unsigned int hh_len = LL_RESERVED_SPACE(dev);
	struct neighbour *neigh;
	u32 nexthop;
 
	if (rt->rt_type == RTN_MULTICAST) {
		IP_UPD_PO_STATS(dev_net(dev), IPSTATS_MIB_OUTMCAST, skb->len);
	} else if (rt->rt_type == RTN_BROADCAST)
		IP_UPD_PO_STATS(dev_net(dev), IPSTATS_MIB_OUTBCAST, skb->len);
 
	/* Be paranoid, rather than too clever. */
	if (unlikely(skb_headroom(skb) < hh_len && dev->header_ops)) {
		struct sk_buff *skb2;
 
		skb2 = skb_realloc_headroom(skb, LL_RESERVED_SPACE(dev));
		if (!skb2) {
			kfree_skb(skb);
			return -ENOMEM;
		}
		if (skb->sk)
			skb_set_owner_w(skb2, skb->sk);
		consume_skb(skb);
		skb = skb2;
	}
 
	rcu_read_lock_bh();
	nexthop = (__force u32) rt_nexthop(rt, ip_hdr(skb)->daddr);
	neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
	if (unlikely(!neigh))
		neigh = __neigh_create(&arp_tbl, &nexthop, dev, false);
	if (!IS_ERR(neigh)) {
		int res = dst_neigh_output(dst, neigh, skb);	//调用邻居子系统封装MAC头，并且调用二层发包函数完成报文发送
 
		rcu_read_unlock_bh();
		return res;
	}
	rcu_read_unlock_bh();
 
	net_dbg_ratelimited("%s: No header cache and no neighbour!\n",
			    __func__);
	kfree_skb(skb);
	return -EINVAL;
}

```


##　网卡
### dev_queue_xmit
```c
static int __dev_queue_xmit(struct sk_buff *skb, void *accel_priv)
{
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq;
	struct Qdisc *q;
	int rc = -ENOMEM;
 
	skb_reset_mac_header(skb);
 
	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_SCHED_TSTAMP))
		__skb_tstamp_tx(skb, NULL, skb->sk, SCM_TSTAMP_SCHED);
 
	/* Disable soft irqs for various locks below. Also
	 * stops preemption for RCU.
	 */
	rcu_read_lock_bh();
 
	skb_update_prio(skb);
 
	/* If device/qdisc don't need skb->dst, release it right now while
	 * its hot in this cpu cache.
	 */
	/*这个地方看netdevcie的flag是否要去掉skb DST相关的信息，一般情况下这个flag是默认被设置的
	 *在alloc_netdev_mqs的时候，已经默认给设置了，其实个人认为这个路由信息也没有太大作用了...
	 *dev->priv_flags = IFF_XMIT_DST_RELEASE | IFF_XMIT_DST_RELEASE_PERM;
	 */
	if (dev->priv_flags & IFF_XMIT_DST_RELEASE)
		skb_dst_drop(skb);
	else
		skb_dst_force(skb);
		
        /*此处主要是取出此netdevice的txq和txq的Qdisc,Qdisc主要用于进行拥塞处理，一般的情况下，直接将
         *数据包发送给driver了，如果遇到Busy的状况，就需要进行拥塞处理了，就会用到Qdisc*/
	txq = netdev_pick_tx(dev, skb, accel_priv);
	q = rcu_dereference_bh(txq->qdisc);
 
#ifdef CONFIG_NET_CLS_ACT
	skb->tc_verd = SET_TC_AT(skb->tc_verd, AT_EGRESS);
#endif
	trace_net_dev_queue(skb);
	
	/*如果Qdisc有对应的enqueue规则，就会调用__dev_xmit_skb，进入带有拥塞的控制的Flow，注意这个地方，虽然是走拥塞控制的
	 *Flow但是并不一定非得进行enqueue操作啦，只有Busy的状况下，才会走Qdisc的enqueue/dequeue操作进行
	 */
	if (q->enqueue) {
		rc = __dev_xmit_skb(skb, q, dev, txq);
		goto out;
	}
 
	/* The device has no queue. Common case for software devices:
	   loopback, all the sorts of tunnels...
	   Really, it is unlikely that netif_tx_lock protection is necessary
	   here.  (f.e. loopback and IP tunnels are clean ignoring statistics
	   counters.)
	   However, it is possible, that they rely on protection
	   made by us here.
	   Check this and shot the lock. It is not prone from deadlocks.
	   Either shot noqueue qdisc, it is even simpler 8)
	 */
	 
	/*此处是设备没有Qdisc的，实际上没有enqueue/dequeue的规则，无法进行拥塞控制的操作，
	 *对于一些loopback/tunnel interface比较常见，判断下设备是否处于UP状态*/
	if (dev->flags & IFF_UP) {
		int cpu = smp_processor_id(); /* ok because BHs are off */
 
		if (txq->xmit_lock_owner != cpu) {
 
			if (__this_cpu_read(xmit_recursion) > RECURSION_LIMIT)
				goto recursion_alert;
			skb = validate_xmit_skb(skb, dev);
			if (!skb)
				goto drop;
 
			HARD_TX_LOCK(dev, txq, cpu);
                       /*这个地方判断一下txq不是stop状态，那么就直接调用dev_hard_start_xmit函数来发送数据*/
			if (!netif_xmit_stopped(txq)) {
				__this_cpu_inc(xmit_recursion);
				skb = dev_hard_start_xmit(skb, dev, txq, &rc);
				__this_cpu_dec(xmit_recursion);
				if (dev_xmit_complete(rc)) {
					HARD_TX_UNLOCK(dev, txq);
					goto out;
				}
			}
			HARD_TX_UNLOCK(dev, txq);
			net_crit_ratelimited("Virtual device %s asks to queue packet!\n",
					     dev->name);
		} else {
			/* Recursion is detected! It is possible,
			 * unfortunately
			 */
recursion_alert:
			net_crit_ratelimited("Dead loop on virtual device %s, fix it urgently!\n",
					     dev->name);
		}
	}
 
	rc = -ENETDOWN;
drop:
	rcu_read_unlock_bh();
 
	atomic_long_inc(&dev->tx_dropped);
	kfree_skb_list(skb);
	return rc;
out:
	rcu_read_unlock_bh();
	return rc;
}

```
### __dev_xmit_skb
```c
static inline int __dev_xmit_skb(struct sk_buff *skb, struct Qdisc *q,
				 struct net_device *dev,
				 struct netdev_queue *txq)
{
	spinlock_t *root_lock = qdisc_lock(q);
	bool contended;
	int rc;
 
	qdisc_pkt_len_init(skb);
	qdisc_calculate_pkt_len(skb, q);
	/*
	 * Heuristic to force contended enqueues to serialize on a
	 * separate lock before trying to get qdisc main lock.
	 * This permits __QDISC___STATE_RUNNING owner to get the lock more
	 * often and dequeue packets faster.
	 */
	contended = qdisc_is_running(q);
	if (unlikely(contended))
		spin_lock(&q->busylock);
 
	spin_lock(root_lock);
	
	/*这个地方主要是判定Qdisc的state: __QDISC_STATE_DEACTIVATED,如果处于非活动的状态，就DROP这个包，返回NET_XMIT_DROP
	 *一般情况下带有Qdisc策略的interface，在被close的时候才会打上这个flag */
	if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED, &q->state))) {
		kfree_skb(skb);
		rc = NET_XMIT_DROP;
	} else if ((q->flags & TCQ_F_CAN_BYPASS) && !qdisc_qlen(q) &&
		   qdisc_run_begin(q)) {
		/*
		 * This is a work-conserving queue; there are no old skbs
		 * waiting to be sent out; and the qdisc is not running -
		 * xmit the skb directly.
		 */
                /* 结合注释以及code来看，此处必须满足3个调节才可以进来，
                 * 1.flag必须有TCQ_F_CAN_BYPASS，默认条件下是有的，表明可以By PASS Qdisc规则
                 * 2.q的len为0，也就是说Qdisc中一个包也没有
                 * 3.Qdisc 起初并没有处于running的状态，然后置位Running！
                 * 满足上述3个条件调用sch_direct_xmit
                 */
		qdisc_bstats_update(q, skb);
 
                /*这个函数*/
		if (sch_direct_xmit(skb, q, dev, txq, root_lock, true)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
		} else
			qdisc_run_end(q);
 
		rc = NET_XMIT_SUCCESS;
	} else {
	
	   /*如果上述3个条件其中任何一个或者多个不满足，就要进行enqueue操作了，这个地方其实就是表明通讯出现拥塞，需要进行管理了
	    *如果q不是运行状态，就设置成运行状况，如果一直是运行状态，那么就不用管了！*/
		rc = q->enqueue(skb, q) & NET_XMIT_MASK;
		if (qdisc_run_begin(q)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
		}
	}
	spin_unlock(root_lock);
	if (unlikely(contended))
		spin_unlock(&q->busylock);
	return rc;
}
```

### dev_hard_start_xmit
```c
struct sk_buff *dev_hard_start_xmit(struct sk_buff *first, struct net_device *dev,
				    struct netdev_queue *txq, int *ret)
{
	struct sk_buff *skb = first;
	int rc = NETDEV_TX_OK;
       /*此处skb为什么会有链表呢？*/
	while (skb) {
	      /*取出skb的下一个数据单元*/
		struct sk_buff *next = skb->next;
              /*置空，待发送数据包的next*/
		skb->next = NULL;
		
	       /*将此数据包送到driver Tx函数，因为dequeue的数据也会从这里发送，所以会有netx！*/
		rc = xmit_one(skb, dev, txq, next != NULL);
		
		/*如果发送不成功，next还原到skb->next 退出*/
		if (unlikely(!dev_xmit_complete(rc))) {
			skb->next = next;
			goto out;
		}
                /*如果发送成功，把next置给skb，一般的next为空 这样就返回，如果不为空就继续发！*/
		skb = next;
		
		/*如果txq被stop，并且skb需要发送，就产生TX Busy的问题！*/
		if (netif_xmit_stopped(txq) && skb) {
			rc = NETDEV_TX_BUSY;
			break;
		}
	}
 
out:
	*ret = rc;
	return skb;
}

static int xmit_one(struct sk_buff *skb, struct net_device *dev,
		    struct netdev_queue *txq, bool more)
{
	unsigned int len;
	int rc;
	
	/*如果有抓包的工具的话，这个地方会进行抓包，such as Tcpdump*/
	if (!list_empty(&ptype_all))
		dev_queue_xmit_nit(skb, dev);
 
	len = skb->len;
	trace_net_dev_start_xmit(skb, dev);
        /*调用netdev_start_xmit，快到driver的tx函数了*/
	rc = netdev_start_xmit(skb, dev, txq, more);
	trace_net_dev_xmit(skb, rc, dev, len);
 
	return rc;
}
 
static inline netdev_tx_t netdev_start_xmit(struct sk_buff *skb, struct net_device *dev,
					    struct netdev_queue *txq, bool more)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int rc;
	/*__netdev_start_xmit 里面就完全是使用driver 的ops去发包了，其实到此为止，一个skb已经从netdevice
	 *这个层面送到driver层了，接下来会等待driver的返回*/
	rc = __netdev_start_xmit(ops, skb, dev, more);
	
	/*如果返回NETDEV_TX_OK，那么会更新下Txq的trans时间戳哦，txq->trans_start = jiffies;*/
	if (rc == NETDEV_TX_OK)
		txq_trans_update(txq);
 
	return rc;
}
 
static inline netdev_tx_t __netdev_start_xmit(const struct net_device_ops *ops,
					      struct sk_buff *skb, struct net_device *dev,
					      bool more)
{
	skb->xmit_more = more ? 1 : 0;
	return ops->ndo_start_xmit(skb, dev);
}

```

http://blog.51cto.com/yaoyang/1359420