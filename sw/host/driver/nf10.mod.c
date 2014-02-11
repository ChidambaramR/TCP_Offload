#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x9a31bb74, "module_layout" },
	{ 0x1fedf0f4, "__request_region" },
	{ 0xb7c59293, "cdev_del" },
	{ 0x5cd9dbb5, "kmalloc_caches" },
	{ 0xd2b09ce5, "__kmalloc" },
	{ 0xaa4fb4f8, "cdev_init" },
	{ 0xf9a482f9, "msleep" },
	{ 0x69a358a6, "iomem_resource" },
	{ 0x7170092f, "skb_pad" },
	{ 0xb3be75f6, "dev_set_drvdata" },
	{ 0x43a53735, "__alloc_workqueue_key" },
	{ 0xe7ae0ca3, "dma_set_mask" },
	{ 0x86db9a2d, "pci_disable_device" },
	{ 0x27e3ef6b, "device_destroy" },
	{ 0x1bfee7ad, "queue_work" },
	{ 0x6efb8d26, "x86_dma_fallback_dev" },
	{ 0x7485e15e, "unregister_chrdev_region" },
	{ 0x7a00547, "__netdev_alloc_skb" },
	{ 0x4f8b5ddb, "_copy_to_user" },
	{ 0xd9acbbd9, "pci_set_master" },
	{ 0x8f64aa4, "_raw_spin_unlock_irqrestore" },
	{ 0x27e1a049, "printk" },
	{ 0x2ccfb983, "class_unregister" },
	{ 0xc3fb7d5d, "free_netdev" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0xe85f172e, "register_netdev" },
	{ 0x52268801, "netif_receive_skb" },
	{ 0x16305289, "warn_slowpath_null" },
	{ 0xd4f29297, "device_create" },
	{ 0x2072ee9b, "request_threaded_irq" },
	{ 0xafd06f3b, "dev_kfree_skb_any" },
	{ 0xebd1da60, "cdev_add" },
	{ 0x42c8de35, "ioremap_nocache" },
	{ 0x3bd1b1f6, "msecs_to_jiffies" },
	{ 0x1ed94e8b, "kfree_skb" },
	{ 0x8f7af24a, "alloc_netdev_mqs" },
	{ 0x87ce1f99, "eth_type_trans" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x7c61340c, "__release_region" },
	{ 0xf55bb238, "pci_unregister_driver" },
	{ 0x9a1b3285, "ether_setup" },
	{ 0xd61adcbd, "kmem_cache_alloc_trace" },
	{ 0x9327f5ce, "_raw_spin_lock_irqsave" },
	{ 0xe52947e7, "__phys_addr" },
	{ 0x37a0cba, "kfree" },
	{ 0x17e6fdf7, "pci_disable_msi" },
	{ 0xedc03953, "iounmap" },
	{ 0xaba33759, "__pci_register_driver" },
	{ 0xf3248a6e, "class_destroy" },
	{ 0xd9d35cd9, "unregister_netdev" },
	{ 0x8a6c7bfe, "pci_enable_msi_block" },
	{ 0xd20f7b8c, "__netif_schedule" },
	{ 0x11f2fca, "skb_put" },
	{ 0xace5f3c3, "pci_enable_device" },
	{ 0x4f6b400b, "_copy_from_user" },
	{ 0x25827ecd, "__class_create" },
	{ 0x10519fe3, "dev_get_drvdata" },
	{ 0xc6849b4a, "dma_ops" },
	{ 0x29537c9e, "alloc_chrdev_region" },
	{ 0xf20dabd8, "free_irq" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("pci:v000010EEd00004244sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "C06313B7CA0704F23901513");
