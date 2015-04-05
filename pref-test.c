#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#ifndef CONFIG_X86
# error "This module only works on X86"
#endif

MODULE_LICENSE("GPL");

static uint iterations = 1000;
module_param(iterations, uint, S_IRUGO);

static int __init fake_init(void)
{
	char * data;
	uint i;
	char res = 0;
	ulong irqs;

	printk(KERN_INFO "Allocating memory\n");
	data = vmalloc(PAGE_SIZE);

	/* Setup TLB miss, and cache miss counters */

	/* Disable interrupts */
	local_irq_save(irqs);

	/* Read TLB miss and Cache miss counters */

	for (i = 0; i < iterations; ++i) {
		/* Flush TLB entry */

		/* Flush both PTE cachelines */

		/* Access data to trigger page walk */
		res ^= data[0];

		/* Access PTE in second cacheline */
	}

	/* Read the counters again */

	/* Enable interrupts */
	local_irq_restore(irqs);

	/* Clean up counters */

	printk(KERN_INFO "Freeing allocated memory\n");
	vfree(data);
	return 0;
}

static void __exit fake_exit(void)
{
	printk(KERN_ALERT "Goodbye, world\n");
}

module_init(fake_init);
module_exit(fake_exit);
