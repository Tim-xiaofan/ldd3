#ifndef _WORKQUEUE_H_
#define _WORKQUEUE_H_

#include <linux/kernel.h>

#ifdef DEBUG
	#define PRINTK(fmt, args...) printk("[%s:%s:%d ] "fmt, __func__, __FILE__, __LINE__, ## args)
#else
	#define PRINTK(fmt, args...) ;
#endif

#endif
