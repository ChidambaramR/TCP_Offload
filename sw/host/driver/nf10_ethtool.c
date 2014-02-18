#include "nf10_ethtool.h"

static int nf10_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd){
	printk("hello world");
}



static const struct ethtool_ops nf10_ethtool_ops = {
	.get_settings  		= nf10_get_settings,
        .set_settings           = NULL,
        .get_drvinfo            = NULL,
        .get_regs_len           = NULL,
	.get_regs               = NULL,
        .get_wol                = NULL,
        .set_wol                = NULL,
        .get_msglevel           = NULL,
        .set_msglevel           = NULL,
        .nway_reset             = NULL,
        .get_link               = NULL,
        .get_eeprom_len         = NULL,
        .get_eeprom             = NULL,
        .set_eeprom             = NULL,
        .get_ringparam          = NULL,
        .set_ringparam          = NULL,
        .get_pauseparam         = NULL,
        .set_pauseparam         = NULL,
        .self_test              = NULL,
        .get_strings            = NULL,
        .set_phys_id            = NULL,
        .get_ethtool_stats      = NULL,
        .get_sset_count         = NULL,
        .get_coalesce           = NULL,
        .set_coalesce           = NULL,
        .get_ts_info            = NULL,
};


void nf10_set_ethtool_ops(struct net_device *netdev){
	SET_ETHTOOL_OPS(netdev, &nf10_ethtool_ops);
}
