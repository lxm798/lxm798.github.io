# applicationContext

## 概述
WebApplicationContext是专门为web应用准备的，它允许从相对于Web根目录的路径中装载资源配置文件完成初始化工作。

从WebApplication中可以获取ServletContext的引用，整个Web应用上线文对象作为属性放在到ServletContext中，以便Web应用能访问Spring应用上下文。

Spring专门为此提供了一个工具类WebApplicationContextUtils，通过该类的getWebApplicationContext(ServletContext sc)方法，可以从ServletContext中获取WebApplicationContext实例。



对BeanFactory进行了扩展
包括:
* BeanFactory 相关的类的初始化,PostProcessor
* 国际化处理
* 对象初始化
* BeanFactoryPostProcessor: PlaceHolderConfigure
* lifecircle对象管理
* spel 支持

## 加载
tomcat 的web.xml需要配置
```xml
<listener>
    <listener-class>org.springframework.web.context.ContextLoaderListener</listener-class>
</listener>
```
在ContextLoaderListener中调用
```java
public void contextInitialized(ServletContextEvent event) {
    this.initWebApplicationContext(event.getServletContext());
}
```
如果没有配置contextClass,默认contextClass就是WebApplicationContext
```java
protected Class<?> determineContextClass(ServletContext servletContext) {
    String contextClassName = servletContext.getInitParameter("contextClass");
    if (contextClassName != null) {
        try {
            return ClassUtils.forName(contextClassName, ClassUtils.getDefaultClassLoader());
        } catch (ClassNotFoundException var4) {
            throw new ApplicationContextException("Failed to load custom context class [" + contextClassName + "]", var4);
        }
    } else {
        contextClassName = defaultStrategies.getProperty(WebApplicationContext.class.getName());

        try {
            return ClassUtils.forName(contextClassName, ContextLoader.class.getClassLoader());
        } catch (ClassNotFoundException var5) {
            throw new ApplicationContextException("Failed to load default context class [" + contextClassName + "]", var5);
        }
    }
}
```

https://blog.csdn.net/yangshangwei/article/details/74962133