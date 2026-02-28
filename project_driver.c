#include "asm/uaccess.h"
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define DEBUG // debug prints

MODULE_DESCRIPTION("placeholder description"); // if you don't include this kbuild gets very mad
MODULE_LICENSE("GPL");                         // same here (why must you force me to use GPL)

static ssize_t mod_read(struct file* fd, char __user* buf, size_t nbytes, loff_t* offset) {
#ifdef DEBUG
    printk(KERN_INFO "reading %zu bytes\n", nbytes);
#endif

    for (int i = 0; i < nbytes; i++) {
        put_user('A', &(buf[i]));
    }
    return nbytes;
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

struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = mod_read,
    .open = mod_open,
    .release = mod_release,
};

static int __init custom_init(void) {
    // get a device number (cat /proc/devices)
    if (alloc_chrdev_region(&dev, 0, 1, "uniproject")) {
        printk(KERN_ERR "device allocation failed!");
        return -1;
    }

    // make a device class (ls /sys/class)
    if (!(cl = class_create("uniprojclass"))) {
        printk(KERN_ERR "class creation failed!");
        unregister_chrdev_region(dev, 1);
        return -1;
    }

    // initialize chardevice
    cdev_init(&cdev, &fops);
    if (cdev_add(&cdev, dev, 1) == -1) {
        printk(KERN_ERR "chardevice init failed!");
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return -1;
    }

    // create the device (ls /dev)
    if (!device_create(cl, NULL, dev, NULL, "uniprojdev")) {
        printk(KERN_ERR "device creation failed!");
        cdev_del(&cdev);
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return -1;
    }

    major = MAJOR(dev);

#ifdef DEBUG
    printk(KERN_INFO "placeholder loaded (%d)\n", major);
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
