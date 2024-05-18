#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>
#include <linux/limits.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <linux/kernel.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
#include <stdbool.h>
#endif
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include "workqueue.h"

#define DEVNAME "_workqueue"
#define BUF_SIZE 1024

/** Structure */
typedef struct
{
	struct cdev cdev;
	spinlock_t lock;
	wait_queue_head_t inq;
	char buf[BUF_SIZE];
	bool busy;
	struct workqueue_struct *workqueue;
	struct delayed_work dwork;
	unsigned long prevjiffies;
	int loops;
}workqueue_dev_t;


/** VARIBALES*/
static workqueue_dev_t *workqueue_devs = NULL;
static int workqueue_minor			  = 0;
static int workqueue_major			  = 0;
static const int WORKQUEUE_COUNT	  = 1;
static bool workqueue_fops_registered	  = false;
static struct class *workqueue_cls   = NULL;
static const int WORKQUEUE_LOOPS = 20;
static int workqueue_delay = 4;
module_param(workqueue_delay, int, S_IRUGO);

/** FUNCTIONS*/
static int workqueue_create_devfiles(void);
static void workqueue_destroy_devfiles(void);

static int workqueue_open(struct inode *node, struct file *fp)
{
	int ret = 0;
	workqueue_dev_t *dev = container_of(node->i_cdev, workqueue_dev_t, cdev);
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

static int workqueue_release(struct inode *node, struct file *fp)
{
	workqueue_dev_t *dev = fp->private_data; 
	spin_lock(&dev->lock);
	dev->busy = false;
	spin_unlock(&dev->lock);
    PRINTK( "workqueue_release\n");
    return 0;
}

static ssize_t workqueue_read(struct file *fp, char __user *buf, 
			size_t size, loff_t * offset)
{
	workqueue_dev_t *dev = fp->private_data;
	unsigned long j;
	size_t min = size;

	spin_lock(&dev->lock);
	dev->loops = WORKQUEUE_LOOPS;
	j = jiffies;
	dev->prevjiffies = j;
	spin_unlock(&dev->lock);

	queue_delayed_work(dev->workqueue, &dev->dwork, workqueue_delay);

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

static struct file_operations workqueue_fops = 
{
	.owner = THIS_MODULE,
	.open = workqueue_open,
	.release = workqueue_release,
	.read = workqueue_read,
};

/** tasklet callback*/
static void 
workqueue_cb(struct work_struct * work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	workqueue_dev_t *dev =  container_of(dwork, workqueue_dev_t, dwork);
	unsigned long j; 

	spin_lock(&dev->lock);

	j = jiffies; 
	sprintf(dev->buf, "%9li(jiffies) %3li(delta) %i(in_interrupt)"
				" %6i(pid) %i(cpu) %s(comm)\n", 
				j, j - dev->prevjiffies, in_interrupt() ? 1 : 0, 
				current->pid, smp_processor_id(), current->comm); 
	if(--dev->loops > 0)
	{
		dev->prevjiffies = j;
		queue_delayed_work(dev->workqueue, &dev->dwork, workqueue_delay);
		spin_unlock(&dev->lock);
	}
	else
	{
		spin_unlock(&dev->lock);
		wake_up_interruptible(&dev->inq);
	}
}

/*
 * Set up the char_dev structure for this device.
 */
static int 
workqueue_setup_cdev(workqueue_dev_t *dev, int index)
{
    int err, devno = MKDEV(workqueue_major, workqueue_minor + index);

    cdev_init(&dev->cdev, &workqueue_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add (&dev->cdev, devno, 1);
	spin_lock_init(&dev->lock);
	memset(dev->buf, 0, sizeof(dev->buf));
	dev->busy = false;
	dev->workqueue = create_singlethread_workqueue(DEVNAME);
	if(!dev->workqueue)
	{
		PRINTK("create_singlethread_queue failed\n");
		return -EFAULT;
	}
	INIT_DELAYED_WORK(&dev->dwork, workqueue_cb);
	init_waitqueue_head(&dev->inq);
    /* Fail gracefully if need be */
    if (err) {
        PRINTK("Error %d: adding %s%d", err, DEVNAME, index);
    }
	return err;
}

static void
workqueue_stop_cdev(workqueue_dev_t *dev)
{
	cdev_del(&dev->cdev);
}

static void
workqueue_stop_cdevs(workqueue_dev_t *dev, int count)
{
	int i;
	for(i = 0; i< count; ++i)
	  workqueue_stop_cdev(&dev[i]);
}

/**
  Sut up devices
 */
static int 
workqueue_setup_cdevs(workqueue_dev_t * devs, int count)
{
	int i, j;
	for(i = 0; i < count; ++i)
	{
		if(workqueue_setup_cdev(&devs[i], i))
		{
		  for(j = 0; j < i; ++j)
			workqueue_stop_cdev(&devs[j]);
		  PRINTK("workqueue_setup_cdev failed\n");
		  return -1;
		}
	}
	PRINTK("workqueue_setup_cdev succeed\n");
	return 0;
}

static int __init 
_workqueue_init(void)
{
	int ret;
	dev_t devnum = 0;

	PRINTK( "ku init\n");

	if(workqueue_delay < 4)
	{
		PRINTK("Invalid workqueue_delay = %d < 4\n", workqueue_delay);
		return -EINVAL;
	}

	/** Asking for a dynamic workqueue_major*/
	ret = alloc_chrdev_region(&devnum, workqueue_minor, WORKQUEUE_COUNT, DEVNAME);
	if(ret < 0)
	{
		PRINTK( "workqueue_init failed : alloc_chrdev_region\n");
		goto ask_major_failed;
	}
	workqueue_major = MAJOR(devnum);
	PRINTK("devnum=%d\n", devnum);

	/*  
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
	workqueue_devs = (workqueue_dev_t *)kmalloc(WORKQUEUE_COUNT * sizeof(workqueue_dev_t), GFP_KERNEL);
	if(!workqueue_devs)
	{
		PRINTK("kmalloc failed\n");
		ret = -ENOMEM;
		goto allocate_devices_failed;
	}
	memset(workqueue_devs, 0, WORKQUEUE_COUNT * sizeof(workqueue_dev_t));

	/* Initialize all device. */
	if(workqueue_setup_cdevs(workqueue_devs, WORKQUEUE_COUNT) == -1)
	  goto setup_devs_failed; 

	workqueue_fops_registered = true;
	
	/** Create all device file*/
	if(workqueue_create_devfiles() == -1)
	  goto create_devfiles_failed;


	PRINTK("workqueue_init success : workqueue_major=%d, workqueue_minor=%d\n", workqueue_major, workqueue_minor);
	return 0;

create_devfiles_failed:
	workqueue_stop_cdevs(workqueue_devs, WORKQUEUE_COUNT);
setup_devs_failed:
	kfree(workqueue_devs);
allocate_devices_failed:
	unregister_chrdev_region(devnum, WORKQUEUE_COUNT);
ask_major_failed:
	return ret;
}

static void __exit 
workqueue_exit(void)
{
    workqueue_destroy_devfiles();
	if(workqueue_fops_registered)
	{
		workqueue_stop_cdevs(workqueue_devs, WORKQUEUE_COUNT);
		kfree(workqueue_devs);
		unregister_chrdev_region(MKDEV(workqueue_major, workqueue_minor), WORKQUEUE_COUNT);
		workqueue_fops_registered = false;
	}
	PRINTK( "finish ku exit.\n");
}

static int 
workqueue_create_devfiles(void)
{
    int i, j;
    struct device *dev;

    /* 在/sys中导出设备类信息 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
    workqueue_cls = class_create(THIS_MODULE, DEVNAME);
#else
    workqueue_cls = class_create(DEVNAME);
#endif
    if(workqueue_cls == NULL)
    {
        PRINTK( "workqueue_create_devfiles failed : class_create\n");
        return -1;
    }

    /* 在workqueue_cls指向的类中创建一组(个)设备文件 */
	PRINTK("workqueue_minor=%d, workqueue_minor + WORKQUEUE_COUNT=%d\n", 
				workqueue_minor, workqueue_minor + WORKQUEUE_COUNT);
    for(i = workqueue_minor;  i < (workqueue_minor + WORKQUEUE_COUNT); i++)
	{
        dev = device_create(workqueue_cls, 
                    NULL, 
                    MKDEV(workqueue_major,i),
                    NULL,
                    "%s%d", 
                    DEVNAME,
                    i);
        if(unlikely(dev == NULL))
        {
            PRINTK( "workqueue_create_devfiles failed : device_create\n");
            for(j = workqueue_minor; j < i; ++j)
            {
                device_destroy(workqueue_cls, MKDEV(workqueue_major,j));
            }
            class_destroy(workqueue_cls);
            return -1;
        }
        PRINTK( "Dev %s%d has been created\n", DEVNAME, i);
    }  
    return 0;
}

static void 
workqueue_destroy_devfiles(void)
{
    int i;
    if(workqueue_cls == NULL) 
    {
        PRINTK( "workqueue_destroy_devfiles : class is null\n");
        return;
    }
    /* 在workqueue_cls指向的类中删除一组(个)设备文件 */
    for(i = workqueue_minor; i<(workqueue_minor+WORKQUEUE_COUNT); i++){
        device_destroy(workqueue_cls, MKDEV(workqueue_major,i));
    }

    /* 在/sys中删除设备类信息 */
    class_destroy(workqueue_cls);             //一定要先卸载device再卸载class
    PRINTK( "workqueue_destroy_devfiles success");
}

module_init(_workqueue_init);
module_exit(workqueue_exit);

MODULE_AUTHOR("ZYJ");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("A simple Dev Module.");
MODULE_ALIAS("KU Module");
