#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#ifndef CONFIG_X86
# error "This module only works on X86"
#endif

MODULE_LICENSE("GPL");

static uint iterations = 1000;
module_param(iterations, uint, S_IRUGO);

static bool is_tlb_flush = 1;
module_param(is_tlb_flush, bool, S_IRUGO);

static struct perf_event_attr tlb_flush_event_attr = {
	.type           = PERF_TYPE_RAW,
	.config         = 0x01ae, /* STLB Flush: 0x20bd, DTLB Flush:0x01bd,
								 ITLB Flush: 0x01ae */
	.size           = sizeof(struct perf_event_attr),
	.pinned         = 1,
	.disabled       = 1,
};

static int __init bench_init(void)
{
	volatile char * data;
	uint i;
	char res = 0;
	int cpu;
	ulong irqs;

	struct perf_event *tlb_flush;

	u64 tlb_flushes_begin, tlb_flushes_end, enabled, running;

	unsigned long cr4;

	cr4 = native_read_cr4();

	pr_info("CR4 is: %lx\n", cr4);

	pr_info("Allocating memory\n");
	data = vzalloc(PAGE_SIZE);
	if (!data) {
		pr_err("Failed to allocate memory");
		goto out;
	}

	/* Access data to put it inside TLB  */
	res = data[0];


	/* Disable interrupts */
	cpu = get_cpu();
	/* Setup TLB miss, and cache miss counters */
	tlb_flush = perf_event_create_kernel_counter(&tlb_flush_event_attr,
		cpu, NULL, NULL, NULL);
	if (IS_ERR(tlb_flush)) {
		pr_err("Failed to create kernel counter\n");
		goto out_putcpu;
	}

	perf_event_enable(tlb_flush);

	local_irq_save(irqs);
	/* Read TLB miss and Cache miss counters */
	tlb_flushes_begin = perf_event_read_value(tlb_flush, &enabled, &running);

	for (i = 0; i < iterations; ++i) {
		/* Flush TLB entry */
		if (is_tlb_flush)
			/*__flush_tlb_single((uintptr_t)data);*/
			__flush_tlb_all();

		/* Access data to trigger page walk */
		res ^= data[0];

	}

	/* Read the counters again */
	tlb_flushes_end = perf_event_read_value(tlb_flush, &enabled, &running);
	local_irq_restore(irqs);

	/* Print results */
	pr_info("TLB Flushes: %llu (%llu - %llu)\n",
		tlb_flushes_end - tlb_flushes_begin,
		tlb_flushes_end, tlb_flushes_begin);

	/* Clean up counters */
	perf_event_disable(tlb_flush);

	perf_event_release_kernel(tlb_flush);

out_putcpu:
	/* Enable interrupts */
	put_cpu();
/*out_unmappte:*/
/*out_putmm:*/
/*out_free:*/
	pr_info("Freeing allocated memory\n");
	vfree((void*)data);
out:
	return 0;
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
