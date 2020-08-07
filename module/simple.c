///////////////////////////////////////////////////////////////////////
//    Usage :                                                        //
//            make                                                   //
//            insmod simple.ko                                       //
//            gcc operate.c -o operate && ./operate                  //
//            # output: hello simple                                 //
///////////////////////////////////////////////////////////////////////

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define simple_MINORS (1)
#define MAX_BUFF_LEN (0x100)

static dev_t simple_chr_devt;
static struct cdev simple_cdev;
static int max_simple_MINORS = 1;
static struct class *simple_class;
static struct device *simple_device;

static char buff[MAX_BUFF_LEN];

static int _simple_open(struct inode *inode, struct file *file)
{
	memset(buff, 0, MAX_BUFF_LEN);
	printk("device open.\n");
	return 0;
}

static int simple_release(struct inode *inode, struct file *file)
{
	printk("device release.\n");
	return 0;
}

static ssize_t simple_read(struct file *file, char *buffer, size_t length, loff_t *offset)
{
	if(length < MAX_BUFF_LEN)
		copy_to_user(buffer, buff, length);
	return length;
}

static ssize_t simple_write(struct file *file, const char *buffer, size_t length, loff_t *offset)
{
	if(length < MAX_BUFF_LEN)
		copy_from_user(buff, buffer, length);
	return length;
}

static long simple_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
	if(command == 0x123)
		return printk("hello world!\n");
	else
		return printk("unkonw command.\n");
}

static const struct file_operations simple_fops = {
	.owner			= THIS_MODULE,
	.read			= simple_read,
	.write 			= simple_write,
	.open 			= _simple_open,
	.unlocked_ioctl	= simple_ioctl,
	.release		= simple_release,
};

static int __init simple_init(void)
{
	int result = -ENOMEM;
	result = alloc_chrdev_region(&simple_chr_devt, 0, simple_MINORS, "simple");
	if (result < 0){
		printk("alloc_chrdev_region failed.\n");
		goto unregister_chrdev;
	}

	cdev_init(&simple_cdev, &simple_fops);

	result = cdev_add(&simple_cdev, simple_chr_devt, max_simple_MINORS);
	if (result){
		printk("cdev_add failed.\n");
		goto unregister_chrdev;
	}

	simple_class = class_create(THIS_MODULE, "simple");
	if(IS_ERR(simple_class)){
		printk("class_create failed.\n");
		result = PTR_ERR(simple_class);
		goto unregister_chrdev;
	}

	simple_device = device_create(simple_class, NULL, simple_chr_devt, NULL, "simple");
	if(IS_ERR(simple_device)){
		printk("device_create failed.\n");
		result = PTR_ERR(simple_device);
		goto destroy_class;
	}
	return 0;


	destroy_class:
		class_destroy(simple_class);
	unregister_chrdev:
		unregister_chrdev_region(simple_chr_devt, simple_MINORS);
	
	return result;
}

static void simple_exit(void)
{
	device_destroy(simple_class, simple_chr_devt);
	class_destroy(simple_class);
	cdev_del(&simple_cdev);
	unregister_chrdev_region(simple_chr_devt, simple_MINORS);
}


MODULE_DESCRIPTION("SIMPLE~~");
MODULE_LICENSE("GPL");
module_init(simple_init);
module_exit(simple_exit);