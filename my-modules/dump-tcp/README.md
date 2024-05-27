# 1. kmod
- 通过`netfilter`获取TCP packet 并保存到 List 中
- read操作将 List 中的 Packet 交付给用户程序

## 1.1 spin_lock
- 相较于`mutex`，可以在ATOMIC上下文(即不能休眠和调度)中使用，例如中断上下文
- 如果spin_lock需要在中断上下文中使用，需要使用带`_bh`或`_irq`后缀的API来避免死锁
- `_bh`和`_irq` 通过禁用当前CPU（local CPU）上的中断来避免发生抢断，从而避免死锁
```c
void spin_lock(spinlock_t *lock);
// 加锁前禁用软硬中断
void spin_lock_irqsave(spinlock_t *lock, unsigned long flags); 
// 加锁前禁用软中断，硬中断不禁止
void spin_lock_irq(spinlock_t *lock);
void spin_lock_bh(spinlock_t *lock);

void spin_unlock(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);
void spin_unlock_irq(spinlock_t *lock);
void spin_unlock_bh(spinlock_t *lock);

// nonblock
// return nonzero on success
int spin_trylock(spinlock_t *lock);
int spin_trylock_bh(spinlock_t *lock);
```

## 1.2 eventpoll
### 1.2.1 相关结构体
```c
/* Wait structure used by the poll hooks */
struct eppoll_entry {
	/* List header used to link this structure to the "struct epitem" */
	struct eppoll_entry *next;

	/* The "base" pointer is set to the container "struct epitem" */
	struct epitem *base;

	/*
	 * Wait queue item that will be linked to the target file wait
	 * queue head（在此个demo中被链接到dev->rwq队列上）.
	 */
	wait_queue_entry_t wait;

	/* The wait queue head that linked the "wait" wait queue item */
	wait_queue_head_t *whead;// 此demo中，值为&dev->rwq
};

/*
 * Each file descriptor added to the eventpoll interface will
 * have an entry of this type linked to the "rbr" RB tree.
 * Avoid increasing the size of this struct, there can be many thousands
 * of these on a server and we do not want this to take another cache line.
 */
struct epitem {
	union {
		/* RB tree node links this structure to the eventpoll RB tree */
		struct rb_node rbn;
		/* Used to free the struct epitem */
		struct rcu_head rcu;
	};

	/* List header used to link this structure to the eventpoll ready list */
	struct list_head rdllink;

	/*
	 * Works together "struct eventpoll"->ovflist in keeping the
	 * single linked chain of items.
	 */
	struct epitem *next;

	/* The file descriptor information this item refers to */
	struct epoll_filefd ffd;

	/*
	 * Protected by file->f_lock, true for to-be-released epitem already
	 * removed from the "struct file" items list; together with
	 * eventpoll->refcount orchestrates "struct eventpoll" disposal
	 */
	bool dying;

	/* List containing poll wait queues */
	struct eppoll_entry *pwqlist;

	/* The "container" of this item */
	struct eventpoll *ep;

	/* List header used to link this item to the "struct file" items list */
	struct hlist_node fllink;

	/* wakeup_source used when EPOLLWAKEUP is set */
	struct wakeup_source __rcu *ws;

	/* The structure that describe the interested events and the source fd */
	struct epoll_event event;
};


/*
 * This structure is stored inside the "private_data" member of the file
 * structure and represents the main data structure for the eventpoll
 * interface.
 */
struct eventpoll {
	/*
	 * This mutex is used to ensure that files are not removed
	 * while epoll is using them. This is held during the event
	 * collection loop, the file cleanup path, the epoll file exit
	 * code and the ctl operations.
	 */
	struct mutex mtx;

	/* Wait queue used by sys_epoll_wait() */
	wait_queue_head_t wq;

	/* Wait queue used by file->poll() 作为另外一个efd上的fd的时候*/
	wait_queue_head_t poll_wait;

	/* List of ready file descriptors */
	struct list_head rdllist;

	/* Lock which protects rdllist and ovflist */
	rwlock_t lock;

	/* RB tree root used to store monitored fd structs */
	struct rb_root_cached rbr;

	/*
	 * This is a single linked list that chains all the "struct epitem" that
	 * happened while transferring ready events to userspace w/out
	 * holding ->lock.
	 */
	struct epitem *ovflist;

	/* wakeup_source used when ep_scan_ready_list is running */
	struct wakeup_source *ws;

	/* The user that created the eventpoll descriptor */
	struct user_struct *user;

	struct file *file;

	/* used to optimize loop detection check */
	u64 gen;
	struct hlist_head refs;

	/*
	 * usage count, used together with epitem->dying to
	 * orchestrate the disposal of this struct
	 */
	refcount_t refcount;

#ifdef CONFIG_NET_RX_BUSY_POLL
	/* used to track busy poll napi_id */
	unsigned int napi_id;
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/* tracks wakeup nests for lockdep validation */
	u8 nests;
#endif
};
```

### 1.2.2 epoll_ctl(EP_CTL_ADD)
将一个eppoll_entry链接到dev->rwq队列相关调用：
```c
/**
* 事件检查；拷贝event到内核空间 
*/
epoll_ctl(epfd, EP_CTL_ADD, fd, event)//eventpoll.c

/*
* 从分别epfd和fd取得对应的struct file *file, *tfile，并取得struct eventpoll *ep
*/
do_epoll_ctl(epfd, EP_CTL_ADD, fd, event)

/*
* 构造struct epitem *epi，将epi插入红黑树
* 初始化struct ep_pqueue epq，调用ep_item_poll(epi, &epq.pt)
*/
ep_insert(ep, event, tfile, fd) //eventpoll.c

/** 
* 调用vfs_posll
*/
ep_item_poll(epi, pt)//eventpoll.c

/**
* 待用内核模块实现的poll op，此demo为dump_tcp_poll
*/
vfs_poll(file, pt)//poll.h

/** 
* (1) 调用poll_wait，其中wait_head参数为kmod相应的waitqueue，此demo为&dev->rwq
* (2) 检查内核模块是否有事件，返货mask
*/
dump_tcp_poll(file, pt)//kmod

/**
* 调用struct poll_table *pt 中的_qproc，此demo为ep_ptable_queue_proc
*/
poll_wait(filp, &dev->rwq, pt)//poll.h

/**
* 通过container_of从pt取得struct ep_pqueue *epq
* 构造struct eppoll_entry *pwq，将wake_up回调函数设置为ep_poll_callback，将pwq->wait添加到whead队列(即dev->rwq)中
*/
ep_ptable_queue_proc(file, whead, pt)
```

### 1.2.3 wakeup
来自kmod的唤醒
```c
/**
* demo hook函数：调用wake_up_interruptible
* x为&dev->rwq
*/
unsigned int dump_tcp_hookfn(void *priv, struct sk_buff *skb,
            const struct nf_hook_state *state);

/** 宏*/
#define wake_up_interruptible(x)	__wake_up(x, TASK_INTERRUPTIBLE, 1, NULL)

/**
 * __wake_up - wake up threads blocked on a waitqueue.
 * @wq_head: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: is directly passed to the wakeup function
 *
 * If this function wakes up a task, it executes a full memory barrier
 * before accessing the task state.  Returns the number of exclusive
 * tasks that were awaken.
 * 
 * 只是一个wrapper
 */
int __wake_up(struct wait_queue_head *wq_head, unsigned int mode,
	      int nr_exclusive, void *key)
{
	return __wake_up_common_lock(wq_head, mode, nr_exclusive, 0, key);
}

/**
* 加锁并调用__wake_up_common 
*/
int __wake_up_common_lock(struct wait_queue_head *wq_head, unsigned int mode,
			int nr_exclusive, int wake_flags, void *key);

/*
 * The core wakeup function. Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up. If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake that number of exclusive tasks, and potentially all
 * the non-exclusive tasks. Normally, exclusive tasks will be at the end of
 * the list and any non-exclusive tasks will be woken first. A priority task
 * may be at the head of the list, and can consume the event without any other
 * tasks being woken.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING. try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 *
 * 最终会调用之前入队的wait_queue_entry中的func，即ep_poll_callback
 */
static int __wake_up_common(struct wait_queue_head *wq_head, unsigned int mode,
			int nr_exclusive, int wake_flags, void *key,
			wait_queue_entry_t *bookmark);

/*
 * This is the callback that is passed to the wait queue wakeup
 * mechanism(等待队列唤醒机制). It is called by the stored file descriptors when they
 * have events to report.
 *
 * This callback takes a read lock in order not to contend(争夺) with concurrent
 * events from another file descriptor, thus all modifications to ->rdllist
 * or ->ovflist are lockless.  Read lock is paired with the write lock from
 * ep_scan_ready_list(), which stops all list modifications and guarantees
 * that lists state is seen correctly.
 *
 * ...
 *
 * 调用wake_up(&ep->wq)，唤醒通过epoll_wait等待在ep->wq队列上的线程
 */
int ep_poll_callback(wait_queue_entry_t *wait, unsigned mode, int sync, void *key);
```
# 2. example
## 2.1 libpcap创建一个pcap_dump的步骤
- (1) opening a capture for output
```c
pcap_t *pcap_open_dead(int linktype, int snaplen);
pcap_t *pcap_open_dead_with_tstamp_precision(int linktype, int snaplen,
           u_int precision);
```

- (2) open a file to which to write packets
```c
pcap_dumper_t *pcap_dump_open(pcap_t *p, const char *fname);
pcap_dumper_t *pcap_dump_open_append(pcap_t *p, const char *fname);
pcap_dumper_t *pcap_dump_fopen(pcap_t *p, FILE *fp);
```

- (3) 创建packet
```c
// create a packet
struct pcap_pkthdr pkt = {
    .ts = tv,
    .caplen = (unsigned)std::min(nread, SNAPLEN),
    .len = (unsigned)nread
};
```

- (4) write a packet to a capture file
```c
void pcap_dump(u_char *user, struct pcap_pkthdr *h,
	u_char *sp);
```

更多细节参考`example/manin.c`