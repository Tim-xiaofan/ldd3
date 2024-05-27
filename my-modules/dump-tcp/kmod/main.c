/**
 * main.c
 * (1)[NFPROTO_INET][NF_INET_LOCAL_IN] hook点采集tcp流，将流保存到List
 * (2)read操作将保存到list中的流返回给用户
 */
#include <linux/netlink.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/if_ether.h>

MODULE_LICENSE("GPL");

const char *DEVNAME = "dump_tcp";
const int MAX_MESSAGES = 1024;

struct dump_tcp_dev {
    struct cdev cdev;
    spinlock_t lock;
    struct list_head messages;
    size_t message_count;
    struct wait_queue_head rwq;
    int refcnt;
};

struct message {
    struct list_head messages;
    char *data;
    size_t size;
};

static int dump_tcp_devnum = 0;
static struct class *dump_tcp_cls = NULL;
static struct dump_tcp_dev dump_tcp_dev;
static bool dump_tcp_inited = false;

static int dump_tcp_open(struct inode*, struct file*);
static int dump_tcp_release(struct inode*, struct file*);
static ssize_t dump_tcp_read(struct file*, char __user *, size_t, loff_t*);
static __poll_t dump_tcp_poll(struct file *, struct poll_table_struct *);
static unsigned int dump_tcp_hookfn(void *priv, struct sk_buff *skb,
            const struct nf_hook_state *state);

static struct file_operations f_ops = {
    .owner = THIS_MODULE,
    .open = dump_tcp_open,
    .release = dump_tcp_release,
    .read = dump_tcp_read,
    .write = NULL,
    .poll = dump_tcp_poll,
};

static struct nf_hook_ops hook_ops = {
	/* User fills in from here down. */
	.hook = dump_tcp_hookfn,
	.dev = NULL,
    .priv = &dump_tcp_dev,
	.pf = NFPROTO_INET,
	.hooknum = NF_INET_LOCAL_IN,
	/* Hooks are ordered in ascending priority. */
	.priority = INT_MAX,
};

static int dump_tcp_open(struct inode*, struct file* file)
{
    struct dump_tcp_dev * dev = &dump_tcp_dev; 
    file->private_data = dev;
    
    spin_lock_bh(&dev->lock);
    dev->refcnt++;
    printk(KERN_INFO "open: refcnt=%d, comm=%s", 
                dev->refcnt, current->comm);
    spin_unlock_bh(&dev->lock);
    
    return 0;
}

static int dump_tcp_release(struct inode*, struct file* file)
{
    struct dump_tcp_dev *dev = file->private_data;
    
    spin_lock_bh(&dev->lock);
    
    dev->refcnt--;
    printk(KERN_INFO "%s: release: refcnt=%d, comm=%s",
                __func__, dev->refcnt, current->comm);
    spin_unlock_bh(&dev->lock);
    
    file->private_data = NULL;
    return 0;
}

static ssize_t dump_tcp_read(struct file* file, char __user * buf, size_t size, loff_t*)
{
    ssize_t ret = -EINVAL;
    struct dump_tcp_dev *dev = file->private_data;
    struct message *msg = NULL;

    //printk(KERN_DEBUG "%s: read: size=%lu, comm=%s",
    //            __func__, size, current->comm);
    
    spin_lock_bh(&dev->lock);
    while(dev->message_count == 0) {
        spin_unlock_bh(&dev->lock); // release mutex
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
        spin_lock_bh(&dev->lock);
    }

    // peek first message in list
    msg = list_first_entry(&dev->messages, struct message, messages);
    
    // user buffer size check
    if(msg->size > size) {
        printk(KERN_ERR "not enough buffer: msg->size=%lu > size=%lu",
                    msg->size, size);
        goto unlock;
    }
   
    if(copy_to_user(buf, msg->data, msg->size)) {
        printk(KERN_ERR "copy_to_user failed");
        ret = -EFAULT;
        goto unlock;
    }
    
    ret = msg->size;

    // remove and free first msg
    list_del(&msg->messages);
    kfree(msg->data);
    kfree(msg);
    
    // decrease message_count
    --dev->message_count;

unlock:
    spin_unlock_bh(&dev->lock);
    //printk(KERN_DEBUG "%s: read: ret=%ld, comm=%s",
    //            __func__, ret, current->comm);
    return ret;
}

static __poll_t dump_tcp_poll(struct file *file, struct poll_table_struct *wait) 
{
    __poll_t mask = 0;
    struct dump_tcp_dev * dev = file->private_data;

    spin_lock_bh(&dev->lock);

    poll_wait(file, &dev->rwq, wait);
    if(dev->message_count) {
        mask |= (POLLIN|POLLRDNORM);
    }

    spin_unlock_bh(&dev->lock);
    return mask;
}

static void
dump_ethhdr(const struct ethhdr *ehdr)
{
	printk("[MAC_DES:%02x:%02x:%02x:%02x:%02x:%02x "
		   "MAC_SRC: %02x:%02x:%02x:%02x:%02x:%02x Prot:%04x]\n",
		   ehdr->h_dest[0], ehdr->h_dest[1], ehdr->h_dest[2], ehdr->h_dest[3],
		   ehdr->h_dest[4], ehdr->h_dest[5], ehdr->h_source[0], ehdr->h_source[1],
		   ehdr->h_source[2], ehdr->h_source[3], ehdr->h_source[4],
		   ehdr->h_source[5], ehdr->h_proto);
}

#define NIPQUAD(addr)                \
	((unsigned char *)&addr)[0],     \
		((unsigned char *)&addr)[1], \
		((unsigned char *)&addr)[2], \
		((unsigned char *)&addr)[3]
#define NIPQUAD_FMT "%u.%u.%u.%u"


static void
dump_iphdr(const struct iphdr * iphdr)
{
	printk("src IP:'" NIPQUAD_FMT "', dst IP:'" NIPQUAD_FMT "', proto:%d, ttl=%u\n",
		   NIPQUAD(iphdr->saddr), NIPQUAD(iphdr->daddr), iphdr->protocol, ntohs(iphdr->tot_len));
}


static unsigned int dump_tcp_hookfn(void *priv, struct sk_buff *skb,
            const struct nf_hook_state *state) 
{
    struct iphdr *iph = NULL;
    struct ethhdr *eth = NULL; 
    struct message *msg = NULL;
    struct dump_tcp_dev *dev = priv;
    bool wakeup = false;

    if(!in_softirq()) {
        printk(KERN_ERR "Why not in softirq???");
    }

    iph = (struct iphdr *)skb_network_header(skb);
    if(iph->protocol == IPPROTO_TCP) {
        spin_lock(&dev->lock);
        if(dev->message_count >= MAX_MESSAGES) {
            goto unlock;
        }

        if(skb_linearize(skb) == -ENOMEM) {
            printk(KERN_ERR "%s: skb_linearize failed", __func__);
            goto unlock;
        }

        iph = (struct iphdr *)skb_network_header(skb);
        eth = (struct ethhdr *)skb_mac_header(skb);
        
        // create and assign message
        msg = kmalloc(sizeof(struct message), GFP_ATOMIC);
        if(!msg) {
            printk(KERN_ERR "%s: msg kmalloc(%lu) failed", __func__, 
                        sizeof(struct message));
            goto unlock;
        }
        INIT_LIST_HEAD(&msg->messages);
        msg->size = skb->len + ETH_HLEN;
        msg->data = kmalloc(msg->size, GFP_ATOMIC);
        if(!msg->data) {
            printk(KERN_ERR "%s: msg->data kmalloc(%d) failed", __func__, 
                        skb->len);
            kfree(msg);
            goto unlock;
        }
        memcpy(msg->data, eth, msg->size);

        // append message to dev
        list_add_tail(&msg->messages, &dev->messages);
        ++dev->message_count;
        //printk(KERN_INFO "%s: in_softirq: %s: append a (%lu)message: message_count=%lu, eth=%p, data=%p", 
        //            __func__, in_softirq() ? "Yes":"No", msg->size, dev->message_count, eth, skb->data);
        dump_ethhdr(eth);
        dump_iphdr(iph);
        wakeup = true;
unlock:
        spin_unlock(&dev->lock);
        if(wakeup) {
            wake_up_interruptible(&dev->rwq); // wakeup reader
        }
    }
    return NF_ACCEPT;
}

/**
  @return   < 0 for failed
 */
static int dump_tcp_dev_setup(void)
{
    int ret = -1;
    cdev_init(&dump_tcp_dev.cdev, &f_ops);
    ret = cdev_add(&dump_tcp_dev.cdev, dump_tcp_devnum, 1);
    if(ret < 0) {
        printk(KERN_EMERG "cdev failed");
        return ret;
    }
    spin_lock_init(&dump_tcp_dev.lock);
    INIT_LIST_HEAD(&dump_tcp_dev.messages);
    dump_tcp_dev.message_count = 0;
    init_waitqueue_head(&dump_tcp_dev.rwq);
    dump_tcp_dev.refcnt = 0;
    return 0;
}

static void dump_tcp_dev_stop(void)
{
    struct message *msg = NULL;
    struct message *next = NULL;

    // Free all messages on device
    list_for_each_entry_safe(msg, next, &dump_tcp_dev.messages, messages) {
        list_del(&msg->messages);
        kfree(msg->data);
        kfree(msg);
    }

    cdev_del(&dump_tcp_dev.cdev);
    INIT_LIST_HEAD(&dump_tcp_dev.messages);
}

static int dump_tcp_hook_init(void) 
{
    struct net *net = NULL, *back = NULL;
    for_each_net(net){
        if(nf_register_net_hook(net, &hook_ops)) {
            printk(KERN_EMERG "nf_register_net_hook failed");
            goto failed;
        }
    }
    return 0;
failed:
    back = net;
    for_each_net(net) {
        if(net == back) {
            break;
        } else {
            nf_unregister_net_hook(net, &hook_ops);
        }
    }
    return -1;
}

void dump_tcp_hook_exit(void)
{
    struct net *net = NULL;
    for_each_net(net) {
        nf_unregister_net_hook(net, &hook_ops);
    }
}

static int __init dump_tcp_init(void)
{
    int ret;
    struct device *device = NULL;

    //struct message *msg = NULL;
    //list_for_each_entry(msg, &dump_tcp_dev.messages, messages) {
    //    //do_something(msg);
    //}	
    
    // 申请设备号
    ret = alloc_chrdev_region(&dump_tcp_devnum, 
                0,// baseminor 
                1,// count
                DEVNAME);
    if(ret) {
        printk(KERN_EMERG "alloc_chrdev_region failed");
        goto failed_region;
    }

    // 初始化驱动设备
    ret = dump_tcp_dev_setup();
    if(ret < 0) {
        goto failed_setup;
    }

    // 申请class
    dump_tcp_cls = class_create(DEVNAME);
    if(!dump_tcp_cls) {
        printk(KERN_EMERG "class_create failed");
        goto failed_class;
    }

    // 创建设备文件
    device = device_create(dump_tcp_cls,
                NULL, //parent
                dump_tcp_devnum,
                NULL, // drvdata
                "%s", //fmt
                DEVNAME
                );
    if(!device) {
        printk(KERN_EMERG "failed");
        goto failed_device;
    }

    // netfilter init
    if(dump_tcp_hook_init()) {
        printk(KERN_EMERG "dump_tcp_hook_init");
        goto failed_hook;
    }

    dump_tcp_inited = true;

    return 0;

failed_hook:
    device_destroy(dump_tcp_cls, dump_tcp_devnum);
failed_device:
    class_destroy(dump_tcp_cls);
    dump_tcp_cls = NULL;
failed_class:
    dump_tcp_dev_stop();
failed_setup:
    unregister_chrdev_region(dump_tcp_devnum, 1);
failed_region:
    return ret;
}

void __exit dump_tcp_exit(void)
{
    if(dump_tcp_inited) {
        dump_tcp_hook_exit();
        device_destroy(dump_tcp_cls, dump_tcp_devnum);
        class_destroy(dump_tcp_cls);
        dump_tcp_dev_stop();
        unregister_chrdev_region(dump_tcp_devnum, 1);
    }
}

module_init(dump_tcp_init);
module_exit(dump_tcp_exit);
