#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
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
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte, *pte_nl;
	struct mm_struct *mm;

	printk(KERN_INFO "Allocating memory\n");
	data = vzalloc(PAGE_SIZE);
	if (!data)
		goto out;

	/* Calculate PTE locations */
	mm = get_task_mm(current);
	if (!mm)
		goto out_free;

	pgd = pgd_offset(current->mm, (uintptr_t)data);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		goto out_putmm;

	pud = pud_offset(pgd, (uintptr_t)data);
	if (pud_none(*pud) || pud_bad(*pud))
		goto out_putmm;

	pmd = pmd_offset(pud, (uintptr_t)data);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		goto out_putmm;

	pte = pte_offset_map(pmd, (uintptr_t)data);
	if (!pte || pte_none(*pte))
		goto out_putmm;

	if ((((uintptr_t)data + 8 * PAGE_SIZE) >> PMD_SHIFT) ==
	    ((uintptr_t)data >> PMD_SHIFT))
		pte_nl = pte_offset_map(pmd, (uintptr_t)(data + 8 * PAGE_SIZE));
	else
		pte_nl = pte_offset_map(pmd, (uintptr_t)(data - 8 * PAGE_SIZE));
	if (!pte_nl || pte_none(*pte_nl))
		goto out_unmappte;

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

	/* Print results */

	/* Clean up counters */

out_unmappte_nl:
	pte_unmap(pte_nl);
out_unmappte:
	pte_unmap(pte);
out_putmm:
	mmput(mm);
out_free:
	printk(KERN_INFO "Freeing allocated memory\n");
	vfree(data);
out:
	return 0;
}

static void __exit fake_exit(void)
{
	printk(KERN_ALERT "Goodbye, world\n");
}

module_init(fake_init);
module_exit(fake_exit);
