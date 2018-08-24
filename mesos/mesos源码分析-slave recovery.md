  # mesos 源码分析-slave recovery
  ## slave recovery
  ### 相关参数
  |配置 | 说明|
|:----:|:---:|
|strict | 如果strict=true,任何recovery错误都会导致agent启动失败,如果strict=false,启动时的错误会被忽略,以便尽可能的恢复运行状态|
|reconfiguration_policy| 如果时equal,启动选项和重启前要严格一致,如果是additive,允许增加资源或者属性|
|recover| 如果时reconnect,允许任何executors重连,如果是clean,会直接kill之前的executor|
|recovery_timeout|如果agent 恢复时间超过recovery_timeout,任何executors会self-terminate. 还不清楚executor有没有回调|

### 流程
Slave 启动时在初始化阶段恢复之前executor和task的信息和状态
```cpp
void Slave::initialize()
{
      // Do recovery.
  async(&state::recover, metaDir, flags.strict)
    .then(defer(self(), &Slave::recover, lambda::_1))
    .then(defer(self(), &Slave::_recover))
    .onAny(defer(self(), &Slave::__recover, lambda::_1));
}
```
recover: 遍历root目录下的文件,找到executor和task的信息,同时发送之前未发送成功的消息
_recover: 如果recover的状态是"reconnect",恢复executor,清除一定延时内没有恢复的executor.
__recover: 清楚之前slave的meta的数据,重新连接master,但是在ping超时后也没有shutdown.
```cpp
```
  ```cpp
  Future<Nothing> TaskStatusUpdateManagerProcess::recover(
    const string& rootDir,
    const Option<SlaveState>& state)
{
  LOG(INFO) << "Recovering task status update manager";

  if (state.isNone()) {
    return Nothing();
  }

  foreachvalue (const FrameworkState& framework, state->frameworks) {
    foreachvalue (const ExecutorState& executor, framework.executors) {
      LOG(INFO) << "Recovering executor '" << executor.id
                << "' of framework " << framework.id;

      if (executor.info.isNone()) {
        LOG(WARNING) << "Skipping recovering task status updates of"
                     << " executor '" << executor.id
                     << "' of framework " << framework.id
                     << " because its info cannot be recovered";
        continue;
      }

      if (executor.latest.isNone()) {
        LOG(WARNING) << "Skipping recovering task status updates of"
                     << " executor '" << executor.id
                     << "' of framework " << framework.id
                     << " because its latest run cannot be recovered";
        continue;
      }

      // We are only interested in the latest run of the executor!
      const ContainerID& latest = executor.latest.get();
      Option<RunState> run = executor.runs.get(latest);
      CHECK_SOME(run);

      if (run->completed) {
        VLOG(1) << "Skipping recovering task status updates of"
                << " executor '" << executor.id
                << "' of framework " << framework.id
                << " because its latest run " << latest.value()
                << " is completed";
        continue;
      }

      foreachvalue (const TaskState& task, run->tasks) {
        // No updates were ever received for this task!
        // This means either:
        // 1) the executor never received this task or
        // 2) executor launched it but the slave died before it got any updates.
        if (task.updates.empty()) {
          LOG(WARNING) << "No status updates found for task " << task.id
                       << " of framework " << framework.id;
          continue;
        }

        // Create a new status update stream.
        TaskStatusUpdateStream* stream = createStatusUpdateStream(
            task.id, framework.id, state->id, true, executor.id, latest);

        // Replay the stream.
        Try<Nothing> replay = stream->replay(task.updates, task.acks);
        if (replay.isError()) {
          return Failure(
              "Failed to replay status updates for task " + stringify(task.id) +
              " of framework " + stringify(framework.id) +
              ": " + replay.error());
        }

        // At the end of the replay, the stream is either terminated or
        // contains only unacknowledged, if any, pending updates. The
        // pending updates will be flushed after the slave
        // reregisters with the master.
        if (stream->terminated) {
          cleanupStatusUpdateStream(task.id, framework.id);
        }
      }
    }
  }

  return Nothing();
}
  ```
  slave在启动的时候检查work_dir,确定启动前的task\executor的信息. 保存在work_dir的meta 目录下
  
  // We pause the task status update manager so that it doesn't forward any
  // updates while the slave is still recovering. It is unpaused/resumed when
  // the slave (re-)registers with the master.
  taskStatusUpdateManager->pause();

    // Do recovery.
  async(&state::recover, metaDir, flags.strict)
    .then(defer(self(), &Slave::recover, lambda::_1))
    .then(defer(self(), &Slave::_recover))
    .onAny(defer(self(), &Slave::__recover, lambda::_1));


    message Task {
      required string name = 1;
      required TaskID id = 2;
      repeated Resource resources = 3;
      optional Labels labels = 4;
    }

    message StatusUpdateRecord {
  enum Type {
    UPDATE = 0;
    ACK = 1;
  }

  required Type type = 1;

  // Required if type == UPDATE.
  optional StatusUpdate update = 2;

  // Required if type == ACK.
  optional bytes uuid = 3;
}