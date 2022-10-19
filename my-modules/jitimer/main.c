#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>
#include <linux/limits.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <stdbool.h>
#include <linux/moduleparam.h>
#include "jitimer.h"

#define DEVNAME "jitimer"
#define BUF_SIZE 1024

/** Structure */
typedef struct
{
	struct cdev cdev;
	spinlock_t lock;
	wait_queue_head_t inq;
	char buf[BUF_SIZE];
	bool busy;
	struct timer_list timer;
	unsigned long prevjiffies;
	int loops;
}jitimer_dev_t;


/** VARIBALES*/
static jitimer_dev_t *jitimer_devs = NULL;
static int jitimer_minor			  = 0;
static int jitimer_major			  = 0;
static const int JITIMER_COUNT	  = 1;
static bool jitimer_fops_registered	  = false;
static struct class *jitimer_cls   = NULL;
static const int JITIMER_LOOPS = 20;
static int jitimer_delay = 4;
module_param(jitimer_delay, int,  S_IRUGO);

/** FUNCTIONS*/
static int jitimer_create_devfiles(void);
static void jitimer_destroy_devfiles(void);

static int jitimer_open(struct inode *node, struct file *fp)
{
	int ret = 0;
	jitimer_dev_t *dev = container_of(node->i_cdev, jitimer_dev_t, cdev);
	fp->private_data = dev;
	spin_lock(&dev->lock);
	if(!dev->busy)
	{
		dev->busy = true;
	}
	else
	  ret = -EBUSY;
	spin_unlock(&dev->lock);
    PRINTK("ep open %s, dev=%p\n", (ret == 0)?"success":"failed" , dev);
    return ret;
}

static int jitimer_release(struct inode *node, struct file *fp)
{
	jitimer_dev_t *dev = fp->private_data; 
	spin_lock(&dev->lock);
	dev->busy = false;
	spin_unlock(&dev->lock);
    PRINTK( "jitimer_release\n");
    return 0;
}

static ssize_t jitimer_read(struct file *fp, char __user *buf, 
			size_t size, loff_t * offset)
{
	jitimer_dev_t *dev = fp->private_data;
	unsigned long j;
	size_t min;

	spin_lock(&dev->lock);
	dev->loops = JITIMER_LOOPS;
	j = jiffies;
	dev->prevjiffies = j;
	dev->timer.expires = j + jitimer_delay;
	add_timer(&dev->timer);
	spin_unlock(&dev->lock);

	/** wait for the buffer to fill*/
	wait_event_interruptible(dev->inq, !dev->loops);

	/** read buffer*/
	spin_lock(&dev->lock);

	min = (size < strlen(dev->buf)) ?size:strlen(dev->buf);
	if(copy_to_user(buf, dev->buf, min))
	{
		PRINTK("copy_to_user failed\n");
		spin_unlock(&dev->lock);
		return -EFAULT;
	}

	spin_unlock(&dev->lock);

	return min;
}

static struct file_operations jitimer_fops = 
{
	.owner = THIS_MODULE,
	.open = jitimer_open,
	.release = jitimer_release,
	.read = jitimer_read,
};

/** timer callback*/
static void 
jitimer_timer(struct timer_list * t)
{
	jitimer_dev_t *dev =  from_timer(dev, t, timer);
	unsigned long j; 

	spin_lock(&dev->lock);
	j = jiffies; 
	sprintf(dev->buf, "%9li(jiffies) %3li(delta) %i(in_interrupt)"
				" %6i(pid) %i(cpu) %s(comm)\n", 
				j, j - dev->prevjiffies, in_interrupt() ? 1 : 0, 
				current->pid, smp_processor_id(), current->comm); 
	if (--dev->loops) { 
		dev->timer.expires += jitimer_delay; 
		dev->prevjiffies = j; 
		add_timer(&dev->timer); 
	} else { 
		wake_up_interruptible(&dev->inq); 
	}
	spin_unlock(&dev->lock);
}

/*
 * Set up the char_dev structure for this device.
 */
static int 
jitimer_setup_cdev(jitimer_dev_t *dev, int index)
{
    int err, devno = MKDEV(jitimer_major, jitimer_minor + index);

    cdev_init(&dev->cdev, &jitimer_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add (&dev->cdev, devno, 1);
	spin_lock_init(&dev->lock);
	memset(dev->buf, 0, sizeof(dev->buf));
	dev->busy = false;
	dev->timer.function = jitimer_timer;
	init_waitqueue_head(&dev->inq);
    /* Fail gracefully if need be */
    if (err)
        PRINTK("Error %d: adding %s%d", err, DEVNAME, index);
	return err;
}

static void
jitimer_stop_cdev(jitimer_dev_t *dev)
{
	cdev_del(&dev->cdev);
}

static void
jitimer_stop_cdevs(jitimer_dev_t *dev, int count)
{
	int i;
	for(i = 0; i< count; ++i)
	  jitimer_stop_cdev(&dev[i]);
}

/**
  Sut up devices
 */
static int 
jitimer_setup_cdevs(jitimer_dev_t * devs, int count)
{
	int i, j;
	for(i = 0; i < count; ++i)
	{
		if(jitimer_setup_cdev(&devs[i], i))
		{
		  for(j = 0; j < i; ++j)
			jitimer_stop_cdev(&devs[j]);
		  PRINTK("jitimer_setup_cdev failed\n");
		  return -1;
		}
	}
	PRINTK("jitimer_setup_cdev succeed\n");
	return 0;
}

static int __init 
jitimer_init(void)
{
	int ret;
	dev_t devnum = 0;

	PRINTK( "ku init\n");

	if(jitimer_delay < 4)
	{
		PRINTK("jitimer_delay = %d < 4\n", jitimer_delay);
		return -EINVAL;
	}
	PRINTK("jitimer_delay = %d\n", jitimer_delay);

	/** Asking for a dynamic jitimer_major*/
	ret = alloc_chrdev_region(&devnum, jitimer_minor, JITIMER_COUNT, DEVNAME);
	if(ret < 0)
	{
		PRINTK( "jitimer_init failed : alloc_chrdev_region\n");
		goto ask_major_failed;
	}
	jitimer_major = MAJOR(devnum);
	PRINTK("devnum=%d\n", devnum);

	/*  
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
	jitimer_devs = (jitimer_dev_t *)kmalloc(JITIMER_COUNT * sizeof(jitimer_dev_t), GFP_KERNEL);
	if(!jitimer_devs)
	{
		PRINTK("kmalloc failed\n");
		ret = -ENOMEM;
		goto allocate_devices_failed;
	}
	memset(jitimer_devs, 0, JITIMER_COUNT * sizeof(jitimer_dev_t));

	/* Initialize all device. */
	if(jitimer_setup_cdevs(jitimer_devs, JITIMER_COUNT) == -1)
	  goto setup_devs_failed; 

	jitimer_fops_registered = true;
	
	/** Create all device file*/
	if(jitimer_create_devfiles() == -1)
	  goto create_devfiles_failed;


	PRINTK("jitimer_init success : jitimer_major=%d, jitimer_minor=%d\n", jitimer_major, jitimer_minor);
	return 0;

create_devfiles_failed:
	jitimer_stop_cdevs(jitimer_devs, JITIMER_COUNT);
setup_devs_failed:
	kfree(jitimer_devs);
allocate_devices_failed:
	unregister_chrdev_region(devnum, JITIMER_COUNT);
ask_major_failed:
	return ret;
}

static void __exit 
jitimer_exit(void)
{
    jitimer_destroy_devfiles();
	if(jitimer_fops_registered)
	{
		jitimer_stop_cdevs(jitimer_devs, JITIMER_COUNT);
		kfree(jitimer_devs);
		unregister_chrdev_region(MKDEV(jitimer_major, jitimer_minor), JITIMER_COUNT);
		jitimer_fops_registered = false;
	}
	PRINTK( "finish ku exit.\n");
}

static int 
jitimer_create_devfiles(void)
{
    int i, j;
    struct device *dev;

    /* 在/sys中导出设备类信息 */
    jitimer_cls = class_create(THIS_MODULE, DEVNAME);
    if(jitimer_cls == NULL)
    {
        PRINTK( "jitimer_create_devfiles failed : class_create\n");
        return -1;
    }

    /* 在jitimer_cls指向的类中创建一组(个)设备文件 */
	PRINTK("jitimer_minor=%d, jitimer_minor + JITIMER_COUNT=%d\n", 
				jitimer_minor, jitimer_minor + JITIMER_COUNT);
    for(i = jitimer_minor;  i < (jitimer_minor + JITIMER_COUNT); i++)
	{
        dev = device_create(jitimer_cls, 
                    NULL, 
                    MKDEV(jitimer_major,i),
                    NULL,
                    "%s%d", 
                    DEVNAME,
                    i);
        if(unlikely(dev == NULL))
        {
            PRINTK( "jitimer_create_devfiles failed : device_create\n");
            for(j = jitimer_minor; j < i; ++j)
            {
                device_destroy(jitimer_cls, MKDEV(jitimer_major,j));
            }
            class_destroy(jitimer_cls);
            return -1;
        }
        PRINTK( "Dev %s%d has been created\n", DEVNAME, i);
    }  
    return 0;
}

static void 
jitimer_destroy_devfiles(void)
{
    int i;
    if(jitimer_cls == NULL) 
    {
        PRINTK( "jitimer_destroy_devfiles : class is null\n");
        return;
    }
    /* 在jitimer_cls指向的类中删除一组(个)设备文件 */
    for(i = jitimer_minor; i<(jitimer_minor+JITIMER_COUNT); i++){
        device_destroy(jitimer_cls, MKDEV(jitimer_major,i));
    }

    /* 在/sys中删除设备类信息 */
    class_destroy(jitimer_cls);             //一定要先卸载device再卸载class
    PRINTK( "jitimer_destroy_devfiles success");
}

module_init(jitimer_init);
module_exit(jitimer_exit);

MODULE_AUTHOR("ZYJ");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("A simple Dev Module.");
MODULE_ALIAS("KU Module");
