https://www.cnblogs.com/s-hk/p/3319455.html
##
```c
const struct sched_class fair_sched_class = {
    .next           = &idle_sched_class,
    .enqueue_task       = enqueue_task_fair,
    .dequeue_task       = dequeue_task_fair,
    .yield_task     = yield_task_fair,
    .yield_to_task      = yield_to_task_fair,
                           
    .check_preempt_curr = check_preempt_wakeup,
                           
    .pick_next_task     = pick_next_task_fair,                                                                                                                                                                     
    .put_prev_task      = put_prev_task_fair,
                           
#ifdef CONFIG_SMP          
    .select_task_rq     = select_task_rq_fair,
                           
    .rq_online      = rq_online_fair,
    .rq_offline     = rq_offline_fair,
                           
    .task_waking        = task_waking_fair,
#endif                     
                           
    .set_curr_task          = set_curr_task_fair,
    .task_tick      = task_tick_fair,
    .task_fork      = task_fork_fair,
                           
    .prio_changed       = prio_changed_fair,
    .switched_from      = switched_from_fair,
    .switched_to        = switched_to_fair,
                           
    .get_rr_interval    = get_rr_interval_fair,
                           
#ifdef CONFIG_FAIR_GROUP_SCHED
    .task_move_group    = task_move_group_fair,
#endif                     
};
```

```c
OTHER CFS CLASS API

set_curr_task_fair(rq)：将cpu运行队列里的当前运行进程设置为cfs运行队列里当前运行的进程(set_curr_task_fair)，对于组调度则必须把它上级的se也设置为相应cfs_rq的当前运行进程。该接口主要用于修改某个进程的调度策略(__sched_setscheduler调度器类的运行队列信息也需要更新)或把一个进程从一个group迁移到另一个group的过程(__sched_move_task此时如果它是正在运行的，则需让它在新的group cfs_rq中也处于正在运行状态)，注意：这里并不需要resched_task当前进程
switched_to_fair(rq, p, running)：把rq里的进程p的调度策略切换到CFS。如果p是正在运行的，那么直接resched_task它；否则判断p是否能够抢占当前运行的进程。显然该接口也用于调度策略改变时调用(check_class_changed)
prio_changed_fair(rq, p, oldprio, running)：该函数在p的优先级被改变时调用(在调度框架的__sched_setscheduler-à check_class_changed被调用)。如果p是正在运行的进程，并且它的优先级降低了，那么直接resched_task它，否则调用check_preempt_curr检查当前运行的是否该被p抢占(check_class_changed)
yield_task_fair(rq)：该接口用于系统调用sched_yield中，这本身的逻辑非常简单：把当前进程从buddies中删除，并且更新它的执行时间update_curr，最后把它设置为skip_buddy（上面的pick_next_task我们说过如果是skip的话，在选择的时候会被skip的），这里及sched_yield系统调用并没有把当前进程设置为TIF_NEED_RESCHED，这是因为在sched_yield里直接调用了schedule，而不再需要在系统调用返回时去检查
yield_to_task_fair(rq, p, preempt)：该函数不仅让当前进程让出CPU，而且想让p进程优先运行set_next_buddy，该接口用于yield_to函数中

CFS内部主要函数

__pick_first_entity(cfs_rq)：获得cfs_rq红黑树队列里最左边的节点
__pick_next_entity(se)：获得se所在红黑树的se的后一个节点
entity_key(cfs_rq, se)：se在cfs_rq的红黑树中的key,se->vruntime - cfs_rq->min_vruntime,这里使用这个差值为key（标准化后的）,是因为vruntime本身为usigned long而且是一个递增的值, vruntime溢出了,那么就会导致顺序混乱,所以key不能直接使用vruntime,而这个差值就是为了防止溢出
update_min_vruntime(cfs_rq)：更新cfs_rq的min_vruntime,并且它保证每次将要更新的min_vruntime都大于等于当前的min_vruntime,即max(cfs_rq->min_vruntime,min(cfs_rq->curr->vruntime, cfs_rq->rb_leftmost->vruntime)),所以min_vruntime也是递增的
calc_delta_mine(delta_exec,weight, lw)：该函数用于计算物理delta_exec时间的相对时间，这个时间用delta_exec为基数 乘以 第二个参数与最后一个参数比重，即delta_exec * weight / lw.于是就有以下几种情况： delta_exec为一个se实质运行的物理时间，第二个参数为NICE_0的权重，第三个参数为该se的本身权重：该函数计算出来的值就是该se它运行了delta_exec物理时间对应的虚拟时间,如calc_delta_fair； delta_exec为一个运行队列的运行周期(物理时间)，第二个参数为当前se的权重，第三个参数为运行队列本身的权重：该函数计算出来的值就是该se在当前运行队列中即将(理想)运行的物理时间,如sched_slice非组调度。
calc_delta_fair(delta,se)：计算se执行的delta物理时间的相应的虚拟时间
__sched_period(nr_running)：该函数用于计算当前运行队列所有se运行一遍所需要的时间周期,这是一个实质物理时间,当运行队列成员小于sched_nr_latency时，该时间为sysctl_sched_latency；否则等于sysctl_sched_min_granularity*运行队列的成员个数（这个函数也是计算所谓的延迟调度周期，即内核保证在这么长的时间内它的运行队列的成员都会被执行至少一次，sysctl_sched_latency、sysctl_sched_min_granularity可以在/proc/sys/kernel/上进行设置）
sched_slice(cfs_rq,se)：对于非组调度它计算的就是当前se在运行队列里所能分配的物理执行时间，首先计算出该队列的调度周期（__sched_period），然后按照该se在队列中所占的比重计算出它自己的理想执行时间__sched_period*(se->load/se->cfs_rq->load)；对于组调度，它则算出该se对应的group root理想的物理执行时间。为什么要算到root？——假设我们有两个group并且它们的shares差一倍，但是它们下面的task load是一样的1024，那么它们的执行时间显然跟它们所属的group有关。即它的se的上级group se是这样计算的：parent_slice=child_slice*(parent_load/parent_cfs_rq->load)。注：如果se不在运行列队（从另一个cpu迁移过来），那么计算队列的load时还应该加上该运行的se的load。
__update_curr(cfs_rq,curr, delta_exec)：更新cfs_rq当前的运行se的累计物理执行时间sum_exec_runtime,vruntime(这里用到calc_delta_fair计算实质物理时间对应的虚拟时间)以及cfs_rq->min_vruntime(update_min_vruntime)
sched_vslice(cfs_rq,se)：计算该se理想情况下运行的物理时间相对应的虚拟时间
update_curr(cfs_rq)：该函数调用__update_curr更新vruntime，及更新一些统计信息
wakeup_gran：用于计算被抢占进程的最小执行时间(物理时间sysctl_sched_wakeup_granularity 1ms)的虚拟时间(calc_delta_fair)
wakeup_preempt_entity：该函数用于判断一个新的se是否应该抢占当前的curr，如果当前的虚拟执行时间小于等于(优先不抢占)新的vruntime，显然当前执行的时间更少，所以不应该被抢占(返回-1)；否则，假设新的se可以抢占curr，那么它最少需要运行wakeup_gran虚拟时间(sysctl_sched_wakeup_granularity保证最少运行时间防止频繁切换)，如果这个时间比当前多运行的时间小的话，那么说明被抢占后新的se运行后的vruntime还会比当前的vruntime还小，这样新的se可以抢占当前curr(返回1)；0好像也表示不应该抢占
place_entity(cfs_rq,se, initial)：确定se被wakeup(initial=0)或者刚被fork的时新(initial=1)的合适vruntime，对于wakeup则进行适当的补偿(补偿因子sysctl_sched_latency，如果睡眠时间太短se->vruntime依然很大（比vruntime大），那么进程虚拟时间不变，而得不到奖励)；对于刚被fork的进程，则把它的vruntime赋值为cfs_rq->min_vruntime，另外因为进程刚被创建，它的运行应当被放在运行周期之后，所以加上运行周期对应虚拟时间（sched_vslice）
set_next_entity(cfs_rq,se)：将se置为当前cfs_rq上正在运行的进程，如果它是在运行队列里，则先出队，否则直接cfs_rq->curr = se
set_last_buddy(se)：设置优先调度的last buddy
set_next_buddy(se)：设置优先调度的next buddy，它的优先级比last buddy高
set_skip_buddy(se)：设置不应该被调度的buddy，也就是它的优先级最最低
update_cfs_shares(cfs_rq)：更新该cfs_rq自身所属的se weight.load值（se = tg->se[cpu_of(rq_of(cfs_rq))]），新的shares值计算公式为：(tg->shares*cfs_rq->load.weight)/(tg->load_weight-cfs_rq->load_contribution+cfs_rq->load.weight)，注意最后在更新时reweight_entity(cfs_rq_of(se), se, shares)，这时传给reweight_entity就是这个se所属的cfs_rq（上级的my_q），而不是它自己本身了，因为如果这个se已经在它的上级cfs_rq里，那么应该先减去它旧的shares值，然后再更新该se的shares值，然后再用新的se shares值加到这个上级cfs_rq里 
account_entity_enqueue(cfs_rq,se)：入队列时相应更新，包括cfs_rq的load，nr_running；另外如果该se是root的话，则增加cfs_rq对应的rq的load，如果该se是task的话，则增加cfs_rq的task weight
account_entity_dequeue(cfs_rq,se)：做account_entity_enqueue的相反操作
reweight_entity(cfs_rq,se, weight)：重新计算se的load及相应的cfs_rq的load（如该se在cfs_rq里），即先把该se account_entity_dequeue，再更新se的load update_load_set，最后再重新计算cfs_rq的load account_entity_enqueue
```