# spring PostProcessor调用处理

## 引言
先看下堆栈
```java
"localhost-startStop-1@3340" daemon prio=5 tid=0xe nid=NA runnable
  java.lang.Thread.State: RUNNABLE
	  at com.netease.vcloud.vodproperty.server.api.HealthController.aa(HealthController.java:47)
	  at sun.reflect.NativeMethodAccessorImpl.invoke0(NativeMethodAccessorImpl.java:-1)
	  at sun.reflect.NativeMethodAccessorImpl.invoke(NativeMethodAccessorImpl.java:57)
	  at sun.reflect.DelegatingMethodAccessorImpl.invoke(DelegatingMethodAccessorImpl.java:43)
	  at java.lang.reflect.Method.invoke(Method.java:606)
	  at org.springframework.beans.factory.annotation.InitDestroyAnnotationBeanPostProcessor$LifecycleElement.invoke(InitDestroyAnnotationBeanPostProcessor.java:365)
	  at org.springframework.beans.factory.annotation.InitDestroyAnnotationBeanPostProcessor$LifecycleMetadata.invokeInitMethods(InitDestroyAnnotationBeanPostProcessor.java:310)
	  at org.springframework.beans.factory.annotation.InitDestroyAnnotationBeanPostProcessor.postProcessBeforeInitialization(InitDestroyAnnotationBeanPostProcessor.java:133)
	  at org.springframework.beans.factory.support.AbstractAutowireCapableBeanFactory.applyBeanPostProcessorsBeforeInitialization(AbstractAutowireCapableBeanFactory.java:408)
	  at org.springframework.beans.factory.support.AbstractAutowireCapableBeanFactory.initializeBean(AbstractAutowireCapableBeanFactory.java:1570)
	  at org.springframework.beans.factory.support.AbstractAutowireCapableBeanFactory.doCreateBean(AbstractAutowireCapableBeanFactory.java:545)
	  at org.springframework.beans.factory.support.AbstractAutowireCapableBeanFactory.createBean(AbstractAutowireCapableBeanFactory.java:482)
	  at org.springframework.beans.factory.support.AbstractBeanFactory$1.getObject(AbstractBeanFactory.java:306)
	  at org.springframework.beans.factory.support.DefaultSingletonBeanRegistry.getSingleton(DefaultSingletonBeanRegistry.java:230)
	  - locked <0x1c11> (a java.util.concurrent.ConcurrentHashMap)
	  at org.springframework.beans.factory.support.AbstractBeanFactory.doGetBean(AbstractBeanFactory.java:302)
	  at org.springframework.beans.factory.support.AbstractBeanFactory.getBean(AbstractBeanFactory.java:197)
	  at org.springframework.beans.factory.support.DefaultListableBeanFactory.preInstantiateSingletons(DefaultListableBeanFactory.java:776)
	  at org.springframework.context.support.AbstractApplicationContext.finishBeanFactoryInitialization(AbstractApplicationContext.java:861)
	  at org.springframework.context.support.AbstractApplicationContext.refresh(AbstractApplicationContext.java:541)
	  - locked <0x734b> (a java.lang.Object)
	  at org.springframework.web.servlet.FrameworkServlet.configureAndRefreshWebApplicationContext(FrameworkServlet.java:668)
	  at org.springframework.web.servlet.FrameworkServlet.createWebApplicationContext(FrameworkServlet.java:634)
	  at org.springframework.web.servlet.FrameworkServlet.createWebApplicationContext(FrameworkServlet.java:682)
	  at org.springframework.web.servlet.FrameworkServlet.initWebApplicationContext(FrameworkServlet.java:553)
	  at org.springframework.web.servlet.FrameworkServlet.initServletBean(FrameworkServlet.java:494)
	  at org.springframework.web.servlet.HttpServletBean.init(HttpServletBean.java:136)
	  at javax.servlet.GenericServlet.init(GenericServlet.java:160)
	  at org.apache.catalina.core.StandardWrapper.initServlet(StandardWrapper.java:1280)
	  - locked <0x1c25> (a org.apache.catalina.core.StandardWrapper)
	  at org.apache.catalina.core.StandardWrapper.loadServlet(StandardWrapper.java:1193)
	  at org.apache.catalina.core.StandardWrapper.load(StandardWrapper.java:1088)
	  at org.apache.catalina.core.StandardContext.loadOnStartup(StandardContext.java:5176)
	  at org.apache.catalina.core.StandardContext.startInternal(StandardContext.java:5460)
	  - locked <0x1c26> (a org.apache.catalina.core.StandardContext)
	  at org.apache.catalina.util.LifecycleBase.start(LifecycleBase.java:150)
	  at org.apache.catalina.core.ContainerBase$StartChild.call(ContainerBase.java:1559)
	  at org.apache.catalina.core.ContainerBase$StartChild.call(ContainerBase.java:1549)
	  at java.util.concurrent.FutureTask.run(FutureTask.java:262)
	  at java.util.concurrent.ThreadPoolExecutor.runWorker(ThreadPoolExecutor.java:1145)
	  at java.util.concurrent.ThreadPoolExecutor$Worker.run(ThreadPoolExecutor.java:615)
	  at java.lang.Thread.run(Thread.java:745)

```
处理@PostConstruct对象的类是
```java
beanPostProcessors
0 = {ApplicationContextAwareProcessor@7306} 
1 = {ServletContextAwareProcessor@7307} 
2 = {PostProcessorRegistrationDelegate$BeanPostProcessorChecker@7308} 
3 = {ConfigurationClassPostProcessor$ImportAwareBeanPostProcessor@7309} 
4 = {ConfigurationClassPostProcessor$EnhancedConfigurationBeanPostProcessor@7310} 
5 = {CommonAnnotationBeanPostProcessor@7228} 
6 = {AutowiredAnnotationBeanPostProcessor@7311} 
7 = {RequiredAnnotationBeanPostProcessor@7312} 
8 = {PostProcessorRegistrationDelegate$ApplicationListenerDetector@7313}
```

初始化实际的注入规则在
```java
public static Set<BeanDefinitionHolder> AnnotationConfigUtils::registerAnnotationConfigProcessors();
```
在初始化BeanFactory的时候进行初始化,加载上述函数
```java
protected final void refreshBeanFactory() throws BeansException {
	if (hasBeanFactory()) {
		destroyBeans();
		closeBeanFactory();
	}
	try {
		DefaultListableBeanFactory beanFactory = createBeanFactory();
		beanFactory.setSerializationId(getId());
		customizeBeanFactory(beanFactory);
		loadBeanDefinitions(beanFactory);
		synchronized (this.beanFactoryMonitor) {
			this.beanFactory = beanFactory;
		}
	}
	catch (IOException ex) {
		throw new ApplicationContextException("I/O error parsing bean definition source for " + getDisplayName(), ex);
	}
}
```
在refresh时初始化对象
```java
@Override
public void refresh() throws BeansException, IllegalStateException {
	synchronized (this.startupShutdownMonitor) {
		// Prepare this context for refreshing.
		prepareRefresh();

		// Tell the subclass to refresh the internal bean factory.
		ConfigurableListableBeanFactory beanFactory = obtainFreshBeanFactory();

		// Prepare the bean factory for use in this context.
		prepareBeanFactory(beanFactory);

		try {
			// Allows post-processing of the bean factory in context subclasses.
			postProcessBeanFactory(beanFactory);

			// Invoke factory processors registered as beans in the context.
			invokeBeanFactoryPostProcessors(beanFactory);

			// Register bean processors that intercept bean creation.
			registerBeanPostProcessors(beanFactory);

			// Initialize message source for this context.
			initMessageSource();

			// Initialize event multicaster for this context.
			initApplicationEventMulticaster();

			// Initialize other special beans in specific context subclasses.
			onRefresh();

			// Check for listener beans and register them.
			registerListeners();

			// Instantiate all remaining (non-lazy-init) singletons.
			finishBeanFactoryInitialization(beanFactory);

			// Last step: publish corresponding event.
			finishRefresh();
		}

		catch (BeansException ex) {
			if (logger.isWarnEnabled()) {
				logger.warn("Exception encountered during context initialization - " +
						"cancelling refresh attempt: " + ex);
			}

			// Destroy already created singletons to avoid dangling resources.
			destroyBeans();

			// Reset 'active' flag.
			cancelRefresh(ex);

			// Propagate exception to caller.
			throw ex;
		}

		finally {
			// Reset common introspection caches in Spring's core, since we
			// might not ever need metadata for singleton beans anymore...
			resetCommonCaches();
		}
	}
}
```
postConstruct依赖于CommonAnnotationBeanPostProcessor处理
处理过程
```java
public Object postProcessBeforeInitialization(Object bean, String beanName) throws BeansException {
	// 找到interface javax.annotation.PostConstruct 或者 interface javax.annotation.PreDestroy注解的函数
	InitDestroyAnnotationBeanPostProcessor.LifecycleMetadata metadata = this.findLifecycleMetadata(bean.getClass());

	try {
		metadata.invokeInitMethods(bean, beanName);
		return bean;
	} catch (InvocationTargetException var5) {
		throw new BeanCreationException(beanName, "Invocation of init method failed", var5.getTargetException());
	} catch (Throwable var6) {
		throw new BeanCreationException(beanName, "Failed to invoke init method", var6);
	}
}
```
```java
private LifecycleMetadata findLifecycleMetadata(Class<?> clazz) {
	if (this.lifecycleMetadataCache == null) {
		// Happens after deserialization, during destruction...
		return buildLifecycleMetadata(clazz);
	}
	// Quick check on the concurrent map first, with minimal locking.
	LifecycleMetadata metadata = this.lifecycleMetadataCache.get(clazz);
	if (metadata == null) {
		synchronized (this.lifecycleMetadataCache) {
			metadata = this.lifecycleMetadataCache.get(clazz);
			if (metadata == null) {
				metadata = buildLifecycleMetadata(clazz);
				this.lifecycleMetadataCache.put(clazz, metadata);
			}
			return metadata;
		}
	}
	return metadata;
}
```
```java
private LifecycleMetadata buildLifecycleMetadata(final Class<?> clazz) {
	final boolean debug = logger.isDebugEnabled();
	// PostConstruct注解
	LinkedList<LifecycleElement> initMethods = new LinkedList<LifecycleElement>();
	// PreDestroy注解
	LinkedList<LifecycleElement> destroyMethods = new LinkedList<LifecycleElement>();
	Class<?> targetClass = clazz;
	// 循环结构用于遍历所有的基类
	do {
		final LinkedList<LifecycleElement> currInitMethods = new LinkedList<LifecycleElement>();
		final LinkedList<LifecycleElement> currDestroyMethods = new LinkedList<LifecycleElement>();

		ReflectionUtils.doWithLocalMethods(targetClass, new ReflectionUtils.MethodCallback() {
			@Override
			public void doWith(Method method) throws IllegalArgumentException, IllegalAccessException {
				if (initAnnotationType != null) {
					if (method.getAnnotation(initAnnotationType) != null) {
						LifecycleElement element = new LifecycleElement(method);
						currInitMethods.add(element);
						if (debug) {
							logger.debug("Found init method on class [" + clazz.getName() + "]: " + method);
						}
					}
				}
				if (destroyAnnotationType != null) {
					if (method.getAnnotation(destroyAnnotationType) != null) {
						currDestroyMethods.add(new LifecycleElement(method));
						if (debug) {
							logger.debug("Found destroy method on class [" + clazz.getName() + "]: " + method);
						}
					}
				}
			}
		});

		initMethods.addAll(0, currInitMethods);
		destroyMethods.addAll(currDestroyMethods);
		targetClass = targetClass.getSuperclass();
	}
	while (targetClass != null && targetClass != Object.class);

	return new LifecycleMetadata(clazz, initMethods, destroyMethods);
}
```
```java
0 = {String@4911} "org.springframework.context.annotation.internalAutowiredAnnotationProcessor"
1 = {String@4912} "org.springframework.context.annotation.internalRequiredAnnotationProcessor"
2 = {String@4823} "org.springframework.context.annotation.internalCommonAnnotationProcessor"
3 = {String@4913} "org.springframework.aop.config.internalAutoProxyCreator"
4 = {String@4914} "org.springframework.context.annotation.ConfigurationClassPostProcessor.importAwareProcessor"
5 = {String@4915} "org.springframework.context.annotation.ConfigurationClassPostProcessor.enhancedConfigurationProcessor"
```