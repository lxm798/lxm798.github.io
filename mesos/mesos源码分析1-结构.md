# Mesos 源码分析--快速入门
## 架构
[comment]: ![](./pic/architecture3.jpg)
<img src="./pic/architecture3.jpg" width="500" height="300" />
mesos 由scheduler,master,agent和exeuctor组成.其中master管理在每个node中的gagent,资源由master负责分配,每份资源由类似(agent ID, resource1: amount1, resource2: amount2,...)结构描述.

## 资源分配示例
<img src="./pic/architecture-example.jpg" width="400" height="300" />

1. Agent1 上报给master其包含(4 CPUS,4GB MEM)
2. master 按照资源分配算法分配资源
3. schdueler 回复master使用哪些资源运行task
4. master将task分发给agent

## 编译
以ubuntu16.04为例
``` shell
git clone git@github.com:apache/mesos.git
cd mesos
sudo apt-get install -y openjdk-8-jdk
sudo apt-get install -y autoconf libtool
sudo apt-get -y install build-essential python-dev python-six python-virtualenv libcurl4-nss-dev libsasl2-dev libsasl2-modules maven libapr1-dev libsvn-dev zlib1g-dev iputils-ping
./bootstrap
mkdir build
cd build
../configure
make -j 4
make check
make install
```

## 运行
``` shell
 ./bin/mesos-master.sh --ip=127.0.0.1 --work_dir=/var/lib/mesos
 ./bin/mesos-agent.sh --master=127.0.0.1:5050 --work_dir=/var/lib/mesos
 ./src/examples/java/test-framework 127.0.0.1:5050
 ```

 ## 编译产出
 编译后在build目录下会有多个文件:
 * mesos : shell 脚本,用于启动master,agent
 * mesos-master.sh : shell脚本启动master
 * mesos-agent.sh : 用于启动agent
 * mesos-execute : mesos的cli,作为mesos的framework运行
 * mesos-executor : mesos executor task的默认executor
