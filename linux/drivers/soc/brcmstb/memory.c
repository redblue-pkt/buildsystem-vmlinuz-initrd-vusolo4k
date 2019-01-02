/*
 * Copyright © 2015-2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * A copy of the GPL is available at
 * http://www.broadcom.com/licenses/GPLv2.php or from the Free Software
 * Foundation at https://www.gnu.org/licenses/ .
 */

#include <asm/page.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <linux/mm.h>   /* for high_memory */
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/vme.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/brcmstb/bmem.h>
#include <linux/brcmstb/cma_driver.h>
#include <linux/brcmstb/memory_api.h>

/* -------------------- Constants -------------------- */

#define DEFAULT_LOWMEM_PCT	20  /* used if only one membank */

/* Macros to help extract property data */
#define U8TOU32(b, offs) \
	((((u32)b[0+offs] << 0)  & 0x000000ff) | \
	 (((u32)b[1+offs] << 8)  & 0x0000ff00) | \
	 (((u32)b[2+offs] << 16) & 0x00ff0000) | \
	 (((u32)b[3+offs] << 24) & 0xff000000))

#define DT_PROP_DATA_TO_U32(b, offs) (fdt32_to_cpu(U8TOU32(b, offs)))

/* Constants used when retrieving memc info */
#define NUM_BUS_RANGES 10
#define BUS_RANGE_ULIMIT_SHIFT 4
#define BUS_RANGE_LLIMIT_SHIFT 4
#define BUS_RANGE_PA_SHIFT 12

/* platform dependant memory flags */
#if defined(CONFIG_BMIPS_GENERIC)
#define BCM_MEM_MASK (_PAGE_VALID)
#elif defined(CONFIG_ARM64)
#define BCM_MEM_MASK (PTE_ATTRINDX_MASK | PTE_TYPE_MASK)
#elif defined(CONFIG_ARM)
#define BCM_MEM_MASK (L_PTE_MT_MASK | L_PTE_VALID)
#else
#error "Platform not supported by bmem"
#endif

enum {
	BUSNUM_MCP0 = 0x4,
	BUSNUM_MCP1 = 0x5,
	BUSNUM_MCP2 = 0x6,
};

/* -------------------- Shared and local vars -------------------- */

const enum brcmstb_reserve_type brcmstb_default_reserve = BRCMSTB_RESERVE_BMEM;
bool brcmstb_memory_override_defaults = false;

static struct brcmstb_reserved_memory reserved_init;

/* -------------------- Functions -------------------- */

/*
 * If the DT nodes are handy, determine which MEMC holds the specified
 * physical address.
 */
#ifdef CONFIG_ARCH_BRCMSTB
int brcmstb_memory_phys_addr_to_memc(phys_addr_t pa)
{
	int memc = -1;
	int i;
	struct device_node *np;
	void __iomem *cpubiuctrl = NULL;
	void __iomem *curr;

	np = of_find_compatible_node(NULL, NULL, "brcm,brcmstb-cpu-biu-ctrl");
	if (!np)
		goto cleanup;

	cpubiuctrl = of_iomap(np, 0);
	if (!cpubiuctrl)
		goto cleanup;

	for (i = 0, curr = cpubiuctrl; i < NUM_BUS_RANGES; i++, curr += 8) {
		const u64 ulimit_raw = readl(curr);
		const u64 llimit_raw = readl(curr + 4);
		const u64 ulimit =
			((ulimit_raw >> BUS_RANGE_ULIMIT_SHIFT)
			 << BUS_RANGE_PA_SHIFT) | 0xfff;
		const u64 llimit = (llimit_raw >> BUS_RANGE_LLIMIT_SHIFT)
				   << BUS_RANGE_PA_SHIFT;
		const u32 busnum = (u32)(ulimit_raw & 0xf);

		if (pa >= llimit && pa <= ulimit) {
			if (busnum >= BUSNUM_MCP0 && busnum <= BUSNUM_MCP2) {
				memc = busnum - BUSNUM_MCP0;
				break;
			}
		}
	}

cleanup:
	if (cpubiuctrl)
		iounmap(cpubiuctrl);

	of_node_put(np);

	return memc;
}
#elif defined(CONFIG_MIPS)
int brcmstb_memory_phys_addr_to_memc(phys_addr_t pa)
{
	/* The logic here is fairly simple and hardcoded: if pa <= 0x5000_0000,
	 * then this is MEMC0, else MEMC1.
	 *
	 * For systems with 2GB on MEMC0, MEMC1 starts at 9000_0000, with 1GB
	 * on MEMC0, MEMC1 starts at 6000_0000.
	 */
	if (pa >= 0x50000000ULL)
		return 1;
	else
		return 0;
}
#endif
EXPORT_SYMBOL(brcmstb_memory_phys_addr_to_memc);

u64 brcmstb_memory_memc_size(int memc)
{
	const void *fdt = initial_boot_params;
	const int mem_offset = fdt_path_offset(fdt, "/memory");
	int addr_cells = 1, size_cells = 1;
	const struct fdt_property *prop;
	int proplen, cellslen;
	u64 memc_size = 0;
	int i;

	/* Get root size and address cells if specified */
	prop = fdt_get_property(fdt, 0, "#size-cells", &proplen);
	if (prop)
		size_cells = DT_PROP_DATA_TO_U32(prop->data, 0);

	prop = fdt_get_property(fdt, 0, "#address-cells", &proplen);
	if (prop)
		addr_cells = DT_PROP_DATA_TO_U32(prop->data, 0);

	if (mem_offset < 0)
		return -1;

	prop = fdt_get_property(fdt, mem_offset, "reg", &proplen);
	cellslen = (int)sizeof(u32) * (addr_cells + size_cells);
	if ((proplen % cellslen) != 0)
		return -1;

	for (i = 0; i < proplen / cellslen; ++i) {
		u64 addr = 0;
		u64 size = 0;
		int memc_idx;
		int j;

		for (j = 0; j < addr_cells; ++j) {
			int offset = (cellslen * i) + (sizeof(u32) * j);

			addr |= (u64)DT_PROP_DATA_TO_U32(prop->data, offset) <<
				((addr_cells - j - 1) * 32);
		}
		for (j = 0; j < size_cells; ++j) {
			int offset = (cellslen * i) +
				(sizeof(u32) * (j + addr_cells));

			size |= (u64)DT_PROP_DATA_TO_U32(prop->data, offset) <<
				((size_cells - j - 1) * 32);
		}

		if ((phys_addr_t)addr != addr) {
			pr_err("phys_addr_t is smaller than provided address 0x%llx!\n",
					addr);
			return -1;
		}

		memc_idx = brcmstb_memory_phys_addr_to_memc((phys_addr_t)addr);
		if (memc_idx == memc)
			memc_size += size;
	}

	return memc_size;

}
EXPORT_SYMBOL(brcmstb_memory_memc_size);

static int populate_memc(struct brcmstb_memory *mem, int addr_cells,
		int size_cells)
{
	const void *fdt = initial_boot_params;
	const int mem_offset = fdt_path_offset(fdt, "/memory");
	const struct fdt_property *prop;
	int proplen, cellslen;
	int i;

	if (mem_offset < 0) {
		pr_err("No memory node?\n");
		return -EINVAL;
	}

	prop = fdt_get_property(fdt, mem_offset, "reg", &proplen);
	cellslen = (int)sizeof(u32) * (addr_cells + size_cells);
	if ((proplen % cellslen) != 0) {
		pr_err("Invalid length of reg prop: %d\n", proplen);
		return -EINVAL;
	}

	for (i = 0; i < proplen / cellslen; ++i) {
		u64 addr = 0;
		u64 size = 0;
		int memc_idx;
		int range_idx;
		int j;

		for (j = 0; j < addr_cells; ++j) {
			int offset = (cellslen * i) + (sizeof(u32) * j);
			addr |= (u64)DT_PROP_DATA_TO_U32(prop->data, offset) <<
				((addr_cells - j - 1) * 32);
		}
		for (j = 0; j < size_cells; ++j) {
			int offset = (cellslen * i) +
				(sizeof(u32) * (j + addr_cells));
			size |= (u64)DT_PROP_DATA_TO_U32(prop->data, offset) <<
				((size_cells - j - 1) * 32);
		}

		if ((phys_addr_t)addr != addr) {
			pr_err("phys_addr_t is smaller than provided address 0x%llx!\n",
					addr);
			return -EINVAL;
		}

		memc_idx = brcmstb_memory_phys_addr_to_memc((phys_addr_t)addr);
		if (memc_idx == -1) {
			pr_err("address 0x%llx does not appear to be in any memc\n",
					addr);
			return -EINVAL;
		}

		range_idx = mem->memc[memc_idx].count;
		if (mem->memc[memc_idx].count >= MAX_BRCMSTB_RANGE)
			pr_warn("%s: Exceeded max ranges for memc%d\n",
					__func__, memc_idx);
		else {
			mem->memc[memc_idx].range[range_idx].addr = addr;
			mem->memc[memc_idx].range[range_idx].size = size;
		}
		++mem->memc[memc_idx].count;
	}

	return 0;
}

static int populate_lowmem(struct brcmstb_memory *mem)
{
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
	mem->lowmem.range[0].addr = __pa(PAGE_OFFSET);
	mem->lowmem.range[0].size = (unsigned long)high_memory - PAGE_OFFSET;
	++mem->lowmem.count;
	return 0;
#else
	return -ENOSYS;
#endif
}

static int populate_bmem(struct brcmstb_memory *mem)
{
#ifdef CONFIG_BRCMSTB_BMEM
	phys_addr_t addr, size;
	int i;

	for (i = 0; i < MAX_BRCMSTB_RANGE; ++i) {
		if (bmem_region_info(i, &addr, &size))
			break;  /* no more regions */
		mem->bmem.range[i].addr = addr;
		mem->bmem.range[i].size = size;
		++mem->bmem.count;
	}
	if (i >= MAX_BRCMSTB_RANGE) {
		while (bmem_region_info(i, &addr, &size) == 0) {
			pr_warn("%s: Exceeded max ranges\n", __func__);
			++mem->bmem.count;
		}
	}

	return 0;
#else
	return -ENOSYS;
#endif
}

static int populate_cma(struct brcmstb_memory *mem)
{
#ifdef CONFIG_BRCMSTB_CMA
	int i;

	for (i = 0; i < CMA_NUM_RANGES; ++i) {
		struct cma_dev *cdev = cma_dev_get_cma_dev(i);
		if (cdev == NULL)
			break;
		if (i >= MAX_BRCMSTB_RANGE)
			pr_warn("%s: Exceeded max ranges\n", __func__);
		else {
			mem->cma.range[i].addr = cdev->range.base;
			mem->cma.range[i].size = cdev->range.size;
		}
		++mem->cma.count;
	}

	return 0;
#else
	return -ENOSYS;
#endif
}

#ifndef CONFIG_HAVE_MEMBLOCK
static inline int populate_reserved(struct brcmstb_memory *mem)
{
	return -ENOSYS;
}
#else
static inline bool address_within_range(phys_addr_t addr, phys_addr_t size,
				      struct brcmstb_range *range)
{
	return (addr >= range->addr &&
		addr + size <= range->addr + range->size);
}

static inline bool rmem_within_range(struct reserved_mem *rmem,
				     struct brcmstb_range *range)
{
	if (!rmem || !rmem->size)
		return false;

	return address_within_range(rmem->base, rmem->size, range);
}

static int range_exists(phys_addr_t addr, phys_addr_t size,
			struct brcmstb_reserved_memory *reserved)
{
	struct brcmstb_range *range;
	unsigned int i;

	/* Don't consider 0 size ranges as valid */
	if (!size)
		return reserved->count;

	for (i = 0; i < reserved->count; i++) {
		range = &reserved->range[i];
		if (range->addr == addr)
			return i;
	}

	return -1;
}

static void range_truncate(phys_addr_t addr, phys_addr_t *size)
{
	struct brcmstb_range range = {
		.addr = addr,
		.size = *size,
	};
	int count = __reserved_mem_get_count();
	struct reserved_mem *rmem;
	int i;

	/* Check if we have an neighboring rmem entry, and if so, adjust
	 * the range by how much
	 */
	for (i = 0; i < count; i++) {
		rmem = __reserved_mem_get_entry(i);
		if (rmem_within_range(rmem, &range) && rmem->reserved_name)
			*size -= rmem->size;
	}
}

/*
 * Attempts the insertion of a given reserved memory entry (rmem) into
 * a larger range using reserved as a database entry
 *
 * return values:
 * true if the entry was successfully inserted (could have been more than just one)
 * false if the entry was not inserted (not a valid candidate)
 */
static bool rmem_insert_into_range(struct reserved_mem *rmem,
				   struct brcmstb_range *range,
				   struct brcmstb_reserved_memory *reserved)
{
	int len = MAX_BRCMSTB_RESERVED_NAME;
	phys_addr_t size1 = 0, size2 = 0;
	int i = reserved->count;
	int j;

	if (!rmem->size)
		return false;

	/* This region does not have a name, it must be part of its larger
	 * range then.
	 */
	if (!rmem->reserved_name)
		return false;

	/* If we have memory below us, we should report it but we also need to
	 * make sure we are not reporting an existing range.
	 */
	if (range) {
		/* Get the memory below */
		size1 = rmem->base - range->addr;
		if (range_exists(range->addr, size1, reserved) < 0) {
			reserved->range[i].addr = range->addr;
			reserved->range[i].size = size1;
			reserved->count++;
			i = reserved->count;
		}
	}

	/* We may have already inserted this rmem entry before, but
	 * without its name, so find it, and update the location
	 */
	j = range_exists(rmem->base, rmem->size, reserved);
	if (j >= 0 && range)
		i = j;

	strncpy(reserved->range_name[i].name, rmem->reserved_name, len);
	reserved->range[i].addr = rmem->base;
	reserved->range[i].size = rmem->size;
	/* Only increment if this was not an existing location we re-used */
	if (i != j)
		reserved->count++;
	i = reserved->count;

	/* If we have memory above us, we should also report it but
	 * we also need to make sure we are not reporting an existing
	 * range either
	 */
	if (range) {
		size2 = (range->addr + range->size) - (rmem->base + rmem->size);
		size1 = rmem->base + rmem->size;
		if (range_exists(rmem->base + rmem->size, size2, reserved) < 0) {
			range_truncate(size1, &size2);
			reserved->range[i].addr = size1;
			reserved->range[i].size = size2;
			reserved->count++;
		}
	}

	return true;
}

static bool contiguous_range_exists(struct brcmstb_range *range,
				    struct brcmstb_reserved_memory *reserved)
{
	struct brcmstb_range *iter;
	phys_addr_t total_size = 0;
	unsigned int i;

	for (i = 0; i < reserved->count; i++) {
		iter = &reserved->range[i];
		if (address_within_range(iter->addr, iter->size, range))
			total_size += iter->size;
	}

	return total_size == range->size;
}

static int populate_reserved(struct brcmstb_memory *mem)
{
	struct brcmstb_reserved_memory reserved;
	struct brcmstb_range *range;
	struct reserved_mem *rmem;
	int count, i, j;
	bool added;

	memset(&reserved, 0, sizeof(reserved));

	count = __reserved_mem_get_count();

	/* No reserved-memory entries, or OF_RESERVED_MEM not built, just
	 * report what we already have */
	if (count <= 0) {
		memcpy(&mem->reserved, &reserved_init, sizeof(reserved_init));
		return 0;
	}

	count = min(count, MAX_BRCMSTB_RESERVED_RANGE);

	/* Loop through the FDT reserved memory regions, first pass
	 * will split the existing reserved ranges into smaller
	 * name-based reserved regions
	 */
	for (i = 0; i < reserved_init.count; i++) {
		range = &reserved_init.range[i];
		added = false;
		for (j = 0; j < count; j++) {
			added = false;
			rmem = __reserved_mem_get_entry(j);
			if (rmem_within_range(rmem, range))
				added = rmem_insert_into_range(rmem, range,
							       &reserved);
		}

		/* rmem_insert_into_range() may be splitting a larger range into
		 * contiguous parts, so we need to check that here too to avoid
		 * re-inserting it another time
		 */
		if (!added && range->size &&
		    !contiguous_range_exists(range, &reserved)) {
			reserved.range[reserved.count].addr = range->addr;
			reserved.range[reserved.count].size = range->size;
			reserved.count++;
		}
	}

	/* Second loop takes care of "no-map" regions which do not show up
	 * in reserved_init and need to be checked separately
	 */
	for (i = 0; i < count; i++) {
		rmem = __reserved_mem_get_entry(i);
		if (!memblock_is_map_memory(rmem->base))
			rmem_insert_into_range(rmem, NULL, &reserved);
	}

	memcpy(&mem->reserved, &reserved, sizeof(reserved));

	return 0;
}
#endif

static int __init brcmstb_memory_set_range(phys_addr_t start, phys_addr_t end,
					   int (*setup)(phys_addr_t start,
							phys_addr_t size));

static int __init brcmstb_memory_region_check(phys_addr_t *start,
					      phys_addr_t *size,
					      phys_addr_t *end,
					      phys_addr_t reg_start,
					      phys_addr_t reg_size,
					      int (*setup)(phys_addr_t start,
						           phys_addr_t size))
{
	/* range is entirely below the reserved region */
	if (*end <= reg_start)
		return 1;

	/* range is entirely above the reserved region */
	if (*start >= reg_start + reg_size)
		return 0;

	if (*start < reg_start) {
		if (*end <= reg_start + reg_size) {
			/* end of range overlaps reservation */
			pr_debug("%s: Reduced default region %pa@%pa\n",
					__func__, size, start);

			*end = reg_start;
			*size = *end - *start;
			pr_debug("%s: to %pa@%pa\n",
					__func__, size, start);
			return 1;
		}

		/* range contains the reserved region */
		pr_debug("%s: Split default region %pa@%pa\n",
			 __func__, size, start);

		*size = reg_start - *start;
		pr_debug("%s: into %pa@%pa\n",
			 __func__, size, start);
		brcmstb_memory_set_range(*start, reg_start, setup);

		*start = reg_start + reg_size;
		*size = *end - *start;
		pr_debug("%s: and %pa@%pa\n", __func__, size, start);
	} else if (*end > reg_start + reg_size) {
		/* start of range overlaps reservation */
		pr_debug("%s: Reduced default region %pa@%pa\n",
			 __func__, size, start);
		*start = reg_start + reg_size;
		*size = *end - *start;
		pr_debug("%s: to %pa@%pa\n", __func__, &size, &start);
	} else {
		/* range is contained by the reserved region */
		pr_debug("%s: Default region %pa@%pa is reserved\n",
			 __func__, size, start);

		return -EINVAL;
	}

	return 0;
}

/*
 * brcmstb_memory_set_range() - validate and set middleware memory range
 * @start: the physical address of the start of a candidate range
 * @end: the physical address one beyond the end of a candidate range
 * @setup: function for setting the start and size of a region
 *
 * This function adjusts a candidate default memory range to accommodate
 * memory reservations and alignment constraints.  If a valid range can
 * be determined, then the setup function is called to actually record
 * the region.
 *
 * This function assumes the memblock.reserved type is incrementally
 * ordered and non-overlapping.  If that changes then this function must
 * be updated.
 */
static int __init brcmstb_memory_set_range(phys_addr_t start, phys_addr_t end,
					   int (*setup)(phys_addr_t start,
							phys_addr_t size))
{
	/* min alignment for mm core */
	const phys_addr_t alignment =
		PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order);
	phys_addr_t temp, size = end - start;
	int i, ret;

	for (i = 0; i < memblock.reserved.cnt; i++) {
		struct memblock_region *region = &memblock.reserved.regions[i];

		ret = brcmstb_memory_region_check(&start, &size, &end,
						  region->base, region->size,
						  setup);
		if (ret == 0)
			continue;

		if (ret == 1)
			break;

		if (ret < 0)
			return ret;

	}

	/* Also range check reserved-memory 'no-map' entries from being
	 * possible candidates
	 */
	for (i = 0; i < __reserved_mem_get_count(); i++) {
		struct reserved_mem *rmem = __reserved_mem_get_entry(i);

		if (memblock_is_map_memory(rmem->base))
			continue;

		ret = brcmstb_memory_region_check(&start, &size, &end,
						  rmem->base, rmem->size,
						  setup);
		if (ret == 0)
			continue;

		if (ret == 1)
			break;

		if (ret < 0)
			return ret;

	}

	/* Exclude reserved-memory 'no-map' entries from being possible
	 * candidates
	 */
	if (!IS_ENABLED(CONFIG_MIPS) && !memblock_is_map_memory(start)) {
		pr_debug("%s: Cannot add nomap %pa%p@\n",
			 __func__, &start, &end);
		return -EINVAL;
	}

	/* Fix up alignment */
	temp = ALIGN(start, alignment);
	if (temp != start) {
		pr_debug("adjusting start from %pa to %pa\n",
			 &start, &temp);

		if (size > (temp - start))
			size -= (temp - start);
		else
			size = 0;

		start = temp;
	}

	temp = round_down(size, alignment);
	if (temp != size) {
		pr_debug("adjusting size from %pa to %pa\n",
			 &size, &temp);
		size = temp;
	}

	if (size == 0) {
		pr_debug("size available in bank was 0 - skipping\n");
		return -EINVAL;
	}

	return setup(start, size);
}

/*
 * brcmstb_memory_default_reserve() - create default reservations
 * @setup: function for setting the start and size of a region
 *
 * This determines the size and address of the default regions
 * reserved for refsw based on the flattened device tree.
 */
void __init brcmstb_memory_default_reserve(int (*setup)(phys_addr_t start,
							phys_addr_t size))
{
	phys_addr_t start, end, size, limit;
	phys_addr_t adj = 0;
	const void *fdt = initial_boot_params;
	int offset;
	const struct fdt_property *prop;
	int addr_cells = 1, size_cells = 1;
	int proplen, cellslen;
	int i;
	u64 tmp;

	if (!fdt) {
		pr_err("No device tree?\n");
		return;
	}

	/* Get root size and address cells if specified */
	prop = fdt_get_property(fdt, 0, "#size-cells", &proplen);
	if (prop)
		size_cells = DT_PROP_DATA_TO_U32(prop->data, 0);
	pr_debug("size_cells = %x\n", size_cells);

	prop = fdt_get_property(fdt, 0, "#address-cells", &proplen);
	if (prop)
		addr_cells = DT_PROP_DATA_TO_U32(prop->data, 0);
	pr_debug("address_cells = %x\n", addr_cells);

	offset = fdt_path_offset(fdt, "/memory");

	if (offset < 0) {
		pr_err("No memory node?\n");
		return;
	}

	prop = fdt_get_property(fdt, offset, "reg", &proplen);
	cellslen = (int)sizeof(u32) * (addr_cells + size_cells);
	if ((proplen % cellslen) != 0) {
		pr_err("Invalid length of reg prop: %d\n", proplen);
		return;
	}

	offset = 0;
	while (offset < proplen) {
		tmp = 0;
		for (i = 0; i < addr_cells; ++i) {
			tmp |= (u64)DT_PROP_DATA_TO_U32(prop->data, offset) <<
			       ((addr_cells - i - 1) * 32);
			offset += sizeof(u32);
		}
		start = (phys_addr_t)tmp;
		if (start != tmp) {
			pr_err("phys_addr_t too small for address 0x%llx!\n",
			       tmp);
			offset += sizeof(u32) * size_cells;
			continue;
		}

		tmp = 0;
		for (i = 0; i < size_cells; ++i) {
			tmp |= (u64)DT_PROP_DATA_TO_U32(prop->data, offset) <<
			       ((size_cells - i - 1) * 32);
			offset += sizeof(u32);
		}
		size = (phys_addr_t)tmp;
		if (size != tmp) {
			pr_err("phys_addr_t too small for size 0x%llx!\n",
			       tmp);
			continue;
		}

		end = start + size;

		if (offset == cellslen) {	/* First Bank */
			limit = memblock_get_current_limit();
			/*
			 *  On ARM64 systems, force the first memory controller
			 * to be partitioned the same way it would on ARM
			 * (32-bit) by giving 256MB to the kernel, the rest to
			 * BMEM
			 */
#ifdef CONFIG_ARM64
			if (limit > start + SZ_256M)
				limit = start + SZ_256M;
#endif
			if (end <= limit &&
				end == memblock_end_of_DRAM()) {
				if (size < SZ_32M) {
					pr_err("low memory too small for default bmem\n");
					continue;
				}

				if (brcmstb_default_reserve ==
				    BRCMSTB_RESERVE_BMEM) {
					if (size <= SZ_128M)
						continue;

					adj = SZ_128M;
				}

				/* kernel reserves X percent,
				 * bmem gets the rest */
				tmp = ((u64)(size - adj)) *
				      (100 - DEFAULT_LOWMEM_PCT);
				do_div(tmp, 100);
				size = tmp;
				start = end - size;
			} else if (end > limit) {
				start = limit;
				size = end - start;
			} else {
				if (size >= SZ_1G)
					start += SZ_512M;
				else if (size >= SZ_512M)
					start += SZ_256M;
				else
					continue;
				size = end - start;
			}
		} else if (start >= VME_A32_MAX && size > SZ_64M) {
			/*
			 * Nexus doesn't use the address extension range yet,
			 * just reserve 64 MiB in these areas until we have a
			 * firmer specification
			 */
			size = SZ_64M;
		}

		brcmstb_memory_set_range(start, start + size, setup);
	}
}

/**
 * brcmstb_memory_reserve() - fill in static brcmstb_memory structure
 *
 * This is a boot-time initialization function used to copy the information
 * stored in the memblock reserve function that is discarded after boot.
 */
void __init brcmstb_memory_reserve(void)
{
#ifdef CONFIG_HAVE_MEMBLOCK
	struct memblock_type *type = &memblock.reserved;
	int i;

	for (i = 0; i < type->cnt; ++i) {
		struct memblock_region *region = &type->regions[i];

		if (i >= MAX_BRCMSTB_RESERVED_RANGE)
			pr_warn_once("%s: Exceeded max ranges\n", __func__);
		else {
			reserved_init.range[i].addr = region->base;
			reserved_init.range[i].size = region->size;
		}
		++reserved_init.count;
	}
#else
	pr_err("No memblock, cannot get reserved range\n");
#endif
}

/*
 * brcmstb_memory_init() - Initialize Broadcom proprietary memory extensions
 *
 * This function is a hook from the architecture specific mm initialization
 * that allows the memory extensions used by Broadcom Set-Top-Box middleware
 * to be initialized.
 */
void __init brcmstb_memory_init(void)
{
	brcmstb_memory_reserve();
#ifdef CONFIG_BRCMSTB_CMA
	cma_reserve();
#endif
#ifdef CONFIG_BRCMSTB_BMEM
	bmem_reserve();
#endif
}

/*
 * brcmstb_memory_get() - fill in brcmstb_memory structure
 * @mem: pointer to allocated struct brcmstb_memory to fill
 *
 * The brcmstb_memory struct is required by the brcmstb middleware to
 * determine how to set up its memory heaps.  This function expects that the
 * passed pointer is valid.  The struct does not need to have be zeroed
 * before calling.
 */
int brcmstb_memory_get(struct brcmstb_memory *mem)
{
	const void *fdt = initial_boot_params;
	const struct fdt_property *prop;
	int addr_cells = 1, size_cells = 1;
	int proplen;
	int ret;

	if (!mem)
		return -EFAULT;

	if (!fdt) {
		pr_err("No device tree?\n");
		return -EINVAL;
	}

	/* Get root size and address cells if specified */
	prop = fdt_get_property(fdt, 0, "#size-cells", &proplen);
	if (prop)
		size_cells = DT_PROP_DATA_TO_U32(prop->data, 0);
	pr_debug("size_cells = %x\n", size_cells);

	prop = fdt_get_property(fdt, 0, "#address-cells", &proplen);
	if (prop)
		addr_cells = DT_PROP_DATA_TO_U32(prop->data, 0);
	pr_debug("address_cells = %x\n", addr_cells);

	memset(mem, 0, sizeof(*mem));

	ret = populate_memc(mem, addr_cells, size_cells);
	if (ret)
		return ret;

	ret = populate_lowmem(mem);
	if (ret)
		pr_debug("no lowmem defined\n");

	ret = populate_bmem(mem);
	if (ret)
		pr_debug("bmem is disabled\n");

	ret = populate_cma(mem);
	if (ret)
		pr_debug("cma is disabled\n");

	ret = populate_reserved(mem);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(brcmstb_memory_get);

static int pte_callback(pte_t *pte, unsigned long x, unsigned long y,
			struct mm_walk *walk)
{
	const pgprot_t pte_prot = __pgprot(pte_val(*pte));
	const pgprot_t req_prot = *((pgprot_t *)walk->private);
	const pgprot_t prot_msk = __pgprot(BCM_MEM_MASK);
	return (((pgprot_val(pte_prot) ^ pgprot_val(req_prot)) & pgprot_val(prot_msk)) == 0) ? 0 : -1;
}

static void *page_to_virt_contig(const struct page *page, unsigned int pg_cnt,
					pgprot_t pgprot)
{
	int rc;
	struct mm_walk walk;
	unsigned long pfn;
	unsigned long pfn_start;
	unsigned long pfn_end;
	unsigned long va_start;
	unsigned long va_end;

	if ((page == NULL) || !pg_cnt)
		return ERR_PTR(-EINVAL);

	pfn_start = page_to_pfn(page);
	pfn_end = pfn_start + pg_cnt;
	for (pfn = pfn_start; pfn < pfn_end; pfn++) {
		const struct page *cur_pg = pfn_to_page(pfn);
		phys_addr_t pa;

		/* Verify range is in low memory only */
		if (PageHighMem(cur_pg))
			return NULL;

		/* Must be mapped */
		pa = page_to_phys(cur_pg);
		if (page_address(cur_pg) == NULL)
			return NULL;
	}

	/*
	 * Aliased mappings with different cacheability attributes on ARM can
	 * lead to trouble!
	 */
	memset(&walk, 0, sizeof(walk));
	walk.pte_entry = &pte_callback;
	walk.private = (void *)&pgprot;
	walk.mm = current->mm;
	va_start = (unsigned long)page_address(page);
	va_end = (unsigned long)(page_address(page) + (pg_cnt << PAGE_SHIFT));
	rc = walk_page_range(va_start,
			     va_end,
			     &walk);
	if (rc)
		pr_debug("cacheability mismatch\n");

	return rc ? NULL : page_address(page);
}

static struct page **get_pages(struct page *page, int num_pages)
{
	struct page **pages;
	long pfn;
	int i;

	if (num_pages == 0) {
		pr_err("bad count\n");
		return NULL;
	}

	if (page == NULL) {
		pr_err("bad page\n");
		return NULL;
	}

	pages = vmalloc(sizeof(struct page *) * num_pages);
	if (pages == NULL)
		return NULL;

	pfn = page_to_pfn(page);
	for (i = 0; i < num_pages; i++) {
		/*
		 * pfn_to_page() should resolve to simple arithmetic for the
		 * FLATMEM memory model.
		 */
		pages[i] = pfn_to_page(pfn++);
	}

	return pages;
}

/*
 * Basically just vmap() without checking that count < totalram_pages,
 * since we want to be able to map pages that aren't managed by Linux
 */
static void *brcmstb_memory_vmap(struct page **pages, unsigned int count,
		unsigned long flags, pgprot_t prot)
{
	struct vm_struct *area;

	might_sleep();

	area = get_vm_area_caller((count << PAGE_SHIFT), flags,
					__builtin_return_address(0));
	if (!area)
		return NULL;

	if (map_vm_area(area, prot, &pages)) {
		vunmap(area->addr);
		vm_unmap_aliases();
		return NULL;
	}

	return area->addr;
}

/**
 * brcmstb_memory_kva_map() - Map page(s) to a kernel virtual address
 *
 * @page: A struct page * that points to the beginning of a chunk of physical
 * contiguous memory.
 * @num_pages: Number of pages
 * @pgprot: Page protection bits
 *
 * Return: pointer to mapping, or NULL on failure
 */
void *brcmstb_memory_kva_map(struct page *page, int num_pages, pgprot_t pgprot)
{
	void *va;

	/* get the virtual address for this range if it exists */
	va = page_to_virt_contig(page, num_pages, pgprot);
	if (IS_ERR(va)) {
		pr_debug("page_to_virt_contig() failed (%ld)\n", PTR_ERR(va));
		return NULL;
	} else if (va == NULL || is_vmalloc_addr(va)) {
		struct page **pages;

		pages = get_pages(page, num_pages);
		if (pages == NULL) {
			pr_err("couldn't get pages\n");
			return NULL;
		}

		va = brcmstb_memory_vmap(pages, num_pages, 0, pgprot);

		vfree(pages);

		if (va == NULL) {
			pr_err("vmap failed (num_pgs=%d)\n", num_pages);
			return NULL;
		}
	}

	return va;
}
EXPORT_SYMBOL(brcmstb_memory_kva_map);

/**
 * brcmstb_memory_kva_map_phys() - map phys range to kernel virtual address
 *
 * @phys: physical address base
 * @size: size of range to map
 * @cached: whether to use cached or uncached mapping
 *
 * Return: NULL on failure, err on success
 */
void *brcmstb_memory_kva_map_phys(phys_addr_t phys, size_t size, bool cached)
{
	void *addr = NULL;
	unsigned long pfn = PFN_DOWN(phys);

	if (!cached) {
		/*
		 * This could be supported for MIPS by using ioremap instead,
		 * but that cannot be done on ARM if you want O_DIRECT support
		 * because having multiple mappings to the same memory with
		 * different cacheability will result in undefined behavior.
		 */
		return NULL;
	}

	if (!pfn_valid(pfn))
		addr = ioremap_cache(phys, size);
	else
		addr = brcmstb_memory_kva_map(pfn_to_page(pfn), size / PAGE_SIZE,
					      PAGE_KERNEL);
	return addr;
}
EXPORT_SYMBOL(brcmstb_memory_kva_map_phys);

/**
 * brcmstb_memory_kva_unmap() - Unmap a kernel virtual address associated
 * to physical pages mapped by brcmstb_memory_kva_map()
 *
 * @kva: Kernel virtual address previously mapped by brcmstb_memory_kva_map()
 *
 * Return: 0 on success, negative on failure.
 */
int brcmstb_memory_kva_unmap(const void *kva)
{
	if (kva == NULL)
		return -EINVAL;

	if (!is_vmalloc_addr(kva)) {
		/* unmapping not necessary for low memory VAs */
		return 0;
	}

	if (pfn_valid(virt_to_pfn(kva))) {
		vunmap(kva);
		vm_unmap_aliases();
	} else {
		iounmap((void *)kva);
	}

	return 0;
}
EXPORT_SYMBOL(brcmstb_memory_kva_unmap);
