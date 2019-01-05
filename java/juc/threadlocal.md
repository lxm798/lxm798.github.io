#

## 
```c
jobject JNIHandles::make_local(oop obj) {
  if (obj == NULL) {
    return NULL;                // ignore null handles
  } else {
    Thread* thread = Thread::current();
    assert(Universe::heap()->is_in_reserved(obj), "sanity check");
    return thread->active_handles()->allocate_handle(obj);
  }
}
 
 
// optimized versions
 
jobject JNIHandles::make_local(Thread* thread, oop obj) {
  if (obj == NULL) {
    return NULL;                // ignore null handles
  } else {
    assert(Universe::heap()->is_in_reserved(obj), "sanity check");
    return thread->active_handles()->allocate_handle(obj);
  }
}
 
 
jobject JNIHandles::make_local(JNIEnv* env, oop obj) {
  if (obj == NULL) {
    return NULL;                // ignore null handles
  } else {
    JavaThread* thread = JavaThread::thread_from_jni_environment(env);
    assert(Universe::heap()->is_in_reserved(obj), "sanity check");
    return thread->active_handles()->allocate_handle(obj);
  }
}

```

```c
JVM_ENTRY(jobject, JVM_CurrentThread(JNIEnv* env, jclass threadClass))                                                                                                                                             
  JVMWrapper("JVM_CurrentThread");
  oop jthread = thread->threadObj();
  assert (thread != NULL, "no current thread!");
  return JNIHandles::make_local(env, jthread);
JVM_END
```