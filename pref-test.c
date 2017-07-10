#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/random.h>

#define block_start_pfn(pfn, order)	round_down(pfn, 1UL << (order))
#define block_end_pfn(pfn, order)	ALIGN((pfn) + 1, 1UL << (order))
#define pageblock_start_pfn(pfn)	block_start_pfn(pfn, pageblock_order)
#define pageblock_end_pfn(pfn)		block_end_pfn(pfn, pageblock_order)

MODULE_LICENSE("GPL");

static uint nid_to_scan = 0;
module_param(nid_to_scan, uint, S_IRUGO);

struct pg_scan_control {
	int nid;
	unsigned long scan_pfn;
	int remain_buf_len;
	int last_migratetype;
	unsigned long last_pfn;
};

char * const migratetype_names[MIGRATE_TYPES] = {
	"Unmovable",
	"Movable",
	"Reclaimable",
	"HighAtomic",
#ifdef CONFIG_CMA
	"CMA",
#endif
#ifdef CONFIG_MEMORY_ISOLATION
	"Isolate",
#endif
};

/* Return a pointer to the bitmap storing bits affecting a block of pages */
static inline unsigned long *get_pageblock_bitmap(struct page *page,
							unsigned long pfn)
{
#ifdef CONFIG_SPARSEMEM
	return __pfn_to_section(pfn)->pageblock_flags;
#else
	return page_zone(page)->pageblock_flags;
#endif /* CONFIG_SPARSEMEM */
}

static inline int pfn_to_bitidx(struct page *page, unsigned long pfn)
{
#ifdef CONFIG_SPARSEMEM
	pfn &= (PAGES_PER_SECTION-1);
	return (pfn >> pageblock_order) * NR_PAGEBLOCK_BITS;
#else
	pfn = pfn - round_down(page_zone(page)->zone_start_pfn, pageblock_nr_pages);
	return (pfn >> pageblock_order) * NR_PAGEBLOCK_BITS;
#endif /* CONFIG_SPARSEMEM */
}

static __always_inline unsigned long __get_pfnblock_flags_mask(struct page *page,
					unsigned long pfn,
					unsigned long end_bitidx,
					unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long word;

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG-1);

	word = bitmap[word_bitidx];
	bitidx += end_bitidx;
	return (word >> (BITS_PER_LONG - bitidx - 1)) & mask;
}

unsigned long get_pfnblock_flags_mask(struct page *page, unsigned long pfn,
					unsigned long end_bitidx,
					unsigned long mask)
{
	return __get_pfnblock_flags_mask(page, pfn, end_bitidx, mask);
}

static struct page *__pageblock_pfn_to_page(unsigned long start_pfn,
				     unsigned long end_pfn, struct zone *zone)
{
	struct page *start_page;
	struct page *end_page;

	/* end_pfn is one past the range we are checking */
	end_pfn--;

	if (!pfn_valid(start_pfn) || !pfn_valid(end_pfn))
		return NULL;

	start_page = pfn_to_page(start_pfn);

	if (page_zone(start_page) != zone)
		return NULL;

	end_page = pfn_to_page(end_pfn);

	/* This gives a shorter code than deriving page_zone(end_page) */
	if (page_zone_id(start_page) != page_zone_id(end_page))
		return NULL;

	return start_page;
}

static inline struct page *pageblock_pfn_to_page(unsigned long start_pfn,
				unsigned long end_pfn, struct zone *zone)
{
	if (zone->contiguous)
		return pfn_to_page(start_pfn);

	return __pageblock_pfn_to_page(start_pfn, end_pfn, zone);
}

static int pageblock_scan_zone(struct zone *scan_zone,
		struct pg_scan_control *cc)
{
	const unsigned long scan_start_pfn = max(scan_zone->zone_start_pfn,
			cc->scan_pfn);
	const unsigned long scan_end_pfn = zone_end_pfn(scan_zone);
	unsigned long block_start_pfn;
	unsigned long block_end_pfn;
	struct page *page;
	int pageblock_migratetype;

	block_start_pfn = pageblock_start_pfn(scan_start_pfn);
	if (block_start_pfn < scan_start_pfn)
		block_start_pfn = scan_start_pfn;

	block_end_pfn = pageblock_end_pfn(scan_start_pfn);

	for (; block_end_pfn < scan_end_pfn;
		 block_start_pfn = block_end_pfn,
		 block_end_pfn += pageblock_nr_pages) {
		page = pageblock_pfn_to_page(block_start_pfn, block_end_pfn, scan_zone);

		if (!page)
			continue;

		cc->scan_pfn = block_start_pfn;

		pageblock_migratetype = get_pageblock_migratetype(page);

		if (cc->last_migratetype != pageblock_migratetype) {
			if (cc->last_migratetype < 0) {
				cc->last_migratetype = pageblock_migratetype;
				cc->last_pfn = block_start_pfn;
				continue;
			}

			pr_info("[%lx, %lx): %s\n", cc->last_pfn, block_start_pfn,
				migratetype_names[cc->last_migratetype]);

			cc->last_migratetype = pageblock_migratetype;
			cc->last_pfn = block_start_pfn;
		}
	}

	/* output last part of the zone  */
	pr_info("[%lx, %lx): %s\n", cc->last_pfn, block_start_pfn,
		migratetype_names[cc->last_migratetype]);

	cc->last_migratetype = -1;
	cc->scan_pfn = cc->last_pfn = scan_end_pfn;

	return 0;
}

int pageblock_scan_node(int nid)
{
	pg_data_t *scan_node = NODE_DATA(nid);
	int zoneid;
	struct zone *zone;
	static struct pg_scan_control cc = {
		.nid = -1,
	};
	int err = 0;

	cc.nid = nid;
	cc.last_migratetype = -1;

	for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {
		zone = &scan_node->node_zones[zoneid];
		if (!populated_zone(zone))
			continue;

		if (cc.scan_pfn >= zone_end_pfn(zone))
			continue;

		if (pageblock_scan_zone(zone, &cc) < 0)
			break;
	}

	return err;
}

static int __init bench_init(void)
{
	return pageblock_scan_node(nid_to_scan);
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
