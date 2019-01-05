# mesos源码分析2-libprocess
## 模型
libprocess 以futures/promises, HTTP和actor 模型为基础,提供一套异步编程的抽象框架.

### Futures & Promises & Callback
并发编程中通常会用到非阻塞模型:Pormise,Future & Callback.Future 表示一个还没有完成的异步任务的结果,针对这个结果可以添加Callback以便在任务执行成功或者失败后做出相应的操作.Promise 交由任务执行者,用于标记任务执行成功或者失败. 使用实例:
```cpp
using process::Future;
using process::Promise;

// Returns an instance of `Person` for the specified `name`.
Future<Person> find(const std::string& name);

// Returns the mother (an instance of `Person`) of the specified `name`.
Future<Person> mother(const std::string& name)
{
  // First find the person.
  Future<Person> person = find(name);

  // Now create a `Promise` that we can use to compose the two asynchronous calls.
  Promise<Person>* promise = new Promise<Person>();

  Future<Person> mother = promise->future();

  // Here is the boiler plate that can be replaced by `Future::then()`!
  person.onAny([](const Future<Person>& person) {
    if (person.isFailed()) {
      promise->fail(person.failure());
    } else if (person.isDiscarded()) {
      promise->discard();
    } else {
      CHECK_READY(person);
      promise->set(find(person->mother));
    }
    delete promise;
  });

  return mother;
}
```
其中Promise主要结构:
```cpp
template <typename T>
class Future
{
public:
  // Constructs a failed future.
  static Future<T> failed(const std::string& message);

  Future();

  ~Future() = default;
  const T& get() const;
private:
  enum State
  {
    PENDING,
    READY,
    FAILED,
    DISCARDED,
  };

  struct Data
  {
    Data();
    ~Data() = default;

    void clearAllCallbacks();

    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    State state;
    bool discard;
    bool associated;
    bool abandoned;

    // One of:
    //   1. None, the state is PENDING or DISCARDED.
    //   2. Some, the state is READY.
    //   3. Error, the state is FAILED; 'error()' stores the message.
    Result<T> result;

    std::vector<AbandonedCallback> onAbandonedCallbacks;
    std::vector<DiscardCallback> onDiscardCallbacks;
    std::vector<ReadyCallback> onReadyCallbacks;
    std::vector<FailedCallback> onFailedCallbacks;
    std::vector<DiscardedCallback> onDiscardedCallbacks;
    std::vector<AnyCallback> onAnyCallbacks;
  };
```
get实现:
```cpp
template <typename T>
const T& Future<T>::get() const
{
  if (!isReady()) {
    await();
  }
  .....
  return data->result.get();
}

template <typename T>
bool Future<T>::await(const Duration& duration) const
{
  Owned<Latch> latch(new Latch());

  bool pending = false;

  synchronized (data->lock) {
    if (data->state == PENDING) {
      pending = true;
      data->onAnyCallbacks.push_back(lambda::bind(&internal::awaited, latch));
    }
  }

  if (pending) {
    return latch->await(duration);
  }

  return true;
}
```
Latch 整体写法比较有意思,但是不知道为什么不直接依赖pthread_condition,可能考虑使用EventLoop降低定时器的代价(猜测),trigger通过结束pid用于触发wait :
```cpp
class Latch
{
public:
  Latch();
  virtual ~Latch();

  bool operator==(const Latch& that) const { return pid == that.pid; }
  bool operator<(const Latch& that) const { return pid < that.pid; }+
  bool trigger();
  bool await(const Duration& duration = Seconds(-1));

private:
  Latch(const Latch& that);
  Latch& operator=(const Latch& that);

  std::atomic_bool triggered;
  UPID pid;
};
```
Latch构造函数和等待
```cpp
Latch::Latch() : triggered(false)
{
  // 创建新的ProcessBase对象,用于
  pid = spawn(new ProcessBase(ID::generate("__latch__")), true);
}

bool Latch::trigger()
{
  bool expected = false;
  if (triggered.compare_exchange_strong(expected, true)) {
    terminate(pid);
    return true;
  }
  return false;
}

bool Latch::await(const Duration& duration)
{
  if (!triggered.load()) {
    process::wait(pid, duration); // Explict to disambiguate.
    // It's possible that we failed to wait because:
    //   (1) Our process has already terminated.
    //   (2) We timed out (i.e., duration was not "infinite").

    // In the event of (1) we might need to return 'true' since a
    // terminated process might imply that the latch has been
    // triggered. To capture this we simply return the value of
    // 'triggered' (which will also capture cases where we actually
    // timed out but have since triggered, which seems like an
    // acceptable semantics given such a "tie").
    return triggered.load();
  }

  return true;
}
```

可以看到这里的利用process的wait来处理等待:
```cpp
bool wait(const UPID& pid, const Duration& duration)
{
  process::initialize();

  if (!pid) {
    return false;
  }

  // This could result in a deadlock if some code decides to wait on a
  // process that has invoked that code!
  if (__process__ != nullptr && __process__->self() == pid) {
    LOG(ERROR) << "\n**** DEADLOCK DETECTED! ****\nYou are waiting on process "
               << pid << " that it is currently executing.";
  }

  if (duration == Seconds(-1)) {
    return process_manager->wait(pid);
  }

  bool waited = false;

  WaitWaiter waiter(pid, duration, &waited);
  spawn(waiter);
  wait(waiter);

  return waited;
}
```
如果没有超时时间就利用processManager的wait,如果有超时就在生成一个Process,然后用定时器触发事件.
```cpp
  WaitWaiter(const UPID& _pid, const Duration& _duration, bool* _waited)
    : ProcessBase(ID::generate("__waiter__")),
      pid(_pid),
      duration(_duration),
      waited(_waited) {}

  virtual void initialize()
  {
    VLOG(3) << "Running waiter process for " << pid;
    link(pid);
    delay(duration, self(), &WaitWaiter::timeout);
  }

  void timeout()
  {
    VLOG(3) << "Waiter process timed out waiting for " << pid;
    *waited = false;
    terminate(self());
  }
```
```cpp
template <typename T>
Timer delay(const Duration& duration,
            const PID<T>& pid,
            void (T::*method)())
{
  return Clock::timer(duration, [=]() {
    dispatch(pid, method);
  });
}

Timer Clock::timer(
    const Duration& duration,
    const lambda::function<void()>& thunk)
{
  ...

  // Add the timer.
  synchronized (timers_mutex) {
      clock::scheduleTick(*timers, clock::ticks);
  }

  return timer;
}

void scheduleTick(const map<Time, list<Timer>>& timers, set<Time>* ticks)
{
  // Determine when the next 'tick' should fire.
  const Option<Time> next = clock::next(timers);

  if (next.isSome()) {
    // Don't schedule a 'tick' if there is a 'tick' scheduled for
    // an earlier time, to avoid excessive pending timers.
    if (ticks->empty() || next.get() < (*ticks->begin())) {
      ticks->insert(next.get());

      // The delay can be negative if the timer is expired, this
      // is expected will result in a 'tick' firing immediately.
      const Duration delay = next.get() - Clock::now(nullptr);
      EventLoop::delay(delay, lambda::bind(tick, next.get()));
    }
  }
}
```
绕了一大圈,超时问题依然用EventLoop的delay解决.
对于等待事件使用ProcessManager的wait,wait函数中最终等待的是ProcessBase中的gate,gate是对pthread_condition相关函数的封装,pthread_cond_singal,在actor收到TerminateEvent,退出时调用open,
```cpp
void ProcessManager::resume(ProcessBase* process) {
    while (!terminate && !blocked) {
        ...
        terminate = event->is<TerminateEvent>();
        process->serve(std::move(*event));
        delete event;
        ...
    }
    reference = ProcessReference();

    if (terminate) {
      cleanup(process);
    }

    __process__ = nullptr;
    if (terminate && manage) {
      delete process;
    }
}
```
这里是底层eventloop 的上层处理,process->serve(std::move(*event));terminate = event->is<TerminateEvent>();使用visitor模式处理.

Promise中set数据数据时调用Future<T>::set:
```cpp
template <typename T>
template <typename U>
bool Future<T>::_set(U&& u)
{
  bool result = false;

  synchronized (data->lock) {
    if (data->state == PENDING) {
      data->result = std::forward<U>(u);
      data->state = READY;
      result = true;
    }
  }

  // Invoke all callbacks associated with this future being READY. We
  // don't need a lock because the state is now in READY so there
  // should not be any concurrent modifications to the callbacks.
  if (result) {
    // Grab a copy of `data` just in case invoking the callbacks
    // erroneously attempts to delete this future.
    std::shared_ptr<typename Future<T>::Data> copy = data;
    // 在Future::await时会将internal::awaited加入onAnyCallbacks中,awaited调用trigger发送结果
    internal::run(std::move(copy->onReadyCallbacks), copy->result.get());
    internal::run(std::move(copy->onAnyCallbacks), *this);

    copy->clearAllCallbacks();
  }

  return result;
}
```


