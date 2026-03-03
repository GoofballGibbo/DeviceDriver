#include "asm/uaccess.h"
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioctl.h>

#define DEBUG // debug prints
#define IOCTL_MAGIC 'p' 
#define SET_CHAR _IOW(IOCTL_MAGIC, 0, char)
#define GET_CHAR _IOR(IOCTL_MAGIC, 1, char)

MODULE_DESCRIPTION("placeholder description"); // if you don't include this kbuild gets very mad
MODULE_LICENSE("GPL");                         // same here (why must you force me to use GPL)

static char stored_char = 'X';

static ssize_t mod_read(struct file* fd, char __user* buf, size_t nbytes, loff_t* offset) {
#ifdef DEBUG
    printk(KERN_INFO "reading %zu bytes\n", nbytes);
#endif

    for (int i = 0; i < nbytes; i++) {
        put_user(stored_char, &(buf[i]));
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

static long mod_ioctl(struct file *file, unsigned int cmd, unsigned long arg) { // written by Hazel
    char temp;
    switch (cmd) {
        case SET_CHAR:
            if (copy_from_user(&temp, (char _user *)arg, sizeof(char))) {
                return -EFAULT;
            }

            stored_char = temp;
            pr_info("character set to: %c\n", stored_char);
            break;

        case GET_CHAR:
            if (copy_to_user((char _user *)arg, &stored_char, sizeof(char))) {
                return -EFAULT;
            }
            pr_info("charater returned: %c\n", stored_char);
            break;
        
        default:
            return -EINVAL;
    }

    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = mod_read,
    .open = mod_open,
    .release = mod_release,
    .unlocked_ioctl = mod_ioctl,
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
