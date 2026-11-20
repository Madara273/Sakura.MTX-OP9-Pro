// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/tracehook.h>

static struct callback_head work_exited; /* all we need is ->next == NULL */

/**
 * task_work_add - ask the @task to execute @work->func()
 * @task: the task which should run the callback
 * @work: the callback to run
 * @notify: mode used to notify the task
 *
 * Queue @work for task_work_run() and notify @task according to @notify.
 * Fails if the @task is exiting/exited and thus it can't process this @work.
 * Otherwise @work->func() will be called when the @task returns from kernel
 * mode or exits.
 *
 * TWA_SIGNAL wakes the task. TWA_RESUME requests a callback on return to
 * user mode, and TWA_NONE queues the work without a notification.
 *
 * Note: there is no ordering guarantee on works queued here.
 *
 * RETURNS:
 * 0 if succeeds or -ESRCH.
 */
int
task_work_add(struct task_struct *task, struct callback_head *work,
	      enum task_work_notify_mode notify)
{
	struct callback_head *head;

	do {
		head = READ_ONCE(task->task_works);
		if (unlikely(head == &work_exited))
			return -ESRCH;
		work->next = head;
	} while (cmpxchg(&task->task_works, head, work) != head);

	switch (notify) {
	case TWA_NONE:
		break;
	case TWA_RESUME:
		set_notify_resume(task);
		break;
	case TWA_SIGNAL:
		task_work_notify_signal(task);
		break;
	}

	return 0;
}

/**
 * task_work_cancel_match - cancel a pending work added by task_work_add()
 * @task: the task which should execute the work
 * @match: identifies the work to remove
 * @data: argument passed to @match
 *
 * Find the last queued pending work matching @match and remove it.
 *
 * RETURNS:
 * The found work or NULL if not found.
 */
struct callback_head *
task_work_cancel_match(struct task_struct *task,
		       bool (*match)(struct callback_head *, void *), void *data)
{
	struct callback_head **pprev = &task->task_works;
	struct callback_head *work;
	unsigned long flags;

	if (likely(!task->task_works))
		return NULL;
	/*
	 * If cmpxchg() fails we continue without updating pprev.
	 * Either we raced with task_work_add() which added the
	 * new entry before this work, we will find it again. Or
	 * we raced with task_work_run(), *pprev == NULL/exited.
	 */
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	while ((work = READ_ONCE(*pprev))) {
		if (!match(work, data))
			pprev = &work->next;
		else if (cmpxchg(pprev, work, work->next) == work)
			break;
	}
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	return work;
}

static bool task_work_func_match(struct callback_head *work, void *data)
{
	return work->func == data;
}

struct callback_head *
task_work_cancel(struct task_struct *task, task_work_func_t func)
{
	return task_work_cancel_match(task, task_work_func_match, func);
}

/**
 * task_work_run - execute the works added by task_work_add()
 *
 * Flush the pending works. Should be used by the core kernel code.
 * Called before the task returns to the user-mode or stops, or when
 * it exits. In the latter case task_work_add() can no longer add the
 * new work after task_work_run() returns.
 */
void task_work_run(void)
{
	struct task_struct *task = current;
	struct callback_head *work, *head, *next;

	for (;;) {
		/*
		 * work->func() can do task_work_add(), do not set
		 * work_exited unless the list is empty.
		 */
		do {
			head = NULL;
			work = READ_ONCE(task->task_works);
			if (!work) {
				if (task->flags & PF_EXITING)
					head = &work_exited;
				else
					break;
			}
		} while (cmpxchg(&task->task_works, work, head) != work);

		if (!work)
			break;
		/*
		 * Synchronize with task_work_cancel(). It can not remove
		 * the first entry == work, cmpxchg(task_works) must fail.
		 * But it can remove another entry from the ->next list.
		 */
		raw_spin_lock_irq(&task->pi_lock);
		raw_spin_unlock_irq(&task->pi_lock);

		do {
			next = work->next;
			work->func(work);
			work = next;
			cond_resched();
		} while (work);
	}
}

void task_work_notify_signal(struct task_struct *task)
{
	unsigned long flags;

	/*
	 * The lockless check pairs with the barrier in get_signal() and
	 * task_work_run_pending().
	 */
	if (!(READ_ONCE(task->jobctl) & JOBCTL_TASK_WORK) &&
	    lock_task_sighand(task, &flags)) {
		task->jobctl |= JOBCTL_TASK_WORK;
		signal_wake_up(task, 0);
		unlock_task_sighand(task, &flags);
	}
}

/* Run callbacks while consuming the signal notification used by 5.4. */
bool task_work_run_pending(void)
{
	struct task_struct *task = current;
	unsigned long flags;
	bool pending;

	if (!(READ_ONCE(task->jobctl) & JOBCTL_TASK_WORK) &&
	    !READ_ONCE(task->task_works))
		return false;

	__set_current_state(TASK_RUNNING);
	spin_lock_irqsave(&task->sighand->siglock, flags);
	smp_store_mb(task->jobctl, task->jobctl & ~JOBCTL_TASK_WORK);
	pending = READ_ONCE(task->task_works) != NULL;
	recalc_sigpending();
	spin_unlock_irqrestore(&task->sighand->siglock, flags);

	task_work_run();
	return pending;
}
