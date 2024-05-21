# 一、Netfilter hooks
![00-nf-hooks](./images/00-nf-hooks.png)
<center>图1.1 nf-hooks</center>

如图所示，有每种proto至多有5个hook点：Prerouting, Input, Output, Forward, Postrouting\
如何确定一个hook点：
```c
// uapi/linux/netfilter.h
enum nf_inet_hooks {
    NF_INET_PRE_ROUTING,
    NF_INET_LOCAL_IN,
    NF_INET_FORWARD,
    NF_INET_LOCAL_OUT,
    NF_INET_POST_ROUTING,
    NF_INET_NUMHOOKS // 5
};

// 比图1.1中所示proto多
enum {
	NFPROTO_UNSPEC =  0,
	NFPROTO_IPV4   =  2,
	NFPROTO_ARP    =  3,
	NFPROTO_BRIDGE =  7,
	NFPROTO_IPV6   = 10,
	NFPROTO_DECNET = 12,
	NFPROTO_NUMPROTO, //13
};

/** linux/netfilter.h
每个元素都是一个链表
*/
extern struct list_head nf_hooks[NFPROTO_NUMPROTO][NF_MAX_HOOKS];
```
根据代码可知，通过查表（二维数组）方式进行确定

# 二、给内核模块的接口

>> PS：Linux-6.5.0：linux/netfilter.h, linux/uapi/netfilter.h, net/netfilter/core.c

## 2.1 hook相关数据结构
```c
struct nf_hook_state {
	u8 hook;
	u8 pf;
	struct net_device *in;
	struct net_device *out;
	struct sock *sk;
	struct net *net;
	int (*okfn)(struct net *, struct sock *, struct sk_buff *);
};
// 回调函数原型
typedef unsigned int nf_hookfn(void *priv,
			       struct sk_buff *skb,
			       const struct nf_hook_state *state);
enum nf_hook_ops_type {
	NF_HOOK_OP_UNDEFINED,
	NF_HOOK_OP_NF_TABLES,
	NF_HOOK_OP_BPF,
};
```
需要用户（内核模块开发者）填写
```c
struct nf_hook_ops {
	/* User fills in from here down. */
	nf_hookfn		*hook; // 用户回调函数
	struct net_device	*dev; // 网卡设备
	void			*priv;
	u8			pf; // NFPROTO_*
	enum nf_hook_ops_type	hook_ops_type:8;
	unsigned int		hooknum; // NF_INET_*
	/* Hooks are ordered in ascending（上升） priority. */
	int			priority;
};
```
回调函数
```c
/* Responses from hook functions. */
#define NF_DROP 0
#define NF_ACCEPT 1
#define NF_STOLEN 2
#define NF_QUEUE 3
#define NF_REPEAT 4
#define NF_STOP 5	/* Deprecated, for userspace nf_queue compatibility. */
#define NF_MAX_VERDICT NF_STOP

static unsigned int
nf_test_in_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{   
    struct ethhdr *eth_header;
    struct iphdr *ip_header;
    static int ct = 0;
    
    ++ct; 
    if(ct > print_point) {
        ct = 0;
        eth_header = (struct ethhdr *)(skb_mac_header(skb));
        ip_header = (struct iphdr *)(skb_network_header(skb));
        dump_hook_state(state);
        PRINTK("priv=%p:%s\n", priv, (char*)priv);
        dump_ethhdr(eth_header);
        dump_iphdr(ip_header);
    }
    return NF_ACCEPT;
}
```
## 2.2 API Lists
注册和注销 hook 点
```c
/* Function to register/unregister hook points. */
int nf_register_net_hook(struct net *net, const struct nf_hook_ops *ops);
void nf_unregister_net_hook(struct net *net, const struct nf_hook_ops *ops);
int nf_register_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			  unsigned int n);
void nf_unregister_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			     unsigned int n);
```

# 三、 Example
## 3.1 nf_hook_ops
```c
static struct nf_hook_ops nf_test_ops[] __read_mostly = {
    {
        .hook = nf_test_in_hook,
        .pf = NFPROTO_IPV4,
        .hooknum = NF_INET_LOCAL_IN,
        .priority = INT_MAX,
        .priv = priv,
    },
};
```
## 3.2 register
```c
	for_each_net(net)
    {   
        ret = nf_register_net_hooks(net, nf_test_ops, ARRAY_SIZE(nf_test_ops));
        if (ret < 0)
        {   
            PRINTK("ERROR: register nf hook fail\n");
            return ret;
        }
        PRINTK("nf_register_net_hooks: net=%p", net);
    }
```