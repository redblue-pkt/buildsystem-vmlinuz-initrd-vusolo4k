#ifndef __NEXUS_INTERNAL_GPIO_H
#define __NEXUS_INTERNAL_GPIO_H

#define GPIO_DT_COMPAT	"brcm,brcmstb-gpio"
#define GIO_BANK_SIZE	0x20
#define GPIO_PER_BANK	32

int brcmstb_gpio_update32(uint32_t addr, uint32_t mask, uint32_t value);

#endif /* __NEXUS_INTERNAL_GPIO_H */
