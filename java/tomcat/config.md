#
##
```java
private synchronized void start() {

    if (debug > 0)
        log(sm.getString("contextConfig.start"));
    context.setConfigured(false);
    ok = true;

    // Set properties based on DefaultContext
    Container container = context.getParent();
    if( !context.getOverride() ) {
        if( container instanceof Host ) {
            ((Host)container).importDefaultContext(context);
            container = container.getParent();
        }
        if( container instanceof Engine ) {
            ((Engine)container).importDefaultContext(context);
        }
    }

    // Process the default and application web.xml files
    defaultConfig();
    applicationConfig();
    if (ok) {
        validateSecurityRoles();
    }

    // Scan tag library descriptor files for additional listener classes
    if (ok) {
        try {
            tldScan();
        } catch (Exception e) {
            log(e.getMessage(), e);
            ok = false;
        }
    }

    // Configure a certificates exposer valve, if required
    if (ok)
        certificatesConfig();

    // Configure an authenticator if we need one
    if (ok)
        authenticatorConfig();

    // Dump the contents of this pipeline if requested
    if ((debug >= 1) && (context instanceof ContainerBase)) {
        log("Pipline Configuration:");
        Pipeline pipeline = ((ContainerBase) context).getPipeline();
        Valve valves[] = null;
        if (pipeline != null)
            valves = pipeline.getValves();
        if (valves != null) {
            for (int i = 0; i < valves.length; i++) {
                log("  " + valves[i].getInfo());
            }
        }
        log("======================");
    }

    // Make our application available if no problems were encountered
    if (ok)
        context.setConfigured(true);
    else {
        log(sm.getString("contextConfig.unavailable"));
        context.setConfigured(false);
    }

}
```