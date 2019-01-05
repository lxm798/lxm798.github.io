## 进程调度
```c
asmlinkage void __sched schedule(void)
{
	struct task_struct *tsk = current;

	sched_submit_work(tsk);
	__schedule();
}
```

### __schedule
主调度过程
```c
static void __sched __schedule(void)
{
	struct task_struct *prev, *next;
	unsigned long *switch_count;
	struct rq *rq;
	int cpu;

need_resched:
	preempt_disable();
	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	rcu_note_context_switch(cpu);
	prev = rq->curr;

	schedule_debug(prev);

	if (sched_feat(HRTICK))
		hrtick_clear(rq);

	raw_spin_lock_irq(&rq->lock);

	switch_count = &prev->nivcsw;
	if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) {
		if (unlikely(signal_pending_state(prev->state, prev))) {
			prev->state = TASK_RUNNING;
		} else {
			deactivate_task(rq, prev, DEQUEUE_SLEEP);
			prev->on_rq = 0;

			/*
			 * If a worker went to sleep, notify and ask workqueue
			 * whether it wants to wake up a task to maintain
			 * concurrency.
			 */
			if (prev->flags & PF_WQ_WORKER) {
				struct task_struct *to_wakeup;

				to_wakeup = wq_worker_sleeping(prev, cpu);
				if (to_wakeup)
					try_to_wake_up_local(to_wakeup);
			}
		}
		switch_count = &prev->nvcsw;
	}

	pre_schedule(rq, prev);

	if (unlikely(!rq->nr_running))
		idle_balance(cpu, rq);

	put_prev_task(rq, prev);
	next = pick_next_task(rq);
	clear_tsk_need_resched(prev);
	rq->skip_clock_update = 0;

	if (likely(prev != next)) {
		rq->nr_switches++;
		rq->curr = next;
		++*switch_count;

		context_switch(rq, prev, next); /* unlocks the rq */
		/*
		 * The context switch have flipped the stack from under us
		 * and restored the local variables which were saved when
		 * this task called schedule() in the past. prev == current
		 * is still correct, but it can be moved to another cpu/rq.
		 */
		cpu = smp_processor_id();
		rq = cpu_rq(cpu);
	} else
		raw_spin_unlock_irq(&rq->lock);

	post_schedule(rq);

	sched_preempt_enable_no_resched();
	if (need_resched())
		goto need_resched;
}
```

### pick_next_task
```c
调用进程调度类调度选择进程
static inline struct task_struct *
pick_next_task(struct rq *rq)
{
	const struct sched_class *class;
	struct task_struct *p;

	if (likely(rq->nr_running == rq->cfs.h_nr_running)) {
		p = fair_sched_class.pick_next_task(rq);
		if (likely(p))
			return p;
	}

	for_each_class(class) {
		p = class->pick_next_task(rq);
		if (p)
			return p;
	}

	BUG(); /* the idle class will always have a runnable task */
}
```

### cfs的选择算法
```c
static struct task_struct *pick_next_task_fair(struct rq *rq)                                                                                                                                                      
{                           
    struct task_struct *p;  
    struct cfs_rq *cfs_rq = &rq->cfs;
    struct sched_entity *se;
                            
    if (!cfs_rq->nr_running)
        return NULL;        
                            
    do {                    
        se = pick_next_entity(cfs_rq);
        set_next_entity(cfs_rq, se);
		// my_q
        cfs_rq = group_cfs_rq(se);
    } while (cfs_rq);       
                            
    p = task_of(se);        
    if (hrtick_enabled(rq)) 
        hrtick_start_fair(rq, p);
                            
    return p;               
} 

static struct sched_entity *pick_next_entity(struct cfs_rq *cfs_rq)
{
    // 找到最左边的节点
    struct sched_entity *se = __pick_first_entity(cfs_rq);
    struct sched_entity *left = se;
       
    // 当前不能被调度?????
    if (cfs_rq->skip == se) {
        struct sched_entity *second = __pick_next_entity(se);
        if (second && wakeup_preempt_entity(second, left) < 1)
            se = second;
    }  

    //     
    if (cfs_rq->last && wakeup_preempt_entity(cfs_rq->last, left) < 1)
        se = cfs_rq->last;
         
    if (cfs_rq->next && wakeup_preempt_entity(cfs_rq->next, left) < 1)
        se = cfs_rq->next;
             
    clear_buddies(cfs_rq, se);
             
    return se;
}
struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq)                                                                                                                                                    
{
    struct rb_node *left = cfs_rq->rb_leftmost;
 
    if (!left)
        return NULL;
 
    return rb_entry(left, struct sched_entity, run_node);
}
```
### set_next_entity
设置队列当前的sched_entity,并从红黑树中删除对应的sched_entity
```c
static void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{  
    /* 'current' is not kept within the tree. */
    if (se->on_rq) {
        /*
         * Any task has to be enqueued before it get to execute on
         * a CPU. So account for the time it spent waiting on the
         * runqueue.
         */
        update_stats_wait_end(cfs_rq, se);
        __dequeue_entity(cfs_rq, se);
    }
   
    update_stats_curr_start(cfs_rq, se);
    cfs_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
    if (rq_of(cfs_rq)->load.weight >= 2*se->load.weight) {
        se->statistics.slice_max = max(se->statistics.slice_max,
            se->sum_exec_runtime - se->prev_sum_exec_runtime);
    }
#endif
    se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static void __dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)                                                                                                                                       
{
    if (cfs_rq->rb_leftmost == &se->run_node) {
        struct rb_node *next_node;
 
        next_node = rb_next(&se->run_node);
        cfs_rq->rb_leftmost = next_node;
    }
 
    rb_erase(&se->run_node, &cfs_rq->tasks_timeline);
}

```


## cfs_bandwidth
```c
void __refill_cfs_bandwidth_runtime(struct cfs_bandwidth *cfs_b)
{
	u64 now;

	if (cfs_b->quota == RUNTIME_INF)
		return;

	now = sched_clock_cpu(smp_processor_id());
	// 剩余的运行cpu周期数
	cfs_b->runtime = cfs_b->quota;
	// 剩余的运行时间
	cfs_b->runtime_expires = now + ktime_to_ns(cfs_b->period);
}
```

```c
static int assign_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	struct task_group *tg = cfs_rq->tg;
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(tg);
	u64 amount = 0, min_amount, expires;

	/* note: this is a positive sum as runtime_remaining <= 0 */
	min_amount = sched_cfs_bandwidth_slice() - cfs_rq->runtime_remaining;

	raw_spin_lock(&cfs_b->lock);
	if (cfs_b->quota == RUNTIME_INF)
		amount = min_amount;
	else {
		/*
		 * If the bandwidth pool has become inactive, then at least one
		 * period must have elapsed since the last consumption.
		 * Refresh the global state and ensure bandwidth timer becomes
		 * active.
		 */
		if (!cfs_b->timer_active) {
			__refill_cfs_bandwidth_runtime(cfs_b);
			__start_cfs_bandwidth(cfs_b);
		}

		if (cfs_b->runtime > 0) {
			amount = min(cfs_b->runtime, min_amount);
			cfs_b->runtime -= amount;
			cfs_b->idle = 0;
		}
	}
	expires = cfs_b->runtime_expires;
	raw_spin_unlock(&cfs_b->lock);

	cfs_rq->runtime_remaining += amount;
	/*
	 * we may have advanced our local expiration to account for allowed
	 * spread between our sched_clock and the one on which runtime was
	 * issued.
	 */
	if ((s64)(expires - cfs_rq->runtime_expires) > 0)
		cfs_rq->runtime_expires = expires;

	return cfs_rq->runtime_remaining > 0;
}
```
```c
static void __account_cfs_rq_runtime(struct cfs_rq *cfs_rq,
				     unsigned long delta_exec)
{
	/* dock delta_exec before expiring quota (as it could span periods) */
	cfs_rq->runtime_remaining -= delta_exec;
	expire_cfs_rq_runtime(cfs_rq);

	if (likely(cfs_rq->runtime_remaining > 0))
		return;

	/*
	 * if we're unable to extend our runtime we resched so that the active
	 * hierarchy can be throttled
	 */
	if (!assign_cfs_rq_runtime(cfs_rq) && likely(cfs_rq->curr))
		resched_task(rq_of(cfs_rq)->curr);
}
```

```c
static void check_enqueue_throttle(struct cfs_rq *cfs_rq)
{
	if (!cfs_bandwidth_used())
		return;

	/* an active group must be handled by the update_curr()->put() path */
	if (!cfs_rq->runtime_enabled || cfs_rq->curr)
		return;

	/* ensure the group is not already throttled */
	if (cfs_rq_throttled(cfs_rq))
		return;

	/* update runtime allocation */
	account_cfs_rq_runtime(cfs_rq, 0);
	if (cfs_rq->runtime_remaining <= 0)
		throttle_cfs_rq(cfs_rq);
}
```

```c
static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	/*
	 * Update the normalized vruntime before updating min_vruntime
	 * through callig update_curr().
	 */
	if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
		se->vruntime += cfs_rq->min_vruntime;

	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);
	update_cfs_load(cfs_rq, 0);
	account_entity_enqueue(cfs_rq, se);
	update_cfs_shares(cfs_rq);

	if (flags & ENQUEUE_WAKEUP) {
		place_entity(cfs_rq, se, 0);
		enqueue_sleeper(cfs_rq, se);
	}

	update_stats_enqueue(cfs_rq, se);
	check_spread(cfs_rq, se);
	if (se != cfs_rq->curr)
		__enqueue_entity(cfs_rq, se);
	se->on_rq = 1;

	if (cfs_rq->nr_running == 1) {
		list_add_leaf_cfs_rq(cfs_rq);
		check_enqueue_throttle(cfs_rq);
	}
}
```


### update_curr 入口
reweight_entity
enqueue_entity
dequeue_entity
entity_tick
check_preempt_wakeup