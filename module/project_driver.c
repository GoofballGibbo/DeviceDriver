#include "asm/uaccess.h"
#include "linux/fs.h"
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>

#define DEBUG // debug prints

#define DEVFILE "/dev/ttyACM0" // terrible solution. if i can do better i will

MODULE_DESCRIPTION("a driver for a uni project"); // if you don't include this kbuild gets very mad
MODULE_LICENSE("GPL");                            // same here (why must you force me to use GPL)


int major;               // our device's major number
dev_t dev;               // similar to above i think..? idk
static struct cdev cdev; // character device
static struct class* cl; // device class (needed to make device)

static struct usb_device* usb_dev = NULL; // usb_device struct. if we have this, the device is present

static struct file* usb_fp = NULL; // file pointer for the terrible solution

static char saved[2];   // saved bytes if we get less than 3
static int saved_n = 0; // number for above


// write to the actual device. should only ever receive 3 bytes but i'm being safe
static int write_led(char rgb[3], size_t n) {
    if (!usb_dev) {
        printk(KERN_ERR "no device!");
        return -ENODEV;
    }

    if (IS_ERR(usb_fp)) {
        printk(KERN_ERR "could not open ttyACM0!");
        return -ENOENT;
    }

    loff_t off = 0;
    kernel_write(usb_fp, rgb, n, &off);

    return 0;
}

static ssize_t mod_read(struct file* fd, char __user* buf, size_t nbytes, loff_t* offset) {
#ifdef DEBUG
    printk(KERN_INFO "reading %zu bytes\n", nbytes);
#endif

    for (int i = 0; i < nbytes; i++) {
        put_user('A', &(buf[i]));
    }
    return nbytes;
}

static ssize_t mod_write(struct file* fd, const char __user* buf, size_t nbytes, loff_t* offset) {
    // if we have less than 3 bytes total, we save them until we have enough
    if (saved_n + nbytes < 3) {
        if (copy_from_user(saved + saved_n, buf, nbytes)) {
            return -EIO;
        }
        saved_n += nbytes;

        return nbytes; // and lie to the OS since otherwise it will just try again
    }

    char msg[3]; // the bytes we're about to send

    if (saved_n > 0) {
        memcpy(msg, saved, saved_n);
    }

    int to_write = 3 - saved_n; // how many bytes from buf we're actually reading

    if (copy_from_user(msg + saved_n, buf, to_write)) {
        return -EIO;
    }

    saved_n = 0;

    write_led(msg, sizeof(msg));

#ifdef DEBUG
    printk(KERN_INFO "wrote %*ph\n", 3, msg);
#endif

    return to_write; // at least on my system, if you return less than nbytes it just tries again with the next bytes. convienent!
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
    .write = mod_write,
    .open = mod_open,
    .release = mod_release,
};


// this gets called whenever a USB device matches the below table usbdid. it's 99% the one we want so we just take it
static int usb_probe(struct usb_interface* intf, const struct usb_device_id* id) {
#ifdef DEBUG
    printk(KERN_INFO "usb plugged in\n");
#endif

    if (usb_dev) {
        return -ENOMEM; // unless we already have one. then we tell the kernel no
    }

    usb_dev = interface_to_usbdev(intf);

    usb_fp = filp_open(DEVFILE, O_WRONLY, 0); // and just open the file now so we don't keep reopening it
                                              // (i hate this solution btw but the usb libraries wer not playing nice with me)

    if (!usb_fp || IS_ERR(usb_fp)) {
        printk(KERN_ERR "could not open ttyACM0!");
        return -ENOENT;
    }

    loff_t off = 0;
    kernel_write(usb_fp, "\n", 1, &off); // since during testing my bytes would be randomnly offset, the board waits for 1 newline first to start the count

    return 0;
}

// called on disconnect. simple enough
static void usb_dc(struct usb_interface* intf) {
#ifdef DEBUG
    printk(KERN_INFO "disconnecting usb\n");
#endif

    filp_close(usb_fp, NULL);

    usb_dev = NULL;
    usb_fp = NULL;
}

struct usb_device_id usbdid[] = {{USB_DEVICE(0x2e8a, 0x0009)}, {}}; // tells the kernel which USB devices we're making a driver for

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

    if (usb_fp && !IS_ERR(usb_fp)) { // does NULL count as IS_ERR? no clue
        filp_close(usb_fp, NULL);
    }

#ifdef DEBUG
    printk(KERN_INFO "placeholder ended\n");
#endif
}

module_init(custom_init);
module_exit(custom_exit);
