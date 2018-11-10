# futex 源码分析

## futex数据结构
futex_q futex等待队列
```c
/**
 * struct futex_q - The hashed futex queue entry, one per waiting task
 * @task:       the task waiting on the futex
 * @lock_ptr:       the hash bucket lock
 * @key:        the key the futex is hashed on
 * @pi_state:       optional priority inheritance state
 * @rt_waiter:      rt_waiter storage for use with requeue_pi
 * @requeue_pi_key: the requeue_pi target futex key
 * @bitset:     bitset for the optional bitmasked wakeup
 *
 * We use this hashed waitqueue, instead of a normal wait_queue_t, so
 * we can wake only the relevant ones (hashed queues may be shared).
 *
 * A futex_q has a woken state, just like tasks have TASK_RUNNING.
 * It is considered woken when plist_node_empty(&q->list) || q->lock_ptr == 0.
 * The order of wakup is always to make the first condition true, then
 * the second.
 *
 * PI futexes are typically woken before they are removed from the hash list via
 * the rt_mutex code. See unqueue_me_pi().
 */
struct futex_q {
    struct plist_node list;
  
    struct task_struct *task;
    spinlock_t *lock_ptr;
    union futex_key key;
    struct futex_pi_state *pi_state;
    struct rt_mutex_waiter *rt_waiter;
    union futex_key *requeue_pi_key;
    u32 bitset;
};

union futex_key {                                                 
    struct {
        unsigned long pgoff;
        struct inode *inode;
        int offset;
    } shared;
    struct {
        // 用户态futex 地址
        unsigned long address;
        // addr 所在mm
        struct mm_struct *mm;
        // 用户态地址页内偏移
        int offset;
    } private;
    struct {
        unsigned long word;
        void *ptr;
        int offset;
    } both;
};

/*              
 * Hash buckets are shared by all the futex_keys that hash to the same
 * location.  Each key may have multiple futex_q structures, one for each task
 * waiting on a futex.
 */
struct futex_hash_bucket {
    spinlock_t lock;
    struct plist_head chain;
};

static struct futex_hash_bucket futex_queues[1<<FUTEX_HASHBITS];
```


## futex 流程
```c
static int futex_wait(u32 __user *uaddr, int fshared,
              u32 val, ktime_t *abs_time, u32 bitset, int clockrt)
{   // 高精度定时器
    struct hrtimer_sleeper timeout, *to = NULL;
    struct restart_block *restart;
    struct futex_hash_bucket *hb;
    struct futex_q q;
    int ret;
           
    if (!bitset)
        return -EINVAL;
           
    q.pi_state = NULL;
    q.bitset = bitset;
    q.rt_waiter = NULL;
    q.requeue_pi_key = NULL;
    // 设置等待超时
    if (abs_time) {
        to = &timeout;
           
        hrtimer_init_on_stack(&to->timer, clockrt ? CLOCK_REALTIME :
                      CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
        hrtimer_init_sleeper(to, current);
        hrtimer_set_expires_range_ns(&to->timer, *abs_time,
                         current->timer_slack_ns);
    }      
           
retry:     
    /* Prepare to wait on uaddr. */
    ret = futex_wait_setup(uaddr, val, fshared, &q, &hb);
    // 用户态数据已经和val一致
    if (ret)
        goto out;
           
    /* queue_me and wait for wakeup, timeout, or a signal. */
    futex_wait_queue_me(hb, &q, to);
           
    /* If we were woken (and unqueued), we succeeded, whatever. */
    ret = 0;
    if (!unqueue_me(&q))
        goto out_put_key;
    ret = -ETIMEDOUT;
    if (to && !to->task)
        goto out_put_key;
           
    /*
     * We expect signal_pending(current), but we might be the
     * victim of a spurious wakeup as well.
     */
         if (!signal_pending(current)) {
        put_futex_key(fshared, &q.key);
        goto retry;                                                                                                                                                                                                
    }    
         
    ret = -ERESTARTSYS;
    if (!abs_time)
        goto out_put_key;
         
    restart = &current_thread_info()->restart_block;
    restart->fn = futex_wait_restart;
    restart->futex.uaddr = (u32 *)uaddr;
    restart->futex.val = val;
    restart->futex.time = abs_time->tv64;
    restart->futex.bitset = bitset;
    restart->futex.flags = FLAGS_HAS_TIMEOUT;
         
    if (fshared)
        restart->futex.flags |= FLAGS_SHARED;
    if (clockrt)
        restart->futex.flags |= FLAGS_CLOCKRT;
         
    ret = -ERESTART_RESTARTBLOCK;
         
out_put_key:
    put_futex_key(fshared, &q.key);
out:     
    if (to) {
        hrtimer_cancel(&to->timer);
        destroy_hrtimer_on_stack(&to->timer);
    }    
    return ret;
}
```


```c
/**                                                                  
 * futex_wait_setup() - Prepare to wait on a futex
 * @uaddr:  the futex userspace address
 * @val:    the expected value
 * @fshared:    whether the futex is shared (1) or not (0)
 * @q:      the associated futex_q
 * @hb:     storage for hash_bucket pointer to be returned to caller
 *
 * Setup the futex_q and locate the hash_bucket.  Get the futex value and
 * compare it with the expected value.  Handle atomic faults internally.
 * Return with the hb lock held and a q.key reference on success, and unlocked
 * with no q.key reference on failure.
 *
 * Returns:
 *  0 - uaddr contains val and hb has been locked
 * <1 - -EFAULT or -EWOULDBLOCK (uaddr does not contain val) and hb is unlcoked
 */
static int futex_wait_setup(u32 __user *uaddr, u32 val, int fshared,
               struct futex_q *q, struct futex_hash_bucket **hb)
{  
    u32 uval;
    int ret;
   
    /*
     * Access the page AFTER the hash-bucket is locked.
     * Order is important:
     *
     *   Userspace waiter: val = var; if (cond(val)) futex_wait(&var, val);
     *   Userspace waker:  if (cond(var)) { var = new; futex_wake(&var); }
     *
     * The basic logical guarantee of a futex is that it blocks ONLY
     * if cond(var) is known to be true at the time of blocking, for
     * any cond.  If we queued after testing *uaddr, that would open
     * a race condition where we could block indefinitely with
     * cond(var) false, which would violate the guarantee.
     *
     * A consequence is that futex_wait() can return zero and absorb
     * a wakeup when *uaddr != val on entry to the syscall.  This is
     * rare, but normal.
     */
retry:
    // 初始化所有值为0
    q->key = FUTEX_KEY_INIT;
    // 初始化futex_key
    ret = get_futex_key(uaddr, fshared, &q->key);
    if (unlikely(ret != 0))
        return ret;
       
retry_private:
    // 初始化q->lock_ptr，并加锁
    *hb = queue_lock(q);
    // 关闭缺页中断，复制用户态值uaddr到uval
    ret = get_futex_value_locked(&uval, uaddr);
    // 失败
    if (ret) {
        queue_unlock(q, *hb);
       // 重试
        ret = get_user(uval, uaddr);
        if (ret)
            goto out;
       // 单进程的锁
        if (!fshared)
            goto retry_private;
       
        put_futex_key(fshared, &q->key);
        goto retry;
    }  
    // 用户态数据不等于expect 的val
    if (uval != val) {
        queue_unlock(q, *hb);
        ret = -EWOULDBLOCK;
    }  
       
out:   
    if (ret)
        put_futex_key(fshared, &q->key);
    return ret;
}
```
```c
/**
 * get_futex_key() - Get parameters which are the keys for a futex
 * @uaddr:	virtual address of the futex
 * @fshared:	0 for a PROCESS_PRIVATE futex, 1 for PROCESS_SHARED
 * @key:	address where result is stored.
 *
 * Returns a negative error code or 0
 * The key words are stored in *key on success.
 *
 * For shared mappings, it's (page->index, vma->vm_file->f_path.dentry->d_inode,
 * offset_within_page).  For private mappings, it's (uaddr, current->mm).
 * We can usually work out the index without swapping in the page.
 *
 * lock_page() might sleep, the caller should not hold a spinlock.
 */
static int
get_futex_key(u32 __user *uaddr, int fshared, union futex_key *key)
{
	unsigned long address = (unsigned long)uaddr;
	struct mm_struct *mm = current->mm;
	struct page *page;
	int err;

	/*
	 * The futex address must be "naturally" aligned.
	 */
	key->both.offset = address % PAGE_SIZE;
	if (unlikely((address % sizeof(u32)) != 0))
		return -EINVAL;
	address -= key->both.offset;

	/*
	 * PROCESS_PRIVATE futexes are fast.
	 * As the mm cannot disappear under us and the 'key' only needs
	 * virtual address, we dont even have to find the underlying vma.
	 * Note : We do have to check 'uaddr' is a valid user address,
	 *        but access_ok() should be faster than find_vma()
	 */
	if (!fshared) {
		if (unlikely(!access_ok(VERIFY_WRITE, uaddr, sizeof(u32))))
			return -EFAULT;
		key->private.mm = mm;
		key->private.address = address;
		get_futex_key_refs(key);
		return 0;
	}

again:
	err = get_user_pages_fast(address, 1, 1, &page);
	if (err < 0)
		return err;

	page = compound_head(page);
	lock_page(page);
	if (!page->mapping) {
		unlock_page(page);
		put_page(page);
		goto again;
	}

	/*
	 * Private mappings are handled in a simple way.
	 *
	 * NOTE: When userspace waits on a MAP_SHARED mapping, even if
	 * it's a read-only handle, it's expected that futexes attach to
	 * the object not the particular process.
	 */
	if (PageAnon(page)) {
		key->both.offset |= FUT_OFF_MMSHARED; /* ref taken on mm */
		key->private.mm = mm;
		key->private.address = address;
	} else {
		key->both.offset |= FUT_OFF_INODE; /* inode-based key */
		key->shared.inode = page->mapping->host;
		key->shared.pgoff = page->index;
	}

	get_futex_key_refs(key);

	unlock_page(page);
	put_page(page);
	return 0;
}
```


```c
/*           
 * Take a reference to the resource addressed by a key.
 * Can be called while holding spinlocks.
 *           
 */          
static void get_futex_key_refs(union futex_key *key)
{            
    if (!key->both.ptr)
        return;
             
    switch (key->both.offset & (FUT_OFF_INODE|FUT_OFF_MMSHARED)) {
    case FUT_OFF_INODE:
        atomic_inc(&key->shared.inode->i_count);
        break;
    case FUT_OFF_MMSHARED:
        atomic_inc(&key->private.mm->mm_count);
        break;
    }        
}
```

```c
/*
 * We hash on the keys returned from get_futex_key (see below).
 */
static struct futex_hash_bucket *hash_futex(union futex_key *key)                                                                                                                                                  
{
    u32 hash = jhash2((u32*)&key->both.word,
              (sizeof(key->both.word)+sizeof(key->both.ptr))/4,
              key->both.offset);
    return &futex_queues[hash & ((1 << FUTEX_HASHBITS)-1)];
}

```

```c
/**  
 * futex_wait_queue_me() - queue_me() and wait for wakeup, timeout, or signal
 * @hb:     the futex hash bucket, must be locked by the caller
 * @q:      the futex_q to queue up on
 * @timeout:    the prepared hrtimer_sleeper, or null for no timeout
 */  
static void futex_wait_queue_me(struct futex_hash_bucket *hb, struct futex_q *q,
                struct hrtimer_sleeper *timeout)
{    
    /*
     * The task state is guaranteed to be set before another task can
     * wake it. set_current_state() is implemented using set_mb() and
     * queue_me() calls spin_unlock() upon completion, both serializing
     * access to the hash list and forcing another memory barrier.
     */
    set_current_state(TASK_INTERRUPTIBLE);
    // 将current 挂载到hb中
    queue_me(q, hb);
     
    /* Arm the timer */
    if (timeout) {
        hrtimer_start_expires(&timeout->timer, HRTIMER_MODE_ABS);
        if (!hrtimer_active(&timeout->timer))
            timeout->task = NULL;
    }  
     
    /*
     * If we have been removed from the hash list, then another task
     * has tried to wake us, and we can skip the call to schedule().
     */
    if (likely(!plist_node_empty(&q->list))) {
        /*
         * If the timer has already expired, current will already be
         * flagged for rescheduling. Only call schedule if there
         * is no timeout, or if it has yet to expire.
         */
        if (!timeout || timeout->task)
            schedule();
    }
    __set_current_state(TASK_RUNNING);
}

// 将current 挂载到hb中
static inline void queue_me(struct futex_q *q, struct futex_hash_bucket *hb)
{  
    int prio;
   
    /*
     * The priority used to register this element is
     * - either the real thread-priority for the real-time threads
     * (i.e. threads with a priority lower than MAX_RT_PRIO)
     * - or MAX_RT_PRIO for non-RT threads.
     * Thus, all RT-threads are woken first in priority order, and
     * the others are woken last, in FIFO order.
     */
    prio = min(current->normal_prio, MAX_RT_PRIO);
   
    plist_node_init(&q->list, prio);
#ifdef CONFIG_DEBUG_PI_LIST
    q->list.plist.spinlock = &hb->lock;
#endif
    plist_add(&q->list, &hb->chain);
    q->task = current;
    spin_unlock(&hb->lock);
}

static int unqueue_me(struct futex_q *q)
{   
    spinlock_t *lock_ptr;
    int ret = 0;
    
    /* In the common case we don't take the spinlock, which is nice. */
retry:
    lock_ptr = q->lock_ptr;
    barrier();
    if (lock_ptr != NULL) {
        spin_lock(lock_ptr);
        /*
         * q->lock_ptr can change between reading it and
         * spin_lock(), causing us to take the wrong lock.  This
         * corrects the race condition.
         *
         * Reasoning goes like this: if we have the wrong lock,
         * q->lock_ptr must have changed (maybe several times)
         * between reading it and the spin_lock().  It can
         * change again after the spin_lock() but only if it was
         * already changed before the spin_lock().  It cannot,
         * however, change back to the original value.  Therefore                                                                                                                                                  
         * we can detect whether we acquired the correct lock.
         */
        if (unlikely(lock_ptr != q->lock_ptr)) {
            spin_unlock(lock_ptr);
            goto retry;
        }
        WARN_ON(plist_node_empty(&q->list));
        plist_del(&q->list, &q->list.plist);
    
        BUG_ON(q->pi_state);
    
        spin_unlock(lock_ptr);
        ret = 1;
    }
    
    drop_futex_key_refs(&q->key);
    return ret;
}
```