# pthread_mutex
pthread_mutex_lock 在非多线程环境中默认直接返回0
##  初始化

```c
#define FORWARD2(name, rettype, decl, params, defaction) \
rettype                                       \        
name decl                                     \        
{                                         \            
  if (!__libc_pthread_functions_init)                         \
    defaction;                                    \    
                                          \            
  return PTHFCT_CALL (ptr_##name, params);                    \
}                                                      
                                                       
/* Same as FORWARD2, only without return.  */          
#define FORWARD_NORETURN(name, rettype, decl, params, defaction) \
rettype                                       \        
name decl                                     \        
{                                         \            
  if (!__libc_pthread_functions_init)                         \
    defaction;                                    \    
                                          \            
  PTHFCT_CALL (ptr_##name, params);                       \
}                                                      
                                                       
#define FORWARD(name, decl, params, defretval) \       
  FORWARD2 (name, int, decl, params, return defretval)

FORWARD (pthread_attr_setscope, (pthread_attr_t *attr, int scope),
     (attr, scope), 0)
```
其中__libc_pthread_functions_init 在libpthread加载时赋值初始化
```
void __libc_pthread_init (unsigned long int *ptr, void (*reclaim) (void),
             const struct pthread_functions *functions)                                                                                                                                                            
{       
  /* Remember the pointer to the generation counter in libpthread.  */
  __fork_generation_pointer = ptr;
        
  /* Called by a child after fork.  */
  __register_atfork (NULL, NULL, reclaim, NULL);
        
#ifdef SHARED
  /* Copy the function pointers into an array in libc.  This enables
     access with just one memory reference but moreso, it prevents
     hijacking the function pointers with just one pointer change.  We
     "encrypt" the function pointers since we cannot write-protect the
     array easily enough.  */
  union ptrhack
  {     
    struct pthread_functions pf;
# define NPTRS (sizeof (struct pthread_functions) / sizeof (void *))
    void *parr[NPTRS];
  } __attribute__ ((may_alias)) const *src;
  union ptrhack *dest;
        
  src = (const void *) functions;
  dest = (void *) &__libc_pthread_functions;
        
  for (size_t cnt = 0; cnt < NPTRS; ++cnt)
    {   
      void *p = src->parr[cnt];
      PTR_MANGLE (p);
      dest->parr[cnt] = p;
    }   
  __libc_pthread_functions_init = 1;
#endif  
        
#ifndef TLS_MULTIPLE_THREADS_IN_TCB
  return &__libc_multiple_threads;
#endif  
}       
```
在段初始化时调用 __libc_pthread_init
```S
// ../sysdeps/x86_64/crti.S
_init:         
    /* Maintain 16-byte stack alignment for called functions.  */
    subq $8, %rsp
#if PREINIT_FUNCTION_WEAK
    movq PREINIT_FUNCTION@GOTPCREL(%rip), %rax
    testq %rax, %rax
    je .Lno_weak_fn                                                                                                                                                                                                
    call PREINIT_FUNCTION@PLT
```
其中PREINIT_FUNCTION定义为
```c
#define PREINIT_FUNCTION __pthread_initialize_minimal_internal
```
