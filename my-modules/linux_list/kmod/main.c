#include <linux/netlink.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/wait.h>

MODULE_LICENSE("GPL");

#define DEVNAME "linux_list"
#define MAX_MESSAGES 128

struct linux_list_dev {
    struct cdev cdev;
    struct mutex mutex;
    struct list_head messages;
    size_t message_count;
    struct wait_queue_head rwq;
    struct wait_queue_head wwq;
    int refcnt;
};

struct message {
    struct list_head messages;
    char *data;
    size_t size;
};

static int linux_list_devnum = 0;
static struct class *linux_list_cls = NULL;
static struct linux_list_dev linux_list_dev;
static bool linux_list_inited = false;

static int linux_list_open(struct inode*, struct file*);
static int linux_list_release(struct inode*, struct file*);
static ssize_t linux_list_read(struct file*, char __user *, size_t, loff_t*);
static ssize_t linux_list_write(struct file*, const char __user *, size_t, loff_t*);

static struct file_operations f_ops = {
    .owner = THIS_MODULE,
    .open = linux_list_open,
    .release = linux_list_release,
    .read = linux_list_read,
    .write = linux_list_write,
};


static int linux_list_open(struct inode*, struct file* file)
{
    struct linux_list_dev * dev = &linux_list_dev; 
    file->private_data = dev;
    
    if(mutex_lock_interruptible(&dev->mutex)) {
        return -ERESTARTSYS;
    }
    dev->refcnt++;
    printk(KERN_INFO "open: refcnt=%d, comm=%s", 
                dev->refcnt, current->comm);
    mutex_unlock(&dev->mutex);
    
    return 0;
}

static int linux_list_release(struct inode*, struct file* file)
{
    struct linux_list_dev *dev = file->private_data;
    
    if(mutex_lock_interruptible(&dev->mutex)) {
        return -ERESTARTSYS;
    }
    dev->refcnt--;
    printk(KERN_INFO "%s: release: refcnt=%d, comm=%s",
                __func__, dev->refcnt, current->comm);
    mutex_unlock(&dev->mutex);
    
    file->private_data = NULL;
    return 0;
}

static ssize_t linux_list_read(struct file* file, char __user * buf, size_t size, loff_t*)
{
    ssize_t ret = -EINVAL;
    struct linux_list_dev *dev = file->private_data;
    struct message *msg = NULL;
    bool wakeup = false;

    //printk(KERN_DEBUG "%s: read: size=%lu, comm=%s",
    //            __func__, size, current->comm);
    
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
                printk(KERN_DEBUG "%s: wait_event_interruptible failed", __func__);
                return -ERESTARTSYS;
            }
        }
        if(mutex_lock_interruptible(&dev->mutex)) {
            return -ERESTARTSYS;
        }
    }

    // peek first message in list
    msg = list_first_entry(&dev->messages, struct message, messages);
    
    // user buffer size check
    if(msg->size > size) {
        goto unlock;
    }
   
    if(copy_to_user(buf, msg->data, msg->size)) {
        printk(KERN_ERR "copy_to_user failed");
        ret = -EFAULT;
        goto unlock;
    }
    
    wakeup = true; 
    ret = msg->size;

    // remove and free first msg
    list_del(&msg->messages);
    kfree(msg->data);
    kfree(msg);
    
    // decrease message_count
    dev->message_count--;

unlock:
    mutex_unlock(&dev->mutex);
    if(wakeup) { // wakeup writer
        wake_up_interruptible(&dev->wwq);
    }
    //printk(KERN_DEBUG "%s: read: ret=%ld, comm=%s",
    //            __func__, ret, current->comm);
    return ret;
}

static ssize_t linux_list_write(struct file* file, const char __user * buf, size_t size, loff_t*)
{
    ssize_t ret = -EINVAL;
    struct linux_list_dev *dev = file->private_data;
    struct message *msg = NULL;
    bool wakeup = false;
    
    //printk(KERN_DEBUG "%s: write: size=%lu, comm=%s",
    //            __func__, size, current->comm);
    
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

    // allocate message
    msg = kmalloc(sizeof(struct message), GFP_KERNEL);
    if(!msg) {
        ret = -ENOMEM;
        goto unlock;
    }
    INIT_LIST_HEAD(&msg->messages);
    msg->size = size;
    msg->data = kmalloc(size, GFP_KERNEL);
    if(!msg->data) {
        ret = -ENOMEM;
        kfree(msg);
        goto unlock;
    }
    
    if(copy_from_user(msg->data, buf, size)) {
        printk(KERN_ERR "copy_from_user failed");
        ret = -EFAULT;
        goto unlock;
    }
    
    wakeup = true; 
    ret = msg->size;

    // add msg to list tail
    list_add_tail(&msg->messages, &dev->messages);
    
    // increase message_count
    dev->message_count++;

unlock:
    mutex_unlock(&dev->mutex);
    if(wakeup) { // wakeup reader
        wake_up_interruptible(&dev->rwq);
    }
    //printk(KERN_DEBUG "%s: write: ret=%ld, comm=%s",
    //            __func__, ret, current->comm);
    return ret;
}

/**
  @return   < 0 for failed
 */
static int linux_list_dev_setup(void)
{
    int ret = -1;
    cdev_init(&linux_list_dev.cdev, &f_ops);
    ret = cdev_add(&linux_list_dev.cdev, linux_list_devnum, 1);
    if(ret < 0) {
        printk(KERN_EMERG "cdev failed");
        return ret;
    }
    mutex_init(&linux_list_dev.mutex);
    INIT_LIST_HEAD(&linux_list_dev.messages);
    linux_list_dev.message_count = 0;
    init_waitqueue_head(&linux_list_dev.rwq);
    init_waitqueue_head(&linux_list_dev.wwq);
    linux_list_dev.refcnt = 0;
    return 0;
}

static void linux_list_dev_stop(void)
{
    struct message *msg = NULL;
    struct message *next = NULL;

    // Free all messages on device
    list_for_each_entry_safe(msg, next, &linux_list_dev.messages, messages) {
        list_del(&msg->messages);
        kfree(msg->data);
        kfree(msg);
    }

    cdev_del(&linux_list_dev.cdev);
    mutex_destroy(&linux_list_dev.mutex);
    INIT_LIST_HEAD(&linux_list_dev.messages);
}

static int __init linux_list_init(void)
{
    int ret;
    struct device *device = NULL;

    //struct message *msg = NULL;
    //list_for_each_entry(msg, &linux_list_dev.messages, messages) {
    //    //do_something(msg);
    //}	
    
    // 申请设备号
    ret = alloc_chrdev_region(&linux_list_devnum, 
                0,// baseminor 
                1,// count
                DEVNAME);
    if(ret) {
        printk(KERN_EMERG "alloc_chrdev_region failed");
        goto failed_region;
    }

    // 初始化驱动设备
    ret = linux_list_dev_setup();
    if(ret < 0) {
        goto failed_setup;
    }

    // 申请class
    linux_list_cls = class_create(DEVNAME);
    if(!linux_list_cls) {
        printk(KERN_EMERG "class_create failed");
        goto failed_class;
    }

    // 创建设备文件
    device = device_create(linux_list_cls,
                NULL, //parent
                linux_list_devnum,
                NULL, // drvdata
                "%s", //fmt
                DEVNAME
                );
    if(!device) {
        printk(KERN_EMERG "failed");
        goto failed_device;
    }

    linux_list_inited = true;

    return 0;

failed_device:
    class_destroy(linux_list_cls);
    linux_list_cls = NULL;
failed_class:
    linux_list_dev_stop();
failed_setup:
    unregister_chrdev_region(linux_list_devnum, 1);
failed_region:
    return ret;
}

void __exit linux_list_exit(void)
{
    if(linux_list_inited) {
        device_destroy(linux_list_cls, linux_list_devnum);
        class_destroy(linux_list_cls);
        linux_list_dev_stop();
        unregister_chrdev_region(linux_list_devnum, 1);
    }
}

module_init(linux_list_init);
module_exit(linux_list_exit);
