# Introduce
linux的进程/线程调度总体依赖于task_entity,对于非实时任务采用cfs算法.每个task_entity有对应的vruntime用于记录运行时间,当占用cpu的时间超过当前最少的用户时,切换线程.
vruntime 受到用户静态优先级的限制,每个优先级的运行时间差1.25,可以用来增加cpu的比例,效果类似shares,可以分配同一个cgroup中不同进程使用的cpu的比例.

## cgroup
### cpu
cfs_period_us:
cfs_quota_us:
shares:
task_group
## scheduler

sched_entity
task_struct