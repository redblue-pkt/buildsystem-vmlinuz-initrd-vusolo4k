#ifndef __LINUX_PLATFORM_DATA_BCMGENET_H__
#define __LINUX_PLATFORM_DATA_BCMGENET_H__

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/phy.h>

struct bcmgenet_platform_data {
	/* used by the BSP code only */
	uintptr_t		base_reg;
	int			irq0;
	int			irq1;

	bool		mdio_enabled;
	phy_interface_t	phy_interface;
	int		phy_address;
	int		phy_speed;
	int		phy_duplex;
	u8		mac_address[ETH_ALEN];
	int		genet_version;
	unsigned int	sw_type;
};

#endif
