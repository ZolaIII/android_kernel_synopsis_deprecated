/* arch/arm/mach-msm/memory.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2012, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/memory_alloc.h>
#include <linux/memblock.h>
#include <asm/memblock.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <asm/cacheflush.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <mach/msm_memtypes.h>
#include <linux/hardirq.h>
#if defined(CONFIG_MSM_NPA_REMOTE)
#include "npa_remote.h"
#include <linux/completion.h>
#include <linux/err.h>
#endif
#include <mach/msm_iomap.h>
#include <mach/socinfo.h>
#include <linux/sched.h>

/* fixme */
#include <asm/tlbflush.h>
#include <../../mm/mm.h>

#if defined(CONFIG_ARCH_MSM7X27)
static void *strongly_ordered_page;
static char strongly_ordered_mem[PAGE_SIZE*2-4];

void __init map_page_strongly_ordered(void)
{
	long unsigned int phys;
	struct map_desc map[1];

	if (strongly_ordered_page)
		return;

	strongly_ordered_page = (void*)PFN_ALIGN((int)&strongly_ordered_mem);
	phys = __pa(strongly_ordered_page);

	map[0].pfn = __phys_to_pfn(phys);
	map[0].virtual = MSM_STRONGLY_ORDERED_PAGE;
	map[0].length = PAGE_SIZE;
	map[0].type = MT_MEMORY_SO;
	iotable_init(map, ARRAY_SIZE(map));

	printk(KERN_ALERT "Initialized strongly ordered page successfully\n");
}
#else
void map_page_strongly_ordered(void) { }
#endif

#if defined(CONFIG_ARCH_MSM7X27)
void write_to_strongly_ordered_memory(void)
{
	*(int *)MSM_STRONGLY_ORDERED_PAGE = 0;
}
#else
void write_to_strongly_ordered_memory(void) { }
#endif
EXPORT_SYMBOL(write_to_strongly_ordered_memory);

/* These cache related routines make the assumption (if outer cache is
 * available) that the associated physical memory is contiguous.
 * They will operate on all (L1 and L2 if present) caches.
 */
void clean_and_invalidate_caches(unsigned long vstart,
	unsigned long length, unsigned long pstart)
{
	dmac_flush_range((void *)vstart, (void *) (vstart + length));
	outer_flush_range(pstart, pstart + length);
}

void clean_caches(unsigned long vstart,
	unsigned long length, unsigned long pstart)
{
	dmac_clean_range((void *)vstart, (void *) (vstart + length));
	outer_clean_range(pstart, pstart + length);
}

void invalidate_caches(unsigned long vstart,
	unsigned long length, unsigned long pstart)
{
	dmac_inv_range((void *)vstart, (void *) (vstart + length));
	outer_inv_range(pstart, pstart + length);
}

void * __init alloc_bootmem_aligned(unsigned long size, unsigned long alignment)
{
	void *unused_addr = NULL;
	unsigned long addr, tmp_size, unused_size;

	/* Allocate maximum size needed, see where it ends up.
	 * Then free it -- in this path there are no other allocators
	 * so we can depend on getting the same address back
	 * when we allocate a smaller piece that is aligned
	 * at the end (if necessary) and the piece we really want,
	 * then free the unused first piece.
	 */

	tmp_size = size + alignment - PAGE_SIZE;
	addr = (unsigned long)alloc_bootmem(tmp_size);
	free_bootmem(__pa(addr), tmp_size);

	unused_size = alignment - (addr % alignment);
	if (unused_size)
		unused_addr = alloc_bootmem(unused_size);

	addr = (unsigned long)alloc_bootmem(size);
	if (unused_size)
		free_bootmem(__pa(unused_addr), unused_size);

	return (void *)addr;
}

char *memtype_name[] = {
	"SMI_KERNEL",
	"SMI",
	"EBI0",
	"EBI1"
};

struct reserve_info *reserve_info;

/**
 * calculate_reserve_limits() - calculate reserve limits for all
 * memtypes
 *
 * for each memtype in the reserve_info->memtype_reserve_table, sets
 * the `limit' field to the largest size of any memblock of that
 * memtype.
 */
static void __init calculate_reserve_limits(void)
{
	struct memblock_region *mr;
	int memtype;
	struct memtype_reserve *mt;

	for_each_memblock(memory, mr) {
		memtype = reserve_info->paddr_to_memtype(mr->base);
		if (memtype == MEMTYPE_NONE) {
			pr_warning("unknown memory type for region at %lx\n",
				(long unsigned int)mr->base);
			continue;
		}
		mt = &reserve_info->memtype_reserve_table[memtype];
		mt->limit = max_t(unsigned long, mt->limit, mr->size);
	}
}

static void __init adjust_reserve_sizes(void)
{
	int i;
	struct memtype_reserve *mt;

	mt = &reserve_info->memtype_reserve_table[0];
	for (i = 0; i < MEMTYPE_MAX; i++, mt++) {
		if (mt->flags & MEMTYPE_FLAGS_1M_ALIGN)
			mt->size = (mt->size + SECTION_SIZE - 1) & SECTION_MASK;
		if (mt->size > mt->limit) {
			pr_warning("%lx size for %s too large, setting to %lx\n",
				mt->size, memtype_name[i], mt->limit);
			mt->size = mt->limit;
		}
	}
}

#ifdef CONFIG_SRECORDER_MSM
static unsigned long s_mempools_pstart_addr = 0x0;

/*
 * Function:       unsigned long get_mempools_pstart_addr(void)
 * Description:    return address of mempools start
 * Calls:          No
 * Called By:      msm8625_reserve
 * Table Accessed: No
 * Table Updated:  No
 * Input:          No
 * Output:         No
 * Return:         s_mempools_pstart_addr: address of mempools start
 * Others:         No
 */
unsigned long get_mempools_pstart_addr(void)
{
    return s_mempools_pstart_addr;
}
#endif /* CONFIG_SRECORDER_MSM */

static void __init reserve_memory_for_mempools(void)
{
	int memtype;
	struct memtype_reserve *mt;
	phys_addr_t alignment;

	mt = &reserve_info->memtype_reserve_table[0];
	for (memtype = 0; memtype < MEMTYPE_MAX; memtype++, mt++) {
		if (mt->flags & MEMTYPE_FLAGS_FIXED || !mt->size)
			continue;

		alignment = (mt->flags & MEMTYPE_FLAGS_1M_ALIGN) ?
			SZ_1M : PAGE_SIZE;
		mt->start = arm_memblock_steal(mt->size, alignment);
		BUG_ON(!mt->start);
#ifdef CONFIG_SRECORDER_MSM
                s_mempools_pstart_addr = mt->start;
#endif /* CONFIG_SRECORDER_MSM */
	}
}

static void __init initialize_mempools(void)
{
	struct mem_pool *mpool;
	int memtype;
	struct memtype_reserve *mt;

	mt = &reserve_info->memtype_reserve_table[0];
	for (memtype = 0; memtype < MEMTYPE_MAX; memtype++, mt++) {
		if (!mt->size)
			continue;
		mpool = initialize_memory_pool(mt->start, mt->size, memtype);
		if (!mpool)
			pr_warning("failed to create %s mempool\n",
				memtype_name[memtype]);
	}
}

#define  MAX_FIXED_AREA_SIZE 0x11000000

void __init msm_reserve(void)
{
	unsigned long msm_fixed_area_size;
	unsigned long msm_fixed_area_start;

	memory_pool_init();
	reserve_info->calculate_reserve_sizes();

	msm_fixed_area_size = reserve_info->fixed_area_size;
	msm_fixed_area_start = reserve_info->fixed_area_start;
	if (msm_fixed_area_size)
		if (msm_fixed_area_start > reserve_info->low_unstable_address
			- MAX_FIXED_AREA_SIZE)
			reserve_info->low_unstable_address =
			msm_fixed_area_start;

	calculate_reserve_limits();
	adjust_reserve_sizes();
	reserve_memory_for_mempools();
	initialize_mempools();
}

static int get_ebi_memtype(void)
{
	/* on 7x30 and 8x55 "EBI1 kernel PMEM" is really on EBI0 */
	if (cpu_is_msm7x30() || cpu_is_msm8x55())
		return MEMTYPE_EBI0;
	return MEMTYPE_EBI1;
}

void *allocate_contiguous_ebi(unsigned long size,
	unsigned long align, int cached)
{
	return allocate_contiguous_memory(size, get_ebi_memtype(),
		align, cached);
}
EXPORT_SYMBOL(allocate_contiguous_ebi);

unsigned long allocate_contiguous_ebi_nomap(unsigned long size,
	unsigned long align)
{
	return _allocate_contiguous_memory_nomap(size, get_ebi_memtype(),
		align, __builtin_return_address(0));
}
EXPORT_SYMBOL(allocate_contiguous_ebi_nomap);

unsigned int msm_ttbr0;

void store_ttbr0(void)
{
	/* Store TTBR0 for post-mortem debugging purposes. */
	asm("mrc p15, 0, %0, c2, c0, 0\n"
		: "=r" (msm_ttbr0));
}

unsigned long get_ddr_size(void)
{
	unsigned int i;
	unsigned long ret = 0;

	for (i = 0; i < meminfo.nr_banks; i++)
		ret += meminfo.bank[i].size;

	return ret;
}
