
#
## Server
```java
// 初始化每个service
public void initialize()
throws LifecycleException {
    if (initialized)
        throw new LifecycleException (
            sm.getString("standardServer.initialize.initialized"));
    initialized = true;

    // Initialize our defined Services
    for (int i = 0; i < services.length; i++) {
        services[i].initialize();
    }
}
// 启动每个Service
public void start() throws LifecycleException {

    if (started)
        throw new LifecycleException
            (sm.getString("standardServer.start.started"));

    lifecycle.fireLifecycleEvent(BEFORE_START_EVENT, null);

    lifecycle.fireLifecycleEvent(START_EVENT, null);
    started = true;

    synchronized (services) {
        for (int i = 0; i < services.length; i++) {
            if (services[i] instanceof Lifecycle)
                ((Lifecycle) services[i]).start();
        }
    }

    lifecycle.fireLifecycleEvent(AFTER_START_EVENT, null);
}
```

```java
// 由外部调用,等待结束
public void await() {

    ServerSocket serverSocket = null;
    try {
        serverSocket =
            new ServerSocket(port, 1,
                                InetAddress.getByName("127.0.0.1"));
    } catch (IOException e) {
        System.err.println("StandardServer.await: create[" + port
                            + "]: " + e);
        e.printStackTrace();
        System.exit(1);
    }

    // Loop waiting for a connection and a valid command
    while (true) {

        Socket socket = null;
        InputStream stream = null;
        try {
            socket = serverSocket.accept();
            socket.setSoTimeout(10 * 1000);  // Ten seconds
            stream = socket.getInputStream();
        } catch (AccessControlException ace) {
            System.err.println("StandardServer.accept security exception: "
                                + ace.getMessage());
            continue;
        } catch (IOException e) {
            System.err.println("StandardServer.await: accept: " + e);
            e.printStackTrace();
            System.exit(1);
        }

        // Read a set of characters from the socket
        StringBuffer command = new StringBuffer();
        int expected = 1024; // Cut off to avoid DoS attack
        while (expected < shutdown.length()) {
            if (random == null)
                random = new Random(System.currentTimeMillis());
            expected += (random.nextInt() % 1024);
        }
        while (expected > 0) {
            int ch = -1;
            try {
                ch = stream.read();
            } catch (IOException e) {
                System.err.println("StandardServer.await: read: " + e);
                e.printStackTrace();
                ch = -1;
            }
            if (ch < 32)  // Control character or EOF terminates loop
                break;
            command.append((char) ch);
            expected--;
        }

        // Close the socket now that we are done with it
        try {
            socket.close();
        } catch (IOException e) {
            ;
        }

        // Match against our command string
        boolean match = command.toString().equals(shutdown);
        if (match) {
            break;
        } else
            System.err.println("StandardServer.await: Invalid command '" +
                                command.toString() + "' received");

    }

    // Close the server socket and return
    try {
        serverSocket.close();
    } catch (IOException e) {
        ;
    }

}
```

## Service
```java
public void setContainer(Container container) {
    // 只能是Engine
    Container oldContainer = this.container;
    if ((oldContainer != null) && (oldContainer instanceof Engine))
        ((Engine) oldContainer).setService(null);
    this.container = container;
    if ((this.container != null) && (this.container instanceof Engine))
        ((Engine) this.container).setService(this);
    // 启动Container,但是started状态下,container已经start 了,这里是重新设置container
    // 运行中更新container
    if (started && (this.container != null) &&
        (this.container instanceof Lifecycle)) {
        try {
            ((Lifecycle) this.container).start();
        } catch (LifecycleException e) {
            ;
        }
    }
    // 给每个connector设置container, 每个connector 目前 只有一个container
    synchronized (connectors) {
        for (int i = 0; i < connectors.length; i++)
            connectors[i].setContainer(this.container);
    }
    // 停止老的container
    if (started && (oldContainer != null) &&
        (oldContainer instanceof Lifecycle)) {
        try {
            ((Lifecycle) oldContainer).stop();
        } catch (LifecycleException e) {
            ;
        }
    }

    // PropertyChangeEvent
    support.firePropertyChange("container", oldContainer, this.container);
}

public void start() throws LifecycleException {

    // 验证状态
    if (started) {
        throw new LifecycleException
            (sm.getString("standardService.start.started"));
    }

    // Notify our interested LifecycleListeners
    lifecycle.fireLifecycleEvent(BEFORE_START_EVENT, null);

    System.out.println
        (sm.getString("standardService.start.name", this.name));
    lifecycle.fireLifecycleEvent(START_EVENT, null);
    started = true;

    // start container
    if (container != null) {
        synchronized (container) {
            if (container instanceof Lifecycle) {
                ((Lifecycle) container).start();
            }
        }
    }

    // start connector
    synchronized (connectors) {
        for (int i = 0; i < connectors.length; i++) {
            if (connectors[i] instanceof Lifecycle)
                ((Lifecycle) connectors[i]).start();
        }
    }

    // Notify our interested LifecycleListeners
    lifecycle.fireLifecycleEvent(AFTER_START_EVENT, null);

}
```