#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

static uint iterations;
module_param(iterations, uint, S_IRUGO);

static int __init fake_init(void)
{
	printk(KERN_ALERT "Hello, world\n");
	return 0;
}

static void __exit fake_exit(void)
{
	printk(KERN_ALERT "Goodbye, world\n");
}

module_init(fake_init);
module_exit(fake_exit);
