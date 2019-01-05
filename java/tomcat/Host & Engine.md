# 
## StandardHostValve
```java
public void invoke(Request request, Response response,
                    ValveContext valveContext)
    throws IOException, ServletException {

    // 只处理HttpServlet
    if (!(request.getRequest() instanceof HttpServletRequest) ||
        !(response.getResponse() instanceof HttpServletResponse)) {
        return;     // NOTE - Not much else we can do generically
    }

    // 获取匹配的Context
    StandardHost host = (StandardHost) getContainer();
    Context context = (Context) host.map(request, true);
    if (context == null) {
        ((HttpServletResponse) response.getResponse()).sendError
            (HttpServletResponse.SC_INTERNAL_SERVER_ERROR,
                sm.getString("standardHost.noContext"));
        return;
    }

    // ????
    Thread.currentThread().setContextClassLoader
        (context.getLoader().getClassLoader());

    // 设置session的访问记录
    HttpServletRequest hreq = (HttpServletRequest) request.getRequest();
    String sessionId = hreq.getRequestedSessionId();
    if (sessionId != null) {
        Manager manager = context.getManager();
        if (manager != null) {
            Session session = manager.findSession(sessionId);
            if ((session != null) && session.isValid())
                session.access();
        }
    }

    // 调用子容器进行处理
    context.invoke(request, response);

}
```

## StandardEngine
```java
public void invoke(Request request, Response response,
                    ValveContext valveContext)
    throws IOException, ServletException {
    // 验证请求的合法性
    if (!(request.getRequest() instanceof HttpServletRequest) ||
        !(response.getResponse() instanceof HttpServletResponse)) {
        return;     // NOTE - Not much else we can do generically
    }

    // 验证请求是HTTP1.1的,ServerName?
    HttpServletRequest hrequest = (HttpServletRequest) request;
    if ("HTTP/1.1".equals(hrequest.getProtocol()) &&
        (hrequest.getServerName() == null)) {
        ((HttpServletResponse) response.getResponse()).sendError
            (HttpServletResponse.SC_BAD_REQUEST,
                sm.getString("standardEngine.noHostHeader",
                            request.getRequest().getServerName()));
        return;
    }

    // 获取Host
    StandardEngine engine = (StandardEngine) getContainer();
    Host host = (Host) engine.map(request, true);
    if (host == null) {
        ((HttpServletResponse) response.getResponse()).sendError
            (HttpServletResponse.SC_BAD_REQUEST,
                sm.getString("standardEngine.noHost",
                            request.getRequest().getServerName()));
        return;
    }

    // 交给Host处理
    host.invoke(request, response);

}
```