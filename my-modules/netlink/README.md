# netlink
- Communication between kernel and user space (AF_NETLINK)
- Netlink is a datagram-oriented service

# 一、User Space 和 Kernel Space共用API
## 1.1 结构体
**netlink socket地址**：用于接收、发送、bind获取地址
```c
struct sockaddr_nl {
    __kernel_sa_family_t nl_family; /* AF_NETLINK	*/
    unsigned short nl_pad;          /* zero		*/
    __u32 nl_pid;                   /* port ID	*/
    __u32 nl_groups;                /* multicast groups mask */
};
```
**netlink 消息格式**：附加在Data之前
```text
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                        Message Length                         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |      Message Type             |     Message Flags             |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                      Sequence number                          |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                          Port ID                              |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                             Data                              |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```
```c
/**
 * struct nlmsghdr - fixed format metadata header of Netlink messages
 * @nlmsg_len:   Length of message including header
 * @nlmsg_type:  Message content type
 * @nlmsg_flags: Additional flags
 * @nlmsg_seq:   Sequence number
 * @nlmsg_pid:   Sending process port ID
 */
struct nlmsghdr {
	__u32		nlmsg_len;
	__u16		nlmsg_type;
	__u16		nlmsg_flags;
	__u32		nlmsg_seq;
	__u32		nlmsg_pid;
};
```

## 1.2 宏（macro）
```c
#define NLMSG_ALIGNTO	4U
#define NLMSG_ALIGN(len) ( ((len)+NLMSG_ALIGNTO-1) & ~(NLMSG_ALIGNTO-1) )
#define NLMSG_HDRLEN	 ((int) NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len) + NLMSG_HDRLEN)
// 使头部+Data按NLMSG_ALIGNTO对齐
#define NLMSG_SPACE(len) NLMSG_ALIGN(NLMSG_LENGTH(len))
// 以头部作为起始地址获取Data
#define NLMSG_DATA(nlh)  ((void *)(((char *)nlh) + NLMSG_HDRLEN))
#define NLMSG_NEXT(nlh,len)	 ((len) -= NLMSG_ALIGN((nlh)->nlmsg_len), \
				  (struct nlmsghdr *)(((char *)(nlh)) + \
				  NLMSG_ALIGN((nlh)->nlmsg_len)))
#define NLMSG_OK(nlh,len) ((len) >= (int)sizeof(struct nlmsghdr) && \
			   (nlh)->nlmsg_len >= sizeof(struct nlmsghdr) && \
			   (nlh)->nlmsg_len <= (len))
#define NLMSG_PAYLOAD(nlh,len) ((nlh)->nlmsg_len - NLMSG_SPACE((len)))
```

# 二、Kernel Space API Lists
## 2.1 配置结构体
```c
/* optional Netlink kernel configuration parameters */
struct netlink_kernel_cfg {
	unsigned int	groups;
	unsigned int	flags;
	void		(*input)(struct sk_buff *skb); // 用于处理来自User Space的消息的回调函数，值NULL不处理
	struct mutex	*cb_mutex;
	int		(*bind)(struct net *net, int group);
	void		(*unbind)(struct net *net, int group);
};
```
## 2.2 管理sock
```c
/**
 * create sock
 * @unit: netlink_family 
 */
static inline struct sock *
netlink_kernel_create(struct net *net, int unit, struct netlink_kernel_cfg *cfg);

/** 
 * release sock
*/
void
netlink_kernel_release(struct sock *sk);
```

## 2.3 发送数据
```c
/** 
 * 发送单播消息
 * @return 0 for success, else failed
*/
int netlink_unicast(struct sock *ssk, struct sk_buff *skb,
		    u32 portid, int nonblock);
/** 
 * 发送单播消息
 * @return 0 for success, else failed
*/
int netlink_broadcast(struct sock *ssk, struct sk_buff *skb, __u32 portid,
		      __u32 group, gfp_t allocation);
```

# 三、User Space API Lists
## 3.1 结构体
```c
struct iovec
{                   /* Scatter/gather array items */
    void *iov_base; /* Starting address(nlmsghdr) */
    size_t iov_len; /* Number of bytes to transfer */
};

struct msghdr
{
    void *msg_name;        /* Optional address */
    socklen_t msg_namelen; /* Size of address */
    struct iovec *msg_iov; /* Scatter/gather array */
    size_t msg_iovlen;     /* # elements in msg_iov */
    void *msg_control;     /* Ancillary data, see below */
    size_t msg_controllen; /* Ancillary data buffer len */
    int msg_flags;         /* Flags on received message */
};

```
## 3.2 创建和bind socket
```c
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
/**
 * Both SOCK_RAW and SOCK_DGRAM are valid values for socket_type
 * However, the netlink protocol  does  not distinguish between
 * datagram and raw sockets.
 * 
 * netlink_family可以选用NETLINK_USERSOCK
 * */
netlink_socket = socket(AF_NETLINK, socket_type, netlink_family);
```
**netlink_family**
```c
#define NETLINK_ROUTE		0	/* Routing/device hook				*/
#define NETLINK_UNUSED		1	/* Unused number				*/
#define NETLINK_USERSOCK	2	/* Reserved for user mode socket protocols 	*/
#define NETLINK_FIREWALL	3	/* Unused number, formerly ip_queue		*/
#define NETLINK_SOCK_DIAG	4	/* socket monitoring				*/
#define NETLINK_NFLOG		5	/* netfilter/iptables ULOG */
#define NETLINK_XFRM		6	/* ipsec */
#define NETLINK_SELINUX		7	/* SELinux event notifications */
#define NETLINK_ISCSI		8	/* Open-iSCSI */
#define NETLINK_AUDIT		9	/* auditing */
#define NETLINK_FIB_LOOKUP	10	
#define NETLINK_CONNECTOR	11
#define NETLINK_NETFILTER	12	/* netfilter subsystem */
#define NETLINK_IP6_FW		13
#define NETLINK_DNRTMSG		14	/* DECnet routing messages (obsolete) */
#define NETLINK_KOBJECT_UEVENT	15	/* Kernel messages to userspace */
#define NETLINK_GENERIC		16
/* leave room for NETLINK_DM (DM Events) */
#define NETLINK_SCSITRANSPORT	18	/* SCSI Transports */
#define NETLINK_ECRYPTFS	19
#define NETLINK_RDMA		20
#define NETLINK_CRYPTO		21	/* Crypto layer */
#define NETLINK_SMC		22	/* SMC monitoring */

#define NETLINK_INET_DIAG	NETLINK_SOCK_DIAG

#define MAX_LINKS 32	
```
## 3.3 收发数据
```c
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
```