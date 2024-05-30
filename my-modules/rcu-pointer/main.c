/**
 * main.c
 *  用RCU保护单指针
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/cpumask.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");

struct foo {
    int a;
    char b;
    long c;
};

struct loop_work{
    struct work_struct work;
    atomic_t loop;
};

DEFINE_SPINLOCK(foo_mutex);

static struct foo __rcu *gbl_foo = NULL;
static struct workqueue_struct *g_wq = NULL;
static struct loop_work *g_works = NULL;
static int g_online_cpus = 0;
static atomic_t g_a = {0};
static const int LOOPS = 1024;

/*
 * Create a new struct foo that is the same as the one currently
 * pointed to by gbl_foo, except that field "a" is replaced
 * with "new_a".  Points gbl_foo to the new structure, and
 * frees up the old structure after a grace period(宽限期).
 *
 * Uses rcu_assign_pointer() to ensure that concurrent readers
 * see the initialized version of the new structure.
 *
 * Uses synchronize_rcu() to ensure that any readers that might
 * have references to the old structure complete before freeing
 * the old structure.
 */
static void foo_update_a(int new_a)
{
    struct foo *new_fp;
    struct foo *old_fp;

    new_fp = kmalloc(sizeof(*new_fp), GFP_KERNEL);

    spin_lock(&foo_mutex);
    
    old_fp = rcu_dereference_protected(gbl_foo, lockdep_is_held(&foo_mutex));
    
    *new_fp = *old_fp;
    new_fp->a = new_a;
    rcu_assign_pointer(gbl_foo, new_fp);
    
    spin_unlock(&foo_mutex);
    
    synchronize_rcu();
    kfree(old_fp);
}

/*
 * Return the value of field "a" of the current gbl_foo
 * structure.  Use rcu_read_lock() and rcu_read_unlock()
 * to ensure that the structure does not get deleted out
 * from under us, and use rcu_dereference() to ensure that
 * we see the initialized version of the structure (important
 * for DEC Alpha and for people reading the code).
 */
static int foo_get_a(void)
{
    int retval;

    rcu_read_lock();
    retval = rcu_dereference(gbl_foo)->a;
    rcu_read_unlock();
    return retval;
}

static void loop(struct work_struct * work)
{
    struct loop_work *lw = container_of(work, struct loop_work, work);
    int loop = atomic_inc_return(&lw->loop);
    printk(KERN_INFO "work@%p: loop=%d", work, loop);
    if(loop < LOOPS) {
        queue_work(g_wq, work);// queue on local lcore
    }
}

static void reader(struct work_struct *work) 
{
    foo_get_a(); 
    loop(work);
}

static void updater(struct work_struct *work) 
{
    foo_update_a(atomic_inc_return(&g_a));
    loop(work);
}

static int __init test_rcu_init(void)
{
    int ret = 0;
    g_online_cpus = num_online_cpus();

    if(g_online_cpus < 2) {
        printk(KERN_ERR "not enough lcores");
        return -EINVAL;
    }

    gbl_foo = kmalloc(sizeof(struct foo), GFP_KERNEL);
    if(!gbl_foo) {
        ret = - ENOMEM;
        goto foo_failed;
    }
    memset(gbl_foo, 0, sizeof(struct foo));

    // Create workqueue on each online lcore
    g_wq = create_workqueue("test_rcu"); 
    if(!g_wq) {
        printk(KERN_ERR "create_workqueue failed");
        ret = -ENOMEM;
        goto wq_failed;
    }
    
    // Create works according to g_online_cpus
    g_works = kmalloc(sizeof(struct loop_work) * g_online_cpus, GFP_KERNEL);
    if(!g_works) {
        printk(KERN_ERR "alloc works failed");
        ret = -ENOMEM;
        goto works_failed;
    }
    memset(g_works, 0, sizeof(struct loop_work) * g_online_cpus);

    // Init and queue work on lcore
    unsigned i = 0, lcore;
    for_each_online_cpu(lcore) {
        work_func_t f = (i % 2  == 0)?updater:reader;
        INIT_WORK(&g_works[i].work, f);
        queue_work_on(lcore, g_wq, &g_works[i].work);
        ++i;
    }

    return 0;

works_failed:
    destroy_workqueue(g_wq);
wq_failed:
    kfree(gbl_foo);
foo_failed:
    return ret;
}

static void __exit test_rcu_exit(void)
{ 
    printk(KERN_INFO "a: %d", foo_get_a());
    flush_workqueue(g_wq);
    kfree(g_works);
    destroy_workqueue(g_wq);
    kfree(gbl_foo);
}

module_init(test_rcu_init);
module_exit(test_rcu_exit);
