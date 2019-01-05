# fork 创建新进程

## do_fork
1. 检验CLONE_XX参数
2. 创建新进程
3. 唤醒新进程
```c
long do_fork(unsigned long clone_flags,
	      unsigned long stack_start,
	      struct pt_regs *regs,
	      unsigned long stack_size,
	      int __user *parent_tidptr,
	      int __user *child_tidptr)
{
	struct task_struct *p;
	int trace = 0;
	long nr;

	/*
	 * CLONE_NEWUSER 和 CLONE_THREAD 冲突
	 */
	if (clone_flags & CLONE_NEWUSER) {
		if (clone_flags & CLONE_THREAD)
			return -EINVAL;
		/* 使用CLONE_NEWUSER时,需要有相应的capable*/
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SETUID) ||
				!capable(CAP_SETGID))
			return -EPERM;
	}

    ....
    // 从父进程copy相关数据结构,创建新进程
	p = copy_process(clone_flags, stack_start, regs, stack_size,
			 child_tidptr, NULL, trace);
	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 */
	if (!IS_ERR(p)) {
		struct completion vfork;

		trace_sched_process_fork(current, p);

		nr = task_pid_vnr(p);

		if (clone_flags & CLONE_PARENT_SETTID)
			put_user(nr, parent_tidptr);

		if (clone_flags & CLONE_VFORK) {
			p->vfork_done = &vfork;
			init_completion(&vfork);
			get_task_struct(p);
		}
        // 唤醒进程
		wake_up_new_task(p);

		/* forking complete and child started to run, tell ptracer */
		if (unlikely(trace))
			ptrace_event(trace, nr);

		if (clone_flags & CLONE_VFORK) {
			if (!wait_for_vfork_done(p, &vfork))
				ptrace_event(PTRACE_EVENT_VFORK_DONE, nr);
		}
	} else {
		nr = PTR_ERR(p);
	}
	return nr;
}
```

## copy_process
1. 参数校验
2. copy 已经打开的文件\信号处理\数据计数
3. 设置pid,tid等
```c
static struct task_struct *copy_process(unsigned long clone_flags,
					unsigned long stack_start,
					struct pt_regs *regs,
					unsigned long stack_size,
					int __user *child_tidptr,
					struct pid *pid,
					int trace)
{
	int retval;
	struct task_struct *p;
	int cgroup_callbacks_done = 0;

    // 依赖selinux_capget 校验clone_flags权限
	retval = security_task_create(clone_flags);
	if (retval)
		goto fork_out;

	retval = -ENOMEM;
	p = dup_task_struct(current);
	if (!p)
		goto fork_out;

	rt_mutex_init_task(p);

	retval = -EAGAIN;
	current->flags &= ~PF_NPROC_EXCEEDED;

	// 不能大于系统最大线程数量
	if (nr_threads >= max_threads)
		goto bad_fork_cleanup_count;

	p->did_exec = 0;
    // 构建tsk->delays
	delayacct_tsk_init(p);
    // 填充进程的flag信息
	copy_flags(clone_flags, p);
    // 初始化兄弟和孩子链表头结点
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	rcu_copy_process(p);
	p->vfork_done = NULL;
	spin_lock_init(&p->alloc_lock);
    // 信号的pending 数组
	init_sigpending(&p->pending);
    // 时间等统计信息/结构初始化
	p->utime = p->stime = p->gtime = 0;
	p->utimescaled = p->stimescaled = 0;
	p->default_timer_slack_ns = current->timer_slack_ns;

	task_io_accounting_init(&p->ioac);
	acct_clear_integrals(p);

	posix_cpu_timers_init(p);

	do_posix_clock_monotonic_gettime(&p->start_time);
	p->real_start_time = p->start_time;
	monotonic_to_bootbased(&p->real_start_time);
	p->io_context = NULL;
	p->audit_context = NULL;
	if (clone_flags & CLONE_THREAD)
		threadgroup_change_begin(current);
    // 初始化cg_list结构
	cgroup_fork(p);

    // 初始化软中断相关能力
	p->hardirq_enable_ip = 0;
	p->hardirq_enable_event = 0;
	p->hardirq_disable_ip = _THIS_IP_;
	p->hardirq_disable_event = 0;
	p->softirqs_enabled = 1;
	p->softirq_enable_ip = _THIS_IP_;
	p->softirq_enable_event = 0;
	p->softirq_disable_ip = 0;
	p->softirq_disable_event = 0;
	p->hardirq_context = 0;
	p->softirq_context = 0;

    // 死锁检测初始化
	p->lockdep_depth = 0; /* no locks held yet */
	p->curr_chain_key = 0;
	p->lockdep_recursion = 0;


#ifdef CONFIG_CGROUP_MEM_RES_CTLR
	p->memcg_batch.do_batch = 0;
	p->memcg_batch.memcg = NULL;
#endif

	// 初始化调度相关的数据结构,task_struct
	sched_fork(p);
    // ???
	retval = perf_event_init_task(p);
	retval = audit_alloc(p);
	retval = copy_semundo(clone_flags, p);
    // copy 已经打开的文件记录
	retval = copy_files(clone_flags, p);
    // ??
	retval = copy_fs(clone_flags, p);
    // copy信号处理
	retval = copy_sighand(clone_flags, p);
    // copy信号
	retval = copy_signal(clone_flags, p);
    // copy 虚拟内存的描述结构
	retval = copy_mm(clone_flags, p);
    // copy namespace
	retval = copy_namespaces(clone_flags, p);
    // ???
	retval = copy_io(clone_flags, p);
    // copy堆栈信息
	retval = copy_thread(clone_flags, stack_start, stack_size, p, regs);
    // 处理namespace 中的pid
	if (pid != &init_struct_pid) {
		retval = -ENOMEM;
		pid = alloc_pid(p->nsproxy->pid_ns);
		if (!pid)
			goto bad_fork_cleanup_io;
	}
    // ???
	p->pid = pid_nr(pid);
	p->tgid = p->pid;
	if (clone_flags & CLONE_THREAD)
		p->tgid = current->tgid;

	p->set_child_tid = (clone_flags & CLONE_CHILD_SETTID) ? child_tidptr : NULL;
	/*
	 * Clear TID on mm_release()?
	 */
	p->clear_child_tid = (clone_flags & CLONE_CHILD_CLEARTID) ? child_tidptr : NULL;
#ifdef CONFIG_BLOCK
	p->plug = NULL;
#endif
#ifdef CONFIG_FUTEX
	p->robust_list = NULL;
#ifdef CONFIG_COMPAT
	p->compat_robust_list = NULL;
#endif
	INIT_LIST_HEAD(&p->pi_state_list);
	p->pi_state_cache = NULL;
#endif
	/*
	 * sigaltstack should be cleared when sharing the same VM
	 */
	if ((clone_flags & (CLONE_VM|CLONE_VFORK)) == CLONE_VM)
		p->sas_ss_sp = p->sas_ss_size = 0;

	/*
	 * Syscall tracing and stepping should be turned off in the
	 * child regardless of CLONE_PTRACE.
	 */
	user_disable_single_step(p);
	clear_tsk_thread_flag(p, TIF_SYSCALL_TRACE);
#ifdef TIF_SYSCALL_EMU
	clear_tsk_thread_flag(p, TIF_SYSCALL_EMU);
#endif
	clear_all_latency_tracing(p);

	/* ok, now we should be set up.. */
	if (clone_flags & CLONE_THREAD)
		p->exit_signal = -1;
	else if (clone_flags & CLONE_PARENT)
		p->exit_signal = current->group_leader->exit_signal;
	else
		p->exit_signal = (clone_flags & CSIGNAL);

	p->pdeath_signal = 0;
	p->exit_state = 0;

	p->nr_dirtied = 0;
	p->nr_dirtied_pause = 128 >> (PAGE_SHIFT - 10);
	p->dirty_paused_when = 0;
	p->group_leader = p;
	INIT_LIST_HEAD(&p->thread_group);

	/* Now that the task is set up, run cgroup callbacks if
	 * necessary. We need to run them before the task is visible
	 * on the tasklist. */
	cgroup_fork_callbacks(p);
	cgroup_callbacks_done = 1;

	/* Need tasklist lock for parent etc handling! */
	write_lock_irq(&tasklist_lock);

	if (clone_flags & (CLONE_PARENT|CLONE_THREAD)) {
		p->real_parent = current->real_parent;
		p->parent_exec_id = current->parent_exec_id;
	} else {
		p->real_parent = current;
		p->parent_exec_id = current->self_exec_id;
	}

	spin_lock(&current->sighand->siglock);

	/*
	 * Process group and session signals need to be delivered to just the
	 * parent before the fork or both the parent and the child after the
	 * fork. Restart if a signal comes in before we add the new process to
	 * it's process group.
	 * A fatal signal pending means that current will exit, so the new
	 * thread can't slip out of an OOM kill (or normal SIGKILL).
	*/
	recalc_sigpending();
	if (signal_pending(current)) {
		spin_unlock(&current->sighand->siglock);
		write_unlock_irq(&tasklist_lock);
		retval = -ERESTARTNOINTR;
		goto bad_fork_free_pid;
	}

	if (clone_flags & CLONE_THREAD) {
		current->signal->nr_threads++;
		atomic_inc(&current->signal->live);
		atomic_inc(&current->signal->sigcnt);
		p->group_leader = current->group_leader;
		list_add_tail_rcu(&p->thread_group, &p->group_leader->thread_group);
	}

	if (likely(p->pid)) {
		ptrace_init_task(p, (clone_flags & CLONE_PTRACE) || trace);

		if (thread_group_leader(p)) {
			if (is_child_reaper(pid))
				p->nsproxy->pid_ns->child_reaper = p;

			p->signal->leader_pid = pid;
			p->signal->tty = tty_kref_get(current->signal->tty);
			attach_pid(p, PIDTYPE_PGID, task_pgrp(current));
			attach_pid(p, PIDTYPE_SID, task_session(current));
			list_add_tail(&p->sibling, &p->real_parent->children);
			list_add_tail_rcu(&p->tasks, &init_task.tasks);
			__this_cpu_inc(process_counts);
		}
		attach_pid(p, PIDTYPE_PID, pid);
		nr_threads++;
	}

	total_forks++;
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);
	proc_fork_connector(p);
	cgroup_post_fork(p);
	if (clone_flags & CLONE_THREAD)
		threadgroup_change_end(current);
	perf_event_fork(p);

	trace_task_newtask(p, clone_flags);

	return p;

bad_fork_free_pid:
	if (pid != &init_struct_pid)
		free_pid(pid);
bad_fork_cleanup_io:
	if (p->io_context)
		exit_io_context(p);
bad_fork_cleanup_namespaces:
	if (unlikely(clone_flags & CLONE_NEWPID))
		pid_ns_release_proc(p->nsproxy->pid_ns);
	exit_task_namespaces(p);
bad_fork_cleanup_mm:
	if (p->mm)
		mmput(p->mm);
bad_fork_cleanup_signal:
	if (!(clone_flags & CLONE_THREAD))
		free_signal_struct(p->signal);
bad_fork_cleanup_sighand:
	__cleanup_sighand(p->sighand);
bad_fork_cleanup_fs:
	exit_fs(p); /* blocking */
bad_fork_cleanup_files:
	exit_files(p); /* blocking */
bad_fork_cleanup_semundo:
	exit_sem(p);
bad_fork_cleanup_audit:
	audit_free(p);
bad_fork_cleanup_policy:
	perf_event_free_task(p);
#ifdef CONFIG_NUMA
	mpol_put(p->mempolicy);
bad_fork_cleanup_cgroup:
#endif
	if (clone_flags & CLONE_THREAD)
		threadgroup_change_end(current);
	cgroup_exit(p, cgroup_callbacks_done);
	delayacct_tsk_free(p);
	module_put(task_thread_info(p)->exec_domain->module);
bad_fork_cleanup_count:
	atomic_dec(&p->cred->user->processes);
	exit_creds(p);
bad_fork_free:
	free_task(p);
fork_out:
	return ERR_PTR(retval);
}
```

### dup_task_struct
创建task_struct,并使用父进程的task_struct初始化改进程
分配thread_info和堆栈
```c
static struct task_struct *dup_task_struct(struct task_struct *orig)
{
	struct task_struct *tsk;
	struct thread_info *ti;
	unsigned long *stackend;
	int node = tsk_fork_get_node(orig);
	int err;

	prepare_to_copy(orig);
    // 创建task_struct
	tsk = alloc_task_struct_node(node);
	if (!tsk)
		return NULL;
    // 创建thread_info
	ti = alloc_thread_info_node(tsk, node);
	if (!ti) {
		free_task_struct(tsk);
		return NULL;
	}
    // 复制父进程task_struct
	err = arch_dup_task_struct(tsk, orig);
	if (err)
		goto out;
    // 分配新的堆栈,堆栈和thread_info绑定
	tsk->stack = ti;

	setup_thread_stack(tsk, orig);
	clear_user_return_notifier(tsk);
	clear_tsk_need_resched(tsk);
	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC;

	atomic_set(&tsk->usage, 2);
	tsk->splice_pipe = NULL;
    // 堆栈页数计数
	account_kernel_stack(ti, 1);

	return tsk;

out:
	free_thread_info(ti);
	free_task_struct(tsk);
	return NULL;
}
```