#
## Depoyer
### HostConfig
```java
protected void deployApps() {

    if (!(host instanceof Deployer))
        return;
    if (debug >= 1)
        log(sm.getString("hostConfig.deploying"));

    File appBase = appBase();
    if (!appBase.exists() || !appBase.isDirectory())
        return;
    String files[] = appBase.list();

    deployDescriptors(appBase, files);
    deployWARs(appBase, files);
    deployDirectories(appBase, files);
}

protected void deployDescriptors(File appBase, String[] files) {

    if (!deployXML)
        return;

    for (int i = 0; i < files.length; i++) {

        if (files[i].equalsIgnoreCase("META-INF"))
            continue;
        if (files[i].equalsIgnoreCase("WEB-INF"))
            continue;
        if (deployed.contains(files[i]))
            continue;
        File dir = new File(appBase, files[i]);
        if (files[i].toLowerCase().endsWith(".xml")) {
            deployed.add(files[i]);

            // Calculate the context path and make sure it is unique
            String file = files[i].substring(0, files[i].length() - 4);
            String contextPath = "/" + file;
            if (file.equals("ROOT")) {
                contextPath = "";
            }
            if (host.findChild(contextPath) != null) {
                continue;
            }

            // Assume this is a configuration descriptor and deploy it
            log(sm.getString("hostConfig.deployDescriptor", files[i]));
            try {
                URL config =
                    new URL("file", null, dir.getCanonicalPath());
                ((Deployer) host).install(config, null);
            } catch (Throwable t) {
                log(sm.getString("hostConfig.deployDescriptor.error",
                                    files[i]), t);
            }

        }

    }

}
```