/* $Id: b1pci.c,v 1.1.2.2 2004/01/16 21:09:27 keil Exp $
 *
 * Module for AVM B1 PCI-card.
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

#include "avm-b1.h"
#include "link-driver.h"


static char *submodule = "TLink";

module_param(submodule, charp, 0000);
MODULE_PARM_DESC(submodule, "Transputer Link Module");

extern int link_construct_device(avmcard *);

/* ------------------------------------------------------------- */
static char *revision = "$Revision: 1.1.2.2 $";

static int b1pci_pci_probe(struct pci_dev *, const struct pci_device_id *);
static void b1pci_pci_remove(struct pci_dev *);

/* ------------------------------------------------------------- */
static struct pci_device_id b1pci_pci_tbl[] = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_B1, PCI_ANY_ID, PCI_ANY_ID },
	{ }				/* Terminating entry */
};

static struct pci_driver b1pci_pci_driver = {
	.name		= "avm_pci",
	.id_table	= b1pci_pci_tbl,
	.probe		= b1pci_pci_probe,
	.remove		= b1pci_pci_remove,
};

MODULE_DEVICE_TABLE(pci, b1pci_pci_tbl);
MODULE_DESCRIPTION("AVM-B1-PCI: Driver for AVM B1 PCI card");
MODULE_AUTHOR("Matsche");
MODULE_LICENSE("GPL");


/* ------------------------------------------------------------- */
irqreturn_t b1_interrupt(int interrupt, void *devptr)
{
	avmcard *card = devptr;
	unsigned char b1cmd;

	unsigned long flags;
	
	//u32 status;

	spin_lock_irqsave(&card->lock, flags);
	


	//spin_lock(&card->lock);
	/*
	status = b1dma_readl(card, AMCC_INTCSR);
	if ((status & ANY_S5933_INT) == 0) {
		spin_unlock_irqrestore(&card->lock, flags);
		return IRQ_HANDLED;
	}
	*/
	if(!b1_rx_full(card->port)) {
		spin_unlock_irqrestore(&card->lock, flags);
		return IRQ_NONE;
	}

	b1cmd = b1_get_byte(card->port);

	switch (b1cmd) {

	default:
		spin_unlock_irqrestore(&card->lock, flags);
		if(b1cmd>31 && b1cmd<127) {
			printk(KERN_ERR "b1_interrupt: %s: %c",
		       card->name, b1cmd);
		}else{
			printk(KERN_ERR "b1_interrupt: %s: 0x%02x ???\n",
		       card->name, b1cmd);
		}
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
static int b1pci_probe(struct capicardparams *p, struct pci_dev *pdev)
{
	avmcard *card;
	int retval;
	
	int (*construct_device)(avmcard *card);
	construct_device = link_construct_device;

	card = b1_alloc_card(1);
	if(!card) {
		printk(KERN_WARNING "avm_pci (b1pci-probe): no memory.\n");
		retval = -ENOMEM;
		goto err;
	}

	sprintf(card->name, "avm-pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1pci;

	if(!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING "avm_pci (b1pci-probe): ports 0x%03x-0x%03x in use.\n",
						card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free;
	}
	b1_reset(card->port);
	
	// hardware detection
	retval = b1_detect(card->port, card->cardtype);
	if(retval) {
		printk(KERN_NOTICE "avm_pci (b1pci-probe): NO card at 0x%x (%d)\n",
		       card->port, retval);
		retval = -ENODEV;
		goto err_release_region;
	}
	b1_reset(card->port);
	
	b1_getrevision(card);

	retval = request_irq(card->irq, b1_interrupt, IRQF_SHARED, card->name, card);
	if(retval){
		printk(KERN_ERR "avm_pci (b1pci-probe): unable to get IRQ %d.\n", card->irq);
		retval = -EBUSY;
		goto err_release_region;
	}

	if(card->revision >= 4) {
		printk(KERN_INFO "avm_pci (b1pci-probe): AVM B1 PCI V4 at i/o %#x, irq %d, revision %d (no dma)\n",
		       card->port, card->irq, card->revision);
	}else {
		printk(KERN_INFO "avm_pci (b1pci-probe): AVM B1 PCI at i/o %#x, irq %d, revision %d\n",
		       card->port, card->irq, card->revision);
	}

	pci_set_drvdata(pdev, card);
	
	// hierher gehoert die tlink driver reg
	printk(KERN_INFO "avm_pci (b1pci-probe): Submodule: %s\n", submodule);
	if(!strncmp(submodule, "TLink", 5)) {
		printk(KERN_INFO "avm_pci (b1pci-probe): Try creating TLink Device...\n");
		retval = (*construct_device)(card);
		if(retval) {
			goto err_free_irq;
		}
	}


	return 0;

err_free_irq:
	free_irq(card->irq, card);
err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
err_free:
	b1_free_card(card);
err:
	return retval;
}

/* ------------------------------------------------------------- */
static void b1pci_remove(struct pci_dev *pdev)
{
	avmcard *card = pci_get_drvdata(pdev);
	//avmctrl_info *cinfo = card->ctrlinfo;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */
/* das ist der teil, der die karte ins pci-subsystem einhaengt */
static int b1pci_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct capicardparams param;
	int retval, iolen;

	// Device aktivieren
	if(pci_enable_device(pdev) < 0) {
		printk(KERN_ERR "avm_pci (b1pci-pci-probe): failed to enable AVM-B1\n");
		return -ENODEV;
	}
	param.irq = pdev->irq;

	if(pci_resource_start(pdev, 2)) { /* B1 PCI V4 */

		param.membase = pci_resource_start(pdev, 0);
		param.port = pci_resource_start(pdev, 2);

		printk(KERN_INFO "avm_pci (b1pci-pci-probe): PCI BIOS reports AVM-B1 V4 at i/o %#x, irq %d, mem %#x\n",
		       param.port, param.irq, param.membase);

		retval = b1pci_probe(&param, pdev);

		if(retval != 0) {
			printk(KERN_ERR "avm_pci (b1pci-pci-probe): no AVM-B1 V4 at i/o %#x, irq %d, mem %#x detected\n",
			       param.port, param.irq, param.membase);
		}
	}else {
		param.membase = 0;
		param.port = pci_resource_start(pdev, 1);

		printk(KERN_INFO "avm_pci (b1pci-pci-probe): PCI BIOS reports AVM-B1 at i/o %#x, irq %d\n",
						param.port, param.irq);
		
		retval = b1pci_probe(&param, pdev);
		if(retval != 0) {
			printk(KERN_ERR "avm_pci (b1pci-pci-probe): no AVM-B1 at i/o %#x, irq %d detected\n",
			       param.port, param.irq);
		}
	}
	
	iolen = pci_resource_len(pdev, 0);
	if((pci_resource_flags(pdev, 0) & IORESOURCE_IO)) {
		printk(KERN_INFO "avm_pci (b1pci-pci-probe): IO-Resource at %#x with size %d\n",
					param.port, iolen);
	}
	if(iolen < AVMB1_PORTLEN) {
		printk(KERN_ERR "avm_pci (b1pci-pci-probe): IO-Resource not sufficient: %d.\n", iolen);
		retval = -ENOMEM;
	}
	return retval;
}

/* ------------------------------------------------------------- */
static void b1pci_pci_remove(struct pci_dev *pdev)
{
	b1pci_remove(pdev);
}

/* ------------------------------------------------------------- */
static int __init b1pci_init(void)
{
	char *p;
	char rev[32];
	int err;
	
	if((p = strchr(revision, ':')) != NULL && p[1]) {
		strlcpy(rev, p + 2, 32);
		if((p = strchr(rev, '$')) != NULL && p > rev)
			*(p - 1) = 0;
	}else
		strcpy(rev, "1.0");

	err = pci_register_driver(&b1pci_pci_driver);

	return err;
}

/* ------------------------------------------------------------- */
static void __exit b1pci_exit(void)
{
	pci_unregister_driver(&b1pci_pci_driver);
	
	printk(KERN_INFO "avm_pci: Module successfully removed!\n");
}

/* ------------------------------------------------------------- */
module_init(b1pci_init);
module_exit(b1pci_exit);


/**
 * 
int (*addcard)(unsigned int port, unsigned irq);
switch(cardtype) {
	case AVM_CARDTYPE_M1: addcard = b1pcmcia_addcard_m1; break;
	case AVM_CARDTYPE_M2: addcard = b1pcmcia_addcard_m2; break;
	default:
	case AVM_CARDTYPE_B1: addcard = b1pcmcia_addcard_b1; break;
	}
	if((i = (*addcard)(link->resource[0]->start, link->irq)) < 0) {
		dev_err(&link->dev,
			"avm_cs: failed to add AVM-Controller at i/o %#x, irq %d\n",
			(unsigned int) link->resource[0]->start, link->irq);
		avmcs_release(link);
		return -ENODEV;
	}
	**/
	
/**
 * register_capi_driver() - register CAPI driver
 * @driver:	driver descriptor structure.
 *
 * Called by hardware driver to register itself with the CAPI subsystem.
 *

void register_capi_driver(struct capi_driver *driver)
{
	mutex_lock(&capi_drivers_lock);
	list_add_tail(&driver->list, &capi_drivers);
	mutex_unlock(&capi_drivers_lock);
}

EXPORT_SYMBOL(register_capi_driver);
**/

/**
 * unregister_capi_driver() - unregister CAPI driver
 * @driver:	driver descriptor structure.
 *
 * Called by hardware driver to unregister itself from the CAPI subsystem.
 *

void unregister_capi_driver(struct capi_driver *driver)
{
	mutex_lock(&capi_drivers_lock);
	list_del(&driver->list);
	mutex_unlock(&capi_drivers_lock);
}

EXPORT_SYMBOL(unregister_capi_driver);
	
	
	**/
