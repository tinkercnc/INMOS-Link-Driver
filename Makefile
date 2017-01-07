###################################################################
#
# New style 'kbuild' makefile for the INMOS Transputer link driver
#
# John Snowdon <john.snowdon@newcastle.ac.uk>, November 2014
#
###################################################################

obj-m := link-driver.o avm-pci.o
#obj-m := avm-pci.o
#obj-$(CONFIG_ISDN_DRV_AVMB1_B1ISA)	+= b1isa.o b1.o
#obj-$(CONFIG_ISDN_DRV_AVMB1_B1PCI)	+= b1pci.o b1.o b1dma.o
#obj-$(CONFIG_ISDN_DRV_AVMB1_B1PCMCIA)	+= b1pcmcia.o b1.o
#obj-$(CONFIG_ISDN_DRV_AVMB1_AVM_CS)	+= avm_cs.o
#obj-$(CONFIG_ISDN_DRV_AVMB1_T1ISA)	+= t1isa.o b1.o
#obj-$(CONFIG_ISDN_DRV_AVMB1_T1PCI)	+= t1pci.o b1.o b1dma.o
#obj-$(CONFIG_ISDN_DRV_AVMB1_C4)		+= c4.o b1.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	cp avm-pci.ko /lib/modules/`uname -r`/kernel/drivers/isdn/hardware/avm/
	cp link-driver.ko /lib/modules/`uname -r`/kernel/drivers/misc/
	depmod -a
	