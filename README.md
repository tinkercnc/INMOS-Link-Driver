# INMOS-Link-Driver

Updated INMOS Transputer link driver model for Linux Kernel 4.x.x.

This supports the AVM Active ISDN PCI and PCMCIA cards which are INMOS B004 compatible.

## Using the Driver

It works with 4.x kernel ix86 and amd64.

In order to avoid loading the original AVM modules at bootup please enter the following lines to
/etc/modprobe.d/blacklist.conf

    # avm b1 isdn pci card
    blacklist kernelcapi
    blacklist b1
    blacklist b1dma
    blacklist b1pci
    blacklist avm-pci
    blacklist link-driver
.

Compiling with:

    make clean
    make
    sudo make install
    sudo modprobe avm-pci

or for the pcmcia version

    sudo modprobe avm-link-cs
