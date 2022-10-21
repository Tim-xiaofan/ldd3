#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>
#include <linux/limits.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <stdbool.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include "sharedqueue.h"

#define DEVNAME "sharedqueue"
#define BUF_SIZE 1024

/** Structure */
typedef struct
{
	struct cdev cdev;
	spinlock_t lock;
	wait_queue_head_t inq;
	char buf[BUF_SIZE];
	bool busy;
	struct delayed_work dwork;
	unsigned long prevjiffies;
	int loops;
}sharedqueue_dev_t;


/** VARIBALES*/
static sharedqueue_dev_t *sharedqueue_devs = NULL;
static int sharedqueue_minor			  = 0;
static int sharedqueue_major			  = 0;
static const int WORKQUEUE_COUNT	  = 1;
static bool sharedqueue_fops_registered	  = false;
static struct class *sharedqueue_cls   = NULL;
static const int WORKQUEUE_LOOPS = 20;
static int sharedqueue_delay = 4;
module_param(sharedqueue_delay, int, S_IRUGO);

/** FUNCTIONS*/
static int sharedqueue_create_devfiles(void);
static void sharedqueue_destroy_devfiles(void);

static int sharedqueue_open(struct inode *node, struct file *fp)
{
	int ret = 0;
	sharedqueue_dev_t *dev = container_of(node->i_cdev, sharedqueue_dev_t, cdev);
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

static int sharedqueue_release(struct inode *node, struct file *fp)
{
	sharedqueue_dev_t *dev = fp->private_data; 
	spin_lock(&dev->lock);
	dev->busy = false;
	spin_unlock(&dev->lock);
    PRINTK( "sharedqueue_release\n");
    return 0;
}

static ssize_t sharedqueue_read(struct file *fp, char __user *buf, 
			size_t size, loff_t * offset)
{
	sharedqueue_dev_t *dev = fp->private_data;
	unsigned long j;
	size_t min = size;
	DEFINE_WAIT(wait);

	spin_lock(&dev->lock);
	dev->loops = WORKQUEUE_LOOPS;
	j = jiffies;
	dev->prevjiffies = j;
	spin_unlock(&dev->lock);

	prepare_to_wait(&dev->inq, &wait, TASK_INTERRUPTIBLE);
	schedule_delayed_work(&dev->dwork, sharedqueue_delay);
	schedule();
	/** wait for the buffer to fill*/
	finish_wait(&dev->inq, &wait);

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

static struct file_operations sharedqueue_fops = 
{
	.owner = THIS_MODULE,
	.open = sharedqueue_open,
	.release = sharedqueue_release,
	.read = sharedqueue_read,
};

/** tasklet callback*/
static void 
sharedqueue_cb(struct work_struct * work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	sharedqueue_dev_t *dev =  container_of(dwork, sharedqueue_dev_t, dwork);
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
		schedule_delayed_work(&dev->dwork, sharedqueue_delay);
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
sharedqueue_setup_cdev(sharedqueue_dev_t *dev, int index)
{
    int err, devno = MKDEV(sharedqueue_major, sharedqueue_minor + index);

    cdev_init(&dev->cdev, &sharedqueue_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add (&dev->cdev, devno, 1);
	spin_lock_init(&dev->lock);
	memset(dev->buf, 0, sizeof(dev->buf));
	dev->busy = false;
	INIT_DELAYED_WORK(&dev->dwork, sharedqueue_cb);
	init_waitqueue_head(&dev->inq);
    /* Fail gracefully if need be */
    if (err)
        PRINTK("Error %d: adding %s%d", err, DEVNAME, index);
	return err;
}

static void
sharedqueue_stop_cdev(sharedqueue_dev_t *dev)
{
	cdev_del(&dev->cdev);
}

static void
sharedqueue_stop_cdevs(sharedqueue_dev_t *dev, int count)
{
	int i;
	for(i = 0; i< count; ++i)
	  sharedqueue_stop_cdev(&dev[i]);
}

/**
  Sut up devices
 */
static int 
sharedqueue_setup_cdevs(sharedqueue_dev_t * devs, int count)
{
	int i, j;
	for(i = 0; i < count; ++i)
	{
		if(sharedqueue_setup_cdev(&devs[i], i))
		{
		  for(j = 0; j < i; ++j)
			sharedqueue_stop_cdev(&devs[j]);
		  PRINTK("sharedqueue_setup_cdev failed\n");
		  return -1;
		}
	}
	PRINTK("sharedqueue_setup_cdev succeed\n");
	return 0;
}

static int __init 
_sharedqueue_init(void)
{
	int ret;
	dev_t devnum = 0;

	PRINTK( "ku init\n");

	if(sharedqueue_delay < 4)
	{
		PRINTK("Invalid sharedqueue_delay = %d < 4\n", sharedqueue_delay);
		return -EINVAL;
	}

	/** Asking for a dynamic sharedqueue_major*/
	ret = alloc_chrdev_region(&devnum, sharedqueue_minor, WORKQUEUE_COUNT, DEVNAME);
	if(ret < 0)
	{
		PRINTK( "sharedqueue_init failed : alloc_chrdev_region\n");
		goto ask_major_failed;
	}
	sharedqueue_major = MAJOR(devnum);
	PRINTK("devnum=%d\n", devnum);

	/*  
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
	sharedqueue_devs = (sharedqueue_dev_t *)kmalloc(WORKQUEUE_COUNT * sizeof(sharedqueue_dev_t), GFP_KERNEL);
	if(!sharedqueue_devs)
	{
		PRINTK("kmalloc failed\n");
		ret = -ENOMEM;
		goto allocate_devices_failed;
	}
	memset(sharedqueue_devs, 0, WORKQUEUE_COUNT * sizeof(sharedqueue_dev_t));

	/* Initialize all device. */
	if(sharedqueue_setup_cdevs(sharedqueue_devs, WORKQUEUE_COUNT) == -1)
	  goto setup_devs_failed; 

	sharedqueue_fops_registered = true;
	
	/** Create all device file*/
	if(sharedqueue_create_devfiles() == -1)
	  goto create_devfiles_failed;


	PRINTK("sharedqueue_init success : sharedqueue_major=%d, sharedqueue_minor=%d\n", sharedqueue_major, sharedqueue_minor);
	return 0;

create_devfiles_failed:
	sharedqueue_stop_cdevs(sharedqueue_devs, WORKQUEUE_COUNT);
setup_devs_failed:
	kfree(sharedqueue_devs);
allocate_devices_failed:
	unregister_chrdev_region(devnum, WORKQUEUE_COUNT);
ask_major_failed:
	return ret;
}

static void __exit 
sharedqueue_exit(void)
{
    sharedqueue_destroy_devfiles();
	if(sharedqueue_fops_registered)
	{
		sharedqueue_stop_cdevs(sharedqueue_devs, WORKQUEUE_COUNT);
		kfree(sharedqueue_devs);
		unregister_chrdev_region(MKDEV(sharedqueue_major, sharedqueue_minor), WORKQUEUE_COUNT);
		sharedqueue_fops_registered = false;
	}
	PRINTK( "finish ku exit.\n");
}

static int 
sharedqueue_create_devfiles(void)
{
    int i, j;
    struct device *dev;

    /* 在/sys中导出设备类信息 */
    sharedqueue_cls = class_create(THIS_MODULE, DEVNAME);
    if(sharedqueue_cls == NULL)
    {
        PRINTK( "sharedqueue_create_devfiles failed : class_create\n");
        return -1;
    }

    /* 在sharedqueue_cls指向的类中创建一组(个)设备文件 */
	PRINTK("sharedqueue_minor=%d, sharedqueue_minor + WORKQUEUE_COUNT=%d\n", 
				sharedqueue_minor, sharedqueue_minor + WORKQUEUE_COUNT);
    for(i = sharedqueue_minor;  i < (sharedqueue_minor + WORKQUEUE_COUNT); i++)
	{
        dev = device_create(sharedqueue_cls, 
                    NULL, 
                    MKDEV(sharedqueue_major,i),
                    NULL,
                    "%s%d", 
                    DEVNAME,
                    i);
        if(unlikely(dev == NULL))
        {
            PRINTK( "sharedqueue_create_devfiles failed : device_create\n");
            for(j = sharedqueue_minor; j < i; ++j)
            {
                device_destroy(sharedqueue_cls, MKDEV(sharedqueue_major,j));
            }
            class_destroy(sharedqueue_cls);
            return -1;
        }
        PRINTK( "Dev %s%d has been created\n", DEVNAME, i);
    }  
    return 0;
}

static void 
sharedqueue_destroy_devfiles(void)
{
    int i;
    if(sharedqueue_cls == NULL) 
    {
        PRINTK( "sharedqueue_destroy_devfiles : class is null\n");
        return;
    }
    /* 在sharedqueue_cls指向的类中删除一组(个)设备文件 */
    for(i = sharedqueue_minor; i<(sharedqueue_minor+WORKQUEUE_COUNT); i++){
        device_destroy(sharedqueue_cls, MKDEV(sharedqueue_major,i));
    }

    /* 在/sys中删除设备类信息 */
    class_destroy(sharedqueue_cls);             //一定要先卸载device再卸载class
    PRINTK( "sharedqueue_destroy_devfiles success");
}

module_init(_sharedqueue_init);
module_exit(sharedqueue_exit);

MODULE_AUTHOR("ZYJ");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("A simple Dev Module.");
MODULE_ALIAS("KU Module");
