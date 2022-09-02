/* simple kernel module: hello
 * Licensed under GPLv2 or later
 * */

#ifndef _KU_IOCTL_H
#define _KU_IOCTL_H
#include <linux/ioctl.h>
#include <linux/kernel.h>

#define IOC_MAGIC 's'
#define IOCSPID     _IOW(IOC_MAGIC, 0, int)
#define IOCGPID     _IOR(IOC_MAGIC, 0, int)
#define IOCSFD      _IOW(IOC_MAGIC, 1, int)
#define IOCGFD      _IOR(IOC_MAGIC, 1, int)
#define IOCSIP      _IOW(IOC_MAGIC, 2, int)
#define IOCGIP      _IOR(IOC_MAGIC, 2, int)
#define IOCSPORT    _IOW(IOC_MAGIC, 3, int)
#define IOCGPORT    _IOR(IOC_MAGIC, 3, int)
#define IOCSONOFF   _IOW(IOC_MAGIC, 4, int)
#define IOCGONOFF   _IOR(IOC_MAGIC, 4, int)
#define IOCSCONFIGS _IOW(IOC_MAGIC, 5, int)
#define IOCGCONFIGS _IOR(IOC_MAGIC, 5, int)
#define IOCADDFILTER _IOW(IOC_MAGIC, 6, int)
#define IOCLSTFILTER _IOR(IOC_MAGIC, 6, int)
#define IOCDELFILTER _IOW(IOC_MAGIC, 7, int)
#define IOCCLRFILTER _IOW(IOC_MAGIC, 8, int)

#define KU_WRITE_BUF_LIMIT  1600 

#define KU_ON   1
#define KU_OFF  0

static inline const char *ku_ip2str(uint32_t ip)
{
    static char buf[32];
    snprintf(buf, 32, "%d.%d.%d.%d", 
                (ip & 0xff000000) >> 24, 
                (ip & 0x00ff0000) >> 16, 
                (ip & 0x0000ff00) >> 8,
                (ip & 0x000000ff));
    return buf;
}

#endif
