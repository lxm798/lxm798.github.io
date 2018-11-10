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

```


## futex 流程
```c
static int futex_wait(u32 __user *uaddr, int fshared,
              u32 val, ktime_t *abs_time, u32 bitset, int clockrt)
{          
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