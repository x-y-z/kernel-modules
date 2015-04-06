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

static struct perf_event_attr cache_miss_event_attr = {
        .type           = PERF_TYPE_HARDWARE,
        .config         = PERF_COUNT_HW_CACHE_MISSES,
        .size           = sizeof(struct perf_event_attr),
        .pinned         = 1,
        .disabled       = 1,
};

static int __init bench_init(void)
{
	char * data;
	uint i;
	char res = 0;
	int cpu;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte, *pte_nl;

	struct mm_struct *mm;
	struct perf_event *cache_miss;

	u64 cache_misses_begin, cache_misses_end, enabled, running;

	pr_info("Allocating memory\n");
	data = vzalloc(PAGE_SIZE);
	if (!data) {
		pr_err("Failed to allocate memory");
		goto out;
	}

	/* Calculate PTE locations */
	mm = get_task_mm(current);
	if (!mm) {
		pr_err("Failed to get task mm\n");
		goto out_free;
	}

	pgd = pgd_offset(current->mm, (uintptr_t)data);
	if (pgd_none(*pgd) || pgd_bad(*pgd)) {
		pr_err("Bad or missing PGD\n");
		goto out_putmm;
	}

	pud = pud_offset(pgd, (uintptr_t)data);
	if (pud_none(*pud) || pud_bad(*pud)) {
		pr_err("Bad or missing PUD\n");
		goto out_putmm;
	}

	pmd = pmd_offset(pud, (uintptr_t)data);
	if (pmd_none(*pmd) || pmd_bad(*pmd)) {
		pr_err("Bad or missing PMD\n");
		goto out_putmm;
	}

	pte = pte_offset_map(pmd, (uintptr_t)data);
	if (!pte || pte_none(*pte)) {
		pr_err("Bad or missing PTE\n");
		goto out_putmm;
	}

	/* 2^6 = 64 is the size of cacheline,
	 * >> 7 means that the pte_nl is on a 'buddy' line */
	if ((((uintptr_t)data + 8 * PAGE_SIZE) >> 7) ==
	    ((uintptr_t)data >> PMD_SHIFT))
		pte_nl = pte_offset_map(pmd, (uintptr_t)(data + 8 * PAGE_SIZE));
	else
		pte_nl = pte_offset_map(pmd, (uintptr_t)(data - 8 * PAGE_SIZE));
	if (!pte_nl || pte_none(*pte_nl)) {
		pr_err("Bad or missing PTE_NL\n");
		goto out_unmappte;
	}


	/* Disable interrupts */
	cpu = get_cpu();
	/* Setup TLB miss, and cache miss counters */
	cache_miss = perf_event_create_kernel_counter(&cache_miss_event_attr,
		cpu, NULL, NULL, NULL);
	if (IS_ERR(cache_miss)) {
		pr_err("Failed to create kernel counter\n");
		goto out_putcpu;
	}

	perf_event_enable(cache_miss);

	/* Read TLB miss and Cache miss counters */
	cache_misses_begin = perf_event_read_value(cache_miss, &enabled, &running);

	for (i = 0; i < iterations; ++i) {
		/* Flush TLB entry */
		__flush_tlb_single((uintptr_t)data);

		/* Flush both PTE cachelines */
		clflush_cache_range(pte, 1);
		clflush_cache_range(pte_nl, 1);

		/* Access data to trigger page walk */
		res ^= data[0];

		/* Access PTE in second cacheline */
		res ^= pte_nl->pte;
	}

	/* Read the counters again */
	cache_misses_end = perf_event_read_value(cache_miss, &enabled, &running);

	/* Print results */
	pr_info("Cache misses: %llu (%llu - %llu)\n",
		cache_misses_end - cache_misses_begin,
		cache_misses_end, cache_misses_begin);

	/* Clean up counters */
	perf_event_disable(cache_miss);

	perf_event_release_kernel(cache_miss);

out_putcpu:
	/* Enable interrupts */
	put_cpu();
	pte_unmap(pte_nl);
out_unmappte:
	pte_unmap(pte);
out_putmm:
	mmput(mm);
out_free:
	pr_info("Freeing allocated memory\n");
	vfree(data);
out:
	return 0;
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
