/*
 * sdhci-brcmstb.c Support for SDHCI on Broadcom SoC's
 *
 * Copyright (C) 2013 Broadcom Corporation
 *
 * Author: Al Cooper <acooper@broadcom.com>
 * Based on sdhci-dove.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/brcmstb/brcmstb.h>
#include "sdhci-brcmstb.h"

#include "sdhci-pltfm.h"

#define SDIO_CFG_REG(x, y)	(x + BCHP_SDIO_0_CFG_##y -	\
				BCHP_SDIO_0_CFG_REG_START)

struct sdhci_brcmstb_priv {
	void __iomem *cfg_regs;
	int host_driver_type;
	int host_hs_driver_type;
	int card_driver_type;
};

#define MASK_OFF_DRV (SDHCI_PRESET_SDCLK_FREQ_MASK |	\
			SDHCI_PRESET_CLKGEN_SEL_MASK)

static char *strength_type_to_string[] = {"B", "A", "C", "D"};

#if defined(CONFIG_BCM74371A0)
/*
 * HW7445-1183
 * Setting the RESET_ALL or RESET_DATA bits will hang the SDIO
 * core so don't allow these bits to be set. This workaround
 * allows the driver to be used for development and testing
 * but will prevent recovery from normally recoverable errors
 * and should NOT be used in production systems.
 */
static void sdhci_brcmstb_writeb(struct sdhci_host *host, u8 val, int reg)
{
	if (reg == SDHCI_SOFTWARE_RESET)
		val &= ~(SDHCI_RESET_ALL | SDHCI_RESET_DATA);
	writeb(val, host->ioaddr + reg);
}

/* We don't support drive strength override on chips that use the
 * old version of the SDIO core.
 */
static void set_host_driver_strength_overrides(
	struct sdhci_host *host,
	struct sdhci_brcmstb_priv *priv)
{
}

#else /* CONFIG_BCM74371A0 */

#ifndef CONFIG_MIPS
static void set_host_driver_strength_overrides(
	struct sdhci_host *host,
	struct sdhci_brcmstb_priv *priv)
{
	u16 strength;
	u16 sdr25;
	u16 sdr50;
	u16 ddr50;
	u16 sdr104;
	u32 val;
	u32 cfg_base = (u32)priv->cfg_regs;

	if (priv->host_driver_type) {
		dev_info(mmc_dev(host->mmc),
			"Changing UHS Host Driver TYPE Presets to TYPE %s\n",
			strength_type_to_string[priv->host_driver_type]);
		strength = (u16)priv->host_driver_type << 11;
		sdr25 = sdhci_readw(host,
				SDHCI_PRESET_FOR_SDR25) & MASK_OFF_DRV;
		sdr50 = sdhci_readw(host,
				SDHCI_PRESET_FOR_SDR50) & MASK_OFF_DRV;
		ddr50 = sdhci_readw(host,
				SDHCI_PRESET_FOR_DDR50) & MASK_OFF_DRV;
		sdr104 = sdhci_readw(host,
				SDHCI_PRESET_FOR_SDR104) & MASK_OFF_DRV;
		val = (sdr25 | strength);
		val |= ((u32)(sdr50 | strength)) << 16;
		val |= 0x80000000;
		DEV_WR(SDIO_CFG_REG(cfg_base, PRESET3), val);
		val = (sdr104 | strength);
		val |= ((u32)(ddr50 | strength)) << 16;
		val |= 0x80000000;
		DEV_WR(SDIO_CFG_REG(cfg_base, PRESET4), val);
	}

	/*
	 * The Host Controller Specification states that the driver
	 * strength setting is only valid for UHS modes, but our
	 * host controller allows this setting to be used for HS modes
	 * as well.
	 */
	if (priv->host_hs_driver_type) {
		u16 sdr12;
		u16 hs;

		dev_info(mmc_dev(host->mmc),
			"Changing HS Host Driver TYPE Presets to TYPE %s\n",
			strength_type_to_string[priv->host_hs_driver_type]);
		strength = (u16)priv->host_hs_driver_type << 11;
		sdr12 = sdhci_readw(host, SDHCI_PRESET_FOR_SDR12) &
			MASK_OFF_DRV;
		hs = sdhci_readw(host, SDHCI_PRESET_FOR_HS) & MASK_OFF_DRV;
		val = (hs | strength);
		val |= ((u32)(sdr12 | strength)) << 16;
		val |= 0x80000000;
		DEV_WR(SDIO_CFG_REG(cfg_base, PRESET2), val);
	}
}
#else
static void set_host_driver_strength_overrides(
		struct sdhci_host *host,
		struct sdhci_brcmstb_priv *priv)
{
}
#endif /* CONFIG_MIPS */

#endif /* CONFIG_BCM74371A0 */

static int select_one_drive_strength(struct sdhci_host *host, int supported,
				int requested, char *type)
{
	char strength_ok_msg[] = "Changing %s Driver to TYPE %s\n";
	char strength_err_msg[] =
		"Request to change %s Driver to TYPE %s not supported by %s\n";
	if (supported & (1 << requested)) {
		if (requested)
			dev_info(mmc_dev(host->mmc), strength_ok_msg, type,
				strength_type_to_string[requested], type);
		return requested;
	} else {
		dev_warn(mmc_dev(host->mmc), strength_err_msg, type,
			strength_type_to_string[requested], type);
		return 0;
	}
}

static int sdhci_brcmstb_select_drive_strength(struct sdhci_host *host,
					struct mmc_card *card,
					unsigned int max_dtr, int host_drv,
					int card_drv, int *drv_type)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_brcmstb_priv *priv = sdhci_pltfm_priv(pltfm_host);

	*drv_type = select_one_drive_strength(host, host_drv,
					priv->host_driver_type,	"Host");
	return select_one_drive_strength(host, card_drv,
					priv->card_driver_type,	"Card");
}

#ifdef CONFIG_MIPS
static int sdhci_brcmstb_supported(void)
{
	/* Chips with broken SDIO - 7429A0, 7435A0, 7425B0 and 7425B1 */
	if ((BRCM_CHIP_ID() == 0x7425) &&
	    ((BRCM_CHIP_REV() == 0x10) || (BRCM_CHIP_REV() == 0x11)))
		return 0;
	if ((BRCM_CHIP_ID() == 0x7429) && (BRCM_CHIP_REV() == 0x00))
		return 0;
	if ((BRCM_CHIP_ID() == 0x7435) && (BRCM_CHIP_REV() == 0x00))
		return 0;
	return 1;
}


static u32 sdhci_brcmstb_readl(struct sdhci_host *host, int reg)
{
	return __raw_readl(host->ioaddr + reg);
}

static void sdhci_brcmstb_writel(struct sdhci_host *host, u32 val, int reg)
{
	__raw_writel(val, host->ioaddr + reg);
}

static u16 sdhci_brcmstb_readw(struct sdhci_host *host, int reg)
{
	return __raw_readw(host->ioaddr + reg);
}

static void sdhci_brcmstb_writew(struct sdhci_host *host, u16 val, int reg)
{
#if defined(CONFIG_BCM7425B0)
	if (reg == SDHCI_HOST_CONTROL2 && BRCM_CHIP_REV() == 0x12) {
		/* HW7425-1414: I/O voltage uses Power Control Register (29h) */
		int pow_reg = __raw_readb(host->ioaddr + SDHCI_POWER_CONTROL);

		pow_reg &= ~SDHCI_POWER_330;
		if (val & SDHCI_CTRL_VDD_180)
			pow_reg |= SDHCI_POWER_180;
		else
			pow_reg |= SDHCI_POWER_330;
		__raw_writeb(pow_reg, host->ioaddr + SDHCI_POWER_CONTROL);
	}
#endif
	__raw_writew(val, host->ioaddr + reg);
}
#endif

static struct sdhci_ops sdhci_brcmstb_ops = {
	.select_drive_strength	= sdhci_brcmstb_select_drive_strength,
#ifdef CONFIG_MIPS
	.read_w		= sdhci_brcmstb_readw,
	.write_w	= sdhci_brcmstb_writew,
	.read_l		= sdhci_brcmstb_readl,
	.write_l	= sdhci_brcmstb_writel,
#endif
};

static struct sdhci_pltfm_data sdhci_brcmstb_pdata = {
#ifdef CONFIG_MIPS
	.quirks2 = SDHCI_QUIRK2_NO_1_8_V
#endif
};

#if defined(CONFIG_BCM3390A0) || defined(CONFIG_BCM7250B0) ||	\
	defined(CONFIG_BCM7364A0) || defined(CONFIG_BCM7445D0)
static void sdhci_override_caps(struct sdhci_host *host,
				struct sdhci_brcmstb_priv *priv,
				uint32_t cap0_setbits,
				uint32_t cap0_clearbits,
				uint32_t cap1_setbits,
				uint32_t cap1_clearbits)
{
	uint32_t val;
	void *cfg_base = priv->cfg_regs;

	/*
	 * The CAP's override bits in the CFG registers default to all
	 * zeros so start by getting the correct settings from the HOST
	 * CAPS registers and then modify the requested bits and write
	 * them to the override CFG registers.
	 */
	val = sdhci_readl(host, SDHCI_CAPABILITIES);
	val &= ~cap0_clearbits;
	val |= cap0_setbits;
	DEV_WR(SDIO_CFG_REG(cfg_base, CAP_REG0), val);
	val = sdhci_readl(host, SDHCI_CAPABILITIES_1);
	val &= ~cap1_clearbits;
	val |= cap1_setbits;
	DEV_WR(SDIO_CFG_REG(cfg_base, CAP_REG1), val);
	DEV_WR(SDIO_CFG_REG(cfg_base, CAP_REG_OVERRIDE),
		BCHP_SDIO_0_CFG_CAP_REG_OVERRIDE_CAP_REG_OVERRIDE_MASK);
}
#elif !defined(CONFIG_MIPS)
static inline void sdhci_override_caps(struct sdhci_host *host,
				       struct sdhci_brcmstb_priv *priv,
				       uint32_t cap0_setbits,
				       uint32_t cap0_clearbits,
				       uint32_t cap1_setbits,
				       uint32_t cap1_clearbits)
{
}
#endif

#if !defined(CONFIG_MIPS)
static void sdhci_fix_caps(struct sdhci_host *host,
			struct sdhci_brcmstb_priv *priv)
{
#if defined(CONFIG_BCM7445D0)
	/* Fixed for E0 and above */
	if (BRCM_CHIP_REV() >= 0x40)
		return;
#endif
	/* Disable SDR50 support because tuning is broken. */
	sdhci_override_caps(host, priv, 0, 0, 0, SDHCI_SUPPORT_SDR50);
}

#else

#define SDIO_CFG_SET(base, reg, mask) do {				\
		DEV_SET(SDIO_CFG_REG(base, reg),			\
			 BCHP_SDIO_0_CFG_##reg##_##mask##_MASK);	\
	} while (0)
#define SDIO_CFG_UNSET(base, reg, mask) do {				\
		DEV_UNSET(SDIO_CFG_REG(base, reg),			\
			   BCHP_SDIO_0_CFG_##reg##_##mask##_MASK);	\
	} while (0)
#define SDIO_CFG_FIELD(base, reg, field, val) do {			\
		DEV_UNSET(SDIO_CFG_REG(base, reg),			\
			   BCHP_SDIO_0_CFG_##reg##_##field##_MASK);	\
		DEV_SET(SDIO_CFG_REG(base, reg),			\
		 val << BCHP_SDIO_0_CFG_##reg##_##field##_SHIFT);	\
	} while (0)

#define SDHCI_OVERRIDE_OPTIONS_NONE		0x00000000
#define SDHCI_OVERRIDE_OPTIONS_UHS_SDR50	0x00000001
#define SDHCI_OVERRIDE_OPTIONS_TUNING		0x00000002

#define CAP0_SHIFT(field) BCHP_SDIO_0_CFG_CAP_REG0_##field##_SHIFT
#define CAP1_SHIFT(field) BCHP_SDIO_0_CFG_CAP_REG1_##field##_SHIFT

static inline void sdhci_override_caps(void __iomem *cfg_base, int base_clock,
				       int timeout_clock, int options)
{
	uint32_t val;

	/* Set default for every field with all options off */
	val = (0 << CAP0_SHIFT(DDR50_SUPPORT) |			\
	       0 << CAP0_SHIFT(SD104_SUPPORT) |			\
	       0 << CAP0_SHIFT(SDR50) |				\
	       0 << CAP0_SHIFT(SLOT_TYPE) |			\
	       0 << CAP0_SHIFT(ASYNCH_INT_SUPPORT) |		\
	       0 << CAP0_SHIFT(64B_SYS_BUS_SUPPORT) |		\
	       0 << CAP0_SHIFT(1_8V_SUPPORT) |			\
	       0 << CAP0_SHIFT(3_0V_SUPPORT) |			\
	       1 << CAP0_SHIFT(3_3V_SUPPORT) |			\
	       1 << CAP0_SHIFT(SUSP_RES_SUPPORT) |		\
	       1 << CAP0_SHIFT(SDMA_SUPPORT) |			\
	       1 << CAP0_SHIFT(HIGH_SPEED_SUPPORT) |		\
	       1 << CAP0_SHIFT(ADMA2_SUPPORT) |			\
	       1 << CAP0_SHIFT(EXTENDED_MEDIA_SUPPORT) |	\
	       1 << CAP0_SHIFT(MAX_BL) |			\
	       0 << CAP0_SHIFT(BASE_FREQ) |			\
	       1 << CAP0_SHIFT(TIMEOUT_CLK_UNIT) |		\
	       0 << CAP0_SHIFT(TIMEOUT_FREQ));

	val |= (base_clock << CAP0_SHIFT(BASE_FREQ));
	val |= (timeout_clock << CAP0_SHIFT(TIMEOUT_FREQ));
	if (options & SDHCI_OVERRIDE_OPTIONS_UHS_SDR50)
		val |= (1 << CAP0_SHIFT(SDR50)) |
			(1 << CAP0_SHIFT(1_8V_SUPPORT));
	DEV_WR(SDIO_CFG_REG(cfg_base, CAP_REG0), val);

	val = (1 << CAP1_SHIFT(CAP_REG_OVERRIDE) |	\
	       0 << CAP1_SHIFT(SPI_BLK_MODE) |		\
	       0 << CAP1_SHIFT(SPI_MODE) |		\
	       0 << CAP1_SHIFT(CLK_MULT) |		\
	       0 << CAP1_SHIFT(RETUNING_MODES) |	\
	       0 << CAP1_SHIFT(USE_TUNING) |		\
	       0 << CAP1_SHIFT(RETUNING_TIMER) |	\
	       0 << CAP1_SHIFT(Driver_D_SUPPORT) |	\
	       0 << CAP1_SHIFT(Driver_C_SUPPORT) |	\
	       0 << CAP1_SHIFT(Driver_A_SUPPORT));
	DEV_WR(SDIO_CFG_REG(cfg_base, CAP_REG1), val);
}
static void sdhci_fix_caps(struct sdhci_host *host,
			struct sdhci_brcmstb_priv *priv)
{
	void __iomem *cfg_base = priv->cfg_regs;

	if (DEV_RD(SDIO_CFG_REG(cfg_base, SCRATCH)) & 0x01) {
		dev_info(mmc_dev(host->mmc), "Disabled by bootloader\n");
		return;
	}
	dev_info(mmc_dev(host->mmc), "Enabling controller\n");
	DEV_UNSET(SDIO_CFG_REG(cfg_base, SDIO_EMMC_CTRL1), 0xf000);
	DEV_UNSET(SDIO_CFG_REG(cfg_base, SDIO_EMMC_CTRL2), 0x00ff);

	/*
	 * This is broken on all chips and defaults to enabled on
	 * some chips so disable it.
	 */
	SDIO_CFG_UNSET(cfg_base, SDIO_EMMC_CTRL1, SCB_SEQ_EN);

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	/* FRAME_NHW | BUFFER_ABO */
	DEV_SET(SDIO_CFG_REG(cfg_base, SDIO_EMMC_CTRL1), 0x3000);
#else
	/* WORD_ABO | FRAME_NBO | FRAME_NHW */
	DEV_SET(SDIO_CFG_REG(cfg_base, SDIO_EMMC_CTRL1), 0xe000);
	/* address swap only */
	DEV_SET(SDIO_CFG_REG(cfg_base, SDIO_EMMC_CTRL2), 0x0050);
#endif

#if defined(CONFIG_BCM7231B0) || defined(CONFIG_BCM7346B0)
	SDIO_CFG_SET(cfg_base, CAP_REG1, CAP_REG_OVERRIDE);
#elif defined(CONFIG_BCM7344B0)
	SDIO_CFG_SET(cfg_base, CAP_REG0, HIGH_SPEED_SUPPORT);
	SDIO_CFG_SET(cfg_base, CAP_REG1, CAP_REG_OVERRIDE);
#elif defined(CONFIG_BCM7425)
	/*
	 * HW7425-1352: Disable TUNING because it's broken.
	 * Use manual input and output clock delays to work around
	 * 7425B2 timing issues.
	 */
	if (BRCM_CHIP_REV() == 0x12) {
		/* disable tuning */
		sdhci_override_caps(cfg_base, 100, 50,
				    SDHCI_OVERRIDE_OPTIONS_UHS_SDR50);

		/* enable input delay, resolution = 1, value = 8 */
		SDIO_CFG_FIELD(cfg_base, IP_DLY, IP_TAP_DELAY, 8);
		SDIO_CFG_FIELD(cfg_base, IP_DLY, IP_DELAY_CTRL, 1);
		SDIO_CFG_SET(cfg_base, IP_DLY, IP_TAP_EN);

		/* enable output delay */
		SDIO_CFG_FIELD(cfg_base, OP_DLY, OP_TAP_DELAY, 4);
		SDIO_CFG_FIELD(cfg_base, OP_DLY, OP_DELAY_CTRL, 3);
		SDIO_CFG_SET(cfg_base, OP_DLY, OP_TAP_EN);

		/* Use the manual clock delay */
		SDIO_CFG_FIELD(cfg_base, SD_CLOCK_DELAY, INPUT_CLOCK_DELAY, 8);
	}
#elif defined(CONFIG_BCM7563A0)
	sdhci_override_caps(cfg_base, 50, 50, SDHCI_OVERRIDE_OPTIONS_NONE);
#elif defined(CONFIG_BCM7584A0)
	/* enable output delay */
	SDIO_CFG_FIELD(cfg_base, OP_DLY, OP_TAP_DELAY, 4);
	SDIO_CFG_FIELD(cfg_base, OP_DLY, OP_DELAY_CTRL, 3);
	SDIO_CFG_SET(cfg_base, OP_DLY, OP_TAP_EN);
#endif
}
#endif /* CONFIG_MIPS */

#ifdef CONFIG_PM_SLEEP

static int sdhci_brcmstb_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int res;

	res = sdhci_suspend_host(host);
	if (res)
		return res;
	clk_disable(pltfm_host->clk);
	return res;
}

static int sdhci_brcmstb_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_brcmstb_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int err;

	err = clk_enable(pltfm_host->clk);
	if (err)
		return err;
	sdhci_fix_caps(host, priv);
	set_host_driver_strength_overrides(host, priv);
	return sdhci_resume_host(host);
}

#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sdhci_brcmstb_pmops, sdhci_brcmstb_suspend,
			sdhci_brcmstb_resume);

static void sdhci_brcmstb_of_get_driver_type(struct device_node *dn,
					char *name, int *dtype)
{
	const char *driver_type;
	int res;

	res = of_property_read_string(dn, name, &driver_type);
	if (res == 0) {
		if (strcmp(driver_type, "A") == 0)
			*dtype = MMC_SET_DRIVER_TYPE_A;
		else if (strcmp(driver_type, "B") == 0)
			*dtype = MMC_SET_DRIVER_TYPE_B;
		else if (strcmp(driver_type, "C") == 0)
			*dtype = MMC_SET_DRIVER_TYPE_C;
		else if (strcmp(driver_type, "D") == 0)
			*dtype = MMC_SET_DRIVER_TYPE_D;
	}
}


static int sdhci_brcmstb_probe(struct platform_device *pdev)
{
	struct sdhci_brcmstb_pdata *pdata = pdev->dev.platform_data;
	struct device_node *dn = pdev->dev.of_node;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_brcmstb_priv *priv;
	struct clk *clk;
	struct resource *resource;
	int res;

#ifdef CONFIG_MIPS
	if (!sdhci_brcmstb_supported()) {
		dev_info(&pdev->dev, "Disabled, unsupported chip revision\n");
		return -ENODEV;
	}
#endif

	clk = of_clk_get_by_name(dn, "sw_sdio");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Clock not found in Device Tree\n");
		clk = NULL;
	}
	res = clk_prepare_enable(clk);
	if (res)
		goto undo_clk_get;

#if defined(CONFIG_BCM74371A0)
	/* Only enable reset workaround for 74371a0 senior */
	if (BRCM_CHIP_ID() == 0x7439)
		sdhci_brcmstb_ops.write_b = sdhci_brcmstb_writeb;
#endif /* CONFIG_BCM74371A0 */
	sdhci_brcmstb_pdata.ops = &sdhci_brcmstb_ops;
	host = sdhci_pltfm_init(pdev, &sdhci_brcmstb_pdata,
				sizeof(struct sdhci_brcmstb_priv));
	if (IS_ERR(host)) {
		res = PTR_ERR(host);
		goto undo_clk_prep;
	}


	/* Enable MMC_CAP2_HC_ERASE_SZ for better max discard calculations */
	host->mmc->caps2 |= MMC_CAP2_HC_ERASE_SZ;
	if (pdata) {
		host->mmc->caps |= pdata->caps;
		host->mmc->caps2 |= pdata->caps2;
	}

	sdhci_get_of_property(pdev);
	mmc_of_parse(host->mmc);
	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);
	resource = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (resource == NULL) {
		dev_err(&pdev->dev, "can't get SDHCI CFG base address\n");
		return -EINVAL;
	}
	priv->cfg_regs = devm_request_and_ioremap(&pdev->dev, resource);
	if (!priv->cfg_regs) {
		dev_err(&pdev->dev, "can't map register space\n");
		return -EINVAL;
	}
	sdhci_fix_caps(host, priv);

	sdhci_brcmstb_of_get_driver_type(dn, "host-driver-strength",
					&priv->host_driver_type);
	sdhci_brcmstb_of_get_driver_type(dn, "host-hs-driver-strength",
					&priv->host_hs_driver_type);
	sdhci_brcmstb_of_get_driver_type(dn, "card-driver-strength",
					&priv->card_driver_type);
	set_host_driver_strength_overrides(host, priv);

	res = sdhci_add_host(host);
	if (res)
		goto undo_pltfm_init;

	pltfm_host->clk = clk;
	return res;

undo_pltfm_init:
	sdhci_pltfm_free(pdev);
undo_clk_prep:
	clk_disable_unprepare(clk);
undo_clk_get:
	clk_put(clk);
	return res;
}

static int sdhci_brcmstb_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int res;
	res = sdhci_pltfm_unregister(pdev);
	clk_disable_unprepare(pltfm_host->clk);
	clk_put(pltfm_host->clk);
	return res;
}


static const struct of_device_id sdhci_brcm_of_match[] = {
	{ .compatible = "brcm,sdhci-brcmstb" },
	{},
};

static struct platform_driver sdhci_brcmstb_driver = {
	.driver		= {
		.name	= "sdhci-brcmstb",
		.owner	= THIS_MODULE,
		.pm	= &sdhci_brcmstb_pmops,
		.of_match_table = of_match_ptr(sdhci_brcm_of_match),
	},
	.probe		= sdhci_brcmstb_probe,
	.remove		= sdhci_brcmstb_remove,
};

module_platform_driver(sdhci_brcmstb_driver);

MODULE_DESCRIPTION("SDHCI driver for Broadcom");
MODULE_AUTHOR("Al Cooper <acooper@broadcom.com>");
MODULE_LICENSE("GPL v2");
