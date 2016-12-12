/************************************************************************
*
*	Device Driver for INMOS-B004/B008 compatible link interfaces
*
*	Copyright (C) 1993,94,95 by Christoph Niemann
*				 niemann@swt.ruhr-uni-bochum.de
*	based on /linux/kernel/drivers/char/lp.c
*
*	The driver may be copied under the same conditions as the
*	Linux operating system
*
*
*	Version 0.8a, for Linux 1.2.X, March 1996.
*	(1) Module support added by Mark Bowyer <mark@lindhard.demon.co.uk>
*
*	Version 0.9  for Linux 2.2.x   November 2000   Mark Bowyer
*	(1) Kernel 2.2.X support by Mark Bowyer, November 2000
*
*	Version 0.91, for Linux 2.2.X  December 2000.  -Kev
*	(1) Removed assembler code for link management. There is
*       no need for this style on fast CPU systems.
*   (2) Removed reference to link adapter mapped to I/O space 0x300
*       because it gets confused with EtherExpress Ethernet cards.
*   (3) Linked all timers to system jiffie counter. This will remove the
*       problem of CPU dependancies which don't work when faster newer
*       CPU's are running this code.
*   (4) Changed Read and Write routines to be more efficient, shorter
*       now everything is timed using jiffie counters.
*
*	Version 0.92, for Linux 2.4.X, March 2002.
*		by Mark Bowyer <m.bowyer@ieee.org>
*	(1) fixed file_operations for 2.4.X (tested on RH 2.4.9 kernel)
*	(2) removed assembler remnant in LINK_REALLY_SLOW_C012
*
*	Version 0.93, for Linux 3.X.X, November 2014.
*		by John Snowdon <john.snowdon@newcastle.ac.uk>
*	(1) updated to new Kernel kbuild scheme (tested on i386 Debian 7, 3.2.63-2+deb7u1)
*	(2) removed non-module defines (no longer possible to build into a static kernel)
*	(3) updated to current Linux kernel header file locations
*	(4) additional debugging code for monitoring data read/written to link device
*	(5) updated to new ioctl interface struct type (.ioctl replaced by .unlocked_ioctl)
*	(6) tidied up some syntax to remove warnings in modern GCC compilers
*	(7) removed MOD_DEC and MOD_INC macros - no longer supported
*       (8) removed slow link defines - unless you're using a 386/486, these are now superfluous
*	TODO: Still using old register_chrdev calls - needs to be converted to cdev
*
*************************************************************************/

/* Copyright (C) 1992 by Jim Weigand, Linus Torvalds, and Michael K. Johnson */


#if !(defined( __i386__) || defined(__x86_64__))
#error This driver requires the Intel architecture
#endif


#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/stat.h>
#include <linux/ioport.h>
#include <linux/linkage.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include "avm-b1.h"
#include "link-driver.h"
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/version.h>
#include <generated/utsrelease.h>

MODULE_LICENSE("GPL");

char kernel_version[] = UTS_RELEASE;

/*
 * Define these symbols if you want to debug the code.
 */

#undef LINK_DEBUG 
#undef LINK_DEBUG_MORE

#define out		outb
#define in		inb

#ifdef LINK_DEBUG
#define DEB(x)		x
#define PRINTK(f, a...)	printk(f, ##a)
#define NO_DEB(x)
#else
#define DEB(x)
#define PRINTK(f, a...)
#define NO_DEB(x)	x
#endif

#ifdef LINK_DEBUG_MORE
#define DEB_MORE(x) x
#else
#define DEB_MORE(x)
#endif

#if !defined(SYMBOL_NAME_STR)
#if defined(__ELF__)
#define SYMBOL_NAME_STR(X) #X
#else
#define SYMBOL_NAME_STR(X) "_"#X
#endif
#endif


/* -------- driver information -------------------------------------- */

static DEFINE_MUTEX(link_mutex);
static struct class *link_class;
static unsigned int link_major = 0;		/* allocated */
static struct link_struct *link_devices = NULL;

//module_param_named(major, link_major, uint, 0);

/**** This structure defines each interface control ****/
static struct link_struct link_table[LINK_NO] =
{
	 { LINK_READ_ABORT | LINK_WRITE_ABORT, 0, 0, 0, 0,
	 		LINK_INIT_READ_TIMEOUT, LINK_INIT_WRITE_TIMEOUT, LINK_B004 },
	 { LINK_READ_ABORT | LINK_WRITE_ABORT, 0, 0, 0, 0,
	 		LINK_INIT_READ_TIMEOUT, LINK_INIT_WRITE_TIMEOUT, LINK_B004 },
};

static unsigned char link_ndevices = LINK_NO;	/* number of devices dectected */

/* ================================================================ */
/**** These are the board interface IO addresses the driver will search ****/
static const short link_base_addresses[] = { 0x150, 0x170, 0x190, 0 };

/*
static const char *link_name(dev_t minor)
{
	static char name[] = LINK_NAME"?";

	if (minor < link_ndevices)
	{
		name[4] = minor + '0';
		return name;
	}
	else
		return (const char*)0;
}
*/

/* ================================================================ */
const char *byte_to_binary(int x)
{
    static char b[9];
    int z;
    
    b[0] = '\0';
    
    for (z = 128; z > 0; z >>= 1)
    {
        strcat(b, ((x & z) == z) ? "1" : "0");
    }
    return b;
}

/* ================================================================ */
/*********************************************************************
 This function is used to generate a fixed delay count of 100 milli-
 seconds. The timer is used to generate fixed delay intervals for the
 hardware reset and analyse sequences of the Transputer.

 This code replaces the original code that relied on a tight CPU loop
 to generate the delay. Unfortunately the delay does not work very
 well when CPU speeds are increased to faster and faster types.
**********************************************************************/
static void link_delay(void)
{
	unsigned int	timer;
	timer = jiffies + (100 / (1000 / HZ));
	while (1)
	{
		if (jiffies > timer){
			break;
		}
		schedule();
	}
}

/* ================================================================ */
/****************************************************************************
 *
 * static void link_reset():
 *
 * reset transputer network.
 *
 ****************************************************************************/
static int link_reset(int minor)
{

	PRINTK("link-driver (link-reset): LINK(%d) resetting transputer. (0x%x)\n", minor, LINK_RESET(minor));

	PRINTK("link-driver (link-reset): LINK(%d) reset() data [0x%02x]\n", minor, LINK_DEASSERT_ANALYSE);
	out( LINK_DEASSERT_ANALYSE, LINK_ANALYSE(minor));
	link_delay();

	PRINTK("link-driver (link-reset): LINK(%d) reset() data [0x%02x]\n", minor, LINK_DEASSERT_RESET);
	out( LINK_DEASSERT_RESET, LINK_RESET(minor));
	link_delay();

	PRINTK("link-driver (link-reset): LINK(%d) reset() data [0x%02x]\n", minor, LINK_ASSERT_RESET);
	out( LINK_ASSERT_RESET, LINK_RESET(minor));
	link_delay();

	PRINTK("link-driver (link-reset): LINK(%d) reset() data [0x%02x]\n", minor, LINK_DEASSERT_RESET);
	out( LINK_DEASSERT_RESET, LINK_RESET(minor));
	link_delay();

	DEB_MORE(printk("link-driver (link-reset): LINK(%d) reset() success!\n\n", minor);)
	return 0;
}

/* ================================================================ */
/****************************************************************************
 *
 * static void link_analyse():
 *
 * switch transputer network to analyse mode.
 *
 ****************************************************************************/
static void link_analyse( const int minor )
{

	PRINTK("link-driver (link-analyse): LINK(%d) switching transputer to analyse mode.\n", minor);

	out( LINK_DEASSERT_ANALYSE, LINK_ANALYSE(minor));
	link_delay();

	out( LINK_ASSERT_ANALYSE, LINK_ANALYSE(minor));
	link_delay();

	out( LINK_ASSERT_RESET, LINK_RESET(minor));
	link_delay();

	out( LINK_DEASSERT_RESET, LINK_RESET(minor));
	link_delay();

	out( LINK_DEASSERT_ANALYSE, LINK_ANALYSE(minor));
	link_delay();

	DEB_MORE(printk("link-driver (link-analyse): LINK(%d) analyse() success!\n\n", minor);)
} /* link_analyse() */

/* ================================================================ */
/****************************************************************************
 *
 * static int link_read() - read bytes from the link interface.
 *
 * All reads from the Transputer link adapter are handled here. Simply it is
 * is a major loop that polls a number of exit conditions then reacts when one
 * becomes true.
 *
 * The loop will react to "count" bytes being read from the Link adapter
 * Rx side of the UART. Or it will react to a timer timeout, or a break
 * condition instructed by the system.
 *
 * Timing is derived from the jiffie counter which removes any dependancy
 * on CPU execution speed for timing.
 *
 * By default, LINK_READ_ABORT is not set.
 *
 * On exit: -EINTR		- Break due to interrupt.
 *          count		- Count of character actually read from Rx.
 *****************************************************************************/
static ssize_t link_read( struct file * file,
                          char * buf,
                          size_t count,
                          loff_t *ppos )
{
	const unsigned int	minor = MINOR(file->f_path.dentry->d_inode->i_rdev);
	               int	l_count;
	               int	max_sleep;
	               int	end;
	               int	copy_result;
	              char 	*temp = buf;
	      unsigned char	c;
	              char	buffer[LINK_MAX_BYTES];
	DEB(  unsigned int	link_total_bytes_read = 0; )

	DEB_MORE(printk("link-driver (link-read): LINK(%d) read() set to %d bytes\n", minor, (int)count);)

	if ( count < 0)
	{
		PRINTK("link-driver (link-read): LINK(%d) read() EINVAL! count = %d\n\n", minor, (int)count);
		return( -EINVAL );
	}

	if((LINK_F(minor) &= ~LINK_BUSY) == 0)
	{
		DEB_MORE(printk("link-driver (link-read): LINK(%d) read() EINVAL!\n\n", minor);)
		return( -EINVAL );
	}

	max_sleep = 0;
	while(count )
	{
		l_count = 0;
		end = count;
		if (end > LINK_MAX_BYTES) end = LINK_MAX_BYTES;

		while(end)
		{
			if(!b1_get_byte_stat(LINK_BASE(minor), &c)) {
				PRINTK("link-driver (link-read): LINK(%d) read() EINTR! Timeout!\n\n", minor);
				return( -EINTR );
			}
			DEB_MORE(PRINTK("link-driver (link-read): LINK(%d) read() data [0x%02x]\n", minor, c));
			buffer[l_count] = c;

			DEB(link_total_bytes_read++;)
			end--;
			l_count++;
			if(signal_pending(current))
			{
				DEB_MORE(printk("link-driver (link-read): LINK(%d) read() EINTR! interrupted!\n\n", minor);)
				return( -EINTR );
			}
		}

/**** Move received characters to user space ****/
		if (l_count)
		{
			copy_result = copy_to_user( temp, buffer, l_count );
			count -= l_count;
			temp += l_count;
		}
	}

	DEB_MORE(printk("link-driver (link-read): LINK(%d) read() success!\n", minor);)
	DEB_MORE(printk("link-driver (link-read): LINK(%d) [bytes read: %d]\n\n", minor, l_count);)

	return( temp - buf );

} /* LINK_read() */

/* ================================================================ */
/****************************************************************************
 *
 * static int link_write() - write to the link interface.
 *
 * This function works in a similar way to link_read() accept that it writes
 * characters out to the Transputers os-link UART (Tx). It also uses
 * jiffies to measure all timing intervals to remove reliance on CPU speeds
 * in tight loops as timing mechanisms.
 *
 * The function sets up a major running loop where it polls a number of events
 * to become true. Should any of these do so then the loop is exited with an
 * appropriate exit code or byte count.
 *
 * The main exit point will be all characters sent, however, UART lockups will
 * trigger a timeout event or a system signal will cause a premature exit.
 *
 * There is no limit to the number of bytes this function can send in a 
 * single call beyond memory limits. The count of bytes actually sent will be
 * returned to the caller on exit.
 *
 * On exit:  -EINT    - System requested exit.
 *           -EINVAL  - Timeout due to locked UART.
 *           count    - Number or bytes written to Transputer.
 *****************************************************************************/
static ssize_t link_write( struct file * file,
                           const char * buf,
                           size_t count,
                           loff_t *ppos )
{
	const unsigned int	minor = MINOR(file->f_path.dentry->d_inode->i_rdev);
	               int	l_count = 0;
	               int	size = count;
	               int	copy_result;
	               int	end;
	        const char	*cptr = buf;
	              char	buffer[LINK_MAX_BYTES];
	              
	              int loopcount = 0;

	DEB_MORE(printk("link-driver (link-write): LINK(%d) writing %d bytes to link %d.\n", minor, (int)count, minor);)

	if((LINK_F(minor) &= ~LINK_BUSY) == 0)
	{
		return( -EINVAL );
	}

	if(count < 0)
	{
		PRINTK("link-driver (link-write): LINK(%d) write() invalid argument: count = %d.\n", minor, (int)count);
		return( -EINVAL );
	}

	while(count)
	{
		l_count = 0;
		end = count;

		if (end > LINK_MAX_BYTES) end = LINK_MAX_BYTES;

		copy_result = copy_from_user( buffer, cptr, end );
		cptr += end;
		
		while(end)
		{
			
			if(b1_save_put_byte(LINK_BASE(minor), buffer[l_count])<0) {
				PRINTK("link-driver (link-write): LINK(%d) write(): Timed out waiting for Tx register\n", minor );
				return( -EINVAL );
			}
			DEB_MORE(PRINTK("link-driver (link-write): LINK(%d) write() data [0x%02x]\n", minor, buffer[l_count]));
			end--;
			l_count++;
			
			DEB_MORE(PRINTK("link-driver (link-write): LINK(%d) write(): Loop: %d\n", minor, loopcount));

			if(signal_pending( current ))
			{
				return( -EINTR );
			}
			
			loopcount++;
		}

		count -= l_count;
	}

	DEB_MORE(printk("link-driver (link-write): LINK(%d) write() success!\n", minor);)
	DEB_MORE(printk("link-driver (link-write): LINK(%d) [bytes remaining: %d | bytes written: %d]\n\n", minor, (int)count, l_count);)
	return( size - count );
} /* link_write() */

/* ================================================================ */
/****************************************************************************
 *
 * static int link_lseek()
 *
 ***************************************************************************/
static loff_t link_llseek(struct file * file, long long offset, int origin)
{
	return -ESPIPE;
}

/* ================================================================ */
/****************************************************************************
 *
 * static int link_open()
 *
 * open the link-device.
 *
 ***************************************************************************/
static int link_open(struct inode *inode, struct file *devfile)
{
	unsigned int major = imajor(inode);
	unsigned int minor = iminor(inode);

	struct link_struct *dev = NULL;
	
	if(major != link_major || minor < 0 || minor >= link_ndevices)
	{
		printk(KERN_WARNING "[target] "
			"link-driver (link-open): No device found with minor=%d and major=%d\n", 
			major, minor);
		return -ENODEV; /* No such device */
	}
	
	/* store a pointer to struct link_struct here for other methods */
	//dev = &link_devices[minor];
	dev = link_devices;
	devfile->private_data = dev; 
	
	if(inode->i_cdev != &dev->cdev)
	{
		printk(KERN_WARNING "link-driver (link-open): [target] open: internal error- No such device\n");
		return -ENODEV; /* No such device */
	}
	
	
	/*
	if (minor >= link_ndevices)
	{
		PRINTK("LINK not opened, minor device number >= %d.\n", link_ndevices);
		return -ENODEV;
	}
*/

	if (LINK_F(minor) & LINK_BUSY)
	{
		PRINTK("LINK not opened, LINK-board busy (minor = %d).\n", minor);
		return -EBUSY;
	}

	LINK_F(minor) |= LINK_BUSY;
	
	
	PRINTK( "link-driver (link-open): LINK(%d) opened.\n\n", minor);
	return 0;

} /* link_open() */

/* ================================================================ */
/****************************************************************************
 *
 * static int link_release()
 *
 * close the link device.
 *
 ****************************************************************************/
static int link_release(struct inode * inode, struct file * file)
{
	const unsigned int minor = MINOR(inode->i_rdev);

	if (minor >= link_ndevices)
	{
		PRINTK("link-driver (link-release): LINK not released, minor device number >= %d.\n", link_ndevices);
		return 0;
	}

	LINK_F(minor) &= ~LINK_BUSY;

	PRINTK("link-driver (link-release): LINK(%d) released.\n\n", minor);

	return 0;

} /* link_release() */

/* ================================================================ */
/****************************************************************************
 *
 * static int link_ioctl()
 *
 * This function performs the various ioctl() functions: resetting the
 * transputer, switching to analyse-mode, testing the status, changing
 * timeouts etc.
 *
 *****************************************************************************/
static int link_ioctl( struct file *file,
		               unsigned int cmd,
		               unsigned long arg )
{
	const unsigned int	minor = MINOR(file->f_path.dentry->d_inode->i_rdev);
	               int	result = arg;

	PRINTK("link-driver (link-ioctl): LINK(%d) ioctl, cmd: 0x%x, arg: 0x%x.\n", minor, cmd, (int) arg);

	if (minor >= link_ndevices || !(LINK_F(minor) & LINK_BUSY) )
	{
		DEB(
			if (minor >= link_ndevices)
				printk("link-driver (link-ioctl): LINK ioctl exit, minor >= %d.\n", link_ndevices );
			else
				printk("link-driver (link-ioctl): LINK ioctl exit, device not opened.\n" );
		)
		return -ENODEV;
	}

	switch (cmd)
	{
		case LINKRESET:		/* reset transputer */
			link_reset(minor);
			break;
		case LINKWRITEABLE:	/* can we write a byte to the C012 ? */
	 		return ( ( in(LINK_OSR(minor)) & LINK_WRITEBYTE) != 0 ); 
		case LINKREADABLE:	/* can we read a byte from C012 ? */
	 		return ( ( in(LINK_ISR(minor)) & LINK_READBYTE) != 0 ); 
		case LINKANALYSE:	/* switch transputer to analyse mode */
			link_analyse(minor);
			break;
		case LINKERROR:		/* test error-flag */
			return ( in(LINK_BASE(minor) + LINK_ERROR_OFFSET) & LINK_TEST_ERROR) ? 0 : 1;
		case LINKREADTIMEOUT:	/* set timeout for reading */
			result = LINK_READ_TIMEOUT(minor);
			LINK_READ_TIMEOUT(minor) = arg;
			break;
		case LINKWRITETIMEOUT:	/* set timeout for writing */
			result = LINK_WRITE_TIMEOUT(minor);
			LINK_WRITE_TIMEOUT(minor) = arg;
			break;
		case LINKREADABORT:	/* abort after a timeout ? */
			if ( arg )
				LINK_F(minor) |= LINK_READ_ABORT;
			else
				LINK_F(minor) &= ~LINK_READ_ABORT;
			break;
		case LINKWRITEABORT:	/* abort after a timeout ? */
			if ( arg )
				LINK_F(minor) |= LINK_WRITE_ABORT;
			else
				LINK_F(minor) &= ~LINK_WRITE_ABORT;
			break;
		default: result = -EINVAL;
	}

	PRINTK("link-driver (link-ioctl): LINK(%d) ioctl done.\n\n", minor);

	return result;

} 

/* ================================================================ */
static long
link_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;

	mutex_lock(&link_mutex);
	ret = link_ioctl(file, cmd, arg);
	mutex_unlock(&link_mutex);

	return ret;
}

/* ================================================================ */
struct file_operations link_fops = {
	.owner 		= THIS_MODULE,	
	.llseek 	= link_llseek,
	.read 		= link_read,
	.write 		= link_write,
	.unlocked_ioctl	= link_unlocked_ioctl,
	.open 		= link_open,
	.release 	= link_release
};

/* ================================================================ */
/* Setup and register the device with specific index (the index is also
 * the minor number of the device).
 * Device class should be created beforehand.
 */
int link_construct_device(unsigned int link_port, int minor)
{
	unsigned int i;
	int theerr = 0;
	//struct link_struct *linkdev = NULL;
	dev_t devno = 0;
	struct device *linkdev = NULL;
	
	/* Get a range of minor numbers (starting with 0) to work with */
	theerr = alloc_chrdev_region(&devno, 0, 1, LINK_NAME);
	if(theerr) {
		printk(KERN_WARNING "link-driver (link-construct-device):[target] alloc_chrdev_region() failed\n");
		goto err3;
	}
	/* */
	link_major = MAJOR(devno);
	/*
	link_major = register_chrdev(0, LINK_NAME, &link_fops);
	if(link_major) {
		printk(KERN_ERR "link-driver (link-construct-device): unable to get major for link interface.\n");
		goto err;
	}
	*/
	
	link_class = class_create(THIS_MODULE, LINK_NAME);
	if (IS_ERR(link_class)) {
		//unregister_chrdev(link_major, LINK_NAME);
		theerr =  PTR_ERR(link_class);
		goto err2;
	}
	
	linkdev = device_create(link_class, NULL, devno, NULL, LINK_NAME "%d", minor);
	if (IS_ERR(linkdev)) {
		theerr = PTR_ERR(linkdev);
		printk(KERN_WARNING "link-driver (link-construct-device):[target] Error %d while trying to create %s%d",
			theerr, LINK_NAME, minor);
		
		goto err1;
	}
	
	/* Allocate the array of devices */
	link_devices = (struct link_struct *)kzalloc(
		1 * sizeof(struct link_struct), 
		GFP_KERNEL);
	if (link_devices == NULL) {
		theerr = -ENOMEM;
		printk(KERN_WARNING "link-driver (link-construct-device):[target] Error %d while allocating memory for device %s%d", theerr, LINK_NAME, minor);
		goto err;
	}
	cdev_init(&link_devices->cdev, &link_fops);
	link_devices->cdev.owner = THIS_MODULE;
	
	theerr = cdev_add(&link_devices->cdev, devno, 1);
	if (theerr)
	{
		printk(KERN_WARNING "link-driver (link-construct-device):[target] Error %d while trying to add %s%d",
			theerr, LINK_NAME, minor);
		link_cleanup_module(1);
		goto err3;
	}
	
	if(link_port) {
		link_delay();
		b1_disable_irq(link_port);
		link_delay();
		LINK_BASE((int) minor) = link_port;
		LINK_ODR((int) minor) = LINK_BASE((int) minor) + LINK_ODR_OFFSET;
		LINK_ISR((int) minor) = LINK_BASE((int) minor) + LINK_ISR_OFFSET;
		LINK_OSR((int) minor) = LINK_BASE((int) minor) + LINK_OSR_OFFSET;
		link_reset(minor);
		link_delay();

		for(i = 0; i < LINK_MAXTRY; i++)
		{
			if(in(LINK_OSR((int) minor)) == LINK_WRITEBYTE)
			{
				out(LINK_BASE((int) minor) + B008_INT_OFFSET, 0);
				link_delay();
				if((in(LINK_BASE((int) minor) + B008_INT_OFFSET) & 0x0f) == 0)
					LINK_BOARDTYPE((int) minor) = LINK_B008;
				else
					LINK_BOARDTYPE((int) minor) = LINK_B004;
				printk("link-driver (link-construct-device): link%d at 0x0%x (polling) is a B00%s\n",
					minor,LINK_IDR((int) minor),
					LINK_BOARDTYPE((int) minor) == LINK_B004 ? "4" : "8");
				request_region(LINK_IDR((int) minor), 
					LINK_BOARDTYPE((int) minor) == LINK_B004 ? B004_IO_SIZE : B008_IO_SIZE,
					LINK_NAME);
				break;
			}
		}
		if (i >= LINK_MAXTRY) {
			printk("link-driver (link-construct-device): no interfaces found.\n");
			goto err;
		}
	}
	
	return 0;
err:
	unregister_chrdev(link_major, LINK_NAME);
err1:
	class_destroy(link_class);
err2:
	unregister_chrdev_region(MKDEV(link_major, 0), 1);
err3:
	return theerr;
}
EXPORT_SYMBOL(link_construct_device);

/* ================================================================ */
/****************************************************************************
 *
 * static int link_init()
 *
 * This function initializes the driver. It tries to detect the hardware
 * and sets up all relevant data structures.
 *
 ****************************************************************************/
static int __init link_init(void)
{
	//char *p;
	//char rev[32];
	int err = 0;
	
	//err = pci_register_driver(&b1pci_pci_driver);

	return err;
}
EXPORT_SYMBOL(link_init);

/* ================================================================ */
static void __exit link_exit(void)
{
	//int i;
	link_cleanup_module(1);
	//for (i = 0; i < link_ndevices; i++)
	//	release_region(LINK_IDR(i), LINK_BOARDTYPE(i) == LINK_B004 ? B004_IO_SIZE : B008_IO_SIZE);
}
EXPORT_SYMBOL(link_exit);

/* ================================================================ */
/* Load module *
int init_module(void)
{
	long dummy = 0;
	dummy = link_init(dummy);
	return 0;
}
*/

/* Destroy the device and free its buffer */
static void link_destroy_device(struct link_struct *dev, int minor, struct class *class)
{
	BUG_ON(dev == NULL || class == NULL);
	device_destroy(class, MKDEV(link_major, minor));
	cdev_del(&dev->cdev);
	//kfree(dev->data);
	//mutex_destroy(&dev->link_mutex);
	return;
}
EXPORT_SYMBOL(link_destroy_device);

/* ================================================================ */
void link_cleanup_module(int devices_to_destroy)
{
	//int i;
	
	/* Get rid of character devices (if any exist) */
	if(link_devices) {
		//for (i = 0; i < devices_to_destroy; ++i) {
			link_destroy_device(link_devices, 0, link_class);
		//}
		kfree(link_devices);
	}
	
	if(link_class)
		class_destroy(link_class);

	/* [NB] link_cleanup_module is never called if alloc_chrdev_region()
	 * has failed. */
	unregister_chrdev_region(MKDEV(link_major, 0), devices_to_destroy);
	return;
}
EXPORT_SYMBOL(link_cleanup_module);

/* ================================================================ */
module_init(link_init);
module_exit(link_exit);
