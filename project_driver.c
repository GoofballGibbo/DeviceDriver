#include "asm/uaccess.h"
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define DEBUG // debug prints

MODULE_DESCRIPTION("placeholder description"); // if you don't include this kbuild gets very mad
MODULE_LICENSE("GPL");                         // same here (why must you force me to use GPL)

typedef struct{
   int index;
   char ch;
}expected_char;

static expected_char fifo(FIFO_SIZE);

static int fifo_head = 0;
static int fifo_tail = 0;
static int fifo_count = 0;
static DEFINE_SPINLOCK(wpm);
static DECLARE_WAIT_QUEUE_HEAD(read_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_wait_queue);




static char device_buffer[BUFFER_SIZE];

static ssize_t drv_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
        
	size_t bytes_to_write;

	expected_char ec;

	unsigned long flags;

	

	if(count != sizeof(expected_char)){
            return -EINVALUE;
	}
	if(copy_to_user( buf, &ec, sizeof(expected_char))){
		return -EFAULT;
	}

 	if(wait_event_interruptible(wrtie_wait_queue, fifo_count < FIFOSIZE)){
           return -ERESTARTSYS;
	}

	spin_lock_irqsave(&wpm_lock, flags);

	fifo[fifo_tail] = ec;
	fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
	fifo_count ++;

	spin_unlock_irqrestore(&wpm_lock,flags);
	wake_up_interruptible(&read_wait_queue);

	return sizeof(expected_char);
}

int major;
dev_t dev;
static struct cdev cdev;
static struct class* cl;

static int mod_open(struct inode* inode, struct file* fileptr) {
    pr_info("Major device number: %d | Minor device number: %d\n", imajor(inode), iminor(inode));

    pr_info("Fileptr->f_pos : %lld\n", fileptr->f_pos);
    pr_info("Fileptr->f_mode : %lld\n", fileptr->f_mode);
    pr_info("Fileptr->f_flags: %lld\n", fileptr->f_flags);
    return 0;
}

static int mod_release(struct inode* inode, struct file* fileptr) {
    pr_info("The file has closed");
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = drv_read,
    .open = mod_open,
    .release = mod_release,
};

static int __init custom_init(void) {
    // get a device number (cat /proc/devices)
    if (alloc_chrdev_region(&dev, 0, 1, "uniproject")) {
        printk(KERN_ERR "device allocation failed!");
        return -ECANCELED;
    }

    // make a device class (ls /sys/class)
    if (!(cl = class_create("uniprojclass"))) {
        printk(KERN_ERR "class creation failed!");
        unregister_chrdev_region(dev, 1);
        return -ECANCELED;
    }

    // initialize chardevice
    cdev_init(&cdev, &fops);
    if (cdev_add(&cdev, dev, 1) < 0) {
        printk(KERN_ERR "chardevice init failed!");
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return -ECANCELED;
    }

    // create the device (ls /dev)
    if (!device_create(cl, NULL, dev, NULL, "uniprojdev")) {
        printk(KERN_ERR "device creation failed!");
        cdev_del(&cdev);
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return -ECANCELED;
    }

    major = MAJOR(dev);

#ifdef DEBUG
    printk(KERN_INFO "placeholder loaded (major num: %d)\n", major);
#endif

    return 0;
}
static void __exit custom_exit(void) {
    device_destroy(cl, dev);
    cdev_del(&cdev);
    class_destroy(cl);
    unregister_chrdev_region(dev, 1);

#ifdef DEBUG
    printk(KERN_INFO "placeholder ended\n");
#endif
}

module_init(custom_init);
module_exit(custom_exit);
