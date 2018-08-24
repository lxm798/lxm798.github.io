```cpp
void Master::__reregisterSlave(
    const UPID& pid,
    ReregisterSlaveMessage&& reregisterSlaveMessage,
    const Future<bool>& future) {
      addSlave(slave, std::move(completedFrameworks));
}
void Master::__registerSlave(
  Slave* slave = new Slave(
      this,
      slaveInfo,
      pid,
      machineId,
      registerSlaveMessage.version(),
      std::move(agentCapabilities),
      Clock::now(),
      std::move(checkpointedResources),
      resourceVersion);

  ++metrics->slave_registrations;

  addSlave(slave, {});
}
```