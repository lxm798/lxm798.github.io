# redis 源码简介

## redis object
字符串

集合

zset

hash

list

## 数据结构

字符串

跳跃表(skiplist)

压缩列表(ziplist)

词典(dict)

双向链表(Dlinklist)

整数集合

## redis object 和数据结构的对应关系
![](https://raw.githubusercontent.com/lxm798/picbed/master/redis_data_struct.png)

## 快照(AOF & RDB)

AOF:增量方式，所有写操作存入文件，会定期从新生成，从新生成的方式

１．根据当前存储的内存遍历，对于每个object 生成一个日志

２．在从新生成的过程中的请求会进入buf中，在快照结束后写入日志文件

RDB:一个在某个时间点的snapshot

## 主从同步
主从同步又两种方式：

１．从每次完全从新同步

２．主节点对于每个从节点维护一个同步的精度，连接断开重连时，根据当前的进度同步(偏移)

## cluster
命令执行

从新分片

转向

故障转移

消息

## 过期策略
（1）、noeviction：当到达内存限制时返回错误。当客户端尝试执行命令时会导致更多内存占用(大多数写命令，除了DEL和一些例外)。 

（2）、allkeys-lru：回收最近最少使用(LRU)的键，为新数据腾出空间。 

（3）、volatile-lru：回收最近最少使用(LRU)的键，但是只回收有设置过期的键，为新数据腾出空间。 

（4）、allkeys-random：回收随机的键，为新数据腾出空间。 

（5）、volatile-random：回收随机的键，但是只回收有设置过期的键，为新数据腾出空间。 

（6）、volatile-ttl：回收有设置过期的键，尝试先回收离TTL最短时间的键，为新数据腾出空间。

## 为什么是跳表
https://www.zhihu.com/question/20202931

## 可能出现的内存暴增
### 读写缓冲区导致

### 执行bgsave的时候同步失败进入全量不同
```cpp
void readSyncBulkPayload(aeEventLoop *el, int fd, void *privdata, int mask) {
//读取master发过来的RDB大小以及文件内容保存到本地文件中；
//如果读取完毕，那么调用rdbLoad加载文件内容。并考虑重新启动startAppendOnly
    if (server.repl_transfer_read == server.repl_transfer_size) {//看看是否文件全部接收完毕，如果完毕，GOOD
        if (rename(server.repl_transfer_tmpfile,server.rdb_filename) == -1) {
            redisLog(REDIS_WARNING,"Failed trying to rename the temp DB into dump.rdb in MASTER <-> SLAVE synchronization: %s", strerror(errno));
            replicationAbortSyncTransfer();
            return;
        }
        redisLog(REDIS_NOTICE, "MASTER <-> SLAVE sync: Loading DB in memory");
        signalFlushedDb(-1);
        emptyDb();//清空整个数据库，这个操作非常重，如果当前正在做BGSAVE，那么会导致快照的COW写时复制机制失效，严重耗费物理内存。
        /* Before loading the DB into memory we need to delete the readable
         * handler, otherwise it will get called recursively since
         * rdbLoad() will call the event loop to process events from time to
         * time for non blocking loading. */
        aeDeleteFileEvent(server.el,server.repl_transfer_s,AE_READABLE);//上面注释说了，避免循环进入。
        if (rdbLoad(server.rdb_filename) != REDIS_OK) {//开始加载RDB文件到内存数据结构中，这个要花费不少时间的。
            redisLog(REDIS_WARNING,"Failed trying to load the MASTER synchronization DB from disk");
            replicationAbortSyncTransfer();
            return;
        }
        /* Final setup of the connected slave <- master link */
        zfree(server.repl_transfer_tmpfile);
        close(server.repl_transfer_fd);
        server.master = createClient(server.repl_transfer_s);//重新注册可读事件毁掉为readQueryFromClient
        server.master->flags |= REDIS_MASTER;
        server.master->authenticated = 1;
        server.repl_state = REDIS_REPL_CONNECTED;
		//当切换server.repl_state 为 REDIS_REPL_CONNECTED的时候，新来的查询请求就能够被处理了，
		//在processCommand里面就不会过滤非STALE请求，同时本slave也能接受下一级slave的SYNC指令了。
        redisLog(REDIS_NOTICE, "MASTER <-> SLAVE sync: Finished with success");
//···
    }
```
理解其实在做SYNC的时候，是不需要做BGSAVE的，因为没用，等SYNC完成后，自然就会将同步回来的RDB文件覆盖BGSAVE的文件的：rename(server.repl_transfer_tmpfile,server.rdb_filename)， 所以BGSAVE等于白做了，甚至更严重的还会导致如下时序竞争条件：

1. SYNC将同步回来的RDB文件临时文件rename成server.rdb_filename，并加载到内存；

2.BGSAVE完成备份到临时文件后，又将其过期的老的备份文件覆盖了相对更新的server.rdb_filename文件，从而就给后面挖坑了····。

当然slave做BGSAVE肯定是最安全的，但有SYNC操作在的时候可以不用做BGSAVE的。