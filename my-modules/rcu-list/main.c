// https://docs.kernel.org/RCU/listRCU.html
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/compiler.h>
#include <linux/sched/task.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

#define MAX_TASKS 8

struct ipv4_entry {
    struct list_head list;
    struct rcu_head rcu;
    uint32_t ipv4;
};

typedef int (*thread_fn_t)(void *data);

static LIST_HEAD(ipv4_list);
static int cpus = 0;
static const int loops = 1024;
static struct task_struct *tasks[MAX_TASKS];
static bool force_quit = false;

static int reader(void *data) 
{
    char thread_name[TASK_COMM_LEN];  // TASK_COMM_LEN is typically 16

    // Get the name of the current task (thread)
    get_task_comm(thread_name, current);

    printk(KERN_INFO "Thread (%s) started", thread_name);

	for (int loop = 0; loop < loops && !force_quit; ++loop) {
		struct ipv4_entry *e;

		// RCU read-side critical section start
		rcu_read_lock();

		// Iterate over the list safely using RCU
		list_for_each_entry_rcu(e, &ipv4_list, list) {
			// Access the ipv4 member of each ipv4_entry
			// printk(KERN_INFO "Reader(%s): IPv4 entry %pI4\n", thread_name, &e->ipv4);
		}

        mdelay(1);
		
        // RCU read-side critical section end
		rcu_read_unlock();

		// Simulate some work
		// cond_resched();  // Yield the CPU, allowing other tasks to run
	}
    printk(KERN_INFO "Thread (%s) finished", thread_name);
	return 0;
}

static int writer(void *data)
{
    char thread_name[TASK_COMM_LEN];  // TASK_COMM_LEN is typically 16

    // Get the name of the current task (thread)
    get_task_comm(thread_name, current);

    printk(KERN_INFO "Thread (%s) started", thread_name);

    for (int loop = 0; loop < loops; ++loop) {
        struct ipv4_entry *e = kmalloc(sizeof(struct ipv4_entry), GFP_KERNEL);
        INIT_LIST_HEAD_RCU(&e->list);
        if (!e) {
            printk(KERN_ERR "Failed to alloc ipv4_entry");
            break;
        }
        
        list_add_tail_rcu(&ipv4_list, &e->list);

        mdelay(2);

        list_del_rcu(&e->list);
        
        kfree_rcu(e, rcu);
		//cond_resched();  // Yield the CPU, allowing other tasks to run

    }
    printk(KERN_INFO "Thread (%s) finished", thread_name);
    return 0;
}

static int __init
test_rcu_list_init(void)
{
    int cpu;
    bool first = true;

    cpus_read_lock();
    for_each_online_cpu(cpu) { 
        char thread_name[TASK_COMM_LEN];  // TASK_COMM_LEN is typically 16
        
        thread_fn_t func = first ? writer: reader;
        snprintf(thread_name, TASK_COMM_LEN, 
                    first ? "writer.%d": "reader.%d", 
                    cpu);
        if (first) {
            first = false;
        }   

        tasks[cpus] = get_task_struct(kthread_run_on_cpu(func, NULL, cpu, thread_name));
    
        if (++cpus == MAX_TASKS) {
            break;
        }
    }
    cpus_read_unlock(); 

    printk(KERN_INFO "test_rcu_list_init success");
    
    return 0;
}

static void __exit
test_rcu_list_exit(void)
{ 
    for (int i = 0; i < cpus; ++i) {
        kthread_stop(tasks[i]);
        put_task_struct(tasks[i]);
    }
    printk(KERN_INFO "test_rcu_list_exit success");
}

module_init(test_rcu_list_init);
module_exit(test_rcu_list_exit);
