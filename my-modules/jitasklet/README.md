# Kernel Timers
## 能做啥
- (1) to schedule an action to happen later
- (2) asynchronous execution

## 使用场景
schedule_timeout、TCP和SCTP协议栈

## 实现
linux/timer.h, kernel/timer.c

## 上下文
In fact, kernel timers are run as
the result of a “`software interrupt`.”

## 特征
- (1) a task can `reregister itself` to run again at a later time
- (2) the timer function is executed by the same `CPU` that registered it
- (3) they are a potential source of `race conditions`, even on uniprocessor systems
- (4) run at `interrupt time`

## API Lists
```c
#include <linux/timer.h>
struct timer_list
{
    /* ... */
    unsigned long expires;
    void (*function)(unsigned long);
    unsigned long data;
};
void init_timer(struct timer_list *timer);
struct timer_list TIMER_INITIALIZER(_function, _expires, _data);
#define setup_timer(timer, fn, data) // 初始化function，data成员

/**
 * add_timer - start a timer
 * @timer: the timer to be added
 *
 * The kernel will do a ->function(->data) callback from the
 * timer interrupt at the ->expires point in the future. The
 * current time is 'jiffies'.
 *
 * The timer's ->expires, ->function (and if the handler uses it, ->data)
 * fields must be set prior calling this function.
 *
 * Timers with an ->expires field in the past will be executed in the next
 * timer tick.
 */
void add_timer(struct timer_list *timer);

/**
 * mod_timer - modify a timer's timeout
 * @timer: the timer to be modified
 * @expires: new timeout in jiffies
 *
 * mod_timer() is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 *
 * mod_timer(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * Note that if there are multiple unserialized concurrent users of the
 * same timer, then mod_timer() is the only safe way to modify the timeout,
 * since add_timer() cannot modify an already running timer.
 *
 * The function returns whether it has modified a pending timer or not.
 * (ie. mod_timer() of an inactive timer returns 0, mod_timer() of an
 * active timer returns 1.)
 */
int mod_timer(struct timer_list *timer, unsigned long expires);

/**
 * del_timer - deactive a timer.
 * @timer: the timer to be deactivated
 *
 * del_timer() deactivates a timer - this works on both active and inactive
 * timers.
 *
 * The function returns whether it has deactivated a pending timer or not.
 * (ie. del_timer() of an inactive timer returns 0, del_timer() of an
 * active timer returns 1.)
 */
int del_timer(struct timer_list *timer);
```

# Tasklets
## 能做啥
- (1) to schedule an action to happen later
- (2) asynchronous execution

## 使用场景
It is mostly used in interrupt management

## 实现
linux/interrupt.h, kernel/softirq.c

## 上下文
`software interrupt`

## 特征
- (1) a tasklet can `reregister itself` to run again.
- (2) run on the same CPU that schedules them.
- (3) you can’t ask to execute the function at a specific time.(a later time chosen by the kernel)
- (4) run at `interrupt time`.
- (5) A tasklet can be `disabled` and `re-enabled` later; it won’t be executed until it is enabled as many times as it has been disabled.
- (6) A tasklet can be scheduled to execute at normal `priority` or high priority. The latter group is always executed first.
- (7) Tasklets may be run immediately if the system is not under heavy load but never
later than the next `timer tick`.
- (8) A tasklets can be `concurrent` with other tasklets but is strictly `serialized` with
respect to itself—the same tasklet never runs simultaneously on more than one
processor. Also, as already noted, a tasklet always runs on the same CPU that
schedules it.

## API Lists
```c
struct tasklet_struct
{
    /** ... */
	void (*func)(unsigned long);
	unsigned long data;
};

/*
This function disables the given tasklet. The tasklet may still be scheduled with
tasklet_schedule, but its execution is deferred until the tasklet has been enabled
again. If the tasklet is currently running, this function busy-waits until the tasklet
exits; thus, after calling tasklet_disable, you can be sure that the tasklet is not
running anywhere in the system.*/
void tasklet_disable(struct tasklet_struct *t);

/*
Disable the tasklet, but without waiting for any currently-running function to
exit. When it returns, the tasklet is disabled and won’t be scheduled in the future
until re-enabled, but it may be still running on another CPU when the function
returns. */
void tasklet_disable_nosync(struct tasklet_struct *t);

/*
Enables a tasklet that had been previously disabled. If the tasklet has already
been scheduled, it will run soon. A call to tasklet_enable must match each call to
tasklet_disable, as the kernel keeps track of the “disable count” for each tasklet. */
void tasklet_enable(struct tasklet_struct *t);

/*
Schedule the tasklet for execution. If a tasklet is scheduled again before it has a
chance to run, it runs only once. However, if it is scheduled while it runs, it runs
again after it completes; this ensures that events occurring while other events are
being processed receive due attention. This behavior also allows a tasklet to
reschedule itself.*/
void tasklet_schedule(struct tasklet_struct *t);

/*
Schedule the tasklet for execution with higher priority. When the soft interrupt
handler runs, it deals with high-priority tasklets before other soft interrupt tasks,
including “normal” tasklets. Ideally, only tasks with low-latency requirements
(such as filling the audio buffer) should use this function, to avoid the additional latencies introduced by other soft interrupt handlers. Actually, /proc/
jitasklethi shows no human-visible difference from /proc/jitasklet. */
void tasklet_hi_schedule(struct tasklet_struct *t);

/*
This function ensures that the tasklet is not scheduled to run again; it is usually
called when a device is being closed or the module removed. If the tasklet is
scheduled to run, the function waits until it has executed. If the tasklet reschedules itself, you must prevent it from rescheduling itself before calling tasklet_kill,
as with del_timer_sync.*/
void tasklet_kill(struct tasklet_struct *t);
```

# Workqueues
## 能做啥
Workqueues are, superficially, similar to tasklets; they allow kernel code to request that a function be called at some future time.

## 使用场景

## 实现
linux/workqueue.h, kernel/workqueue.c

## 上下文
`software interrupt`

## 特征
- (1) Tasklets run in software interrupt context with the result that all tasklet code must be atomic. Instead, workqueue functions run in the context of a `special kernel process`; as a result, they have more flexibility. In particular, workqueue functions can `sleep`.
```shell
$ ps -elf | grep 'workqueue' | grep -v grep
1 I root        9566       2  0  60 -20 -     0 -      15:12 ?        00:00:00 [_workqueue]
```
- (2) Tasklets always run on the processor from which they were originally submitted. Workqueues work in the same way, by default.
- (3) Kernel code can request that the execution of workqueue functions be `delayed` for an explicit interval.
- (4) The key difference between the two is that tasklets execute quickly, for a short period of time, and in atomic mode, while workqueue functions may have higher latency but need not be atomic

## API Lists
```c
struct workqueue_struct;
struct workqueue_struct *create_workqueue(const char *name);
struct workqueue_struct *create_singlethread_workqueue(const char *name);
void flush_workqueue(struct workqueue_struct *queue);
void destroy_workqueue(struct workqueue_struct *queue);

// init
struct work_struct;
struct delayed_work;
DECLARE_WORK(name, void (*function)(void *), void *data); // compile time
INIT_WORK(struct work_struct *work, void (*function)(void *), void *data);
PREPARE_WORK(struct work_struct *work, void (*function)(void *), void *data);

// There are two functions for submitting work to a workqueue
int queue_work(struct workqueue_struct *queue, struct work_struct *work);
/**
 * queue_delayed_work - queue work on a workqueue after delay
 * @wq: workqueue to use
 * @dwork: delayable work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Equivalent to queue_delayed_work_on() but tries to use the local CPU.
 */
int queue_delayed_work(struct workqueue_struct *queue,
                      struct work_struct *work, unsigned long delay);

/**
 * cancel_delayed_work - cancel a delayed work
 * @dwork: delayed_work to cancel
 *
 * Kill off a pending delayed_work.  Returns %true if @dwork was pending
 * and canceled; %false if wasn't pending.  Note that the work callback
 * function may still be running on return, unless it returns %true and the
 * work doesn't re-arm itself.  Explicitly flush or use
 * cancel_delayed_work_sync() to wait on it.
 *
 * This function is safe to call from any context including IRQ handler.
 */
bool cancel_delayed_work(struct delayed_work *dwork)

```

# 附录A 非进程上下文（process context）的约束
- No access to user space is allowed. Because there is no process context, there is no path to the user space associated with any particular process.
- The `current` pointer is not meaningful in atomic mode and cannot be used since the relevant code has no connection with the process that has been interrupted.
- No sleeping or scheduling may be performed. Atomic code may not call schedule or a form of `wait_event`, nor may it call any other function that could sleep.
For example, calling `kmalloc(..., GFP_KERNEL)` is against the rules. `Semaphores` also must not be used since they can sleep.（还有`mutex`）

```c
/*
 * Are we doing bottom half or hardware interrupt processing?
 * Are we in a softirq context? Interrupt context?
 * in_softirq - Are we currently processing softirq or have bh disabled?
 * in_serving_softirq - Are we currently processing softirq?
 */
#define in_irq()		(hardirq_count())
#define in_softirq()		(softirq_count())
#define in_interrupt()		(irq_count())
#define in_serving_softirq()	(softirq_count() & SOFTIRQ_OFFSET)

/*
 * Are we running in atomic context?  WARNING: this macro cannot
 * always detect atomic context; in particular, it cannot know about
 * held spinlocks in non-preemptible kernels.  Thus it should not be
 * used in the general case to determine whether sleeping is possible.
 * Do not use in_atomic() in driver code.
 */
#define in_atomic()	((preempt_count() & ~PREEMPT_ACTIVE) != 0)
```