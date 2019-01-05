# ThreadPoolExecutor

## Introduce
ThreadPoolExecutor 能够依据corePoolSize和maximumPoolSize自适应调整pool 的大小.

### 创建线程
新任务提交时:
* 当前线程数量小于corePoolSize
* 当前线程数量大于corePoolSize,小于maximumPoolSize，并且queue已经满了

### Keep-alive times
线程回收
默认情况下：
当前线程数超过corePoolSize,线程空闲时间超过keepAliveTime时就会被回收
通过设置allowCoreThreadTimeOut，在任何情况下，线程空闲时间超过keepAliveTime时都会被回收

### queue

当前线程数少于corePoolSize，增加一个线程,taskzhi
如果超过corePoolSize，executor会将task放入queue中。
如果request 不能放入queue中，新thread会创建知道达到maximumPoolSize

## 构造函数和成员

### 构造函数
```java
public ScheduledThreadPoolExecutor(int corePoolSize,
                                    ThreadFactory threadFactory) {
    super(corePoolSize, Integer.MAX_VALUE, 0, TimeUnit.NANOSECONDS,
            new DelayedWorkQueue(), threadFactory);
}

public static ExecutorService newFixedThreadPool(int nThreads) {
    return new ThreadPoolExecutor(nThreads, nThreads,
                                    0L, TimeUnit.MILLISECONDS,
                                    new LinkedBlockingQueue<Runnable>());
}

public static ExecutorService newSingleThreadExecutor() {
    return new FinalizableDelegatedExecutorService
        (new ThreadPoolExecutor(1, 1,
                                0L, TimeUnit.MILLISECONDS,
                                new LinkedBlockingQueue<Runnable>()));
}

public static ExecutorService newCachedThreadPool() {
    return new ThreadPoolExecutor(0, Integer.MAX_VALUE,
                                    60L, TimeUnit.SECONDS,
                                    new SynchronousQueue<Runnable>());
}
public ThreadPoolExecutor(int corePoolSize,
                            int maximumPoolSize,
                            long keepAliveTime,
                            TimeUnit unit,
                            BlockingQueue<Runnable> workQueue,
                            ThreadFactory threadFactory) {
    this(corePoolSize, maximumPoolSize, keepAliveTime, unit, workQueue,
            threadFactory, defaultHandler);
}
public ThreadPoolExecutor(int corePoolSize,
                            int maximumPoolSize,
                            long keepAliveTime,
                            TimeUnit unit,
                            BlockingQueue<Runnable> workQueue,
                            ThreadFactory threadFactory,
                            RejectedExecutionHandler handler) {
    if (corePoolSize < 0 ||
        maximumPoolSize <= 0 ||
        maximumPoolSize < corePoolSize ||
        keepAliveTime < 0)
        throw new IllegalArgumentException();
    if (workQueue == null || threadFactory == null || handler == null)
        throw new NullPointerException();
    this.acc = System.getSecurityManager() == null ?
            null :
            AccessController.getContext();
    this.corePoolSize = corePoolSize;
    this.maximumPoolSize = maximumPoolSize;
    this.workQueue = workQueue;
    this.keepAliveTime = unit.toNanos(keepAliveTime);
    this.threadFactory = threadFactory;
    this.handler = handler;
}
```

## 任务提交
```java
    public Future<?> submit(Runnable task) {
        if (task == null) throw new NullPointerException();
        RunnableFuture<Void> ftask = newTaskFor(task, null);
        execute(ftask);
        return ftask;
    }

    protected <T> RunnableFuture<T> newTaskFor(Runnable runnable, T value) {
        return new FutureTask<T>(runnable, value);
    }

    public void execute(Runnable command) {
        if (command == null)
            throw new NullPointerException();
         /*
         * 分3步
         * 1. 少于corePoolSize线程数，尝试新建线程
         * 2. 尝试放入queue，成功后即可返回
         * 3. 创建新线程
         */
        int c = ctl.get();
        // 当前worker数量小于corePoolSize
        if (workerCountOf(c) < corePoolSize) {
            // 创建worker直接 运行command
            if (addWorker(command, true))
                return;
            c = ctl.get();
        }
        // 线程池当前是运行状态，并且成功放入workQueu
        // queue已经满的情况下，需要addworker
        if (isRunning(c) && workQueue.offer(command)) {
            int recheck = ctl.get();
            if (! isRunning(recheck) && remove(command))
                reject(command);
            else if (workerCountOf(recheck) == 0)
                addWorker(null, false);
        }
        else if (!addWorker(command, false))
            reject(command);
    }
```
新增worker
```java
private boolean addWorker(Runnable firstTask, boolean core) {
    retry:
    for (;;) {
        int c = ctl.get();
        int rs = runStateOf(c);

        // ThreadPool已经停止，firstTask是null，并且有运行的线程
        if (rs >= SHUTDOWN &&
            ! (rs == SHUTDOWN &&
                firstTask == null &&
                ! workQueue.isEmpty()))
            return false;

        for (;;) {
            int wc = workerCountOf(c);
            // 满足线程数量的限制
            if (wc >= CAPACITY ||
                wc >= (core ? corePoolSize : maximumPoolSize))
                return false;
            if (compareAndIncrementWorkerCount(c))
                break retry;
            c = ctl.get();  // Re-read ctl
            if (runStateOf(c) != rs)
                continue retry;
            // else CAS failed due to workerCount change; retry inner loop
        }
    }
    // 新的worker是否运行了
    boolean workerStarted = false;
    // worker是否添加到workers容器中
    boolean workerAdded = false;
    Worker w = null;
    try {
        final ReentrantLock mainLock = this.mainLock;
        w = new Worker(firstTask);
        final Thread t = w.thread;
        if (t != null) {
            mainLock.lock();
            try {
                // Recheck while holding lock.
                // Back out on ThreadFactory failure or if
                // shut down before lock acquired.
                int c = ctl.get();
                int rs = runStateOf(c);
                // ThreadPool是运行状态，或者是关闭状态？？？
                if (rs < SHUTDOWN ||
                    (rs == SHUTDOWN && firstTask == null)) {
                    // 线程当前必须是未启动状态
                    if (t.isAlive()) // precheck that t is startable
                        throw new IllegalThreadStateException();
                    // 放入workers容器中记录
                    workers.add(w);
                    int s = workers.size();
                    if (s > largestPoolSize)
                        largestPoolSize = s;
                    workerAdded = true;
                }
            } finally {
                mainLock.unlock();
            }
            // 已经添加的worker，一定会启动
            if (workerAdded) {
                t.start();
                workerStarted = true;
            }
        }
    } finally {
        // worker 现场没有启动
        if (! workerStarted)
            addWorkerFailed(w);
    }
    return workerStarted;
}

// 回收worker相关对象
private void addWorkerFailed(Worker w) {
    final ReentrantLock mainLock = this.mainLock;
    mainLock.lock();
    try {
        if (w != null)
            workers.remove(w);
        decrementWorkerCount();
        tryTerminate();
    } finally {
        mainLock.unlock();
    }
}
```

### worker实际的执行流程
Worker的构造函数
```java
Worker(Runnable firstTask) {
    setState(-1); // inhibit interrupts until runWorker
    this.firstTask = firstTask;
    this.thread = getThreadFactory().newThread(this);
}
```
构造函数中可以看出，包含创建时的task和一个线程

Worker继承自Runnable，使用run来执行具体的流程
```java
final void runWorker(Worker w) {
    Thread wt = Thread.currentThread();
    Runnable task = w.firstTask;
    w.firstTask = null;
    w.unlock(); // allow interrupts
    boolean completedAbruptly = true;
    try {
        while (task != null || (task = getTask()) != null) {
            w.lock();
            // If pool is stopping, ensure thread is interrupted;
            // if not, ensure thread is not interrupted.  This
            // requires a recheck in second case to deal with
            // shutdownNow race while clearing interrupt
            if ((runStateAtLeast(ctl.get(), STOP) ||
                    (Thread.interrupted() &&
                    runStateAtLeast(ctl.get(), STOP))) &&
                !wt.isInterrupted())
                wt.interrupt();
            try {
                beforeExecute(wt, task);
                Throwable thrown = null;
                try {
                    task.run();
                } catch (RuntimeException x) {
                    thrown = x; throw x;
                } catch (Error x) {
                    thrown = x; throw x;
                } catch (Throwable x) {
                    thrown = x; throw new Error(x);
                } finally {
                    afterExecute(task, thrown);
                }
            } finally {
                task = null;
                w.completedTasks++;
                w.unlock();
            }
        }
        completedAbruptly = false;
    } finally {
        processWorkerExit(w, completedAbruptly);
    }
}
```
这里是一个死循环，循环获取getTask中的新task，注意三点
1.利用beforeExecute、afterExecute，可以在task开始和结束前做一些事情，比如记录数据，threadlocal的相关初始化等
2.第一个task是不进入队列的
3.在没有新任务时，整个for循环就退出了，而无论当前线程数量是否小于corePoolSize，最后都会结束

### getTask
```java
private Runnable getTask() {
    boolean timedOut = false; // Did the last poll() time out?

    for (;;) {
        int c = ctl.get();
        int rs = runStateOf(c);

        if (rs >= SHUTDOWN && (rs >= STOP || workQueue.isEmpty())) {
            decrementWorkerCount();
            return null;
        }

        int wc = workerCountOf(c);

        // 当前线程数超过corePoolSize 或者timeout
        boolean timed = allowCoreThreadTimeOut || wc > corePoolSize;
        // queue是空或者当前线程数量大于1
        if ((wc > maximumPoolSize || (timed && timedOut))
            && (wc > 1 || workQueue.isEmpty())) {
            if (compareAndDecrementWorkerCount(c))
                return null;
            continue;
        }

        try {
            // 允许线程回收的情况下，用poll的timeout，默认60s
            Runnable r = timed ?
                workQueue.poll(keepAliveTime, TimeUnit.NANOSECONDS) :
                workQueue.take();
            if (r != null)
                return r;
            timedOut = true;
        } catch (InterruptedException retry) {
            timedOut = false;
        }
    }
}
```

## 拒绝策略
在上面的代码中，task不能运行时，会执行reject
```java
final void reject(Runnable command) {
    handler.rejectedExecution(command, this);
}
```
handler的类型为抽象类
```java
public interface RejectedExecutionHandler {
    void rejectedExecution(Runnable r, ThreadPoolExecutor executor);
}
```
和blockqueue的add策略一样，有抛出异常和丢弃策略，同时有额外的补偿策略，在调用线程处理、丢弃最老的任务
有四种实现：
```java
    public static class CallerRunsPolicy implements RejectedExecutionHandler {
        public CallerRunsPolicy() { }

        /**
         * 在运行线程继续运行
         */
        public void rejectedExecution(Runnable r, ThreadPoolExecutor e) {
            if (!e.isShutdown()) {
                r.run();
            }
        }
    }

    // 默认策略
    public static class AbortPolicy implements RejectedExecutionHandler {

        /**
         * 不具有运行条件时抛出异常 RejectedExecutionException
         */
        public void rejectedExecution(Runnable r, ThreadPoolExecutor e) {
            throw new RejectedExecutionException("Task " + r.toString() +
                                                 " rejected from " +
                                                 e.toString());
        }
    }

    public static class DiscardPolicy implements RejectedExecutionHandler {
        /**
         * 丢弃
         */
        public void rejectedExecution(Runnable r, ThreadPoolExecutor e) {
        }
    }

    public static class DiscardOldestPolicy implements RejectedExecutionHandler {

        /**
         * 从queue中取出最老的任务，重新尝试运行任务
         */
        public void rejectedExecution(Runnable r, ThreadPoolExecutor e) {
            if (!e.isShutdown()) {
                e.getQueue().poll();
                e.execute(r);
            }
        }
    }
```