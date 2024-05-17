#ifndef _MMAP_TEST_H_
#define _MMAP_TEST_H_

#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/atomic.h>

//#define PRINTK(fmt, args...) 

#ifdef DEBUG
	#define PRINTK(fmt, args...) printk("[%s:%s:%d ] "fmt, __func__, __FILE__, __LINE__, ## args)
#else
	#define PRINTK(fmt, args...) ;
#endif

#define MMAP_BUF_SIZE PAGE_SIZE * 4

typedef struct mmap_dev
{
	struct mutex lock; /* mutual exclusion semaphore */
	struct cdev cdev;  /* Char device structure */
    atomic_t refcnt;
    char *mmap_buf;
} mmap_dev_t;


#endif
