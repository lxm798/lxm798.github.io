#mesos 源码分析 -master 和slave 通信
## 支持的消息
### master
类型 | 函数 | 功能
--|--|--
SubmitSchedulerRequest | submitScheduler | ??
RegisterFrameworkMessage | registerFramework | 注册Framework
ReregisterFrameworkMessage | reregisterFramework | 重新注册Framework
UnregisterFrameworkMessage | unregisterFramework | 注销Framework
DeactivateFrameworkMessage | deactivateFramework | ??
ResourceRequestMessage | resourceRequest | 主动申请资源,scheudler调用
LaunchTasksMessage | launchTasks | 执行task
ReviveOffersMessage | reviveOffers | ??
KillTaskMessage | killTask | ??
StatusUpdateAcknowledgementMessage | statusUpdateAcknowledgement | ??
FrameworkToExecutorMessage | schedulerMessage | 转发请求到Executor
RegisterSlaveMessage | registerSlave | 
ReregisterSlaveMessage | reregisterSlave | 
UnregisterSlaveMessage | unregisterSlave | 
StatusUpdateMessage | statusUpdate | Executor 发起,task状态变化
ExecutorToFrameworkMessage | executorMessage | executor 发送消息到framework
ReconcileTasksMessage | reconcileTasks | ??
UpdateOperationStatusMessage | updateOperationStatus | ??
ExitedExecutorMessage | exitedExecutor | ??
UpdateSlaveMessage | updateSlave | slave在注册或者reconcile时发送
AuthenticateMessage | authenticate | ??


/**
 * This message is sent by the agent to the master to inform the
 * master about the total amount of oversubscribed (allocated and
 * allocatable), or total resources. For `RESOURCE_PROVIDER` capable
 * agents, this message also includes the operations that are either
 * pending, or terminal but have unacknowledged status updates.
 */
message UpdateSlaveMessage {
  required SlaveID slave_id = 1;

  // Top-level fields in this message should only contain information
  // on the agent itself; information on local resource providers is
  // passed explicitly in the `resource_providers` message.
  //
  // TODO(bbannier): Consider passing agent information inside a
  // `ResourceProvider` value as well where applicable.

  // Whether to update oversubscribed resources or not. If we just use
  // `oversubscribed_resources`, we don't have a way to tell if the
  // intention is to update the oversubscribed resoruces to empty, or
  // leave it unchanged. For backwards compatibility, if this field is
  // unset (version < 1.5), we treat that as if only
  // `oversubscribed_resources` was set.
  optional bool update_oversubscribed_resources = 5;
  repeated Resource oversubscribed_resources = 2;

  message Operations {
    repeated Operation operations = 1;
  }

  // Pending operations or terminal operations that have
  // unacknowledged status updates, which are known to this agent.
  optional Operations operations = 6;

  // Used to establish the relationship between the operation and the
  // resources that the operation is operating on. Each agent will
  // keep a resource version UUID, and change it when it believes that
  // its resources are out of sync from the master's view. The master
  // will keep track of the last known resource version UUID for each
  // resource provider or agent, and attach the resource version UUID
  // in each operation it sends out. The resource provider or agent
  // should reject operations that have a different resource version
  // UUID than that it maintains, because this means the operation is
  // operating on resources that might have already been invalidated.
  optional UUID resource_version_uuid = 7;

  // Describes an agent-local resource provider.
  message ResourceProvider {
    optional ResourceProviderInfo info = 1;
    repeated Resource total_resources = 2;
    required Operations operations = 3;
    required UUID resource_version_uuid = 4;
  }
