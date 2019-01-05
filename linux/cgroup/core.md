# 调度core.c

## 说明
get_cpu/put_cpu 禁止/开启内核抢占
## 进程创建
1. 关闭内核抢占
2. 初始化化sched_entity中计数,state和prio
3. 重新计算静态优先级和动态优先级
4. 根据task_struct设置调度器类型
5. 初始化sched_entity,将sched_entity中的cfs指向对应的task_group的cfs_rq

```c
void sched_fork(struct task_struct *p)
{
	unsigned long flags;
	int cpu = get_cpu();

	__sched_fork(p);
	// 设置为running状态
	p->state = TASK_RUNNING;

    // 继承父进程prio
	p->prio = current->normal_prio;

	if (unlikely(p->sched_reset_on_fork)) {
		if (task_has_rt_policy(p)) {
			p->policy = SCHED_NORMAL;
			p->static_prio = NICE_TO_PRIO(0);
			p->rt_priority = 0;
		} else if (PRIO_TO_NICE(p->static_prio) < 0)
			p->static_prio = NICE_TO_PRIO(0);

		p->prio = p->normal_prio = __normal_prio(p);
		set_load_weight(p);

		p->sched_reset_on_fork = 0;
	}
    // 设置调度的类型是cfs还是实时
	if (!rt_prio(p->prio))
		p->sched_class = &fair_sched_class;

	if (p->sched_class->task_fork)
		p->sched_class->task_fork(p);

	raw_spin_lock_irqsave(&p->pi_lock, flags);
    // 将进程添加到到指定的cpu队列
	set_task_cpu(p, cpu);
	raw_spin_unlock_irqrestore(&p->pi_lock, flags); 
	put_cpu();
}
```

###  __sched_fork
初始化 sched_entity为空的数据
```c
static void __sched_fork(struct task_struct *p)
{
	p->on_rq			= 0;

	p->se.on_rq			= 0;
	p->se.exec_start		= 0;
	p->se.sum_exec_runtime		= 0;
	p->se.prev_sum_exec_runtime	= 0;
	p->se.nr_migrations		= 0;
	p->se.vruntime			= 0;
	INIT_LIST_HEAD(&p->se.group_node);

	INIT_LIST_HEAD(&p->rt.run_list);
}
```

### set_task_rq
将进程的sched_entity的队列绑定到对应task_group的队列
```c
static inline void set_task_rq(struct task_struct *p, unsigned int cpu)                                                                                                                                            
{                   
#if defined(CONFIG_FAIR_GROUP_SCHED) || defined(CONFIG_RT_GROUP_SCHED)
    struct task_group *tg = task_group(p);
#endif              
                    
#ifdef CONFIG_FAIR_GROUP_SCHED
    p->se.cfs_rq = tg->cfs_rq[cpu];
    p->se.parent = tg->se[cpu];
#endif              
                    
#ifdef CONFIG_RT_GROUP_SCHED
    p->rt.rt_rq  = tg->rt_rq[cpu];
    p->rt.parent = tg->rt_se[cpu];
#endif              
} 
```

```c
static inline struct task_group *task_group(struct task_struct *p)
{           
    struct task_group *tg;
    struct cgroup_subsys_state *css;
            
    css = task_subsys_state_check(p, cpu_cgroup_subsys_id,
            lockdep_is_held(&p->pi_lock) ||
            lockdep_is_held(&task_rq(p)->lock));
    tg = container_of(css, struct task_group, css);
            
    return autogroup_task_group(p, tg);
}

static inline struct task_group *
autogroup_task_group(struct task_struct *p, struct task_group *tg)
{
    int enabled = ACCESS_ONCE(sysctl_sched_autogroup_enabled);
 
    if (enabled && task_wants_autogroup(p, tg))
        return p->signal->autogroup->tg;
 
    return tg; 
}

static inline struct task_group *
autogroup_task_group(struct task_struct *p, struct task_group *tg)
{
    return tg; 
}
```

