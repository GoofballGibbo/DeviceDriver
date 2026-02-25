#include <linux/kernel.h>
#include <linux/module.h>

MODULE_DESCRIPTION("placeholder description"); // if you don't include this kbuild gets very mad
MODULE_LICENSE("MIT");                         // same here

static int __init custom_init(void) {
    printk(KERN_INFO "placeholder loaded\n");
    return 0;
}
static void __exit custom_exit(void) { printk(KERN_INFO "placeholder ended\n"); }

module_init(custom_init);
module_exit(custom_exit);
