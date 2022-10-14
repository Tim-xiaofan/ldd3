#include <linux/netlink.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#define NETLINK_TEST  17
MODULE_LICENSE("GPL");

struct sock *nl_sk = NULL;

static void
netlink_send(int pid, uint8_t *message, int len)
{
   struct sk_buff *skb_1;
   struct nlmsghdr *nlh;
 
   if(!message || !nl_sk) {
	   return;
   }
 
   skb_1 = alloc_skb(NLMSG_SPACE(len), GFP_KERNEL);
   if( !skb_1 ) {
	   printk(KERN_ERR "alloc_skb error!\n");
   }
 
   nlh = nlmsg_put(skb_1, 
			   0, //portid: netlink PORTID of requesting application
			   0, //seq: sequence number of message
			   0, //type: message type
			   len, // payload:
			   0); // flags:
   NETLINK_CB(skb_1).portid = 0; 
   NETLINK_CB(skb_1).dst_group = 0;
   memcpy(NLMSG_DATA(nlh), message, len);
   netlink_unicast(nl_sk, 
			   skb_1, 
			   pid, // 目的进程ID
			   MSG_DONTWAIT);
}

static void input (struct sk_buff *skb)
{
	struct nlmsghdr *nlh = NULL;
	char *payload = NULL;
	pid_t pid;

	/* process netlink message pointed by skb->data */
	nlh = (struct nlmsghdr *)skb->data;
	payload = NLMSG_DATA(nlh);
	pid = nlh->nlmsg_pid;
	/* process netlink message with header pointed by
	 * nlh	and payload pointed by payload
	 */
	printk("NL got a user message from pid=%d,len=%d:%s\n", pid, skb->len, payload);
	//NETLINK_CB(skb).groups = 0; /** not in mcast group*/
	//NETLINK_CB(skb).pid = 0;      /* from kernel */
	//NETLINK_CB(skb).portid = pid;
	//NETLINK_CB(skb).dst_group = 0;  /* unicast */
	//if(netlink_unicast(nl_sk, skb, pid, MSG_DONTWAIT) )
	//{
	//	printk("netlink_unicast failed\n");
	//}
	netlink_send(pid, NLMSG_DATA(nlh), nlh->nlmsg_len - NLMSG_SPACE(0));
}

static int __init nl_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.groups	= 1,
		.input = input,
		.flags	= NL_CFG_F_NONROOT_RECV
	};
	nl_sk = netlink_kernel_create(&init_net, NETLINK_TEST, &cfg);
	if(!nl_sk)
	{
		printk("netlink_kernel_create failed");
		return -1;
	}
	return 0;
}


void __exit nl_exit(void)
{
	netlink_kernel_release(nl_sk);
}

module_init(nl_init);
module_exit(nl_exit);
