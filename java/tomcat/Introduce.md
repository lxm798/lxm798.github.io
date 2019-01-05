# tomcat 

## introduce/当前印象
tomcat是一个http请求处理框架,根据用户配置的路由规则,将请求分发的对应的tomcat或者抛出异常. 和nginx的区别?

### 和竞品的区别
性能:弱于nginx 原因?

### HTTP
状态码 100的处理


### tomcat 载入器
tomcat 载入器 如何重载?

### session
session 如何绑定到request上?

### Mapper

// 用于根据协议找到对应的Container
public interface Mapper {
    public Container getContainer();
    public void setContainer(Container container);
    public String getProtocol();
    public void setProtocol(String protocol);
    public Container map(Request request, boolean update);


}

### Resources 
是什么概念?

## Servlet 初始化和调用流程
Servlet 主要函数
load\loadServlet\allocate\invoke
allocate 调用loadServlet
load 调用loadServlet
这两个的区别是什么?


### SystemLogHandler
这个和logger什么关系?

### 默认错误处理
默认错误处理的触发流程


### Service 和 Server的关系

### Start和Initialize的区别

### Host,Context,Wrapper
这些和web应用什么关系
webApp 对应到Context
### 配置之间的关系
第一个配置是什么,如何找到其他的配置
ContextConfig的作用
ContextConfig -> 对应web.xml?????
###