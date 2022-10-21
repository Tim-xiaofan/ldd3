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
#include "jitasklet.h"

#define DEVNAME "jitasklet"
#define BUF_SIZE 1024

/** Structure */
typedef struct
{
	struct cdev cdev;
	spinlock_t lock;
	wait_queue_head_t inq;
	char buf[BUF_SIZE];
	bool busy;
	struct tasklet_struct tasklet;
	unsigned long prevjiffies;
	int loops;
}jitasklet_dev_t;


/** VARIBALES*/
static jitasklet_dev_t *jitasklet_devs = NULL;
static int jitasklet_minor			  = 0;
static int jitasklet_major			  = 0;
static const int JITASKLET_COUNT	  = 1;
static bool jitasklet_fops_registered	  = false;
static struct class *jitasklet_cls   = NULL;
static const int JITASKLET_LOOPS = 20;

/** FUNCTIONS*/
static int jitasklet_create_devfiles(void);
static void jitasklet_destroy_devfiles(void);

static int jitasklet_open(struct inode *node, struct file *fp)
{
	int ret = 0;
	jitasklet_dev_t *dev = container_of(node->i_cdev, jitasklet_dev_t, cdev);
	if(dev != jitasklet_devs)
	  PRINTK("jitasklet_cb is called, dev=%p, jitasklet_devs=%p, eq=%s\n", 
				  dev, jitasklet_devs, (dev==jitasklet_devs)?"yes":"no");
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

static int jitasklet_release(struct inode *node, struct file *fp)
{
	jitasklet_dev_t *dev = fp->private_data; 
	spin_lock(&dev->lock);
	dev->busy = false;
	spin_unlock(&dev->lock);
    PRINTK( "jitasklet_release\n");
    return 0;
}

static ssize_t jitasklet_read(struct file *fp, char __user *buf, 
			size_t size, loff_t * offset)
{
	jitasklet_dev_t *dev = fp->private_data;
	unsigned long j;
	size_t min = size;
	if(dev != jitasklet_devs)
	  PRINTK("jitasklet_cb is called, dev=%p, jitasklet_devs=%p, eq=%s\n", 
				  dev, jitasklet_devs, (dev==jitasklet_devs)?"yes":"no");

	spin_lock(&dev->lock);
	dev->loops = JITASKLET_LOOPS;
	j = jiffies;
	dev->prevjiffies = j;
	spin_unlock(&dev->lock);

	//PRINTK("call tasklet_schedule\n");
	tasklet_schedule(&dev->tasklet);
	//PRINTK("finished calling tasklet_schedule\n");

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
	tasklet_kill(&dev->tasklet);

	return min;
}

static struct file_operations jitasklet_fops = 
{
	.owner = THIS_MODULE,
	.open = jitasklet_open,
	.release = jitasklet_release,
	.read = jitasklet_read,
};

/** tasklet callback*/
static void 
jitasklet_cb(struct tasklet_struct * t)
{
	jitasklet_dev_t *dev =  from_tasklet(dev, t, tasklet);
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
		spin_unlock(&dev->lock);
		mdelay(4);
		tasklet_schedule(&dev->tasklet);
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
jitasklet_setup_cdev(jitasklet_dev_t *dev, int index)
{
    int err, devno = MKDEV(jitasklet_major, jitasklet_minor + index);

    cdev_init(&dev->cdev, &jitasklet_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add (&dev->cdev, devno, 1);
	spin_lock_init(&dev->lock);
	memset(dev->buf, 0, sizeof(dev->buf));
	dev->busy = false;
	tasklet_setup(&dev->tasklet, jitasklet_cb);
	init_waitqueue_head(&dev->inq);
    /* Fail gracefully if need be */
    if (err)
        PRINTK("Error %d: adding %s%d", err, DEVNAME, index);
	return err;
}

static void
jitasklet_stop_cdev(jitasklet_dev_t *dev)
{
	cdev_del(&dev->cdev);
}

static void
jitasklet_stop_cdevs(jitasklet_dev_t *dev, int count)
{
	int i;
	for(i = 0; i< count; ++i)
	  jitasklet_stop_cdev(&dev[i]);
}

/**
  Sut up devices
 */
static int 
jitasklet_setup_cdevs(jitasklet_dev_t * devs, int count)
{
	int i, j;
	for(i = 0; i < count; ++i)
	{
		if(jitasklet_setup_cdev(&devs[i], i))
		{
		  for(j = 0; j < i; ++j)
			jitasklet_stop_cdev(&devs[j]);
		  PRINTK("jitasklet_setup_cdev failed\n");
		  return -1;
		}
	}
	PRINTK("jitasklet_setup_cdev succeed\n");
	return 0;
}

static int __init 
jitasklet_init(void)
{
	int ret;
	dev_t devnum = 0;

	PRINTK( "ku init\n");

	/** Asking for a dynamic jitasklet_major*/
	ret = alloc_chrdev_region(&devnum, jitasklet_minor, JITASKLET_COUNT, DEVNAME);
	if(ret < 0)
	{
		PRINTK( "jitasklet_init failed : alloc_chrdev_region\n");
		goto ask_major_failed;
	}
	jitasklet_major = MAJOR(devnum);
	PRINTK("devnum=%d\n", devnum);

	/*  
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
	jitasklet_devs = (jitasklet_dev_t *)kmalloc(JITASKLET_COUNT * sizeof(jitasklet_dev_t), GFP_KERNEL);
	if(!jitasklet_devs)
	{
		PRINTK("kmalloc failed\n");
		ret = -ENOMEM;
		goto allocate_devices_failed;
	}
	memset(jitasklet_devs, 0, JITASKLET_COUNT * sizeof(jitasklet_dev_t));

	/* Initialize all device. */
	if(jitasklet_setup_cdevs(jitasklet_devs, JITASKLET_COUNT) == -1)
	  goto setup_devs_failed; 

	jitasklet_fops_registered = true;
	
	/** Create all device file*/
	if(jitasklet_create_devfiles() == -1)
	  goto create_devfiles_failed;


	PRINTK("jitasklet_init success : jitasklet_major=%d, jitasklet_minor=%d\n", jitasklet_major, jitasklet_minor);
	return 0;

create_devfiles_failed:
	jitasklet_stop_cdevs(jitasklet_devs, JITASKLET_COUNT);
setup_devs_failed:
	kfree(jitasklet_devs);
allocate_devices_failed:
	unregister_chrdev_region(devnum, JITASKLET_COUNT);
ask_major_failed:
	return ret;
}

static void __exit 
jitasklet_exit(void)
{
    jitasklet_destroy_devfiles();
	if(jitasklet_fops_registered)
	{
		jitasklet_stop_cdevs(jitasklet_devs, JITASKLET_COUNT);
		kfree(jitasklet_devs);
		unregister_chrdev_region(MKDEV(jitasklet_major, jitasklet_minor), JITASKLET_COUNT);
		jitasklet_fops_registered = false;
	}
	PRINTK( "finish ku exit.\n");
}

static int 
jitasklet_create_devfiles(void)
{
    int i, j;
    struct device *dev;

    /* 在/sys中导出设备类信息 */
    jitasklet_cls = class_create(THIS_MODULE, DEVNAME);
    if(jitasklet_cls == NULL)
    {
        PRINTK( "jitasklet_create_devfiles failed : class_create\n");
        return -1;
    }

    /* 在jitasklet_cls指向的类中创建一组(个)设备文件 */
	PRINTK("jitasklet_minor=%d, jitasklet_minor + JITASKLET_COUNT=%d\n", 
				jitasklet_minor, jitasklet_minor + JITASKLET_COUNT);
    for(i = jitasklet_minor;  i < (jitasklet_minor + JITASKLET_COUNT); i++)
	{
        dev = device_create(jitasklet_cls, 
                    NULL, 
                    MKDEV(jitasklet_major,i),
                    NULL,
                    "%s%d", 
                    DEVNAME,
                    i);
        if(unlikely(dev == NULL))
        {
            PRINTK( "jitasklet_create_devfiles failed : device_create\n");
            for(j = jitasklet_minor; j < i; ++j)
            {
                device_destroy(jitasklet_cls, MKDEV(jitasklet_major,j));
            }
            class_destroy(jitasklet_cls);
            return -1;
        }
        PRINTK( "Dev %s%d has been created\n", DEVNAME, i);
    }  
    return 0;
}

static void 
jitasklet_destroy_devfiles(void)
{
    int i;
    if(jitasklet_cls == NULL) 
    {
        PRINTK( "jitasklet_destroy_devfiles : class is null\n");
        return;
    }
    /* 在jitasklet_cls指向的类中删除一组(个)设备文件 */
    for(i = jitasklet_minor; i<(jitasklet_minor+JITASKLET_COUNT); i++){
        device_destroy(jitasklet_cls, MKDEV(jitasklet_major,i));
    }

    /* 在/sys中删除设备类信息 */
    class_destroy(jitasklet_cls);             //一定要先卸载device再卸载class
    PRINTK( "jitasklet_destroy_devfiles success");
}

module_init(jitasklet_init);
module_exit(jitasklet_exit);

MODULE_AUTHOR("ZYJ");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("A simple Dev Module.");
MODULE_ALIAS("KU Module");
