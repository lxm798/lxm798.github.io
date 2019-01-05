# task_group

## 生命周期
### 初始化
创建目录
```c
static int cgroup_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct cgroup *c_parent = dentry->d_parent->d_fsdata;

	/* the vfs holds inode->i_mutex already */
	return cgroup_create(c_parent, dentry, mode | S_IFDIR);
}
```

```c
static long cgroup_create(struct cgroup *parent, struct dentry *dentry,
			     umode_t mode)
{
	struct cgroup *cgrp;
	struct cgroupfs_root *root = parent->root;
	int err = 0;
	struct cgroup_subsys *ss;
	struct super_block *sb = root->sb;

	cgrp = kzalloc(sizeof(*cgrp), GFP_KERNEL);
	if (!cgrp)
		return -ENOMEM;

	/* Grab a reference on the superblock so the hierarchy doesn't
	 * get deleted on unmount if there are child cgroups.  This
	 * can be done outside cgroup_mutex, since the sb can't
	 * disappear while someone has an open control file on the
	 * fs */
	atomic_inc(&sb->s_active);

	mutex_lock(&cgroup_mutex);

	init_cgroup_housekeeping(cgrp);

	cgrp->parent = parent;
	cgrp->root = parent->root;
	cgrp->top_cgroup = parent->top_cgroup;

	if (notify_on_release(parent))
		set_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);

	if (clone_children(parent))
		set_bit(CGRP_CLONE_CHILDREN, &cgrp->flags);

	for_each_subsys(root, ss) {
		struct cgroup_subsys_state *css = ss->create(cgrp);

		if (IS_ERR(css)) {
			err = PTR_ERR(css);
			goto err_destroy;
		}
		init_cgroup_css(css, ss, cgrp);
		if (ss->use_id) {
			err = alloc_css_id(ss, parent, cgrp);
			if (err)
				goto err_destroy;
		}
		/* At error, ->destroy() callback has to free assigned ID. */
		if (clone_children(parent) && ss->post_clone)
			ss->post_clone(cgrp);
	}

	cgroup_lock_hierarchy(root);
	list_add(&cgrp->sibling, &cgrp->parent->children);
	cgroup_unlock_hierarchy(root);
	root->number_of_cgroups++;

	err = cgroup_create_dir(cgrp, dentry, mode);
	if (err < 0)
		goto err_remove;

	/* The cgroup directory was pre-locked for us */
	BUG_ON(!mutex_is_locked(&cgrp->dentry->d_inode->i_mutex));

	err = cgroup_populate_dir(cgrp);
	/* If err < 0, we have a half-filled directory - oh well ;) */

	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);

	return 0;

 err_remove:

	cgroup_lock_hierarchy(root);
	list_del(&cgrp->sibling);
	cgroup_unlock_hierarchy(root);
	root->number_of_cgroups--;

 err_destroy:

	for_each_subsys(root, ss) {
		if (cgrp->subsys[ss->subsys_id])
			ss->destroy(cgrp);
	}

	mutex_unlock(&cgroup_mutex);

	/* Release the reference count that we took on the superblock */
	deactivate_super(sb);

	kfree(cgrp);
	return err;
}
```
```c
static struct cgroup_subsys_state *cpu_cgroup_create(struct cgroup *cgrp)
{
	struct task_group *tg, *parent;

	if (!cgrp->parent) {
		/* This is early initialization for the top cgroup */
		return &root_task_group.css;
	}

	parent = cgroup_tg(cgrp->parent);
	tg = sched_create_group(parent);
	if (IS_ERR(tg))
		return ERR_PTR(-ENOMEM);

	return &tg->css;
}
```
```c
struct task_group *sched_create_group(struct task_group *parent)
{
	struct task_group *tg;
	unsigned long flags;

	tg = kzalloc(sizeof(*tg), GFP_KERNEL);
	if (!tg)
		return ERR_PTR(-ENOMEM);

	if (!alloc_fair_sched_group(tg, parent))
		goto err;

	if (!alloc_rt_sched_group(tg, parent))
		goto err;

	spin_lock_irqsave(&task_group_lock, flags);
	list_add_rcu(&tg->list, &task_groups);

	WARN_ON(!parent); /* root should already exist */

	tg->parent = parent;
	INIT_LIST_HEAD(&tg->children);
	list_add_rcu(&tg->siblings, &parent->children);
	spin_unlock_irqrestore(&task_group_lock, flags);

	return tg;

err:
	free_sched_group(tg);
	return ERR_PTR(-ENOMEM);
}
```
```c
int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se;
	int i;

	tg->cfs_rq = kzalloc(sizeof(cfs_rq) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->cfs_rq)
		goto err;

	tg->se = kzalloc(sizeof(se) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->se)
		goto err;

	tg->shares = NICE_0_LOAD;

	init_cfs_bandwidth(tg_cfs_bandwidth(tg));

	for_each_possible_cpu(i) {
		cfs_rq = kzalloc_node(sizeof(struct cfs_rq),
				      GFP_KERNEL, cpu_to_node(i));
		if (!cfs_rq)
			goto err;

		se = kzalloc_node(sizeof(struct sched_entity),
				  GFP_KERNEL, cpu_to_node(i));
		if (!se)
			goto err_free_rq;

		init_cfs_rq(cfs_rq);
		init_tg_cfs_entry(tg, cfs_rq, se, i, parent->se[i]);
	}

	return 1;

err_free_rq:
	kfree(cfs_rq);
err:
	return 0;
}
```

### 初始化cfs_rq和task_group
```c
void init_cfs_rq(struct cfs_rq *cfs_rq)
{
	cfs_rq->tasks_timeline = RB_ROOT;
	cfs_rq->min_vruntime = (u64)(-(1LL << 20));
#ifndef CONFIG_64BIT
	cfs_rq->min_vruntime_copy = cfs_rq->min_vruntime;
#endif
}
```

#### init_tg_cfs_entry

```c
void init_tg_cfs_entry(struct task_group *tg, struct cfs_rq *cfs_rq,
			struct sched_entity *se, int cpu,
			struct sched_entity *parent)
{

	struct rq *rq = cpu_rq(cpu);
    // 初始化task_group和队列
	cfs_rq->tg = tg;
	cfs_rq->rq = rq;
#ifdef CONFIG_SMP
	/* allow initial update_cfs_load() to truncate */
	cfs_rq->load_stamp = 1;
#endif
	init_cfs_rq_runtime(cfs_rq);

	tg->cfs_rq[cpu] = cfs_rq;
	tg->se[cpu] = se;

	/* se could be NULL for root_task_group */
	if (!se)
		return;

	if (!parent)
		se->cfs_rq = &rq->cfs;
	else
		se->cfs_rq = parent->my_q;

	se->my_q = cfs_rq;
	update_load_set(&se->load, 0);
	se->parent = parent;
}
```