# futex
## unlock
```c
/*                                                                                                                                                                                                                 
 * Wake up waiters matching bitset queued on this futex (uaddr).
 */              
static int futex_wake(u32 __user *uaddr, int fshared, int nr_wake, u32 bitset)
{                
    struct futex_hash_bucket *hb;
    struct futex_q *this, *next;
    struct plist_head *head;
    union futex_key key = FUTEX_KEY_INIT;
    int ret;     
                 
    if (!bitset) 
        return -EINVAL;
                 
    ret = get_futex_key(uaddr, fshared, &key);
    if (unlikely(ret != 0))
        goto out;
                 
    hb = hash_futex(&key);
    spin_lock(&hb->lock);
    head = &hb->chain;
                 
    plist_for_each_entry_safe(this, next, head, list) {
        if (match_futex (&this->key, &key)) {
            if (this->pi_state || this->rt_waiter) {
                ret = -EINVAL;
                break;
            }    
                 
            /* Check if one of the bits is set in both bitsets */
            if (!(this->bitset & bitset))
                continue;
                 
            wake_futex(this);
            if (++ret >= nr_wake)
                break;
        }        
    }            
                 
    spin_unlock(&hb->lock);
    put_futex_key(fshared, &key);
out:             
    return ret;  
}

/*  
 * The hash bucket lock must be held when this is called.
 * Afterwards, the futex_q must not be accessed.
 */ 
static void wake_futex(struct futex_q *q)
{   
    struct task_struct *p = q->task;
    
    /*
     * We set q->lock_ptr = NULL _before_ we wake up the task. If
     * a non futex wake up happens on another CPU then the task
     * might exit and p would dereference a non existing task
     * struct. Prevent this by holding a reference on p across the
     * wake up.
     */
    get_task_struct(p);
    
    plist_del(&q->list, &q->list.plist);
    /*
     * The waiting task can free the futex_q as soon as
     * q->lock_ptr = NULL is written, without taking any locks. A
     * memory barrier is required here to prevent the following
     * store to lock_ptr from getting ahead of the plist_del.
     */
    smp_wmb();
    q->lock_ptr = NULL;
    
    wake_up_state(p, TASK_NORMAL);
    put_task_struct(p);                                                                                                                                                                                            
}
```