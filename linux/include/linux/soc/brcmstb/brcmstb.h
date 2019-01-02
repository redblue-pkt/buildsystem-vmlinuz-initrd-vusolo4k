#ifndef __BRCMSTB_SOC_H
#define __BRCMSTB_SOC_H

int brcmstb_biuctrl_init(void);

/*
 * Helper functions for getting family or product id from the
 * SoC driver.
 */
u32 brcmstb_get_family_id(void);
u32 brcmstb_get_product_id(void);

#ifdef CONFIG_BRCMSTB_MEMORY_API
int brcmstb_memory_phys_addr_to_memc(phys_addr_t pa);
u64 brcmstb_memory_memc_size(unsigned int memc);
#else
static inline int brcmstb_memory_phys_addr_to_memc(phys_addr_t pa)
{
	return -EINVAL;
}

static inline u64 brcmstb_memory_memc_size(int memc)
{
	return -1;
}
#endif

#endif /* __BRCMSTB_SOC_H */
