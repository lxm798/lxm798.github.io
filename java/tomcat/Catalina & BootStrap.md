#

## Catalina
```java
public void process(String args[]) {
    // catalina.home  -> user.dir
    setCatalinaHome();
    // catalina.base -> catalina.home
    setCatalinaBase();
    try {
        // 初始化参数解析
        if (arguments(args))
            execute();
    } catch (Exception e) {
        e.printStackTrace(System.out);
    }
}
```
```java
protected void start() {

    // XML 解析对象
    Digester digester = createStartDigester();
    File file = configFile();
    try {
        InputSource is =
            new InputSource("file://" + file.getAbsolutePath());
        FileInputStream fis = new FileInputStream(file);
        is.setByteStream(fis);
        digester.push(this);
        digester.parse(is);
        fis.close();
    } catch (Exception e) {
        System.out.println("Catalina.start: " + e);
        e.printStackTrace(System.out);
        System.exit(1);
    }

    // 设置环境变量
    if (!useNaming) {
        System.setProperty("catalina.useNaming", "false");
    } else {
        System.setProperty("catalina.useNaming", "true");
        String value = "org.apache.naming";
        String oldValue =
            System.getProperty(javax.naming.Context.URL_PKG_PREFIXES);
        if (oldValue != null) {
            value = value + ":" + oldValue;
        }
        System.setProperty(javax.naming.Context.URL_PKG_PREFIXES, value);
        value = System.getProperty
            (javax.naming.Context.INITIAL_CONTEXT_FACTORY);
        if (value == null) {
            System.setProperty
                (javax.naming.Context.INITIAL_CONTEXT_FACTORY,
                    "org.apache.naming.java.javaURLContextFactory");
        }
    }

    // If a SecurityManager is being used, set properties for
    // checkPackageAccess() and checkPackageDefinition
    if( System.getSecurityManager() != null ) {
        String access = Security.getProperty("package.access");
        if( access != null && access.length() > 0 )
            access += ",";
        else
            access = "sun.,";
        Security.setProperty("package.access",
            access + "org.apache.catalina.,org.apache.jasper.");
        String definition = Security.getProperty("package.definition");
        if( definition != null && definition.length() > 0 )
            definition += ",";
        else
            definition = "sun.,";
        Security.setProperty("package.definition",
            // FIX ME package "javax." was removed to prevent HotSpot
            // fatal internal errors
            definition + "java.,org.apache.catalina.,org.apache.jasper.");
    }

    // 重定向标准输出和标准错误
    SystemLogHandler log = new SystemLogHandler(System.out);
    System.setOut(log);
    System.setErr(log);
    // 设置ShutDownHook,进程退出时关闭Server
    Thread shutdownHook = new CatalinaShutdownHook();

    // 启动Server
    if (server instanceof Lifecycle) {
        try {
            server.initialize();
            ((Lifecycle) server).start();
            try {
                // Register shutdown hook
                Runtime.getRuntime().addShutdownHook(shutdownHook);
            } catch (Throwable t) {
                // This will fail on JDK 1.2. Ignoring, as Tomcat can run
                // fine without the shutdown hook.
            }
            // Wait for the server to be told to shut down
            server.await();
        } catch (LifecycleException e) {
            System.out.println("Catalina.start: " + e);
            e.printStackTrace(System.out);
            if (e.getThrowable() != null) {
                System.out.println("----- Root Cause -----");
                e.getThrowable().printStackTrace(System.out);
            }
        }
    }

    // 命令控制的退出就不需要通过hook关闭
    if (server instanceof Lifecycle) {
        try {
            try {
                // Remove the ShutdownHook first so that server.stop()
                // doesn't get invoked twice
                Runtime.getRuntime().removeShutdownHook(shutdownHook);
            } catch (Throwable t) {
                // This will fail on JDK 1.2. Ignoring, as Tomcat can run
                // fine without the shutdown hook.
            }
            ((Lifecycle) server).stop();
        } catch (LifecycleException e) {
            System.out.println("Catalina.stop: " + e);
            e.printStackTrace(System.out);
            if (e.getThrowable() != null) {
                System.out.println("----- Root Cause -----");
                e.getThrowable().printStackTrace(System.out);
            }
        }
    }

}
```

## BootStrap
```java
public static void main(String args[]) {

    // Set the debug flag appropriately
    for (int i = 0; i < args.length; i++)  {
        if ("-debug".equals(args[i]))
            debug = 1;
    }
    
    // Configure catalina.base from catalina.home if not yet set
    if (System.getProperty("catalina.base") == null)
        System.setProperty("catalina.base", getCatalinaHome());

    // Construct the class loaders we will need
    ClassLoader commonLoader = null;
    ClassLoader catalinaLoader = null;
    ClassLoader sharedLoader = null;
    try {

        File unpacked[] = new File[1];
        File packed[] = new File[1];
        File packed2[] = new File[2];
        ClassLoaderFactory.setDebug(debug);

        unpacked[0] = new File(getCatalinaHome(),
                                "common" + File.separator + "classes");
        packed2[0] = new File(getCatalinaHome(),
                                "common" + File.separator + "endorsed");
        packed2[1] = new File(getCatalinaHome(),
                                "common" + File.separator + "lib");
        commonLoader =
            ClassLoaderFactory.createClassLoader(unpacked, packed2, null);

        unpacked[0] = new File(getCatalinaHome(),
                                "server" + File.separator + "classes");
        packed[0] = new File(getCatalinaHome(),
                                "server" + File.separator + "lib");
        catalinaLoader =
            ClassLoaderFactory.createClassLoader(unpacked, packed,
                                                    commonLoader);

        unpacked[0] = new File(getCatalinaBase(),
                                "shared" + File.separator + "classes");
        packed[0] = new File(getCatalinaBase(),
                                "shared" + File.separator + "lib");
        sharedLoader =
            ClassLoaderFactory.createClassLoader(unpacked, packed,
                                                    commonLoader);
    } catch (Throwable t) {

        log("Class loader creation threw exception", t);
        System.exit(1);

    }

    Thread.currentThread().setContextClassLoader(catalinaLoader);
    try {

        SecurityClassLoad.securityClassLoad(catalinaLoader);

        // Instantiate a startup class instance
        if (debug >= 1)
            log("Loading startup class");
        Class startupClass =
            catalinaLoader.loadClass
            ("org.apache.catalina.startup.Catalina");
        Object startupInstance = startupClass.newInstance();

        // 初始化Catalina的parent classloader 为sharedLoader
        if (debug >= 1)
            log("Setting startup class properties");
        String methodName = "setParentClassLoader";
        Class paramTypes[] = new Class[1];
        paramTypes[0] = Class.forName("java.lang.ClassLoader");
        Object paramValues[] = new Object[1];
        paramValues[0] = sharedLoader;
        Method method =
            startupInstance.getClass().getMethod(methodName, paramTypes);
        method.invoke(startupInstance, paramValues);

        // 调用catalina的process接口
        if (debug >= 1)
            log("Calling startup class process() method");
        methodName = "process";
        paramTypes = new Class[1];
        paramTypes[0] = args.getClass();
        paramValues = new Object[1];
        paramValues[0] = args;
        method =
            startupInstance.getClass().getMethod(methodName, paramTypes);
        method.invoke(startupInstance, paramValues);

    } catch (Exception e) {
        System.out.println("Exception during startup processing");
        e.printStackTrace(System.out);
        System.exit(2);
    }
}
```