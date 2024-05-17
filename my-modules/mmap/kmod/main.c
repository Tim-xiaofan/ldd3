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
#include <linux/errno.h>
#include <linux/kernel.h>
#include "mmap_test.h"

#define DEVNAME "mmap_test"

/** VARIBALES*/
static mmap_dev_t *mmap_devs = NULL;

static int mmap_minor			  = 0;
static int mmap_major			  = 0;
static const int mmap_count	  = 1;
static int mmap_fops_registered	  = 0;
static struct class *mmap_cls   = NULL;

/** FUNCTIONS*/
static int mmap_create_devfiles(void);
static void mmap_destroy_devfiles(void);

static int 
mmap_open(struct inode *node, struct file *fp)
{
	mmap_dev_t *dev = NULL; /** device infomation*/
	
	dev = container_of(node->i_cdev, mmap_dev_t, cdev);
    if(!dev) {
        return -ENODEV;
    }
	
    if(atomic_inc_return(&dev->refcnt) == 1)
	{
		PRINTK("First one to open\n");
	}

	fp->private_data = dev;
    PRINTK("ep open success, dev=%p\n", dev);
    return 0;
}

static int 
mmap_release(struct inode *node, struct file *fp)
{
	mmap_dev_t *dev = fp->private_data; /** device infomation*/

	atomic_dec_if_positive(&dev->refcnt);
    if(atomic_read(&dev->refcnt) == 0)
	{
		PRINTK("Last one to close\n");
	}
    PRINTK( "mmap_release\n");
    return 0;
}

static ssize_t 
mmap_read(struct file *fp, char __user *buf, size_t size, loff_t * offset)
{
    int uncopied, ret, mlen, len;
	mmap_dev_t *dev = fp->private_data;

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
	mlen = strlen(dev->mmap_buf);
    PRINTK("mmap_read: mlen=%d, %s", mlen, dev->mmap_buf);
    len = min(mlen, (int)size);
    uncopied = copy_to_user(buf, dev->mmap_buf, len);
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

static ssize_t 
mmap_write(struct file *fp, const char __user *buf, size_t size, loff_t * offset)
{
    int uncopied, ret; 
    size_t len; 
	char pathname[NAME_MAX];

	mmap_dev_t *dev = fp->private_data;

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
    len = min(size, MMAP_BUF_SIZE);
    uncopied = copy_from_user(dev->mmap_buf, buf, len);
    if(unlikely(uncopied != 0))//拷贝出错
    {
        PRINTK( "write : %d byte(s) not copy_from_user\n", uncopied);
        ret =  -EFAULT;
		goto out;
    }
    PRINTK( "write to file %s: len=%ld, %s\n", 
				dentry_path_raw(fp->f_path.dentry, pathname, sizeof(pathname)), 
				len, dev->mmap_buf);
	ret = len;
out:
	mutex_unlock(&dev->lock);
    return len;
}

static long 
mmap_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	PRINTK("ioctl is not support\n");
	return -ENOTSUPP;
}

static int 
mmap_mmap(struct file *filep, struct vm_area_struct * vma)
{
    char *mb = filep->private_data; /*获得设备结构体指针*/

    PRINTK("mmmap_cb called\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
    vma->vm_flags |= VM_IO;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
    vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);
#else
    vma->vm_flags |= VM_RESERVED; 
#endif
#endif

    if(remap_pfn_range(vma,vma->vm_start,// target user address to start at
                virt_to_phys(mb)>>PAGE_SHIFT, // physical address of kernel memory
                vma->vm_end - vma->vm_start, // size of map area
                vma->vm_page_prot)) // page protection flags for this mapping
        return  -EAGAIN;

    PRINTK("mmmap_cb exit\n");
    return 0;
}

static struct file_operations mmap_fops = 
{
	.owner = THIS_MODULE,
	.open = mmap_open,
	.release =mmap_release,
	.read = mmap_read,
	.write = mmap_write,
	.unlocked_ioctl = mmap_ioctl,
    .mmap = mmap_mmap,
};

/*
 * Set up the char_dev structure for this device.
 */
static int 
mmap_setup_cdev(mmap_dev_t *dev, int index)
{
    int err, devno = MKDEV(mmap_major, mmap_minor + index);

    atomic_set(&dev->refcnt, 0);
    cdev_init(&dev->cdev, &mmap_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add (&dev->cdev, devno, 1);
	mutex_init(&dev->lock);
    dev->mmap_buf = kmalloc(MMAP_BUF_SIZE, GFP_KERNEL);
    if(!dev->mmap_buf) {
        PRINTK("Error: can not allocate mmap_buf");
        return -ENOMEM;
    }
    memset(dev->mmap_buf, 0, MMAP_BUF_SIZE);
    /* Fail gracefully if need be */
    if (err) {
        PRINTK("Error %d: adding %s%d", err, DEVNAME, index);
    }
	return err;
}

static void
mmap_stop_cdev(mmap_dev_t *dev)
{
	mutex_destroy(&dev->lock);
	cdev_del(&dev->cdev);
}

static void
mmap_stop_cdevs(mmap_dev_t *dev, int count)
{
	int i;
	for(i = 0; i< count; ++i)
	  mmap_stop_cdev(&dev[i]);
}

/**
  Sut up devices
 */
static int 
mmap_setup_cdevs(mmap_dev_t * devs, int count)
{
	int i, j;
	for(i = 0; i < count; ++i)
	{
		if(mmap_setup_cdev(&devs[i], i))
		{
		  for(j = 0; j < i; ++j)
			mmap_stop_cdev(&devs[j]);
		  PRINTK("mmap_setup_cdev failed\n");
		  return -1;
		}
	}
	PRINTK("mmap_setup_cdev succeed\n");
	return 0;
}

static int __init 
_mmap_init(void)
{
	int ret;
	dev_t devnum = 0;

	PRINTK( "ku init\n");

	/** Asking for a dynamic mmap_major*/
	ret = alloc_chrdev_region(&devnum, mmap_minor, mmap_count, DEVNAME);
	if(ret < 0)
	{
		PRINTK( "mmap_init failed : alloc_chrdev_region\n");
		goto ask_major_failed;
	}
	mmap_major = MAJOR(devnum);
	PRINTK("devnum=%d\n", devnum);

	/*  
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
	mmap_devs = (mmap_dev_t *)kmalloc(mmap_count * sizeof(mmap_dev_t), GFP_KERNEL);
	if(!mmap_devs)
	{
		PRINTK("kmalloc failed\n");
		ret = -ENOMEM;
		goto allocate_devices_failed;
	}
	memset(mmap_devs, 0, mmap_count * sizeof(mmap_dev_t));

	/* Initialize all device. */
	if(mmap_setup_cdevs(mmap_devs, mmap_count) == -1)
	  goto setup_devs_failed; 

	mmap_fops_registered = 1;
	
	/** Create all device file*/
	if(mmap_create_devfiles() == -1)
	  goto create_devfiles_failed;


	PRINTK("mmap_init success : mmap_major=%d, mmap_minor=%d\n", mmap_major, mmap_minor);
	return 0;

create_devfiles_failed:
	mmap_stop_cdevs(mmap_devs, mmap_count);
setup_devs_failed:
	kfree(mmap_devs);
allocate_devices_failed:
	unregister_chrdev_region(devnum, mmap_count);
ask_major_failed:
	return ret;
}

static void __exit 
mmap_exit(void)
{
    mmap_destroy_devfiles();
	if(mmap_fops_registered)
	{
		mmap_stop_cdevs(mmap_devs, mmap_count);
		kfree(mmap_devs);
		unregister_chrdev_region(MKDEV(mmap_major, mmap_minor), mmap_count);
		mmap_fops_registered = 0;
	}
	PRINTK( "finish ku exit.\n");
}

static int 
mmap_create_devfiles(void)
{
    int i, j;
    struct device *dev;

    /* 在/sys中导出设备类信息 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
    mmap_cls = class_create(THIS_MODULE, DEVNAME);
#else
    mmap_cls = class_create(DEVNAME);
#endif
    if(mmap_cls == NULL)
    {
        PRINTK( "mmap_create_devfiles failed : class_create\n");
        return -1;
    }

    /* 在mmap_cls指向的类中创建一组(个)设备文件 */
	PRINTK("mmap_minor=%d, mmap_minor + mmap_count=%d\n", mmap_minor, mmap_minor + mmap_count);
    for(i = mmap_minor;  i < (mmap_minor + mmap_count); i++)
	{
        dev = device_create(mmap_cls, 
                    NULL, 
                    MKDEV(mmap_major,i),
                    NULL,
                    "%s%d", 
                    DEVNAME,
                    i);
        if(unlikely(dev == NULL))
        {
            PRINTK( "mmap_create_devfiles failed : device_create\n");
            for(j = mmap_minor; j < i; ++j)
            {
                device_destroy(mmap_cls, MKDEV(mmap_major,j));
            }
            class_destroy(mmap_cls);
            return -1;
        }
        PRINTK( "Dev %s%d has been created\n", DEVNAME, i);
    }  
    return 0;
}

static void 
mmap_destroy_devfiles(void)
{
    int i;
    if(mmap_cls == NULL) 
    {
        PRINTK( "mmap_destroy_devfiles : class is null\n");
        return;
    }
    /* 在mmap_cls指向的类中删除一组(个)设备文件 */
    for(i = mmap_minor; i<(mmap_minor+mmap_count); i++){
        device_destroy(mmap_cls, MKDEV(mmap_major,i));
    }

    /* 在/sys中删除设备类信息 */
    class_destroy(mmap_cls);             //一定要先卸载device再卸载class
    PRINTK( "mmap_destroy_devfiles success");
}

module_init(_mmap_init);
module_exit(mmap_exit);

MODULE_AUTHOR("ZYJ");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("A simple Dev Module.");
MODULE_ALIAS("KU Module");
