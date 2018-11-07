# Large-scale cluster management at Google with Borg


### cluster and cells
cluster: 通常由一个大的cell,可能有多个小规模的测试/特定功能的cells
中等规模的cell 有1w台机器.集群中的机器通常时异构的
borg隔离了底层异构信息,通过计算在那个cell运行task,分配资源,部署程序和依赖,健康检查,应用重启

### jobs & tasks
job的属性包括 name, owner和包含的task.
jobs 可以包含特定的属性限制,以便于运行在特定的机器上, 比如os, ip,job 只能在一个cell中运行.
每个task 是在同一台机器中的多个进程

大部分的负载不是运行在vm中.
task的属性: 资源请求描述,task 在job 中的索引信息.

用户通过命令行工具发起rpc调用来操作job.
job通过bcl语言描述
用户可以改变job中部分或者全部job的描述,borg 会更新task的规格描述(specification)
task 需要处理SIGTERM,以便于平滑关闭

### Allocs
alloc: 一组alloc 在多个机器上保留资源.一旦alloc 被创建一个或者多个job可以运行在其中.

### Priority, quota, and admission control
所有job都有优先级,高优先级的任务可以抢占(preempting/killing)低优先级任务的资源,
borg 定义了正交的优先级:监控(monitoring),生产(production),批量(batch),尽力(effort)
quota 用于确定哪个job可用于提交.quota通过一组资源向量描述(CPU, MEM, disk, etc.)
Quota 分配在Borg之外,并且和物理容量规划紧密相关,反应在不同数据中心的价格和可用性上.
用户job只有在他们有足够的quota和priorty时才能得到处理
borg有独立的容量系统为用户提供特殊的权限.例如管理员杀出或者修改job

### Naming and monitoring
每个task 内置HTTP server用于健康检查.borg 监控健康检查url,如果没有响应,borg 会重启应用.

## borg 架构
### borgmaster
每个cell中的borgmaster包含两个部分:
* borgmaster进程: 
  处理rpc请求(创建job, 查询job,管理任何对象的状态机,和borglets通信,提供webui)
* scheduler