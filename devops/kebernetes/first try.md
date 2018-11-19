# Kubernetes 初试

双十一打折买了本k8s权威指南,在周末尝试体验一下.按照我的尝试步骤记录一下

## ubuntu 安装 k8s
周六尝试在ubuntu 上安装k8s,发现没有相关的源.14.04版本太老,尝试升级到16.04,果不其然,系统重启后崩溃. 然后尝试进入windows进行修复,然后打开游戏界面晚到凌晨一点多,放弃

## centos 安装k8s
周日在一台空闲的服务器上安装
yum install -y etcd kubernets docker
然后编写rc:
```yaml
apiVersion: v1
kind: ReplicationController
metadata:
  name: mysql
spec:
  replicas: 1
  selector:
    app: mysql
  template:
    metadata:
      labels:
        app: mysql
    spec:
      containers:
      - name: mysql
        image: mysql
        ports:
        - containerPort: 3306
        env:
        - name: MYSQL_ROOT_PASSWORD
          value: "123456"
```
运行kebectl create -f mysql-rc.yaml,发现失败端口8080没有启动,程序没有启动,然后启动程序
```shell
systemctl start etcd
systemctl start docker
systemctl start kube-apiserver
....
```
再次运行上述命令,成功. 接着查看rc的状态,kubectl get rc
```shell
NAME      DESIRED   CURRENT   READY     AGE
mysql     1         0         0         1h
```
mysql没有部署起来...,查找后这里给出答案https://blog.csdn.net/trend_h/article/details/83824075,
需要跳过认证,编辑/etc/kubernetes/apiserver去除 KUBE_ADMISSION_CONTROL中的SecurityContextDeny,ServiceAccount，并重启kube-apiserver服务,状态变成了
```shell
NAME      DESIRED   CURRENT   READY     AGE
mysql     1         1         0         1h
```
但是依然没有ready,查看pods,一直处于ContainingCreating状态
```shell
NAME          READY     STATUS              RESTARTS   AGE
mysql-sr3g3   0/1       ContainerCreating   0          2h
```
使用命令kubectl describe pod mysql查看详细信息,出现具体错误
```shell
Error syncing pod, skipping: failed to "StartContainer" for "POD" with ErrImagePull: "image pull failed for registry.access.redhat.com/rhel7/pod-infrastructure:latest, this may be because there are no credentials on this request.  details: (open /etc/docker/certs.d/registry.access.redhat.com/redhat-ca.crt: no such file or directory)"
```
google后,缺少rhsm,安装
```shell
yum install rhsm
```
没有效果,执行以下命令:
```shell
wget http://mirror.centos.org/centos/7/os/x86_64/Packages/python-rhsm-certificates-1.19.10-1.el7_4.x86_64.rpm
rpm2cpio python-rhsm-certificates-1.19.10-1.el7_4.x86_64.rpm | cpio -iv --to-stdout ./etc/rhsm/ca/redhat-uep.pem | tee /etc/rhsm/ca/redhat-uep.pem  
```
done,执行成功,docker ps 后有mysqldocker 启动

## 启动service
```yaml
apiVersion: v1
kind: Service
metadata:
  name: mysql
spec:
  ports:
  - port: 3306
  selector:
    app: mysql
```
开始时spec写成了sepc,查了十分钟...

## 驱动myweb
```yaml
apiVersion: v1
kind: ReplicationController
metadata:
  name: myweb
spec:
  replicas: 2
  selector:
    app: myweb
  template:
    metadata:
      labels:
        app: myweb
    spec:
      containers:
      - name: myweb
        image: kubeguide/tomcat-app:v1
        ports:
        - containerPort: 8080
```
开始时上面kubeguide写成了kuberguide,变成了
```shell
myweb-d9gpk   0/1       ImagePullBackOff   0          1m
myweb-dbq9r   0/1       ImagePullBackOff   0          1m
```

## 启动web应用
```yaml
apiVersion: v1
kind: Service
metadata:
  name: myweb
spec:
  type: NodePort
  ports:
  - port: 8080
    nodePort: 30001
  selector:
    app: myweb
```


##  概念和术语
### 概念
master: 
node:

### service
每个pod一个IP
每个service 一个虚拟IP cluster IP
Node Ip:node所在机器的wangkaicip


## 和mesos的对比
mesos 包含master和agent模块,但是没有提供上层类似任务pod数量维护\pod调度等功能,相比于k8s更加灵活,但是需要做的工作更多.

