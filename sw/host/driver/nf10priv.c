/*******************************************************************************
 *
 *  NetFPGA-10G http://www.netfpga.org
 *
 *  File:
 *        nf10priv.c
 *
 *  Project:
 *        nic
 *
 *  Author:
 *        Mario Flajslik
 *
 *  Description:
 *        These functions control the card tx/rx operation. 
 *        nf10priv_xmit i- gets called for every transmitted packet
 *                         (on any nf interface)
 *        work_handler  -- gets called when the interrupt handler puts work
 *                         on the queue
 *        nf10priv_send_rx_dsc -- allocates and sends a receive descriptor
 *                                to the nic
 *
 *        There also exists a LOOPBACK_MODE (enabled by defining constant
 *        LOOPBACK_MODE) that allows the driver to be tested on a single
 *        machine. This mode, at receive, flips the last bit in the second
 *        to last octet of the source and destination IP addresses. (e.g.
 *        address 192.168.2.1 is converted to 192.168.3.1 and vice versa).
 *
 *        An example configuration that has been tested with loopback between 
 *        interfaces 0 and 3 (must add static ARP entries, because ARPs
 *        aren't fixed by the LOOPBACK_MODE):
 *            ifconfig nf0 192.168.2.11;
 *            ifconfig nf3 192.168.3.12;
 *            arp -s 192.168.2.12 00:4E:46:31:30:03;
 *            arp -s 192.168.3.11 00:4E:46:31:30:00;
 *
 *            "ping 192.168.2.12" -- should now work with packets going over
 *                                   the wire.
 *
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

#include "nf10priv.h"
#include <linux/spinlock.h>
#include <linux/pci.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <net/ip.h>
#include <net/tcp.h>

#define SK_BUFF_ALLOC_SIZE  1533
#define NF10_GET_DESC(R, i, dsc, type)   (&(((struct type*)(R->dsc))[i]))
#define NF10_TX_DESC(R, i)  NF10_GET_DESC(R, i, tx_dsc, nf10_tx_desc)
#define NF10_RX_DESC(R, i)  NF10_GET_DESC(R, i, rx_dsc, nf10_rx_desc)

//#define LOOPBACK_MODE

static DEFINE_SPINLOCK(tx_lock);
static DEFINE_SPINLOCK(work_lock);
static DEFINE_SPINLOCK(rx_dsc_lock);

int mydbg = 0;

DECLARE_WORK(wq, work_handler);

int nf10priv_xmit(struct nf10_card *card, struct sk_buff *skb, int port){
    uint8_t* data = skb->data;
    uint32_t len = skb->len;
    unsigned long flags;
    uint64_t pkt_addr = 0, pkt_addr_fixed = 0;
    uint64_t dsc_addr = 0, dsc_index = 0;
    uint64_t cl_size = (len + 2*63) / 64; // need engouh room for data in any alignment case
    uint64_t port_decoded = 0;
    uint64_t dsc_l0, dsc_l1;
    uint64_t dma_addr;
    struct nf10_tx_desc *tx_desc;

    if(len > 1514)
        printk(KERN_ERR "nf10: ERROR too big packet. TX size: %d\n", len);

    // packet buffer management
    spin_lock_irqsave(&tx_lock, flags);

    //printk("hello world  ");

    // make sure we fit in the descriptor ring and packet buffer
    if( (atomic64_read(&card->tx_ring->mem_tx_dsc.cnt) + 1 <= card->tx_ring->mem_tx_dsc.cl_size) &&
        (atomic64_read(&card->tx_ring->mem_tx_pkt.cnt) + cl_size <= card->tx_ring->mem_tx_pkt.cl_size)){
        
    //printk("hello world1  ");
        pkt_addr = card->tx_ring->mem_tx_pkt.wr_ptr;
        card->tx_ring->mem_tx_pkt.wr_ptr = (pkt_addr + 64*cl_size) & card->tx_ring->mem_tx_pkt.mask;

        dsc_addr = card->tx_ring->mem_tx_dsc.wr_ptr;
        card->tx_ring->mem_tx_dsc.wr_ptr = (dsc_addr + 64) & card->tx_ring->mem_tx_dsc.mask;

        // get physical address of the data
        dma_addr = pci_map_single(card->pdev, data, len, PCI_DMA_TODEVICE);
        
        if(pci_dma_mapping_error(card->pdev, dma_addr)){
            printk(KERN_ERR "nf10: dma mapping error");
            spin_unlock_irqrestore(&tx_lock, flags);
            return -1;
        }

        atomic64_inc(&card->tx_ring->mem_tx_dsc.cnt);
        atomic64_add(cl_size, &card->tx_ring->mem_tx_pkt.cnt);
        
        // there is space in the descriptor ring and at least 2k space in the pkt buffer
        if( !(( atomic64_read(&card->tx_ring->mem_tx_dsc.cnt) + 1 <= card->tx_ring->mem_tx_dsc.cl_size  ) &&
              ( atomic64_read(&card->tx_ring->mem_tx_pkt.cnt) + 32 <= card->tx_ring->mem_tx_pkt.cl_size )) ){   
//    printk("hello world2  ");

            netif_stop_queue(card->ndev[port]);
        }

    } 
    else{
        spin_unlock_irqrestore(&tx_lock, flags);
//    printk("hello world3  ");
        return -1;
    }
    
    spin_unlock_irqrestore(&tx_lock, flags);

    dsc_index = dsc_addr / 64;
    
    // figure out ports
    if(port == 0)
        port_decoded = 0x0102;
    else if(port == 1)
        port_decoded = 0x0408;
    else if(port == 2)
        port_decoded = 0x1020;
    else if(port == 3)
        port_decoded = 0x4080;

    // fix address for alignment issues
    pkt_addr_fixed = pkt_addr + (dma_addr & 0x3fULL);

    // prepare TX descriptor
    dsc_l0 = ((uint64_t)len << 48) + ((uint64_t)port_decoded << 32) + (pkt_addr_fixed & 0xffffffff);
    dsc_l1 = dma_addr;

    // book keeping
    card->tx_bk_dma_addr[dsc_index] = dma_addr;
    card->tx_bk_skb[dsc_index] = skb;
    card->tx_bk_size[dsc_index] = cl_size;
    card->tx_bk_port[dsc_index] = port;

    // write to the card
    //printk("hello world4  ");

    tx_desc = (struct nf10_tx_desc*)NF10_TX_DESC(card->tx_ring, dsc_index);

	//printk("writing to device");

    mb();
    tx_desc->cmd_word = dsc_l0;
    tx_desc->buffer_addr = dsc_l1;
    mb();
    /*
    mb();
    *(((uint64_t*)card->tx_ring->tx_dsc) + 8 * dsc_index + 0) = dsc_l0;
    *(((uint64_t*)card->tx_ring->tx_dsc) + 8 * dsc_index + 1) = dsc_l1;
    mb();
    */

    return 0;
}

void work_handler(struct work_struct *w){
    struct nf10_card * card = ((struct my_work_t *)w)->card;
    int irq_done = 0;
    uint32_t tx_int;
    uint64_t rx_int;
    uint64_t addr;
    uint64_t index;
    struct sk_buff *skb;
    uint64_t len;
    int port = -1;
    uint64_t port_encoded;
    unsigned long flags;
#ifdef LOOPBACK_MODE
    struct iphdr *iph;
    struct tcphdr *th;
    struct sock sck;
    struct inet_sock *isck;
#endif
    int work_counter = 0;
    int tcnt = 1;
    int int_enabled = 1;
    int i;

    spin_lock_irqsave(&work_lock, flags);

    while(tcnt){

        if((int_enabled == 1) && work_counter > 0){
            int_enabled = 0;
            mb();
            *(((uint64_t*)card->cfg_addr)+25) = 0; // disable TX interrupts
            *(((uint64_t*)card->cfg_addr)+26) = 0; // disable RX interrupts
            mb();
        }

        irq_done = 1;
        
        // read the host completion buffers
	// Why are they incrementing by 64 bytes here??
        tx_int = *(((uint32_t*)card->tx_ring->host_tx_dne_ptr) + (card->tx_ring->host_tx_dne.rd_ptr)/4);
        rx_int = *(((uint64_t*)card->rx_ring->host_rx_dne_ptr) + (card->rx_ring->host_rx_dne.rd_ptr)/8 + 7);
	
	if(mydbg < 5){
		printk("\n\nNew Cycle:\n");
		printk("ADD: tx_ring->host_tx_dne_ptr %p, VAL: tx_ring->host_tx_dne_ptr %x\n",(void*)((uint32_t*)card->tx_ring->host_tx_dne_ptr), *((uint32_t*)card->tx_ring->host_tx_dne_ptr));
		printk("ADD: rx_ring->host_rx_dne_ptr %p, VAL: rx_ring->host_rx_dne_ptr %016llx\n",(void*)((uint64_t*)card->rx_ring->host_rx_dne_ptr), *((uint64_t*)card->rx_ring->host_rx_dne_ptr));
		printk("ADD: tx_int = %p, VAL: tx_int = %x\n",(void*)(((uint32_t*)card->tx_ring->host_tx_dne_ptr) + (card->tx_ring->host_tx_dne.rd_ptr)/4), tx_int);
		printk("ADD: rx_int = %p, VAL: tx_int = %016llx\n",(void*)(((uint64_t*)card->rx_ring->host_rx_dne_ptr) + (card->rx_ring->host_rx_dne.rd_ptr)/8 + 7), rx_int);
	}

        if( (tx_int & 0xffff) == 1 ){
            irq_done = 0;
            
            // manage host completion buffer
            addr = card->tx_ring->host_tx_dne.rd_ptr;
            card->tx_ring->host_tx_dne.rd_ptr = (addr + 64) & card->tx_ring->host_tx_dne.mask;
            index = addr / 64;
            
            // clean up the skb
            pci_unmap_single(card->pdev, card->tx_bk_dma_addr[index], card->tx_bk_skb[index]->len, PCI_DMA_TODEVICE);
            dev_kfree_skb_any(card->tx_bk_skb[index]);
            atomic64_sub(card->tx_bk_size[index], &card->tx_ring->mem_tx_pkt.cnt);
            atomic64_dec(&card->tx_ring->mem_tx_dsc.cnt);
            
            // invalidate host tx completion buffer
            *(((uint32_t*)card->tx_ring->host_tx_dne_ptr) + index * 16) = 0xffffffff;

            // restart queue if needed
            if( ((atomic64_read(&card->tx_ring->mem_tx_dsc.cnt) + 8*1) <= card->tx_ring->mem_tx_dsc.cl_size) &&
                ((atomic64_read(&card->tx_ring->mem_tx_pkt.cnt) + 4*32) <= card->tx_ring->mem_tx_pkt.cl_size) ){
                
                for(i = 0; i < 4; i++){
                    if(netif_queue_stopped(card->ndev[i]))
                        netif_wake_queue(card->ndev[i]);
                }

            }
        }
        
        if( ((rx_int >> 48) & 0xffff) != 0xffff ){
            irq_done = 0;

		if(mydbg < 5){
			printk("ADD: tx_ring->host_tx_dne_ptr %p, VAL: tx_ring->host_tx_dne_ptr %x\n",(void*)((uint32_t*)card->tx_ring->host_tx_dne_ptr), *((uint32_t*)card->tx_ring->host_tx_dne_ptr));
			printk("ADD: rx_ring->host_rx_dne_ptr %p, VAL: rx_ring->host_rx_dne_ptr %016llx\n",(void*)((uint64_t*)card->rx_ring->host_rx_dne_ptr), *((uint64_t*)card->rx_ring->host_rx_dne_ptr));
			printk("ADD: tx_int = %p, VAL: tx_int = %x\n",(void*)(((uint32_t*)card->tx_ring->host_tx_dne_ptr) + (card->tx_ring->host_tx_dne.rd_ptr)/4), tx_int);
			printk("ADD: rx_int = %p, VAL: tx_int = %016llx\n",(void*)(((uint64_t*)card->rx_ring->host_rx_dne_ptr) + (card->rx_ring->host_rx_dne.rd_ptr)/8 + 7), rx_int);
			mydbg++;
	}
                
            // manage host completion buffer
            addr = card->rx_ring->host_rx_dne.rd_ptr;
            card->rx_ring->host_rx_dne.rd_ptr = (addr + 64) & card->rx_ring->host_rx_dne.mask;
            index = addr / 64;
            
            // invalidate host rx completion buffer
            *(((uint64_t*)card->rx_ring->host_rx_dne_ptr) + index * 8 + 7) = 0xffffffffffffffffULL;

            // skb is now ready
            skb = card->rx_bk_skb[index];
            pci_unmap_single(card->pdev, card->rx_bk_dma_addr[index], skb->len, PCI_DMA_FROMDEVICE);
            atomic64_sub(card->rx_bk_size[index], &card->rx_ring->mem_rx_pkt.cnt);
            atomic64_dec(&card->rx_ring->mem_rx_dsc.cnt);
            
            // give the card a new RX descriptor
            nf10priv_send_rx_dsc(card);

            // read data from the completion buffer
            len = rx_int & 0xffff;
            port_encoded = (rx_int >> 16) & 0xffff;
            
            if(port_encoded & 0x0200)
                port = 0;
            else if(port_encoded & 0x0800)
                port = 1;
            else if(port_encoded & 0x2000)
                port = 2;
            else if(port_encoded & 0x8000)
                port = 3;
            else 
                port = -1;

            //printk(KERN_ERR "rec %d\n", len);

            if(len > 1514 || len < 60 || port < 0 || port > 3){
                printk(KERN_ERR"nf10: invalid pakcet\n");
            }
            else if(((struct nf10_ndev_priv*)netdev_priv(card->ndev[port]))->port_up){

                // update skb with port information
                skb_put(skb, len);            
                
                skb->dev = card->ndev[port];
                skb->protocol = eth_type_trans(skb, card->ndev[port]);
                skb->ip_summed = CHECKSUM_NONE;

                // update stats
                card->ndev[port]->stats.rx_packets++;
                card->ndev[port]->stats.rx_bytes += skb->len;

#ifdef LOOPBACK_MODE
                iph = (struct iphdr *)skb->data;
                if(skb->protocol == htons(ETH_P_IP)){
                    ((uint8_t*)skb->data)[14] ^= 0x1;
                    ((uint8_t*)skb->data)[18] ^= 0x1;

		    /* 
		    Computes the checksum on the IP header
		    Precisely this is the IP checksum calculation by the device. 
                    The driver computes the checksum and tells the stack that checksum has been
		    calculated and updates the information with CHECKSUM_PARTIAL.
		    */
                    ip_send_check(iph);
                    if(((uint8_t*)skb->data)[9] == 6){
                        memset(&sck, 0, sizeof(sck));
                        th = tcp_hdr(skb);
                        th->check = 0;
                        skb->ip_summed = CHECKSUM_PARTIAL;
                        isck = inet_sk(&sck);
                        isck->inet_saddr = iph->saddr;
                        isck->inet_daddr = iph->daddr;
			/*
			Calculation of TCP checksum by the driver
			*/
                        tcp_v4_send_check(&sck, skb);
                    }

                    if(((uint8_t*)skb->data)[9] == 17){
                        ((uint8_t*)skb->data)[26] = 0;
                        ((uint8_t*)skb->data)[27] = 0;
                    }
              	    /*
                    The netif_receive_skb function sets the pointer to the L3 protocol (skb->nh) at the 
		    end of the L2 header. IP layer functions can therefore safely cast it to an iphdr structure.
		    It is used to process ingress packets. 
		    */ 
                    netif_receive_skb(skb);
                    
                }
                else{
                    kfree_skb(skb);
                }
#else
                netif_receive_skb(skb);
#endif            
            }
            else{ // invalid or down port, drop packet
                kfree_skb(skb);
            }
        }

        work_counter++;

        if(irq_done)
            tcnt--;
        else if(tcnt < 10000)
            tcnt++;

        if((tcnt <= 1) && (int_enabled == 0)){
            work_counter = 0;
            tcnt = 1;
            int_enabled = 1;
            mb();
            *(((uint64_t*)card->cfg_addr)+25) = 1; // enable TX interrupts
            *(((uint64_t*)card->cfg_addr)+26) = 1; // enable RX interrupts
            mb();
        }
    }

    spin_unlock_irqrestore(&work_lock, flags);
}

int nf10priv_send_rx_dsc(struct nf10_card *card){
    struct sk_buff *skb;
    uint64_t dma_addr;
    uint64_t pkt_addr = 0, pkt_addr_fixed = 0;
    uint64_t dsc_addr = 0, dsc_index = 0;
    /*
    The resulting number is aligned. i.e it can be divided by 8, 64 as well as 4.
    SK_BUFF_ALLOC_SIZE is 1533 which is not aligned by default. The above operation leads to
    a quotient (24 in this case) which when multiplied by 64 is divisible by 8, 24 and 4
    */
    uint64_t cl_size = (SK_BUFF_ALLOC_SIZE + 66) / 64;
    unsigned long flags;
    uint64_t dsc_l0, dsc_l1;

    struct nf10_rx_desc *rx_desc;
    // packet buffer management
    spin_lock_irqsave(&rx_dsc_lock, flags);

    // make sure we fit in the descriptor ring and packet buffer
    if( (atomic64_read(&card->rx_ring->mem_rx_dsc.cnt) + 1 <= card->rx_ring->mem_rx_dsc.cl_size) &&
        (atomic64_read(&card->rx_ring->mem_rx_pkt.cnt) + cl_size <= card->rx_ring->mem_rx_pkt.cl_size)){
        
        skb = dev_alloc_skb(SK_BUFF_ALLOC_SIZE + 2);
        if(!skb) {
            printk(KERN_ERR "nf10: skb alloc failed\n");
            spin_unlock_irqrestore(&rx_dsc_lock, flags);
            return -1;
        }

	/*
	* When setting up receive packets that an ethernet device will DMA into, we typically call 
	* skb_reserve(skb, NET_IP_ALIGN)
	* NET_IP_ALIGN is defined to 2. This makes it so that, after the ethernet header, the protocol header
	* will be aligned on atleast 4 byte boundary. 
	*/
        skb_reserve(skb, 2); /* align IP on 16B boundary */   

        pkt_addr = card->rx_ring->mem_rx_pkt.wr_ptr;
//	printk("pkt_addr = %016llx\n",pkt_addr);
        card->rx_ring->mem_rx_pkt.wr_ptr = (pkt_addr + 64*cl_size) & card->rx_ring->mem_rx_pkt.mask;

        dsc_addr = card->rx_ring->mem_rx_dsc.wr_ptr;
//	printk("dsc_addr = %016llx\n",dsc_addr);
        card->rx_ring->mem_rx_dsc.wr_ptr = (dsc_addr + 64) & card->rx_ring->mem_rx_dsc.mask;

        atomic64_inc(&card->rx_ring->mem_rx_dsc.cnt);
        atomic64_add(cl_size, &card->rx_ring->mem_rx_pkt.cnt);
//	printk("dsc cnt = %016llx, rx_pkt_cnt = %016llx\n",card->rx_ring->mem_rx_dsc.cnt, card->rx_ring->mem_rx_pkt.cnt);
        
    } 
    else{
        spin_unlock_irqrestore(&rx_dsc_lock, flags);
        return -1;
    }

    spin_unlock_irqrestore(&rx_dsc_lock, flags);

    dsc_index = dsc_addr / 64;

    // physical address
    dma_addr = pci_map_single(card->pdev, skb->data, SK_BUFF_ALLOC_SIZE, PCI_DMA_FROMDEVICE);

    // fix address for alignment issues
    pkt_addr_fixed = pkt_addr + (dma_addr & 0x3ULL);
    //printk("dma_addr = %016llx  pkt_addr_fixed = %016llx\n",dma_addr, pkt_addr_fixed);

    // prepare RX descriptor
    dsc_l0 = ((uint64_t)SK_BUFF_ALLOC_SIZE << 48) + (pkt_addr_fixed & 0xffffffff);
    dsc_l1 = dma_addr;
    //printk("dsc_lo = %016llx    dsc_l1 = %016llx\n",dsc_l0, dsc_l1);

    // book keeping
    card->rx_bk_dma_addr[dsc_index] = dma_addr;
    card->rx_bk_skb[dsc_index] = skb;
    card->rx_bk_size[dsc_index] = cl_size;

    /*
    * Sample data
    * This is how the RX ring would look like
     	pkt_addr = 0000000000000000 dsc_addr       = 0000000000000000
	dsc cnt  = 0000000000000001 rx_pkt_cnt     = 0000000000000018
	dma_addr = 000000002989e842 pkt_addr_fixed = 0000000000000002
	dsc_lo   = 05fd000000000002   dsc_l1       = 000000002989e842

	pkt_addr = 0000000000000600 dsc_addr       = 0000000000000040
	dsc cnt  = 0000000000000002 rx_pkt_cnt     = 0000000000000030
	dma_addr = 00000000299d2042 pkt_addr_fixed = 0000000000000602
	dsc_lo   = 05fd000000000602 dsc_l1         = 00000000299d2042

	.
	.
	.

	pkt_addr = 0000000000007800 dsc_addr       = 0000000000000500
	dsc cnt  = 0000000000000015 rx_pkt_cnt     = 00000000000001f8
	dma_addr = 000000000dcb7042 pkt_addr_fixed = 0000000000007802
	dsc_lo   = 05fd000000007802 dsc_l1         = 000000000dcb7042

    */


    rx_desc = (struct nf10_rx_desc*)NF10_RX_DESC(card->rx_ring, dsc_index);

	//printk("writing to device");

    printk("Using my changed\n");
    mb();
    rx_desc->cmd_word = dsc_l0;
    rx_desc->buffer_addr = dsc_l1;
    mb();

    // write to the card
    /*
    mb();
    *(((uint64_t*)card->rx_ring->rx_dsc) + 8 * dsc_index + 0) = dsc_l0;
    *(((uint64_t*)card->rx_ring->rx_dsc) + 8 * dsc_index + 1) = dsc_l1;
    mb();
    */

    return 0;
}
