  // Make sure the executor registers within the given timeout.
  delay(flags.executor_registration_timeout,
        self(),
        &Self::registerExecutorTimeout,
        frameworkId,
        executor->id,
        executor->containerId);

  return;
}


  if (message.to.address == __address__) {
    // Local message.
    MessageEvent* event = new MessageEvent(std::move(message));
    process_manager->deliver(event->message.to, event, sender);
  } else {
    // Remote message.
    socket_manager->send(std::move(message));
  }