#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include "netfilter.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SHI");
MODULE_DESCRIPTION("Netfliter test");

static bool hooked = false;
static char priv[] = {'p', 'r', 'i', 'v', '\0'};
static int print_point = 50;
module_param(print_point, int, S_IRUGO);

static unsigned int
nf_test_in_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state);

static struct nf_hook_ops nf_test_ops[] __read_mostly = {
	{
		.hook = nf_test_in_hook,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = INT_MAX,
		.priv = priv,
	},
};

static void
dump_ethhdr(const struct ethhdr *ehdr)
{
	PRINTK("[MAC_DES:%x,%x,%x,%x,%x,%x "
		   "MAC_SRC: %x,%x,%x,%x,%x,%x Prot:%x]\n",
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
	PRINTK("src IP:'" NIPQUAD_FMT "', dst IP:'" NIPQUAD_FMT "', proto:%d\n",
		   NIPQUAD(iphdr->saddr), NIPQUAD(iphdr->daddr), iphdr->protocol);
}

static void 
dump_hook_state(const struct nf_hook_state * state)
{
	PRINTK("in:%s, out:%s\n", state->in->name, state->out->name);
}

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
	return NF_ACCEPT; // 内核继续处理
}

static int hook_init(void)
{
	int ret;
	struct net *net;
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
	PRINTK("register nf test hook\n");
	hooked = true;

	return 0;
}

static int __init init_nf_test(void)
{
    PRINTK("print_point: %d", print_point);
	return hook_init();
}

static void hook_exit(void)
{
	struct net *net;
	if(hooked)
	{
		for_each_net(net)
		{
			nf_unregister_net_hooks(net, nf_test_ops, ARRAY_SIZE(nf_test_ops));
		}
	}
	PRINTK("unregister nf test hook\n");

}

static void __exit exit_nf_test(void)
{
	hook_exit();
}

module_init(init_nf_test);
module_exit(exit_nf_test);
