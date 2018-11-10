# pthread mutex源码分析
## pthread_mutex_t数据结构
```c
typedef union
{
  struct __pthread_mutex_s
  {
    // 用户态futex
    int __lock;
    unsigned int __count;
    // 锁拥有者
    int __owner;
#ifdef __x86_64__
    unsigned int __nusers;
#endif
    /* KIND must stay at this position in the structure to maintain
       binary compatibility.  */
    int __kind;
#ifdef __x86_64__
    short __spins;
    short __elision;
    __pthread_list_t __list;
# define __PTHREAD_MUTEX_HAVE_PREV	1
/* Mutex __spins initializer used by PTHREAD_MUTEX_INITIALIZER.  */
# define __PTHREAD_SPINS             0, 0
#else
    unsigned int __nusers;
    __extension__ union
    {
      struct
      {
	short __espins;
	short __elision;
# define __spins __elision_data.__espins
# define __elision __elision_data.__elision
# define __PTHREAD_SPINS         { 0, 0 }
      } __elision_data;
      __pthread_slist_t __list;
    };
#endif
  } __data;
  char __size[__SIZEOF_PTHREAD_MUTEX_T];
  long int __align;
} pthread_mutex_t;
```
核心数据结构：


## pthread_mutex_lock
```c
int
__pthread_mutex_lock (pthread_mutex_t *mutex)
{
  assert (sizeof (mutex->__size) >= sizeof (mutex->__data));

  unsigned int type = PTHREAD_MUTEX_TYPE_ELISION (mutex);

  LIBC_PROBE (mutex_entry, 1, mutex);

  if (__builtin_expect (type & ~(PTHREAD_MUTEX_KIND_MASK_NP
				 | PTHREAD_MUTEX_ELISION_FLAGS_NP), 0))
    return __pthread_mutex_lock_full (mutex);

  if (__glibc_likely (type == PTHREAD_MUTEX_TIMED_NP))
    {
      FORCE_ELISION (mutex, goto elision);
    simple:
      /* Normal mutex.  */
      LLL_MUTEX_LOCK (mutex);
      assert (mutex->__data.__owner == 0);
    }
#ifdef HAVE_ELISION
  else if (__glibc_likely (type == PTHREAD_MUTEX_TIMED_ELISION_NP))
    {
  elision: __attribute__((unused))
      /* This case can never happen on a system without elision,
         as the mutex type initialization functions will not
	 allow to set the elision flags.  */
      /* Don't record owner or users for elision case.  This is a
         tail call.  */
      return LLL_MUTEX_LOCK_ELISION (mutex);
    }
#endif
  else if (__builtin_expect (PTHREAD_MUTEX_TYPE (mutex)
			     == PTHREAD_MUTEX_RECURSIVE_NP, 1))
    {
      /* Recursive mutex.  */
      pid_t id = THREAD_GETMEM (THREAD_SELF, tid);

      /* Check whether we already hold the mutex.  */
      if (mutex->__data.__owner == id)
	{
	  /* Just bump the counter.  */
	  if (__glibc_unlikely (mutex->__data.__count + 1 == 0))
	    /* Overflow of the counter.  */
	    return EAGAIN;

	  ++mutex->__data.__count;

	  return 0;
	}

      /* We have to get the mutex.  */
      LLL_MUTEX_LOCK (mutex);

      assert (mutex->__data.__owner == 0);
      mutex->__data.__count = 1;
    }
  else if (__builtin_expect (PTHREAD_MUTEX_TYPE (mutex)
			  == PTHREAD_MUTEX_ADAPTIVE_NP, 1))
    {
      if (! __is_smp)
	goto simple;

      if (LLL_MUTEX_TRYLOCK (mutex) != 0)
	{
	  int cnt = 0;
	  int max_cnt = MIN (MAX_ADAPTIVE_COUNT,
			     mutex->__data.__spins * 2 + 10);
	  do
	    {
	      if (cnt++ >= max_cnt)
		{
		  LLL_MUTEX_LOCK (mutex);
		  break;
		}
	      atomic_spin_nop ();
	    }
	  while (LLL_MUTEX_TRYLOCK (mutex) != 0);

	  mutex->__data.__spins += (cnt - mutex->__data.__spins) / 8;
	}
      assert (mutex->__data.__owner == 0);
    }
  else
    {
      pid_t id = THREAD_GETMEM (THREAD_SELF, tid);
      assert (PTHREAD_MUTEX_TYPE (mutex) == PTHREAD_MUTEX_ERRORCHECK_NP);
      /* Check whether we already hold the mutex.  */
      if (__glibc_unlikely (mutex->__data.__owner == id))
	return EDEADLK;
      goto simple;
    }

  pid_t id = THREAD_GETMEM (THREAD_SELF, tid);
#define lll_lock(futex, private) \
  (void)                                      \
    ({ int ignore1, ignore2, ignore3;                         \
       if (__builtin_constant_p (private) && (private) == LLL_PRIVATE)        \
     __asm __volatile (__lll_lock_asm_start                   \
               ".subsection 1\n\t"                    \
               ".type _L_lock_%=, @function\n"            \
               "_L_lock_%=:\n"                    \
               "1:\tlea %2, %%" RDI_LP "\n"               \
               "2:\tsub $128, %%" RSP_LP "\n"             \
               "3:\tcallq __lll_lock_wait_private\n"          \
               "4:\tadd $128, %%" RSP_LP "\n"             \
               "5:\tjmp 24f\n"                    \
               "6:\t.size _L_lock_%=, 6b-1b\n\t"              \
               ".previous\n"                      \
               LLL_STUB_UNWIND_INFO_5                 \
               "24:"                          \
               : "=S" (ignore1), "=&D" (ignore2), "=m" (futex),   \
                 "=a" (ignore3)                   \
               : "0" (1), "m" (futex), "3" (0)            \
               : "cx", "r11", "cc", "memory");            \
       else                                   \
     __asm __volatile (__lll_lock_asm_start                   \
               ".subsection 1\n\t"                    \
               ".type _L_lock_%=, @function\n"            \
               "_L_lock_%=:\n"                    \
               "1:\tlea %2, %%" RDI_LP "\n"               \
               "2:\tsub $128, %%" RSP_LP "\n"             \
               "3:\tcallq __lll_lock_wait\n"              \
               "4:\tadd $128, %%" RSP_LP "\n"             \
               "5:\tjmp 24f\n"                    \
               "6:\t.size _L_lock_%=, 6b-1b\n\t"              \
               ".previous\n"                      \
               LLL_STUB_UNWIND_INFO_5                 \
               "24:"                          \
               : "=S" (ignore1), "=D" (ignore2), "=m" (futex),    \
                 "=a" (ignore3)                   \
               : "1" (1), "m" (futex), "3" (0), "0" (private)     \
               : "cx", "r11", "cc", "memory");            \
    })
  /* Record the ownership.  */
  mutex->__data.__owner = id;
#ifndef NO_INCR
  ++mutex->__data.__nusers;
#endif

  LIBC_PROBE (mutex_acquired, 1, mutex);

  return 0;
}
```
```s
#define lll_lock(futex, private) \
  (void)                                      \
    ({ int ignore1, ignore2, ignore3;                         \
       if (__builtin_constant_p (private) && (private) == LLL_PRIVATE)        \
     __asm __volatile (__lll_lock_asm_start                   \
               ".subsection 1\n\t"                    \
               ".type _L_lock_%=, @function\n"            \
               "_L_lock_%=:\n"                    \
               "1:\tlea %2, %%" RDI_LP "\n"               \
               "2:\tsub $128, %%" RSP_LP "\n"             \
               "3:\tcallq __lll_lock_wait_private\n"          \
               "4:\tadd $128, %%" RSP_LP "\n"             \
               "5:\tjmp 24f\n"                    \
               "6:\t.size _L_lock_%=, 6b-1b\n\t"              \
               ".previous\n"                      \
               LLL_STUB_UNWIND_INFO_5                 \
               "24:"                          \
               : "=S" (ignore1), "=&D" (ignore2), "=m" (futex),   \
                 "=a" (ignore3)                   \
               : "0" (1), "m" (futex), "3" (0)            \
               : "cx", "r11", "cc", "memory");            \
       else                                   \
     __asm __volatile (__lll_lock_asm_start                   \
               ".subsection 1\n\t"                    \
               ".type _L_lock_%=, @function\n"            \
               "_L_lock_%=:\n"                    \
               "1:\tlea %2, %%" RDI_LP "\n"               \
               "2:\tsub $128, %%" RSP_LP "\n"             \
               "3:\tcallq __lll_lock_wait\n"              \
               "4:\tadd $128, %%" RSP_LP "\n"             \
               "5:\tjmp 24f\n"                    \
               "6:\t.size _L_lock_%=, 6b-1b\n\t"              \
               ".previous\n"                      \
               LLL_STUB_UNWIND_INFO_5                 \
               "24:"                          \
               : "=S" (ignore1), "=D" (ignore2), "=m" (futex),    \
                 "=a" (ignore3)                   \
               : "1" (1), "m" (futex), "3" (0), "0" (private)     \
               : "cx", "r11", "cc", "memory");            \
    })
```s
__lll_lock_wait:
    cfi_startproc
    pushq   %r10
    cfi_adjust_cfa_offset(8)
    pushq   %rdx
    cfi_adjust_cfa_offset(8)
    cfi_offset(%r10, -16)
    cfi_offset(%rdx, -24)
    xorq    %r10, %r10  /* No timeout.  */
    movl    $2, %edx
    LOAD_FUTEX_WAIT (%esi)
             
    cmpl    %edx, %eax  /* NB:   %edx == 2 */
    jne 2f                                                                                                                                                                                                         
             
1:  LIBC_PROBE (lll_lock_wait, 2, %rdi, %rsi)
    movl    $SYS_futex, %eax
    syscall
             
2:  movl    %edx, %eax
    xchgl   %eax, (%rdi)    /* NB:   lock is implied */
             
    testl   %eax, %eax
    jnz 1b
             
    popq    %rdx
    cfi_adjust_cfa_offset(-8)
    cfi_restore(%rdx)
    popq    %r10
    cfi_adjust_cfa_offset(-8)
    cfi_restore(%r10)
    retq  
    cfi_endproc
    .size   __lll_lock_wait,.-__lll_lock_wait
             
    /*      %rdi: futex
        %rsi: flags
        %rdx: timeout
        %eax: futex value
    */       
    .globl  __lll_timedlock_wait
    .type   __lll_timedlock_wait,@function
    .hidden __lll_timedlock_wait
    .align  16
```
