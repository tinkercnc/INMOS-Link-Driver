/* $Id: avm-cs.c,v 1.4.6.3 2001/09/23 22:24:33 kai Exp $
 *
 * A PCMCIA client driver for AVM B1/M1/M2
 *
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <linux/pci.h>

#include <linux/cdev.h>
#include <linux/device.h>

#include <asm/io.h>
#include <linux/init.h>

#include <linux/b1pcmcia.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include "avm-b1.h"
#include "link-driver.h"
/*====================================================================*/

MODULE_DESCRIPTION("CAPI4Linux: PCMCIA client driver for AVM B1/M1/M2");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/*====================================================================*/

static char *submodule = "TLink";

module_param(submodule, charp, 0000);
MODULE_PARM_DESC(submodule, "Transputer Link Module");

extern int link_construct_device(unsigned int, int);

/* ------------------------------------------------------------- */
static int avmcs_config(struct pcmcia_device *link);
static void avmcs_release(struct pcmcia_device *link);
static void avmcs_detach(struct pcmcia_device *p_dev);

/* ------------------------------------------------------------- */
static int avmcs_probe(struct pcmcia_device *p_dev)
{
	/* General socket configuration */
	printk(KERN_INFO "avm-cs (avmcs-probe): probing PCMCIA devices...\n");
	p_dev->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;
	p_dev->config_index = 1;
	p_dev->config_regs = PRESENT_OPTION;

	return avmcs_config(p_dev);
} /* avmcs_attach */

/* ------------------------------------------------------------- */
static void avmcs_detach(struct pcmcia_device *link)
{
	avmcs_release(link);
} /* avmcs_detach */

/* ------------------------------------------------------------- */
static int avmcs_configcheck(struct pcmcia_device *p_dev, void *priv_data)
{
	/*p_dev->resource[0]->end = 16;
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	*/
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->resource[1]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[1]->flags |= IO_DATA_PATH_WIDTH_8;


	return pcmcia_request_io(p_dev);
}

/* ------------------------------------------------------------- */
irqreturn_t b1_interrupt(int interrupt, void *devptr)
{
	avmcard *card = devptr;
	unsigned char b1cmd;

	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	if (!b1_rx_full(card->port)) {
		spin_unlock_irqrestore(&card->lock, flags);
		return IRQ_NONE;
	}

	b1cmd = b1_get_byte(card->port);

	switch (b1cmd) {

	default:
		spin_unlock_irqrestore(&card->lock, flags);
		printk(KERN_ERR "b1_interrupt: %s: b1_interrupt: 0x%x ???\n",
		       card->name, b1cmd);
		return IRQ_HANDLED;
	}
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------- */
void b1_free_card(avmcard *card)
{
	kfree(card->ctrlinfo);
	kfree(card);
}

/* ------------------------------------------------------------- */
avmcard *b1_alloc_card(int nr_controllers)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int i;

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return NULL;

	cinfo = kzalloc(sizeof(*cinfo) * nr_controllers, GFP_KERNEL);
	if (!cinfo) {
		kfree(card);
		return NULL;
	}

	card->ctrlinfo = cinfo;
	for (i = 0; i < nr_controllers; i++) {
		INIT_LIST_HEAD(&cinfo[i].ncci_head);
		cinfo[i].card = card;
	}
	spin_lock_init(&card->lock);
	card->nr_controllers = nr_controllers;

	return card;
}

/* ------------------------------------------------------------- */

static void b1pcmcia_remove(struct pcmcia_device *pdev)
{
	avmcard *card = (avmcard *)dev_get_drvdata(&(pdev->dev));
	//avmctrl_info *cinfo = card->ctrlinfo;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */
/*
static void b1pcmcia_cs_remove(struct pcmcia_device *pdev)
{
	b1pcmcia_remove(pdev);
}
*/
/* ------------------------------------------------------------- */
/*
int b1pcmcia_delcard(unsigned int port, unsigned irq)
{
	struct list_head *l;
	avmcard *card;

	list_for_each(l, &cards) {
		card = list_entry(l, avmcard, list);
		if (card->port == port && card->irq == irq) {
			b1pcmcia_remove_ctr(&card->ctrlinfo[0].capi_ctrl);
			return 0;
		}
	}
	return -ESRCH;
}
*/
/* ------------------------------------------------------------- */
static int avmcs_config(struct pcmcia_device *link)
{
	int i = -1;
	//char devname[128];
	//int cardtype;
	int retval;
	
	avmcard *card;
	//avmctrl_info *cinfo;
	//int (*addcard)(unsigned int port, unsigned irq);
	int (*construct_device)(unsigned int port, int cardnr);
	construct_device = link_construct_device;

	printk(KERN_INFO "avm-cs (avmcs-config): try to allocate PCMCIA devices...\n");
	card = b1_alloc_card(1);
	if(!card) {
		printk(KERN_WARNING "avm-cs (avmcs-config): no memory.\n");
		retval = -ENOMEM;
		goto err;
	}
	
	
	/*--------------
	devname[0] = 0;
	if (link->prod_id[1])
		strlcpy(devname, link->prod_id[1], sizeof(devname));
	*/
	
	/*
	 * find IO port
	 */
	printk(KERN_INFO "avm-cs (avmcs-config): try to find IO port...\n");
	if (pcmcia_loop_config(link, avmcs_configcheck, NULL)) {
		printk(KERN_WARNING "avm-cs (avmcs-config): no IO port found.. \n");
		return -ENODEV;
	}

	do {
		printk(KERN_INFO "avm-cs (avmcs-config): try to find IRQ...\n");
		if (!link->irq) {
			/* undo */
			printk(KERN_WARNING "avm-cs (avmcs_config):  no IRQ - pcmcia_disable_device.\n");
			pcmcia_disable_device(link);
			break;
		}

		/*
		 * configure the PCMCIA socket
		 */
		printk(KERN_INFO "avm-cs (avmcs-config): try to configure the PCMCIA socket...\n");
		i = pcmcia_enable_device(link);
		if (i != 0) {
			printk(KERN_WARNING "avm-cs (avmcs_config):  can't enable device, pcmcia_disable_device.\n");
			pcmcia_disable_device(link);
			break;
		}

	} while (0);

	/*
	if (devname[0]) {
		char *s = strrchr(devname, ' ');
		if (!s)
			s = devname;
		else s++;
		if (strcmp("M1", s) == 0) {
			cardtype = AVM_CARDTYPE_M1;
		} else if (strcmp("M2", s) == 0) {
			cardtype = AVM_CARDTYPE_M2;
		} else {
			cardtype = AVM_CARDTYPE_B1;
		}
	} else
		cardtype = AVM_CARDTYPE_B1;
	*/
	
	/* If any step failed, release any partially configured state 
	if (i != 0) {
		avmcs_release(link);
		return -ENODEV;
	}
	*/


	/*
	printk(KERN_INFO "avm-cs (avmcs-config): try to register IRQ...\n");
	retval = request_irq(link->irq, b1_interrupt, IRQF_SHARED, card->name, card);
	if(retval){
		printk(KERN_ERR "avm-cs (avmcs_config):  unable to get IRQ %d.\n", link->irq);
		retval = -EBUSY;
		goto err_release_region;
	}
	*/

	// hierher gehoert die tlink driver reg
	printk(KERN_INFO "avm-cs (avmcs-config): Submodule: %s\n", submodule);
	if(!strncmp(submodule, "TLink", 5)) {
		printk(KERN_INFO "avm-cs (avmcs-config): Try creating TLink Device...\n");
		retval = (*construct_device)(link->resource[0]->start, card->cardnr);
		if(retval) {
			goto err_free_irq;
		}
	}
	/*else {
	
		switch (cardtype) {
		case AVM_CARDTYPE_M1: addcard = b1pcmcia_addcard_m1; break;
		case AVM_CARDTYPE_M2: addcard = b1pcmcia_addcard_m2; break;
		default:
		case AVM_CARDTYPE_B1: addcard = b1pcmcia_addcard_b1; break;
		}
		if ((i = (*addcard)(link->resource[0]->start, link->irq)) < 0) {
			dev_err(&link->dev,
				"avm-cs: failed to add AVM-Controller at i/o %#x, irq %d\n",
				(unsigned int) link->resource[0]->start, link->irq);
			avmcs_release(link);
			return -ENODEV;
		}
	}*/
	return 0;

	
err_free_irq:
	free_irq(card->irq, card);
//err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
//err_free:
	b1_free_card(card);
err:
	return retval;
} /* avmcs_config */

/* ------------------------------------------------------------- */
static void avmcs_release(struct pcmcia_device *link)
{
	//b1pcmcia_delcard(link->resource[0]->start, link->irq);
	b1pcmcia_remove(link);
	pcmcia_disable_device(link);
} /* avmcs_release */

/* ------------------------------------------------------------- */
static const struct pcmcia_device_id avmcs_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("AVM", "ISDN-Controller B1", 0x95d42008, 0x845dc335),
	PCMCIA_DEVICE_PROD_ID12("AVM", "Mobile ISDN-Controller M1", 0x95d42008, 0x81e10430),
	PCMCIA_DEVICE_PROD_ID12("AVM", "Mobile ISDN-Controller M2", 0x95d42008, 0x18e8558a),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, avmcs_ids);

/* ------------------------------------------------------------- */
static struct pcmcia_driver avmcs_driver = {
	.owner	= THIS_MODULE,
	.name		= "avm-cs",
	.probe = avmcs_probe,
	.remove	= avmcs_detach,
	.id_table = avmcs_ids,
};
module_pcmcia_driver(avmcs_driver);
