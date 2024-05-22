# linux/list.h
- Circular doubly linked list \
PS: Linux-6.5.0 Source Code
# 1. 结构体
Linux定义：List
```c
struct list_head {
	struct list_head *next, *prev;
};
```
用户定义：Node
```c
struct message {
    struct list_head messages;
    char *data;
	size_t size;
};
```

# 2. List初始化API
静态（编译期）
```c
#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

// 声明并初始化一个消息List
LIST_HEAD_INIT(messages);
```
动态（运行期）
```c
/**
 * INIT_LIST_HEAD - Initialize a list_head structure
 * @list: list_head structure to be initialized.
 *
 * Initializes the list_head to point to itself.  If it is a list header,
 * the result is an empty list.
 */
static inline void INIT_LIST_HEAD(struct list_head *list)
{
	WRITE_ONCE(list->next, list);//list->prev=list
	WRITE_ONCE(list->prev, list);//list->next=list
}

//声明并初始化一个消息List
struct list_head messages;
INIT_LIST_HEAD(&messages);
```

# 3. 相关宏
## 3.1 container_of 
从成员指针到包含成员的结构体的指针
```c
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 * WARNING: any const qualifier of @ptr is lost.
 */
#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	static_assert(__same_type(*(ptr), ((type *)0)->member) ||	\
		      __same_type(*(ptr), void),			\
		      "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member))); })

// pos 类型为 struct list_head*
struct message* msg = container_of(pos, struct message, messages);
```

## 3.2 list_entry
```c
/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_head within the struct.
 */
#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

struct message* msg = container_of(ptr, struct message, messages);
```
其他：
```c
list_first_entry(ptr, type, member)
list_last_entry(ptr, type, member)
list_first_entry_or_null(ptr, type, member)
/** .... */
```

## 3.3 for_each: List遍历
### 3.3.1 list_for_each：遍历List
```c
/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; !list_is_head(pos, (head)); pos = pos->next)

struct list_head *pos = NULL;
struct message *msg = NULL;
list_for_each(pos, &messages){
    msg = list_entry(pos, struct message, messages);
    //do_something(msg);
}
```

### 3.3.2 list_for_each_entry: 更方便的遍历——带类型
对于用户，少定义一个变量，且无需调用list_entry
```c
/**
 * list_for_each_entry	-	iterate over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     !list_entry_is_head(pos, head, member);			\
	     pos = list_next_entry(pos, member))

/** sample*/
struct message *msg = NULL;
list_for_each_entry(msg, &messages, messages) {
	//do_something(msg);
}
```

### 3.3.3 list_for_each_entry_safe：在遍历时，可以安全地从List中删除节点
```c
/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
		n = list_next_entry(pos, member);			\
	     !list_entry_is_head(pos, head, member); 			\
	     pos = n, n = list_next_entry(n, member))

/** sample*/
struct message *msg = NULL;
struct message *next = NULL;
list_for_each_entry_safe(msg, next, &messages, messages) {
    list_del(&msg->messages);
	/** ... */
}
```

## 3.4 添加元素
### 3.4.1 在某个位置之后插入元素
```c
/*
 * Insert a new entry between two known consecutive entries.
 * O(1)
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	if (!__list_add_valid(new, prev, next))
		return;

	next->prev = new;
	new->next = next;
	new->prev = prev;
	WRITE_ONCE(prev->next, new);
}
/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}
```

### 3.4.2 尾部插入
```c
/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}
```

### sample
```c
// add msg to list head
list_add(&msg->messages, &messages);
// add msg to list tail
list_add_tail(&msg->messages, &messages);
```

## 3.5 删除元素
```c
/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void list_del(struct list_head *entry)
{
	__list_del_entry(entry);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}

//sample
list_del(&msg->messages, messages);
```

## 3.6 替换元素
```c
/**
 * list_replace - replace old entry by new one
 * @old : the element to be replaced
 * @new : the new element to insert
 *
 * If @old was empty, it will be overwritten.
 */
static inline void list_replace(struct list_head *old,
				struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}
```

# 附录A 内核模块支持阻塞读写
A.1 Linux waitqueue
```c
struct wait_queue_head {
	spinlock_t		lock;
	struct list_head	head;
};

// 初始化
#define init_waitqueue_head(wq_head)						\
	do {									\
		static struct lock_class_key __key;				\
										\
		__init_waitqueue_head((wq_head), #wq_head, &__key);		\
	} while (0)

// 休眠等待条件为真
/**
 * wait_event_interruptible - sleep until a condition gets true
 * @wq_head: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 *
 * The process is put to sleep (TASK_INTERRUPTIBLE) until the
 * @condition evaluates to true or a signal is received.
 * The @condition is checked each time the waitqueue @wq_head is woken up.
 *
 * wake_up() has to be called after changing any variable that could
 * change the result of the wait condition.
 *
 * The function will return -ERESTARTSYS if it was interrupted by a
 * signal and 0 if @condition evaluated to true.
 */
#define wait_event_interruptible(wq_head, condition)				\
({										\
	int __ret = 0;								\
	might_sleep();								\
	if (!(condition))							\
		__ret = __wait_event_interruptible(wq_head, condition);		\
	__ret;									\
})

// 唤醒
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
 */
int __wake_up(struct wait_queue_head *wq_head, unsigned int mode,
	      int nr_exclusive, void *key)
{
	return __wake_up_common_lock(wq_head, mode, nr_exclusive, 0, key);
} 
#define wake_up_interruptible(x)	__wake_up(x, TASK_INTERRUPTIBLE, 1, NULL)
```

A.2 Sample
```c
// reader
/** ... */
if(mutex_lock_interruptible(&dev->mutex)) {
        return -ERESTARTSYS;
    }
while(dev->message_count == 0) {
    mutex_unlock(&dev->mutex); // release mutex
    if(file->f_flags & O_NONBLOCK) {
        return -EAGAIN;
    } else {
        if(wait_event_interruptible(dev->rwq,
                        dev->message_count != 0)
                    == -ERESTARTSYS) {
            printk(KERN_DEBUG "%s: wait_event_interruptible failed",__func__);
            return -ERESTARTSYS;
        }
    }
    if(mutex_lock_interruptible(&dev->mutex)) {
        return -ERESTARTSYS;
    }
}
/** ... */
mutex_unlock(&dev->mutex);
/** ...  */
wake_up_interruptible(&dev->wwq); // wakeup writer
/** ... */
```

```c
// writer
if(mutex_lock_interruptible(&dev->mutex)) {
    return -ERESTARTSYS;
}
while(dev->message_count >= MAX_MESSAGES) {
    mutex_unlock(&dev->mutex); // release mutex
    if(file->f_flags & O_NONBLOCK) {
        return -EAGAIN;
    } else {
        if(wait_event_interruptible(dev->wwq,
                        dev->message_count < MAX_MESSAGES)
                    == -ERESTARTSYS) {
            printk(KERN_DEBUG "%s: wait_event_interruptible failed", __func__);
            return -ERESTARTSYS;
        }
    }
    if(mutex_lock_interruptible(&dev->mutex)) {
        return -ERESTARTSYS;
    }
}
/** ... */
mutex_unlock(&dev->mutex);
/** ...  */
wake_up_interruptible(&dev->wwq); // wakeup reader
/** ... */
```