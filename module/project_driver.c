#include "asm/uaccess.h"
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#define DEBUG // debug prints

MODULE_DESCRIPTION("a driver for a uni project"); // if you don't include this kbuild gets very mad
MODULE_LICENSE("GPL");                            // same here (why must you force me to use GPL)

int major;
dev_t dev;
static struct cdev cdev;
static struct class* cl;


static ssize_t mod_read(struct file* fd, char __user* buf, size_t nbytes, loff_t* offset) {
#ifdef DEBUG
    printk(KERN_INFO "reading %zu bytes\n", nbytes);
#endif

    for (int i = 0; i < nbytes; i++) {
        put_user('A', &(buf[i]));
    }
    return nbytes;
}

static int mod_open(struct inode* inode, struct file* fileptr) {
#ifdef DEBUG
    pr_info("Major device number: %d | Minor device number: %d\n", imajor(inode), iminor(inode));

    pr_info("Fileptr->f_pos : %lld\n", fileptr->f_pos);
    pr_info("Fileptr->f_mode : %u\n", fileptr->f_mode);
    pr_info("Fileptr->f_flags: %u\n", fileptr->f_flags);
#endif
    return 0;
}

static int mod_release(struct inode* inode, struct file* fileptr) {
#ifdef DEBUG
    pr_info("The file has closed\n");
#endif
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = mod_read,
    .open = mod_open,
    .release = mod_release,
};


static int usb_probe(struct usb_interface* intf, const struct usb_device_id* id) {
#ifdef DEBUG
    printk(KERN_INFO "usb plugged in\n");
#endif
    return 0;
}

static void usb_dc(struct usb_interface* intf) {
#ifdef DEBUG
    printk(KERN_INFO "disconnecting usb\n");
#endif
}

struct usb_device_id usbdid[] = {{USB_DEVICE(0x2e8a, 0x0009)}, {}};

static struct usb_driver usbd = {
    .name = "uniproject",
    .probe = usb_probe,
    .disconnect = usb_dc,
    .id_table = usbdid,
};


static int __init custom_init(void) {
    // get a device number (cat /proc/devices)
    if (alloc_chrdev_region(&dev, 0, 1, "uniproject")) {
        printk(KERN_ERR "device allocation failed!\n");
        return -ECANCELED;
    }

    // make a device class (ls /sys/class)
    if (!(cl = class_create("uniprojclass"))) {
        printk(KERN_ERR "class creation failed!\n");
        unregister_chrdev_region(dev, 1);
        return -ECANCELED;
    }

    // initialize chardevice
    cdev_init(&cdev, &fops);
    if (cdev_add(&cdev, dev, 1) < 0) {
        printk(KERN_ERR "chardevice init failed!\n");
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return -ECANCELED;
    }

    // create the device (ls /dev)
    if (!device_create(cl, NULL, dev, NULL, "uniprojdev")) {
        printk(KERN_ERR "device creation failed!\n");
        cdev_del(&cdev);
        class_destroy(cl);
        unregister_chrdev_region(dev, 1);
        return -ECANCELED;
    }

    if (usb_register(&usbd) < 0) {
        printk(KERN_ERR "USB registration failed!\n");
        device_destroy(cl, dev);
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
    usb_deregister(&usbd);
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
