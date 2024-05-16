/* simple kernel module: hello
 * Licensed under GPLv2 or later
 * */
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>
#include <linux/limits.h>
#include "major_minor.h"

#define DEVNAME "major_minor"

/** VARIBALES*/
static mm_dev_t *mm_devs = NULL;

static int mm_minor			  = 0;
static int mm_major			  = 0;
static const int mm_count	  = 2;
static int mm_fops_registered	  = 0;
static struct class *mm_cls   = NULL;
static atomic_t mm_ref = {0};

/** FUNCTIONS*/
static int mm_create_devfiles(void);
static void mm_destroy_devfiles(void);

int 
mm_open(struct inode *node, struct file *fp)
{
	mm_dev_t *dev; /** device infomation*/
	if(atomic_inc_return(&mm_ref) == 1)
	{
		PRINTK("First one to open\n");
	}
	dev = container_of(node->i_cdev, mm_dev_t, cdev);
	fp->private_data = dev;
    PRINTK("ep open success, dev=%p\n", dev);
    return 0;
}

int 
_mm_release(struct inode *node, struct file *fp)
{
	atomic_dec_if_positive(&mm_ref);
    if(atomic_read(&mm_ref) == 0)
	{
		PRINTK("Last one to close\n");
	}
    PRINTK( "mm_release\n");
    return 0;
}

ssize_t 
mm_read(struct file *fp, char __user *buf, size_t size, loff_t * offset)
{
    int uncopied, ret, mlen, len;
	mm_dev_t *dev = fp->private_data;

	if(unlikely(dev == NULL))
	{
		PRINTK("private_data is NULL\n");
		return -EFAULT;
	}
	
	if(unlikely(mutex_lock_interruptible(&dev->lock)))
	{
		PRINTK("mutex_lock_interruptible failed\n");
		return -ERESTARTSYS;
	}
    
    /*将内核空间的数据copy到用户空间*/
	mlen = strlen(dev->read_buf);
    len = (mlen <= size) ? mlen : size;
    PRINTK("Message:%s, mlen = %d, len=%d\n", dev->read_buf, mlen, len);
    uncopied = copy_to_user(buf, dev->read_buf, len);
    if(unlikely(uncopied != 0))
    {
        PRINTK("read : %d byte(s) not copy_to_user\n", uncopied);
        ret = -EFAULT;
		goto out;
    }
	ret = len;

out:
	mutex_unlock(&dev->lock);
    return len;
}

ssize_t 
mm_write(struct file *fp, const char __user *buf, size_t size, loff_t * offset)
{
    int uncopied, ret; 
    size_t len; 
	char pathname[NAME_MAX];

	mm_dev_t *dev = fp->private_data;

	if(unlikely(dev == NULL))
	{
		PRINTK("private_data is NULL\n");
		return -EFAULT;
	}
	
	if(unlikely(mutex_lock_interruptible(&dev->lock)))
	{
		PRINTK("mutex_lock_interruptible failed\n");
		return -ERESTARTSYS;
	}

    /*将用户空间的数据copy到内核空间*/
    len = (size <= BUF_SIZE) ? size : BUF_SIZE;
    uncopied = copy_from_user(dev->write_buf, buf, len);
    if(unlikely(uncopied != 0))//拷贝出错
    {
        PRINTK( "write : %d byte(s) not copy_from_user\n", uncopied);
        ret =  -EFAULT;
		goto out;
    }
    PRINTK( "write to file %s: len=%ld, %s\n", 
				dentry_path_raw(fp->f_path.dentry, pathname, sizeof(pathname)), 
				len, dev->write_buf);
	ret = len;
out:
	mutex_unlock(&dev->lock);
    return len;
}

long mm_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	PRINTK("ioctl is not support\n");
	return -EINVAL;
}

static struct file_operations mm_fops = 
{
	.owner = THIS_MODULE,
	.open = mm_open,
	.release = _mm_release,
	.read = mm_read,
	.write = mm_write,
	.unlocked_ioctl = mm_ioctl,
};

/*
 * Set up the char_dev structure for this device.
 */
static int 
mm_setup_cdev(mm_dev_t *dev, int index)
{
    int err, devno = MKDEV(mm_major, mm_minor + index);

    cdev_init(&dev->cdev, &mm_fops);
    dev->cdev.owner = THIS_MODULE;
     err = cdev_add (&dev->cdev, devno, 1);
	mutex_init(&dev->lock);
	memset(dev->read_buf, 0, sizeof(dev->read_buf));
	snprintf(dev->read_buf, sizeof(dev->read_buf), 
				"Message from %s%d", DEVNAME, index);
	memset(dev->write_buf, 0, sizeof(dev->write_buf));
    /* Fail gracefully if need be */
    if (err) {
        PRINTK("Error %d: adding %s%d", err, DEVNAME, index);
    }
	return err;
}

static void
mm_stop_cdev(mm_dev_t *dev)
{
	mutex_destroy(&dev->lock);
	cdev_del(&dev->cdev);
}

static void
mm_stop_cdevs(mm_dev_t *dev, int count)
{
	int i;
	for(i = 0; i< count; ++i)
	  mm_stop_cdev(&dev[i]);
}

/**
  Sut up devices
 */
static int 
mm_setup_cdevs(mm_dev_t * devs, int count)
{
	int i, j;
	for(i = 0; i < count; ++i)
	{
		if(mm_setup_cdev(&devs[i], i))
		{
		  for(j = 0; j < i; ++j)
			mm_stop_cdev(&devs[j]);
		  PRINTK("mm_setup_cdev failed\n");
		  return -1;
		}
	}
	PRINTK("mm_setup_cdev succeed\n");
	return 0;
}

static int __init 
mm_init(void)
{
	int ret;
	dev_t devnum = 0;

	PRINTK( "ku init\n");

	/** Asking for a dynamic mm_major*/
	ret = alloc_chrdev_region(&devnum, mm_minor, mm_count, DEVNAME);
	if(ret < 0)
	{
		PRINTK( "mm_init failed : alloc_chrdev_region\n");
		goto ask_major_failed;
	}
	mm_major = MAJOR(devnum);
	PRINTK("devnum=%d\n", devnum);

	/*  
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
	mm_devs = (mm_dev_t *)kmalloc(mm_count * sizeof(mm_dev_t), GFP_KERNEL);
	if(!mm_devs)
	{
		PRINTK("kmalloc failed\n");
		ret = -ENOMEM;
		goto allocate_devices_failed;
	}
	memset(mm_devs, 0, mm_count * sizeof(mm_dev_t));

	/* Initialize all device. */
	if(mm_setup_cdevs(mm_devs, mm_count) == -1)
	  goto setup_devs_failed; 

	mm_fops_registered = 1;
	
	/** Create all device file*/
	if(mm_create_devfiles() == -1)
	  goto create_devfiles_failed;


	PRINTK("mm_init success : mm_major=%d, mm_minor=%d\n", mm_major, mm_minor);
	return 0;

create_devfiles_failed:
	mm_stop_cdevs(mm_devs, mm_count);
setup_devs_failed:
	kfree(mm_devs);
allocate_devices_failed:
	unregister_chrdev_region(devnum, mm_count);
ask_major_failed:
	return ret;
}

static void __exit 
mm_exit(void)
{
    mm_destroy_devfiles();
	if(mm_fops_registered)
	{
		mm_stop_cdevs(mm_devs, mm_count);
		kfree(mm_devs);
		unregister_chrdev_region(MKDEV(mm_major, mm_minor), mm_count);
		mm_fops_registered = 0;
	}
	PRINTK( "finish ku exit.\n");
}

static int 
mm_create_devfiles(void)
{
    int i, j;
    struct device *dev;

    /* 在/sys中导出设备类信息 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
    mm_cls = class_create(THIS_MODULE, DEVNAME);
#else
    mm_cls = class_create(DEVNAME);
#endif
    if(mm_cls == NULL)
    {
        PRINTK( "mm_create_devfiles failed : class_create\n");
        return -1;
    }

    /* 在mm_cls指向的类中创建一组(个)设备文件 */
	PRINTK("mm_minor=%d, mm_minor + mm_count=%d\n", mm_minor, mm_minor + mm_count);
    for(i = mm_minor;  i < (mm_minor + mm_count); i++)
	{
        dev = device_create(mm_cls, 
                    NULL, 
                    MKDEV(mm_major,i),
                    NULL,
                    "%s%d", 
                    DEVNAME,
                    i);
        if(unlikely(dev == NULL))
        {
            PRINTK( "mm_create_devfiles failed : device_create\n");
            for(j = mm_minor; j < i; ++j)
            {
                device_destroy(mm_cls, MKDEV(mm_major,j));
            }
            class_destroy(mm_cls);
            return -1;
        }
        PRINTK( "Dev %s%d has been created\n", DEVNAME, i);
    }  
    return 0;
}

static void 
mm_destroy_devfiles(void)
{
    int i;
    if(mm_cls == NULL) 
    {
        PRINTK( "mm_destroy_devfiles : class is null\n");
        return;
    }
    /* 在mm_cls指向的类中删除一组(个)设备文件 */
    for(i = mm_minor; i<(mm_minor+mm_count); i++){
        device_destroy(mm_cls, MKDEV(mm_major,i));
    }

    /* 在/sys中删除设备类信息 */
    class_destroy(mm_cls);             //一定要先卸载device再卸载class
    PRINTK( "mm_destroy_devfiles success");
}

module_init(mm_init);
module_exit(mm_exit);

MODULE_AUTHOR("ZYJ");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("A simple Dev Module.");
MODULE_ALIAS("KU Module");
