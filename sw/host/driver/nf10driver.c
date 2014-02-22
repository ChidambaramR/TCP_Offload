/*******************************************************************************
 *
 *  NetFPGA-10G http://www.netfpga.org
 *
 *  File:
 *        nf10driver.c
 *
 *  Project:
 *        nic
 *
 *  Author:
 *        Mario Flajslik
 *
 *  Description:
 *        Top level file for the nic driver. Contains functions that are called
 *        when module is loaded/unloaded to initialize/remove PCIe device and
 *        nic datastructures.
 *
 *  Copyright notice:
 *        Copyright (C) 2010, 2011 The Board of Trustees of The Leland Stanford
 *                                 Junior University
 *
 *  Licence:
 *        This file is part of the NetFPGA 10G development base package.
 *
 *        This file is free code: you can redistribute it and/or modify it under
 *        the terms of the GNU Lesser General Public License version 2.1 as
 *        published by the Free Software Foundation.
 *
 *        This package is distributed in the hope that it will be useful, but
 *        WITHOUT ANY WARRANTY; without even the implied warranty of
 *        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *        Lesser General Public License for more details.
 *
 *        You should have received a copy of the GNU Lesser General Public
 *        License along with the NetFPGA source package.  If not, see
 *        http://www.gnu.org/licenses/.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include "nf10driver.h"
#include "nf10fops.h"
#include "nf10iface.h"

// These attributes have been removed since Kernel 3.8.x. Keep them here for backward
// compatibility.
// See https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=54b956b903607
#ifndef __devinit
  #define __devinit
#endif
#ifndef __devexit
  #define __devexit
#endif
#ifndef __devexit_p
  #define __devexit_p
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mario Flajslik");
MODULE_DESCRIPTION("nf10 nic driver");

static struct pci_device_id pci_id[] = {
    {PCI_DEVICE(PCI_VENDOR_ID_NF10, PCI_DEVICE_ID_NF10)},
    {0}
};
MODULE_DEVICE_TABLE(pci, pci_id);

static int __devinit nf10_probe(struct pci_dev *pdev, const struct pci_device_id *id){
	int err;
    int i;
    int ret = -ENODEV;
    struct nf10_card *card;

	// enable device
    	/* Enable the device. pci_enable_device() will do the following (ref. PCI/pci.txt kernel doc):
     	*     - wake up the device if it was in suspended state
     	*     - allocate I/O and memory regions of the device (if BIOS did not)
     	*     - allocate an IRQ (if BIOS did not) */
	if((err = pci_enable_device(pdev))) {
		printk(KERN_ERR "nf10: Unable to enable the PCI device!\n");
        ret = -ENODEV;
		goto err_out_none;
	}

    // set DMA addressing masks (full 64bit)
    if(dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)) < 0){
        printk(KERN_ERR "nf10: dma_set_mask fail!\n");
        ret = -EFAULT;
        goto err_out_disable_device;

        if(dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64)) < 0){
            printk(KERN_ERR "nf10: dma_set_mask fail!\n");
            ret = -EFAULT;
            goto err_out_disable_device;
        }
    }

    	 // enable BusMaster (enables generation of pcie requests)
	 /* Enable DMA functionality for the device.
     	  * pci_set_master() does this by (ref. PCI/pci.txt kernel doc) setting the bus master bit
     	  * in the PCI_COMMAND register. pci_clear_master() will disable DMA by clearing the bit.
    	  * This function also sets the latency timer value if necessary. */
	pci_set_master(pdev);

    // enable MSI
    if(pci_enable_msi(pdev) != 0){
        printk(KERN_ERR "nf10: failed to enable MSI interrupts\n");
        ret = -EFAULT;
		goto err_out_disable_device;
    }
	
    // be nice and tell kernel that we'll use this resource
    /*
    * request_mem_region(unsigned long start, unsigned long len, char *name)
    * I/O memory region. Allocates a memory region of len bytes, starting at start. A non NULL pointer is returned on success
    * This sort of I/O memory is not directly accessible. It should be mapped. The mapping is done by ioremap function. 
    */
	printk(KERN_INFO "nf10: Reserving memory region for NF10\n");
	if (!request_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0), DEVICE_NAME)) {
		printk(KERN_ERR "nf10: Reserving memory region failed\n");
        ret = -ENOMEM;
        goto err_out_msi;
	}
	if (!request_mem_region(pci_resource_start(pdev, 2), pci_resource_len(pdev, 2), DEVICE_NAME)) {
		printk(KERN_ERR "nf10: Reserving memory region failed\n");
        ret = -ENOMEM;
		goto err_out_release_mem_region1;
	}

    // create private structure
	card = (struct nf10_card*)kmalloc(sizeof(struct nf10_card), GFP_KERNEL);
	if (card == NULL) {
		printk(KERN_ERR "nf10: Private card memory alloc failed\n");
		ret = -ENOMEM;
		goto err_out_release_mem_region2;
	}
	memset(card, 0, sizeof(struct nf10_card));
    card->pdev = pdev;
	
    // map the cfg memory
    /*
    * This function assigns virtual addresses to I/O memory regions
    * The addresses returned by ioremap should not be accessed directly. Kernel helper functions should  be used. 
    * ioremap_nocache is hardware related.
    */
	printk(KERN_INFO "nf10: mapping cfg memory\n");
    card->cfg_addr = ioremap_nocache(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	if (!card->cfg_addr)
	{
		printk(KERN_ERR "nf10: cannot mem region len:%lx start:%lx\n",
			(long unsigned)pci_resource_len(pdev, 0),
			(long unsigned)pci_resource_start(pdev, 0));
		goto err_out_iounmap;
	}

	printk(KERN_INFO "nf10: mapping mem memory\n");

	/*
 	* Get memory for TX ring 
 	*/ 
	card->tx_ring = (struct nf10_tx_ring*)kmalloc(sizeof(struct nf10_tx_ring), GFP_KERNEL);
	if (card->tx_ring == NULL) {
		printk(KERN_ERR "nf10: Private card memory alloc failed\n");
		ret = -ENOMEM;
		goto err_out_release_mem_region2;
	}
	memset(card->tx_ring, 0, sizeof(struct nf10_tx_ring));


	/*
 	* Get memory for RX ring 
 	*/ 
	card->rx_ring = (struct nf10_rx_ring*)kmalloc(sizeof(struct nf10_rx_ring), GFP_KERNEL);
	if (card->rx_ring == NULL) {
		printk(KERN_ERR "nf10: Private card memory alloc failed\n");
		ret = -ENOMEM;
		goto err_out_release_mem_region2;
	}
	memset(card->tx_ring, 0, sizeof(struct nf10_tx_ring));

	card->tx_ring->tx_dsc = ioremap_nocache(pci_resource_start(pdev, 2) + 0 * 0x00100000ULL, 0x00100000ULL);
        card->rx_ring->rx_dsc = ioremap_nocache(pci_resource_start(pdev, 2) + 1 * 0x00100000ULL, 0x00100000ULL);
	
	printk("TX_DSC addr: %p   RX_DSC addr: %p\n",card->tx_ring->tx_dsc, card->rx_ring->rx_dsc);

	if (!card->tx_ring->tx_dsc || !card->rx_ring->rx_dsc)
	{
		printk(KERN_ERR "nf10: cannot mem region len:%lx start:%lx\n",
			(long unsigned)pci_resource_len(pdev, 2),
			(long unsigned)pci_resource_start(pdev, 2));
		goto err_out_iounmap;
	}

    // reset
    *(((uint64_t*)card->cfg_addr)+30) = 1;
    msleep(1);

    // set buffer masks
    card->tx_ring->tx_dsc_mask = 0x000007ffULL;
    card->rx_ring->rx_dsc_mask = 0x000007ffULL;
    card->tx_ring->tx_pkt_mask = 0x00007fffULL;
    card->rx_ring->rx_pkt_mask = 0x00007fffULL;
    card->tx_ring->tx_dne_mask = 0x000007ffULL;
    card->rx_ring->rx_dne_mask = 0x000007ffULL;
   
    /*
    This might look a little awkward that we are checking for the same values which we set above.
    I believe the code has been written with flexibility in mind. 
    */ 
    if(card->tx_ring->tx_dsc_mask > card->tx_ring->tx_dne_mask){
        *(((uint64_t*)card->cfg_addr)+1) = card->tx_ring->tx_dne_mask;
        card->tx_ring->tx_dsc_mask = card->tx_ring->tx_dne_mask;
    }
    else if(card->tx_ring->tx_dne_mask > card->tx_ring->tx_dsc_mask){
        *(((uint64_t*)card->cfg_addr)+7) = card->tx_ring->tx_dsc_mask;
        card->tx_ring->tx_dne_mask = card->tx_ring->tx_dsc_mask;
    }

    if(card->rx_ring->rx_dsc_mask > card->rx_ring->rx_dne_mask){
        *(((uint64_t*)card->cfg_addr)+9) = card->rx_ring->rx_dne_mask;
        card->rx_ring->rx_dsc_mask = card->rx_ring->rx_dne_mask;
    }
    else if(card->rx_ring->rx_dne_mask > card->rx_ring->rx_dsc_mask){
        *(((uint64_t*)card->cfg_addr)+15) = card->rx_ring->rx_dsc_mask;
        card->rx_ring->rx_dne_mask = card->rx_ring->rx_dsc_mask;
    }

    // allocate buffers to play with
    /*
    * DMA requires some memory space that can be accessed by the hardware, which is not cached and which is 
    * physically contiguous. 
    */
    card->tx_ring->host_tx_dne_ptr = pci_alloc_consistent(pdev, card->tx_ring->tx_dne_mask+1, &(card->tx_ring->host_tx_dne_dma));
    card->rx_ring->host_rx_dne_ptr = pci_alloc_consistent(pdev, card->rx_ring->rx_dne_mask+1, &(card->rx_ring->host_rx_dne_dma));

    printk("\nVirtual address of TX buffer = %p, physical address of TX buffer = %016llx\n",(void*)card->tx_ring->host_tx_dne_ptr, card->tx_ring->host_tx_dne_dma);
    printk("\nVirtual address of RX buffer = %p, physical address of RX buffer = %016llx\n",(void*)card->rx_ring->host_rx_dne_ptr, card->rx_ring->host_rx_dne_dma);
   
    if( (card->rx_ring->host_rx_dne_ptr == NULL) ||
        (card->tx_ring->host_tx_dne_ptr == NULL) ){
        
        printk(KERN_ERR "nf10: cannot allocate dma buffer\n");
        goto err_out_free_private2;
    }

    // set host buffer addresses
    *(((uint64_t*)card->cfg_addr)+16) = card->tx_ring->host_tx_dne_dma;
    *(((uint64_t*)card->cfg_addr)+17) = card->tx_ring->tx_dne_mask;
    *(((uint64_t*)card->cfg_addr)+18) = card->rx_ring->host_rx_dne_dma;
    *(((uint64_t*)card->cfg_addr)+19) = card->rx_ring->rx_dne_mask;

    // init mem buffers
    card->tx_ring->mem_tx_dsc.wr_ptr = 0;
    card->tx_ring->mem_tx_dsc.rd_ptr = 0;
    atomic64_set(&card->tx_ring->mem_tx_dsc.cnt, 0);
    card->tx_ring->mem_tx_dsc.mask = card->tx_ring->tx_dsc_mask;
    card->tx_ring->mem_tx_dsc.cl_size = (card->tx_ring->tx_dsc_mask+1)/64;
    card->tx_ring->mem_tx_pkt.wr_ptr = 0;
    card->tx_ring->mem_tx_pkt.rd_ptr = 0;
    atomic64_set(&card->tx_ring->mem_tx_pkt.cnt, 0);
    card->tx_ring->mem_tx_pkt.mask = card->tx_ring->tx_pkt_mask;
    card->tx_ring->mem_tx_pkt.cl_size = (card->tx_ring->tx_pkt_mask+1)/64;
    card->rx_ring->mem_rx_dsc.wr_ptr = 0;
    card->rx_ring->mem_rx_dsc.rd_ptr = 0;
    atomic64_set(&card->rx_ring->mem_rx_dsc.cnt, 0);
    card->rx_ring->mem_rx_dsc.mask = card->rx_ring->rx_dsc_mask;
    card->rx_ring->mem_rx_dsc.cl_size = (card->rx_ring->rx_dsc_mask+1)/64;
    card->rx_ring->mem_rx_pkt.wr_ptr = 0;
    card->rx_ring->mem_rx_pkt.rd_ptr = 0;
    atomic64_set(&card->rx_ring->mem_rx_pkt.cnt, 0);
    card->rx_ring->mem_rx_pkt.mask = card->rx_ring->rx_pkt_mask;
    card->rx_ring->mem_rx_pkt.cl_size = (card->rx_ring->rx_pkt_mask+1)/64;
    card->tx_ring->host_tx_dne.wr_ptr = 0;
    card->tx_ring->host_tx_dne.rd_ptr = 0;
    atomic64_set(&card->tx_ring->host_tx_dne.cnt, 0);
    card->tx_ring->host_tx_dne.mask = card->tx_ring->tx_dne_mask;
    card->tx_ring->host_tx_dne.cl_size = (card->tx_ring->tx_dne_mask+1)/64;
    card->rx_ring->host_rx_dne.wr_ptr = 0;
    card->rx_ring->host_rx_dne.rd_ptr = 0;
    atomic64_set(&card->rx_ring->host_rx_dne.cnt, 0);
    card->rx_ring->host_rx_dne.mask = card->rx_ring->rx_dne_mask;
    card->rx_ring->host_rx_dne.cl_size = (card->rx_ring->rx_dne_mask+1)/64;
    
    for(i = 0; i < card->tx_ring->host_tx_dne.cl_size; i++){
        *(((uint32_t*)card->tx_ring->host_tx_dne_ptr) + i * 16) = 0xffffffff;
        printk("%s: Add: %p Val: %x\n",__FUNCTION__, (void*)(((uint32_t*)card->tx_ring->host_tx_dne_ptr) + i * 16), *(((uint32_t*)card->tx_ring->host_tx_dne_ptr) + i * 16));
    }

    for(i = 0; i < card->rx_ring->host_rx_dne.cl_size; i++){
        *(((uint64_t*)card->rx_ring->host_rx_dne_ptr) + i * 8 + 7) = 0xffffffffffffffffULL;
	printk("%s: Add: %p  Val: %016llx\n",__FUNCTION__, (((uint64_t*)card->rx_ring->host_rx_dne_ptr) + i * 8 + 7), *(((uint64_t*)card->rx_ring->host_rx_dne_ptr) + i * 8 + 7));
    }

    // initialize work queue
    if(!(card->wq = create_workqueue("int_hndlr"))){
        printk(KERN_ERR "nf10: workqueue failed\n");
    }
    INIT_WORK((struct work_struct*)&card->work, work_handler);
    card->work.card = card;

    // allocate book keeping structures
    card->tx_bk_skb = (struct sk_buff**)kmalloc(card->tx_ring->mem_tx_dsc.cl_size*sizeof(struct sk_buff*), GFP_KERNEL);
    card->tx_bk_dma_addr = (uint64_t*)kmalloc(card->tx_ring->mem_tx_dsc.cl_size*sizeof(uint64_t), GFP_KERNEL);
    card->tx_bk_size = (uint64_t*)kmalloc(card->tx_ring->mem_tx_dsc.cl_size*sizeof(uint64_t), GFP_KERNEL);
    card->tx_bk_port = (uint64_t*)kmalloc(card->tx_ring->mem_tx_dsc.cl_size*sizeof(uint64_t), GFP_KERNEL);

    card->rx_bk_skb = (struct sk_buff**)kmalloc(card->rx_ring->mem_rx_dsc.cl_size*sizeof(struct sk_buff*), GFP_KERNEL);
    card->rx_bk_dma_addr = (uint64_t*)kmalloc(card->rx_ring->mem_rx_dsc.cl_size*sizeof(uint64_t), GFP_KERNEL);
    card->rx_bk_size = (uint64_t*)kmalloc(card->rx_ring->mem_rx_dsc.cl_size*sizeof(uint64_t), GFP_KERNEL);
    
    if(card->tx_bk_skb == NULL || card->tx_bk_dma_addr == NULL || card->tx_bk_size == NULL || card->tx_bk_port == NULL ||
       card->rx_bk_skb == NULL || card->rx_bk_dma_addr == NULL || card->rx_bk_size == NULL){
        printk(KERN_ERR "nf10: kmalloc failed");
        goto err_out_free_private2;
    }

    // store private data to pdev
	pci_set_drvdata(pdev, card);

    // success
    ret = nf10iface_probe(pdev, card);
    if(ret < 0){
        printk(KERN_ERR "nf10: failed to initialize interfaces\n");
        goto err_out_free_private2;
    }

    ret = nf10fops_probe(pdev, card);
    if(ret < 0){
        printk(KERN_ERR "nf10: failed to initialize dev file\n");
        goto err_out_free_private2;
    }
    else{
        printk(KERN_INFO "nf10: device ready\n");
        return ret;
    }

 // error out
 err_out_free_private2:
    if(card->tx_bk_dma_addr) kfree(card->tx_bk_dma_addr);
    if(card->tx_bk_skb) kfree(card->tx_bk_skb);
    if(card->tx_bk_size) kfree(card->tx_bk_size);
    if(card->tx_bk_port) kfree(card->tx_bk_port);
    if(card->rx_bk_dma_addr) kfree(card->rx_bk_dma_addr);
    if(card->rx_bk_skb) kfree(card->rx_bk_skb);
    if(card->rx_bk_size) kfree(card->rx_bk_size);
    pci_free_consistent(pdev, card->tx_ring->tx_dne_mask+1, card->tx_ring->host_tx_dne_ptr, card->tx_ring->host_tx_dne_dma);
    pci_free_consistent(pdev, card->rx_ring->rx_dne_mask+1, card->rx_ring->host_rx_dne_ptr, card->rx_ring->host_rx_dne_dma);
 err_out_iounmap:
    if(card->tx_ring->tx_dsc) iounmap(card->tx_ring->tx_dsc);
    if(card->rx_ring->rx_dsc) iounmap(card->rx_ring->rx_dsc);
    if(card->cfg_addr)   iounmap(card->cfg_addr);
	pci_set_drvdata(pdev, NULL);
	kfree(card);
 err_out_release_mem_region2:
	release_mem_region(pci_resource_start(pdev, 2), pci_resource_len(pdev, 2));
 err_out_release_mem_region1:
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
 err_out_msi:
    pci_disable_msi(pdev);
 err_out_disable_device:
	pci_disable_device(pdev);
 err_out_none:
	return ret;
}

static void __devexit nf10_remove(struct pci_dev *pdev){
    struct nf10_card *card;

    // free private data
    printk(KERN_INFO "nf10: releasing private memory\n");
    card = (struct nf10_card*)pci_get_drvdata(pdev);
    if(card){

        nf10fops_remove(pdev, card);
        nf10iface_remove(pdev, card);

        if(card->cfg_addr) iounmap(card->cfg_addr);

        if(card->tx_ring->tx_dsc) iounmap(card->tx_ring->tx_dsc);
        if(card->rx_ring->rx_dsc) iounmap(card->rx_ring->rx_dsc);

        pci_free_consistent(pdev, card->tx_ring->tx_dne_mask+1, card->tx_ring->host_tx_dne_ptr, card->tx_ring->host_tx_dne_dma);
        pci_free_consistent(pdev, card->rx_ring->rx_dne_mask+1, card->rx_ring->host_rx_dne_ptr, card->rx_ring->host_rx_dne_dma);

        if(card->tx_bk_dma_addr) kfree(card->tx_bk_dma_addr);
        if(card->tx_bk_skb) kfree(card->tx_bk_skb);
        if(card->tx_bk_size) kfree(card->tx_bk_size);
        if(card->tx_bk_port) kfree(card->tx_bk_port);
        if(card->rx_bk_dma_addr) kfree(card->rx_bk_dma_addr);
        if(card->rx_bk_skb) kfree(card->rx_bk_skb);
        if(card->rx_bk_size) kfree(card->rx_bk_size);
        
        kfree(card);
    }

    pci_set_drvdata(pdev, NULL);

    // release memory
    printk(KERN_INFO "nf10: releasing mem region\n");
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	release_mem_region(pci_resource_start(pdev, 2), pci_resource_len(pdev, 2));

    // disabling device
    printk(KERN_INFO "nf10: disabling device\n");
    pci_disable_msi(pdev);
	pci_disable_device(pdev);
}

pci_ers_result_t nf10_pcie_error(struct pci_dev *dev, enum pci_channel_state state){
    printk(KERN_ALERT "nf10: PCIe error: %d\n", state);
    return PCI_ERS_RESULT_RECOVERED;
}

static struct pci_error_handlers pcie_err_handlers = {
    .error_detected = nf10_pcie_error
};

static struct pci_driver pci_driver = {
	.name = "nf10",
	.id_table = pci_id,
	.probe = nf10_probe,
	.remove = __devexit_p(nf10_remove),
    .err_handler = &pcie_err_handlers
};

static int __init nf10_init(void)
{
	printk(KERN_INFO "nf10: module loaded\n");
	return pci_register_driver(&pci_driver);
}

static void __exit nf10_exit(void)
{
    pci_unregister_driver(&pci_driver);
	printk(KERN_INFO "nf10: module unloaded\n");
}

module_init(nf10_init);
module_exit(nf10_exit);

