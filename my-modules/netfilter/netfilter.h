#ifndef _NETFILTER_H_
#define _NETFILTER_H_

#include <linux/ioctl.h>
#include <linux/kernel.h>

//#define PRINTK(fmt, args...) 

#ifdef DEBUG
	#define PRINTK(fmt, args...) printk("[%s:%s:%d ] "fmt, __func__, __FILE__, __LINE__, ## args)
#else
	#define PRINTK(fmt, args...) ;
#endif

typedef struct netfilter_dev
{
	struct net net;
}
netfilter_dev_t;


#endif
