/* $Id: avmcard.h,v 1.1.4.1.2.1 2001/12/21 15:00:17 kai Exp $
 *
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __H_
#define _AVM_B1_H_

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <asm/io.h>


#define	AVMB1_PORTLEN		0x1f
#define AVM_MAXVERSION		8
#define AVM_NCCI_PER_CHANNEL	4

/*
 * Versions
 */

#define	VER_DRIVER	0
#define	VER_CARDTYPE	1
#define	VER_HWID	2
#define	VER_SERIAL	3
#define	VER_OPTION	4
#define	VER_PROTO	5
#define	VER_PROFILE	6
#define	VER_CAPI	7

enum avmcardtype {
	avm_b1isa,
	avm_b1pci,
	avm_b1pcmcia,
	avm_m1,
	avm_m2,
	avm_t1isa,
	avm_t1pci,
	avm_c4,
	avm_c2
};
typedef struct capicardparams {
	unsigned int port;
	unsigned irq;
	int cardtype;
	int cardnr;
	unsigned int membase;
} capicardparams;

typedef struct avmcard {
	char name[32];

	spinlock_t lock;
	unsigned int port;
	unsigned irq;
	unsigned long membase;
	enum avmcardtype cardtype;
	unsigned char revision;
	unsigned char class;
	int cardnr; /* for t1isa */

	char msgbuf[128];	/* capimsg msg part */
	char databuf[2048];	/* capimsg data part */

	void __iomem *mbase;
	volatile u32 csr;
	//avmcard_dmainfo *dma;

	//struct avmctrl_info *ctrlinfo;

	u_int nr_controllers;
	u_int nlogcontr;
	struct list_head list;
} avmcard;


//extern int b1_irq_table[16];
/* ------------------------------------------------------------- */

int b1_irq_table[16] =
{0,
 0,
 0,
 192,				/* irq 3 */
 32,				/* irq 4 */
 160,				/* irq 5 */
 96,				/* irq 6 */
 224,				/* irq 7 */
 0,
 64,				/* irq 9 */
 80,				/* irq 10 */
 208,				/* irq 11 */
 48,				/* irq 12 */
 0,
 0,
 112,				/* irq 15 */
};

/* ------------------------------------------------------------- */

/* ------------------------------------------------------------- */

/* AMCC S5933 pci controller registers offsets*/

#define	AMCC_RXPTR	0x24
#define	AMCC_RXLEN	0x28
#define	AMCC_TXPTR	0x2c
#define	AMCC_TXLEN	0x30

#define	AMCC_INTCSR	0x38
#	define EN_READ_TC_INT		0x00008000L
#	define EN_WRITE_TC_INT		0x00004000L
#	define EN_TX_TC_INT		EN_READ_TC_INT
#	define EN_RX_TC_INT		EN_WRITE_TC_INT
#	define AVM_FLAG			0x30000000L

#	define ANY_S5933_INT		0x00800000L
#	define READ_TC_INT		0x00080000L
#	define WRITE_TC_INT		0x00040000L
#	define	TX_TC_INT		READ_TC_INT
#	define	RX_TC_INT		WRITE_TC_INT
#	define MASTER_ABORT_INT		0x00100000L
#	define TARGET_ABORT_INT		0x00200000L
#	define BUS_MASTER_INT		0x00200000L
#	define ALL_INT			0x000C0000L

#define	AMCC_MCSR	0x3c
#	define A2P_HI_PRIORITY		0x00000100L
#	define EN_A2P_TRANSFERS		0x00000400L
#	define P2A_HI_PRIORITY		0x00001000L
#	define EN_P2A_TRANSFERS		0x00004000L
#	define RESET_A2P_FLAGS		0x04000000L
#	define RESET_P2A_FLAGS		0x02000000L

/* ------------------------------------------------------------- */


static inline void b1dma_writel(avmcard *card, u32 value, int off)
{
	writel(value, card->mbase + off);
}

static inline u32 b1dma_readl(avmcard *card, int off)
{
	return readl(card->mbase + off);
}

/* ------------------------------------------------------------- */

#define WRITE_REGISTER		0x00
#define READ_REGISTER		0x01

/*
 * port offsets
 */

#define B1_READ			0x00
#define B1_WRITE		0x01
#define B1_INSTAT		0x02
#define B1_OUTSTAT		0x03
#define B1_ANALYSE		0x04
#define B1_REVISION		0x05
#define B1_RESET		0x10


#define B1_STAT0(cardtype)  ((cardtype) == avm_m1 ? 0x81200000l : 0x80A00000l)
#define B1_STAT1(cardtype)  (0x80E00000l)

/* ---------------------------------------------------------------- */

static inline uint8_t b1outp(unsigned int base,
				   uint32_t offset,
				   uint8_t value)
{
	outb(value, base + offset);
	return inb(base + B1_ANALYSE);
}
//EXPORT_SYMBOL(b1outp);

static inline int32_t b1_rx_full(unsigned int base)
{
	return inb(base + B1_INSTAT) & 0x1;
}

static inline uint8_t b1_get_byte(unsigned int base)
{
	unsigned long stop = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	while (!b1_rx_full(base) && time_before(jiffies, stop));
	if (b1_rx_full(base))
		return inb(base + B1_READ);
	//printk(KERN_CRIT "avm-b1: (b1-get-byte(0x%x)): rx not full after 1 second\n", base);
	return 0;
}

static inline uint32_t b1_get_byte_stat(unsigned int base, char* db)
{
	unsigned long stop = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	while (!b1_rx_full(base) && time_before(jiffies, stop));
	if (b1_rx_full(base)) {
		*db = inb(base + B1_READ);
		return 1;
	}

	//printk(KERN_CRIT "avm-b1: (b1_get_byte_stat(0x%x)): rx not full after 1 second\n", base);
	
	return 0;
}

static inline int32_t b1_get_word(unsigned int base)
{
	int32_t val = 0;
	val |= b1_get_byte(base);
	val |= (b1_get_byte(base) << 8);
	val |= (b1_get_byte(base) << 16);
	val |= (b1_get_byte(base) << 24);
	return val;
}

static inline int32_t b1_tx_empty(unsigned int base)
{
	return inb(base + B1_OUTSTAT) & 0x1;
}

static inline void b1_put_byte(unsigned int base, unsigned char val)
{
	while (!b1_tx_empty(base));
	b1outp(base, B1_WRITE, val);
}

static inline int b1_save_put_byte(unsigned int base, unsigned char val)
{
	unsigned long stop = jiffies + 2 * HZ;
	while (!b1_tx_empty(base) && time_before(jiffies, stop));
	if (!b1_tx_empty(base)) return -1;
	b1outp(base, B1_WRITE, val);
	return 0;
}

static inline void b1_put_word(unsigned int base, int32_t val)
{
	b1_put_byte(base, val & 0xff);
	b1_put_byte(base, (val >> 8) & 0xff);
	b1_put_byte(base, (val >> 16) & 0xff);
	b1_put_byte(base, (val >> 24) & 0xff);
}

static inline uint32_t b1_get_slice(unsigned int base,
					uint8_t *dp)
{
	uint32_t len, i;

	len = i = b1_get_word(base);
	while (i-- > 0) *dp++ = b1_get_byte(base);
	return len;
}

static inline void b1_put_slice(unsigned int base,
				uint8_t *dp, uint32_t len)
{
	uint32_t i = len;
	b1_put_word(base, i);
	while (i-- > 0)
		b1_put_byte(base, *dp++);
}

static void b1_wr_reg(unsigned int base,
		      uint32_t reg,
		      uint32_t value)
{
	b1_put_byte(base, WRITE_REGISTER);
	b1_put_word(base, reg);
	b1_put_word(base, value);
}

static inline uint32_t b1_rd_reg(unsigned int base,
				     uint32_t reg)
{
	b1_put_byte(base, READ_REGISTER);
	b1_put_word(base, reg);
	return b1_get_word(base);

}

static inline void b1_reset(unsigned int base)
{
	b1outp(base, B1_RESET, 0);
	mdelay(55 * 2);	/* 2 TIC's */

	b1outp(base, B1_RESET, 1);
	mdelay(55 * 2);	/* 2 TIC's */

	b1outp(base, B1_RESET, 0);
	mdelay(55 * 2);	/* 2 TIC's */
}

static inline uint8_t b1_disable_irq(unsigned int base)
{
	return b1outp(base, B1_INSTAT, 0x00);
}
//EXPORT_SYMBOL(b1_disable_irq);

/* ---------------------------------------------------------------- */

static inline void b1_set_test_bit(unsigned int base,
				   enum avmcardtype cardtype,
				   int onoff)
{
	b1_wr_reg(base, B1_STAT0(cardtype), onoff ? 0x21 : 0x20);
}

static inline int b1_get_test_bit(unsigned int base,
				  enum avmcardtype cardtype)
{
	return (b1_rd_reg(base, B1_STAT0(cardtype)) & 0x01) != 0;
}

static inline void b1_setinterrupt(unsigned int base, unsigned irq,
				   enum avmcardtype cardtype)
{
	printk(KERN_WARNING "avm_b1 (b1-setinterrupt entry): port=0x%04x, irq=%d, cardtype=%d \n", base, irq, cardtype);
						
	switch (cardtype) {
	case avm_b1isa:
		b1outp(base, B1_INSTAT, 0x00);
		b1outp(base, B1_RESET, b1_irq_table[irq]);
		b1outp(base, B1_INSTAT, 0x02);
		break;
	default:
	case avm_m1:
	case avm_m2:
	case avm_b1pci:
		printk(KERN_WARNING "avm_b1 (b1-setinterrupt): port=0x%04x, irq=%d, cardtype=%s \n", base, irq, "avm_b1pci");
		b1outp(base, B1_INSTAT, 0x00);
		b1outp(base, B1_RESET, 0xf0);
		b1outp(base, B1_INSTAT, 0x02);
		break;
	case avm_c4:
	case avm_t1pci:
		b1outp(base, B1_RESET, 0xf0);
		break;
	}
}

/* b1.c */
/* ------------------------------------------------------------- */
avmcard *b1_alloc_card(int nr_controllers)
{
	avmcard *card;
	
	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return NULL;

	spin_lock_init(&card->lock);
	card->nr_controllers = nr_controllers;

	return card;
}

/* ------------------------------------------------------------- */
int b1_detect(unsigned int base, enum avmcardtype cardtype)
{
	int onoff, i;

	/*
	 * Statusregister 0000 00xx
	 */
	if((inb(base + B1_INSTAT) & 0xfc)
	    || (inb(base + B1_OUTSTAT) & 0xfc))
		return 1;
	/*
	 * Statusregister 0000 001x
	 */
	b1outp(base, B1_INSTAT, 0x2);	/* enable irq */
	/* b1outp(base, B1_OUTSTAT, 0x2); */
	if((inb(base + B1_INSTAT) & 0xfe) != 0x2
	    /* || (inb(base + B1_OUTSTAT) & 0xfe) != 0x2 */)
		return 2;
	/*
	 * Statusregister 0000 000x
	 */
	b1outp(base, B1_INSTAT, 0x0);	/* disable irq */
	b1outp(base, B1_OUTSTAT, 0x0);
	if((inb(base + B1_INSTAT) & 0xfe)
	    || (inb(base + B1_OUTSTAT) & 0xfe))
		return 3;

	for(onoff = !0, i = 0; i < 10; i++) {
		b1_set_test_bit(base, cardtype, onoff);
		if(b1_get_test_bit(base, cardtype) != onoff)
			return 4;
		onoff = !onoff;
	}

	if(cardtype == avm_m1)
		return 0;

	if((b1_rd_reg(base, B1_STAT1(cardtype)) & 0x0f) != 0x01)
		return 5;

	return 0;
}

/* ------------------------------------------------------------- */
void b1_getrevision(avmcard *card)
{
	card->class = inb(card->port + B1_ANALYSE);
	card->revision = inb(card->port + B1_REVISION);
}


irqreturn_t b1_interrupt(int interrupt, void *devptr);

extern const struct file_operations b1ctl_proc_fops;

/* b1dma.c */
int b1pciv4_detect(avmcard *card);
int t1pci_detect(avmcard *card);

#endif /* _AVM_B1_H_ */
