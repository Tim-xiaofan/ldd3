#ifndef _MAJOR_MINOR_H_
#define _MAJOR_MINOR_H_

#include <linux/ioctl.h>
#include <linux/kernel.h>

//#define PRINTK(fmt, args...) 

#ifdef DEBUG
	#define PRINTK(fmt, args...) printk("[%s:%s:%d ] "fmt, __func__, __FILE__, __LINE__, ## args)
#else
	#define PRINTK(fmt, args...) ;
#endif

#define BUF_SIZE 128

typedef struct mm_dev
{
	struct mutex lock; /* mutual exclusion semaphore */
	struct cdev cdev;  /* Char device structure */
	char read_buf[BUF_SIZE]; 
	char write_buf[BUF_SIZE];
} mm_dev_t;


#endif
