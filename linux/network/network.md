
1.file   --slab
2.dentry --slab
3.sock   --slab

struct file，对应每个打开的文件
struct dentry，文件所在的目录
struct socket_alloc，包含 struct socket 和 struct inode 两个成员，是连接 VFS 和 tcp_sock 的桥梁
struct socket_wq，用于 wait queue（例如阻塞IO时挂起当前线程）

http://blog.51cto.com/weiguozhihui/1585297
