# STW和Safepint

## 引言

Safe-point (or safepoint)
In order to support precise enumeration, JIT compiler should do additional work, because only JIT knows exactly stack frame info and register contents. When JIT compiles a method, for every instruction, it can book-keep the root reference information in case the execution is suspended at that instruction. 

But to remember the info for every instruction is too expensive. It requires substantial space to store the information. This is also unnecessary, because only a few instructions will have the chances to be the suspension points in real execution. JIT only needs to book-keep information for those instruction points -- they are called safe-points. Safe-point means it is a safe suspension point for root set enumeration.

Btw, the ability of a compiler to know exact stack slots' information is not universally available in all programming languages. Only safe languages have the ability. For example, C/C++ doesn't.

## 触发

safepoint指的特定位置主要有:

1. 循环的末尾 (防止大循环的时候一直不进入safepoint，而其他线程在等待它进入safepoint)

2. 方法返回前

3. 调用方法的call之后

4. 抛出异常的位置

## STW的行为/代码

在VMThead 触发begin后,VMThread循环遍历Thread状态
```java
  unsigned int iterations = 0;
  int steps = 0 ;
  while(still_running > 0) {
    for (JavaThread *cur = Threads::first(); cur != NULL; cur = cur->next()) {
      assert(!cur->is_ConcurrentGC_thread(), "A concurrent GC thread is unexpectly being suspended");
      ThreadSafepointState *cur_state = cur->safepoint_state();
      if (cur_state->is_running()) {
        cur_state->examine_state_of_thread();
        if (!cur_state->is_running()) {
           still_running--;
           // consider adjusting steps downward:
           //   steps = 0
           //   steps -= NNN
           //   steps >>= 1
           //   steps = MIN(steps, 2000-100)
           //   if (iterations != 0) steps -= NNN
        }
        if (TraceSafepoint && Verbose) cur_state->print();
      }
    }

    if (PrintSafepointStatistics && iterations == 0) {
      begin_statistics(nof_threads, still_running);
    }

    if (still_running > 0) {
      // Check for if it takes to long
      if (SafepointTimeout && safepoint_limit_time < os::javaTimeNanos()) {
        print_safepoint_timeout(_spinning_timeout);
    }
```

```java
void ThreadSafepointState::examine_state_of_thread() {
 
  JavaThreadState state = _thread->thread_state();

  // Save the state at the start of safepoint processing.
  _orig_thread_state = state;

  bool is_suspended = _thread->is_ext_suspended();
  if (is_suspended) {
    roll_forward(_at_safepoint);
    return;
  }

  // Some JavaThread states have an initial safepoint state of
  // running, but are actually at a safepoint. We will happily
  // agree and update the safepoint state here.
  if (SafepointSynchronize::safepoint_safe(_thread, state)) {
    SafepointSynchronize::check_for_lazy_critical_native(_thread, state);
    roll_forward(_at_safepoint);
    return;
  }

  if (state == _thread_in_vm) {
    roll_forward(_call_back);
    return;
  }
```









http://xiao-feng.blogspot.com/2008/01/gc-safe-point-and-safe-region.html
http://jpbempel.blogspot.com/2013/03/safety-first-safepoints.html
https://www.jianshu.com/p/43bc97230299