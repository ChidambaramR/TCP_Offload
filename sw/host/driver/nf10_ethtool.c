#include "nf10_ethtool.h"

static int nf10_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd){
	printk("hello world");
	return 0;
}



static const struct ethtool_ops nf10_ethtool_ops = {
	.get_settings  		= nf10_get_settings,
};


void nf10_set_ethtool_ops(struct net_device *netdev){
	SET_ETHTOOL_OPS(netdev, &nf10_ethtool_ops);
}
