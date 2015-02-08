#include <linux/module.h>  /* Needed by all modules */
#include <linux/kernel.h>  /* Needed for KERN_ALERT */
#include <linux/of.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/hwmon.h>
#include <linux/debugfs.h>
#include <linux/spmi.h>
#include <linux/of_irq.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/hwmon-sysfs.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/fs.h> /* for file operations structure */
#include <asm/uaccess.h> /* for put_user */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yan Michalevsky, 2015");
MODULE_DESCRIPTION("Power sampling");

/*
	Use
	# dd if=/dev/<DEVICE_NAME>
	to read instead of cat, since 'dd' catches CTRL+C signal
	and releases device, while 'cat' keeps the device busy.
*/

#ifdef DEBUG
#define TRACE(x) x
#else
#define TRACE(x)
#endif

/* File operations */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define DEVICE_NAME "iadc_dev"

/* Max device message size */
#define BUFFER_LEN (8192)
static char message[BUFFER_LEN + 1] = "";

static int major; /* module major assigned by kernel */
static int device_open_refcount = 0; /* is device open */

/* Set callbacks for file operations */
struct file_operations fops = {
	read: device_read,
	write: device_write,
	open: device_open,
	release: device_release
};

int init_module(void)
{
	printk(KERN_INFO "Loading power measurement module\n");

	major = register_chrdev(0, DEVICE_NAME, &fops);
	if (major < 0) {
		printk(KERN_ALERT "Registering char device %s failed with %d\n", DEVICE_NAME, major);
		return major;
	}
	
	printk(KERN_INFO "I was assigned major number %d. To talk to\n", major);
	printk(KERN_INFO "the driver, create a dev file with\n");
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, major);
	printk(KERN_INFO "Try various minor numbers. Try to cat and echo to\n");
	printk(KERN_INFO "the device file.\n");
	printk(KERN_INFO "Remove the device file and module when done.\n");
	
	// A non 0 return means init_module failed; module can't be loaded.
	return 0;
}

void cleanup_module(void)
{
  printk(KERN_INFO "Unloading power measurement module\n");
	printk(KERN_INFO "Unregistering device %s\n", DEVICE_NAME);
	unregister_chrdev(major, DEVICE_NAME);
}

static int init_adc( void )
{
	int ret;

	if ( (ret = qpnp_iadc_is_ready()) != 0 ) {
		printk( KERN_ALERT "Device qpnp_iadc is not ready\n" );
		return ret;
	}

	TRACE( printk(KERN_DEBUG "ADC is ready\n") );

#if 0
	ret = qpnp_iadc_enable(true);
	if (ret) {
		printk(KERN_ALERT "Failed enabling iadc (%d)\n", ret);
		return ret;
	}

	printk(KERN_DEBUG "iadc enabled\n");
#endif

	return 0;
}

static int release_adc( void )
{
	int ret = 0;

#if 0
	ret = qpnp_iadc_enable(false); /* disable ADC */
	if (ret < 0) {
		printk(KERN_ALERT "Failed disabling ADC\n");
	}
#endif

	return ret;
}

int device_open(struct inode* inode, struct file* file)
{
	int ret;
	TRACE( printk(KERN_DEBUG "iadc device_open\n") );
	
	if (device_open_refcount) {
		TRACE( printk(KERN_DEBUG "Device %s is busy\n", DEVICE_NAME) );
		return -EBUSY;
	}

	ret = init_adc();
	if (ret) {
		printk(KERN_ALERT "Failed initializing ADC (%d)\n", ret);
		return ret;
	}

	device_open_refcount++;
	try_module_get(THIS_MODULE); /* increment reference count */

	TRACE( printk(KERN_DEBUG "Opened device %s\n", DEVICE_NAME) );
	return 0;
}

static int read_adc_sample(int32_t* data)
{ 
	int ret;
	struct qpnp_iadc_result result;

	ret =	qpnp_iadc_read(INTERNAL_RSENSE, &result);
	if (ret) {
		printk(KERN_ALERT "Failed reading from ADC (%d)\n", ret);
		return ret;
	}

	*data = result.result_ua;

	return 0;
}

static int device_release(struct inode* inode, struct file* file)
{
	TRACE( printk(KERN_DEBUG "iadc device_release\n") );

	(void) release_adc();

	device_open_refcount--;
	module_put(THIS_MODULE); /* decrement reference count */
	TRACE( printk(KERN_DEBUG "Released device %s\n", DEVICE_NAME) );
	return 0;
}

static 
ssize_t device_read(struct file* file, /* unused */
										char* buffer, /* buffer to fill with data */
										size_t length, /* buffer size */
										loff_t* offset) /* unused */
{
	unsigned int bytes_read = 0;
	int ret;
	int32_t data;
	int msg_len;
	const char* message_ptr = NULL;

	TRACE( printk(KERN_DEBUG "iadc device_read: requested %u bytes\n", length) );

	while ( length > 0 ) {

		ret = read_adc_sample(&data);
		if (ret != 0) {
			printk(KERN_ALERT "Error while reading ADC data (%d)\n", ret);
			break;
		}
		
		msg_len = snprintf(message, BUFFER_LEN, "%d\n", data);
		
		/* skip messages that overflow the buffer */
		if (length - msg_len < 0) {
			break;
		}

		message_ptr = message;
	
		while ( (length > 0) && (msg_len > 0) ) {
			/* 
			 * The buffer is in the user data segment, not the kernel 
			 * segment so "*" assignment won't work.  We have to use 
			 * put_user which copies data from the kernel data segment to
			 * the user data segment. 
			 */
			put_user( *(message_ptr++), buffer++);
			--msg_len;
			--length;
			++bytes_read;
		}
	} /* while not read length */

	TRACE( printk(KERN_DEBUG "Read %u bytes\n", bytes_read) );
	return bytes_read;
}

static ssize_t device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	printk(KERN_ALERT "Write operation unsupported by %s\n", DEVICE_NAME);
	return -EINVAL;
}
