#
##
```java
    public synchronized void start() throws LifecycleException {
        // 当前已经启动的情况下,又重新启动
        if (started)
            throw new LifecycleException
                (sm.getString("containerBase.alreadyStarted", logName()));
        // 打印debug 日志
        if (debug >= 1)
            log("Starting");

        // 通知监听器
        lifecycle.fireLifecycleEvent(BEFORE_START_EVENT, null);

        if (debug >= 1)
            log("Processing start(), current available=" + getAvailable());
        // 设置available 和configured 为false
        setAvailable(false);
        setConfigured(false);
        boolean ok = true;

        // Add missing components as necessary
        if (getResources() == null) {   // (1) Required by Loader
            if (debug >= 1)
                log("Configuring default Resources");
            try {
                if ((docBase != null) && (docBase.endsWith(".war")))
                    setResources(new WARDirContext());
                else
                    setResources(new FileDirContext());
            } catch (IllegalArgumentException e) {
                log("Error initializing resources: " + e.getMessage());
                ok = false;
            }
        }
        if (ok && (resources instanceof ProxyDirContext)) {
            DirContext dirContext =
                ((ProxyDirContext) resources).getDirContext();
            if ((dirContext != null)
                && (dirContext instanceof BaseDirContext)) {
                ((BaseDirContext) dirContext).setDocBase(getBasePath());
                ((BaseDirContext) dirContext).allocate();
            }
        }
        if (getLoader() == null) {
            if (getPrivileged()) {
                if (debug >= 1)
                    log("Configuring privileged default Loader");
                setLoader(new WebappLoader(this.getClass().getClassLoader()));
            } else {
                if (debug >= 1)
                    log("Configuring non-privileged default Loader");
                // 设置默认loader,loader 用于加载Servlet
                setLoader(new WebappLoader(getParentClassLoader()));
            }
        }
        if (getManager() == null) {     // (3) After prerequisites
            if (debug >= 1)
                log("Configuring default Manager");
            // 设置默认StandarManager,管理session
            setManager(new StandardManager());
        }

        // org.apache.catalina.util.CharsetMapper  ??
        getCharsetMapper();

        // 构造workDir 目录为 worl/engineName/hostName/temp    temp是当前Context的name
        // 创建catalina.base目录,设置javax.servlet.context.tempdir环境变量
        // Context提供的临时目录的路径，用于servlet的临时读/写。利用javax.servlet.context.tempdir属性，servlet可以访问该目录。如果没有指定，使用$CATALINA_HOME/work下一个合适的目录
        postWorkDirectory();

        // 如果希望Catalina为该web应用使能一个JNDIInitialContext对象，设为true
        String useNamingProperty = System.getProperty("catalina.useNaming");
        if ((useNamingProperty != null)
            && (useNamingProperty.equals("false"))) {
            useNaming = false;
        }

        if (ok && isUseNaming()) {
            if (namingContextListener == null) {
                namingContextListener = new NamingContextListener();
                namingContextListener.setDebug(getDebug());
                namingContextListener.setName(getNamingContextName());
                addLifecycleListener(namingContextListener);
            }
        }

        // Binding thread
        ClassLoader oldCCL = bindThread();

        // Standard container startup
        if (debug >= 1)
            log("Processing standard container startup");

        if (ok) {

            try {
                // org.apache.catalina.core.StandardContextMapper
                addDefaultMapper(this.mapperClass);
                started = true;

                // webloader和logger初始化
                if ((loader != null) && (loader instanceof Lifecycle))
                    ((Lifecycle) loader).start();
                if ((logger != null) && (logger instanceof Lifecycle))
                    ((Lifecycle) logger).start();

                // ????
                unbindThread(oldCCL);

                // ????
                oldCCL = bindThread();

                if ((cluster != null) && (cluster instanceof Lifecycle))
                    ((Lifecycle) cluster).start();
                if ((realm != null) && (realm instanceof Lifecycle))
                    ((Lifecycle) realm).start();
                if ((resources != null) && (resources instanceof Lifecycle))
                    ((Lifecycle) resources).start();

                // 初始化所有Mapper
                Mapper mappers[] = findMappers();
                for (int i = 0; i < mappers.length; i++) {
                    if (mappers[i] instanceof Lifecycle)
                        ((Lifecycle) mappers[i]).start();
                }

                // 初始化所有的子Container
                Container children[] = findChildren();
                for (int i = 0; i < children.length; i++) {
                    if (children[i] instanceof Lifecycle)
                        ((Lifecycle) children[i]).start();
                }

                // 初始化Valve pipeline 和 basic Valve
                if (pipeline instanceof Lifecycle)
                    ((Lifecycle) pipeline).start();

                // START_EVENT
                lifecycle.fireLifecycleEvent(START_EVENT, null);
                // session manager 初始化
                if ((manager != null) && (manager instanceof Lifecycle))
                    ((Lifecycle) manager).start();

            } finally {
                // Unbinding thread
                unbindThread(oldCCL);
            }

        }
        // 如果没有listener设置为configured, 不正常
        if (!getConfigured())
            ok = false;

        // We put the resources into the servlet context
        if (ok)
            getServletContext().setAttribute
                (Globals.RESOURCES_ATTR, getResources());

        // Binding thread
        oldCCL = bindThread();

        // Create context attributes that will be required
        if (ok) {
            if (debug >= 1)
                log("Posting standard context attributes");
            // 设置org.apache.catalina.WELCOME_FILES 属性
            postWelcomeFiles();
        }

        // ?????
        if (ok) {
            if (!listenerStart())
                ok = false;
        }
        if (ok) {
            // 初始化所有的filter
            if (!filterStart())
                ok = false;
        }

        // 初始化 "load on startup"的类
        if (ok)
            loadOnStartup(findChildren());

        // ?????
        unbindThread(oldCCL);

        // Set available status depending upon startup success
        if (ok) {
            if (debug >= 1)
                log("Starting completed");
            // Avaliable 是内部初始化完成,Configured是外部监听到的正常
            setAvailable(true);
        } else {
            log(sm.getString("standardContext.startFailed"));
            try {
                stop();
            } catch (Throwable t) {
                log(sm.getString("standardContext.startCleanup"), t);
            }
            setAvailable(false);
        }

        // Notify our interested LifecycleListeners
        lifecycle.fireLifecycleEvent(AFTER_START_EVENT, null);

    }
```

```java
private ClassLoader bindThread() {

    ClassLoader oldContextClassLoader =
        Thread.currentThread().getContextClassLoader();
    // ???
    if (getResources() == null)
        return oldContextClassLoader;
    // 设置线程内部的classloader,部分代码会使用thread 内部的classloader
    Thread.currentThread().setContextClassLoader
        (getLoader().getClassLoader());

    DirContextURLStreamHandler.bind(getResources());

    if (isUseNaming()) {
        try {
            ContextBindings.bindThread(this, this);
        } catch (NamingException e) {
            // Silent catch, as this is a normal case during the early
            // startup stages
        }
    }

    return oldContextClassLoader;

}
```

```java
public Container map(Request request, boolean update) {
    int debug = context.getDebug();

    // Has this request already been mapped?
    if (update && (request.getWrapper() != null))
        return (request.getWrapper());

    // Identify the context-relative URI to be mapped
    String contextPath =
        ((HttpServletRequest) request.getRequest()).getContextPath();
    String requestURI = ((HttpRequest) request).getDecodedRequestURI();
    String relativeURI = requestURI.substring(contextPath.length());


    if (debug >= 1)
        context.log("Mapping contextPath='" + contextPath +
                    "' with requestURI='" + requestURI +
                    "' and relativeURI='" + relativeURI + "'");

    // Apply the standard request URI mapping rules from the specification
    Wrapper wrapper = null;
    String servletPath = relativeURI;
    String pathInfo = null;
    String name = null;
    // 找到和路径匹配的Wrapper
    // 如果满足条件返回Wrapper
    // Rule 1 -- Exact Match
    if (wrapper == null) {
        if (debug >= 2)
            context.log("  Trying exact match");
        if (!(relativeURI.equals("/")))
            name = context.findServletMapping(relativeURI);
        if (name != null)
            wrapper = (Wrapper) context.findChild(name);
        if (wrapper != null) {
            servletPath = relativeURI;
            pathInfo = null;
        }
    }

    // Rule 2 -- Prefix Match
    if (wrapper == null) {
        if (debug >= 2)
            context.log("  Trying prefix match");
        servletPath = relativeURI;
        while (true) {
            name = context.findServletMapping(servletPath + "/*");
            if (name != null)
                wrapper = (Wrapper) context.findChild(name);
            if (wrapper != null) {
                pathInfo = relativeURI.substring(servletPath.length());
                if (pathInfo.length() == 0)
                    pathInfo = null;
                break;
            }
            int slash = servletPath.lastIndexOf('/');
            if (slash < 0)
                break;
            servletPath = servletPath.substring(0, slash);
        }
    }

    // *.XX
    if (wrapper == null) {
        if (debug >= 2)
            context.log("  Trying extension match");
        int slash = relativeURI.lastIndexOf('/');
        if (slash >= 0) {
            String last = relativeURI.substring(slash);
            int period = last.lastIndexOf('.');
            if (period >= 0) {
                String pattern = "*" + last.substring(period);
                name = context.findServletMapping(pattern);
                if (name != null)
                    wrapper = (Wrapper) context.findChild(name);
                if (wrapper != null) {
                    servletPath = relativeURI;
                    pathInfo = null;
                }
            }
        }
    }

    // Rule 4 -- Default Match
    if (wrapper == null) {
        if (debug >= 2)
            context.log("  Trying default match");
        name = context.findServletMapping("/");
        if (name != null)
            wrapper = (Wrapper) context.findChild(name);
        if (wrapper != null) {
            servletPath = relativeURI;
            pathInfo = null;
        }
    }

    // Update the Request (if requested) and return this Wrapper
    if ((debug >= 1) && (wrapper != null))
        context.log(" Mapped to servlet '" + wrapper.getName() +
                    "' with servlet path '" + servletPath +
                    "' and path info '" + pathInfo +
                    "' and update=" + update);
    if (update) {
        request.setWrapper(wrapper);
        ((HttpRequest) request).setServletPath(servletPath);
        ((HttpRequest) request).setPathInfo(pathInfo);
    }
    return (wrapper);

}
```
匹配策略
1. 全匹配
2. 前缀匹配
3. 后缀匹配 *.jsp之类
4. 默认 /

## reload
reload 交给WebLoader,WebLoader在初始化时setContainer中获取是否需要reload,如果需要开启reload线程定时check 文件时间戳