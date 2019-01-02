/*
 * Copyright (C) 2014-2016 Broadcom Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This module is used by both the bootloader and Linux and
 * contains USB initialization for power up and S3 resume.
 */

#if defined(_BOLT_)
#include "lib_types.h"
#include "common.h"
#include "bchp_common.h"
#include "bchp_usb_ctrl.h"
#include "bchp_sun_top_ctrl.h"
#include "timer.h"
#define msleep bolt_msleep
#define udelay bolt_usleep
#else
#include <linux/delay.h>
#include <linux/brcmstb/brcmstb.h>
#endif

#include "usb-brcm-common-init.h"

#define USB_CTRL_REG(base, reg)	(base + BCHP_USB_CTRL_##reg - \
		BCHP_USB_CTRL_SETUP)
#define USB_CTRL_MASK(reg, field) (BCHP_USB_CTRL_##reg##_##field##_MASK)
#define USB_CTRL_SET(base, reg, mask) DEV_SET(USB_CTRL_REG(base, reg),	\
						USB_CTRL_MASK(reg, mask))
#define USB_CTRL_UNSET(base, reg, mask) DEV_UNSET(USB_CTRL_REG(base, reg),  \
						USB_CTRL_MASK(reg, mask))
#define USB_XHCI_EC_REG(base, reg) (base + BCHP_USB_XHCI_EC_##reg - \
		BCHP_USB_XHCI_EC_REG_START)

#define MDIO_USB2	0
#define MDIO_USB3	(1 << 31)

#define USB_CTRL_SETUP_CONDITIONAL_BITS (	\
		USB_CTRL_MASK(SETUP, BABO) |	\
		USB_CTRL_MASK(SETUP, FNHW) |	\
		USB_CTRL_MASK(SETUP, FNBO) |	\
		USB_CTRL_MASK(SETUP, WABO) |	\
		USB_CTRL_MASK(SETUP, IOC)  |	\
		USB_CTRL_MASK(SETUP, IPP))

#ifdef __LITTLE_ENDIAN
#define ENDIAN_SETTINGS ( \
		USB_CTRL_MASK(SETUP, BABO) |	\
		USB_CTRL_MASK(SETUP, FNHW))
#else
#define ENDIAN_SETTINGS ( \
		USB_CTRL_MASK(SETUP, FNHW) |	 \
		USB_CTRL_MASK(SETUP, FNBO) |	 \
		USB_CTRL_MASK(SETUP, WABO))
#endif

static uint32_t usb_mdio_read(uintptr_t ctrl_base, uint32_t reg, int mode)
{
	uint32_t data;

	data = (reg << 16) | mode;
	DEV_WR(USB_CTRL_REG(ctrl_base, MDIO), data);
	data |= (1 << 24);
	DEV_WR(USB_CTRL_REG(ctrl_base, MDIO), data);
	data &= ~(1 << 24);
	udelay(10);
	DEV_WR(USB_CTRL_REG(ctrl_base, MDIO), data);
	udelay(10);

	return DEV_RD(USB_CTRL_REG(ctrl_base, MDIO2)) & 0xffff;
}

static void usb_mdio_write(uintptr_t ctrl_base, uint32_t reg,
			uint32_t val, int mode)
{
	uint32_t data;

	data = (reg << 16) | val | mode;
	DEV_WR(USB_CTRL_REG(ctrl_base, MDIO), data);
	data |= (1 << 25);
	DEV_WR(USB_CTRL_REG(ctrl_base, MDIO), data);
	data &= ~(1 << 25);
	udelay(10);
	DEV_WR(USB_CTRL_REG(ctrl_base, MDIO), data);
}


static void usb_phy_ldo_fix(uintptr_t ctrl_base)
{
	/* first disable FSM but also leave it that way */
	/* to allow normal suspend/resume */
	DEV_UNSET(USB_CTRL_REG(ctrl_base, UTMI_CTL_1),
		USB_CTRL_MASK(UTMI_CTL_1, POWER_UP_FSM_EN_P1) |
		USB_CTRL_MASK(UTMI_CTL_1, POWER_UP_FSM_EN));

	/* reset USB 2.0 PLL */
	USB_CTRL_UNSET(ctrl_base, PLL_CTL, PLL_RESETB);
	msleep(1);
	USB_CTRL_SET(ctrl_base, PLL_CTL, PLL_RESETB);
	msleep(10);

}


static void usb2_eye_fix(uintptr_t ctrl_base)
{
	/* Increase USB 2.0 TX level to meet spec requirement */
	usb_mdio_write(ctrl_base, 0x1f, 0x80a0, MDIO_USB2);
	usb_mdio_write(ctrl_base, 0x0a, 0xc6a0, MDIO_USB2);
}


static void usb3_pll_fix(uintptr_t ctrl_base)
{
	/* Set correct window for PLL lock detect */
	usb_mdio_write(ctrl_base, 0x1f, 0x8000, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x07, 0x1503, MDIO_USB3);
}


static void usb3_enable_pipe_reset(uintptr_t ctrl_base)
{
	uint32_t val;

	/* Re-enable USB 3.0 pipe reset */
	usb_mdio_write(ctrl_base, 0x1f, 0x8000, MDIO_USB3);
	val = usb_mdio_read(ctrl_base, 0x0f, MDIO_USB3) | 0x200;
	usb_mdio_write(ctrl_base, 0x0f, val, MDIO_USB3);
}


static void usb3_enable_sigdet(uintptr_t ctrl_base)
{
	uint32_t val, ofs;
	int ii;

	ofs = 0;
	for (ii = 0; ii < 2; ++ii) {
		/* Set correct default for sigdet */
		usb_mdio_write(ctrl_base, 0x1f, (0x8080 + ofs), MDIO_USB3);
		val = usb_mdio_read(ctrl_base, 0x05, MDIO_USB3);
		val = (val & ~0x800f) | 0x800d;
		usb_mdio_write(ctrl_base, 0x05, val, MDIO_USB3);
		ofs = 0x1000;
	}
}


static void usb3_enable_skip_align(uintptr_t ctrl_base)
{
	uint32_t val, ofs;
	int ii;

	ofs = 0;
	for (ii = 0; ii < 2; ++ii) {
		/* Set correct default for SKIP align */
		usb_mdio_write(ctrl_base, 0x1f, (0x8060 + ofs), MDIO_USB3);
		val = usb_mdio_read(ctrl_base, 0x01, MDIO_USB3) | 0x200;
		usb_mdio_write(ctrl_base, 0x01, val, MDIO_USB3);
		ofs = 0x1000;
	}
}


static void usb3_unfreeze_aeq(uintptr_t ctrl_base)
{
	uint32_t val, ofs;
	int ii;

	ofs = 0;
	for (ii = 0; ii < 2; ++ii) {
		/* Let EQ freeze after TSEQ */
		usb_mdio_write(ctrl_base, 0x1f, (0x80e0 + ofs), MDIO_USB3);
		val = usb_mdio_read(ctrl_base, 0x01, MDIO_USB3);
		val &= ~0x0008;
		usb_mdio_write(ctrl_base, 0x01, val, MDIO_USB3);
		ofs = 0x1000;
	}
}


static void usb3_pll_54Mhz(uintptr_t ctrl_base)
{
#if defined(CONFIG_BCM7271A0) || defined(CONFIG_BCM7268A0) || \
	defined(CONFIG_BCM7364)
	/*
	 * On the 7271a0 and 7268a0, the reference clock for the
	 * 3.0 PLL has been changed from 50MHz to 54MHz so the
	 * PLL needs to be reprogramed. Later chips will have
	 * the PLL programmed correctly on power-up.
	 * See SWLINUX-4006.
	 *
	 * On the 7364C0, the reference clock for the
	 * 3.0 PLL has been changed from 50MHz to 54MHz to
	 * work around a MOCA issue.
	 * See SWLINUX-4169.
	 */
	uint32_t ofs;
	int ii;

#if defined(CONFIG_BCM7364)
	/* Only for 7364C0 and later */
	if ((BDEV_RD(BCHP_SUN_TOP_CTRL_PRODUCT_ID) & 0xff) < 0x20)
		return;
#endif

	/* set USB 3.0 PLL to accept 54Mhz reference clock */
	USB_CTRL_UNSET(ctrl_base, USB30_CTL1, phy3_pll_seq_start);

	usb_mdio_write(ctrl_base, 0x1f, 0x8000, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x10, 0x5784, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x11, 0x01d0, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x12, 0x1DE8, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x13, 0xAA80, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x14, 0x8826, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x15, 0x0044, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x16, 0x8000, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x17, 0x0851, MDIO_USB3);
	usb_mdio_write(ctrl_base, 0x18, 0x0000, MDIO_USB3);

	/* both ports */
	ofs = 0;
	for (ii = 0; ii < 2; ++ii) {
		usb_mdio_write(ctrl_base, 0x1f, (0x8040 + ofs), MDIO_USB3);
		usb_mdio_write(ctrl_base, 0x03, 0x0090, MDIO_USB3);
		usb_mdio_write(ctrl_base, 0x04, 0x0134, MDIO_USB3);
		usb_mdio_write(ctrl_base, 0x1f, (0x8020 + ofs), MDIO_USB3);
		usb_mdio_write(ctrl_base, 0x01, 0x00e2, MDIO_USB3);
		ofs = 0x1000;
	}

	/* restart  PLL sequence */
	USB_CTRL_SET(ctrl_base, USB30_CTL1, phy3_pll_seq_start);
	msleep(1);
#endif
}


static void usb3_ssc_enable(uintptr_t ctrl_base)
{
	uint32_t val;

	/* Enable USB 3.0 TX spread spectrum */
	usb_mdio_write(ctrl_base, 0x1f, 0x8040, MDIO_USB3);
	val = usb_mdio_read(ctrl_base, 0x01, MDIO_USB3) | 0xf;
	usb_mdio_write(ctrl_base, 0x01, val, MDIO_USB3);

	/* Currently, USB 3.0 SSC is enabled via port 0 MDIO registers,
	 * which should have been adequate. However, due to a bug in the
	 * USB 3.0 PHY, it must be enabled via both ports (HWUSB3DVT-26).
	 */
	usb_mdio_write(ctrl_base, 0x1f, 0x9040, MDIO_USB3);
	val = usb_mdio_read(ctrl_base, 0x01, MDIO_USB3) | 0xf;
	usb_mdio_write(ctrl_base, 0x01, val, MDIO_USB3);
}


static void usb3_phy_workarounds(uintptr_t ctrl_base)
{
	usb3_pll_fix(ctrl_base);
	usb3_pll_54Mhz(ctrl_base);
	usb3_ssc_enable(ctrl_base);
	usb3_enable_pipe_reset(ctrl_base);
	usb3_enable_sigdet(ctrl_base);
	usb3_enable_skip_align(ctrl_base);
	usb3_unfreeze_aeq(ctrl_base);
}


static void memc_fix(uintptr_t ctrl_base)
{
#if defined(CONFIG_BCM7445D0) || defined(CONFIG_BCM7445E0)
	/*
	 * This is a workaround for HW7445-1869 where a DMA write ends up
	 * doing a read pre-fetch after the end of the DMA buffer. This
	 * causes a problem when the DMA buffer is at the end of physical
	 * memory, causing the pre-fetch read to access non-existent memory,
	 * and the chip bondout has MEMC2 disabled. When the pre-fetch read
	 * tries to use the disabled MEMC2, it hangs the bus. The workaround
	 * is to disable MEMC2 access in the usb controller which avoids
	 * the hang.
	 */
	uint32_t prid;

	prid = BDEV_RD(BCHP_SUN_TOP_CTRL_PRODUCT_ID) & 0xfffff000;
	switch (prid) {
	case 0x72520000:
	case 0x74480000:
	case 0x74490000:
	case 0x07252000:
	case 0x07448000:
	case 0x07449000:
		USB_CTRL_UNSET(ctrl_base, SETUP, scb2_en);
	}
#endif
}

#if defined(CONFIG_BCM74371A0)
#if defined(_BOLT_)
#include "bchp_usb_xhci_ec.h"
#endif
static void usb3_otp_fix(uintptr_t ctrl_base, uintptr_t xhci_ec_base)
{
	uintptr_t val;

	if (xhci_ec_base == 0)
		return;
	DEV_WR(USB_XHCI_EC_REG(xhci_ec_base, IRAADR), 0xa20c);
	val = DEV_RD(USB_XHCI_EC_REG(xhci_ec_base, IRADAT));

	/* set cfg_pick_ss_lock */
	val |= (1 << 27);
	DEV_WR(USB_XHCI_EC_REG(xhci_ec_base, IRADAT), val);

	/* Reset USB 3.0 PHY for workaround to take effect */
	USB_CTRL_UNSET(ctrl_base, USB30_CTL1, phy3_resetb);
	USB_CTRL_SET(ctrl_base, USB30_CTL1, phy3_resetb);
}
#else
static void usb3_otp_fix(uintptr_t ctrl_base, uintptr_t xhci_ec_regs)
{
}
#endif


static void xhci_soft_reset(uintptr_t ctrl, int on_off)
{
	/* Assert reset */
	if (on_off) {
#if defined(BCHP_USB_CTRL_USB_PM_xhc_soft_resetb_MASK)
		USB_CTRL_UNSET(ctrl, USB_PM, xhc_soft_resetb);
#else
		USB_CTRL_UNSET(ctrl, USB30_CTL1, xhc_soft_resetb);
#endif
	}
	/* De-assert reset */
	else {
#if defined(BCHP_USB_CTRL_USB_PM_xhc_soft_resetb_MASK)
		USB_CTRL_SET(ctrl, USB_PM, xhc_soft_resetb);
#else
		USB_CTRL_SET(ctrl, USB30_CTL1, xhc_soft_resetb);
#endif
	}
}


void brcm_usb_common_init(struct brcm_usb_common_init_params *params)
{
	uint32_t reg;
	uintptr_t ctrl = params->ctrl_regs;
	int change_ipp = 0;

	xhci_soft_reset(ctrl, 1);
#if defined(CONFIG_BCM7366)
	/*
	 * The PHY3_SOFT_RESETB bits default to the wrong state.
	 */
	DEV_SET(USB_CTRL_REG(ctrl, USB30_PCTL),
		USB_CTRL_MASK(USB30_PCTL, PHY3_SOFT_RESETB_P1) |
		USB_CTRL_MASK(USB30_PCTL, PHY3_SOFT_RESETB));
#endif
#if defined(CONFIG_BCM7366C0)
	/*
	 * Don't enable this so the memory controller doesn't read
	 * into memory holes. NOTE: This bit is low true  on 7366C0.
	 */
	DEV_SET(USB_CTRL_REG(ctrl, EBRIDGE),
		USB_CTRL_MASK(EBRIDGE, ESTOP_SCB_REQ));
#endif

	/* Take USB out of power down */
#if defined(BCHP_USB_CTRL_PLL_CTL_PLL_IDDQ_PWRDN_MASK)
	USB_CTRL_UNSET(ctrl, PLL_CTL, PLL_IDDQ_PWRDN);
	/* 1 millisecond - for USB clocks to settle down */
	msleep(1);
#endif
#if defined(BCHP_USB_CTRL_USB_PM_USB_PWRDN_MASK)
	USB_CTRL_UNSET(ctrl, USB_PM, USB_PWRDN);
	/* 1 millisecond - for USB clocks to settle down */
	msleep(1);
#endif
#if defined(BCHP_USB_CTRL_USB_PM_soft_reset_MASK)
	/* 7271a0. */
	USB_CTRL_UNSET(ctrl, USB_PM, soft_reset);
	msleep(1);
#endif

#if defined(BCHP_USB_CTRL_USB30_CTL1_usb3_ipp_MASK)
	/* Starting with the 7445d0, there are no longer separate 3.0
	 * versions of IOC and IPP.
	 */
	if (params->ioc)
		USB_CTRL_SET(ctrl, USB30_CTL1, usb3_ioc);
	if (params->ipp == 1)
		USB_CTRL_SET(ctrl, USB30_CTL1, usb3_ipp);
#endif

#if !defined(CONFIG_BCM74371A0) && !defined(CONFIG_BCM7364)
	/*
	 * HW7439-637: 7439a0 and its derivatives do not have large enough
	 * descriptor storage for this.
	 */
	USB_CTRL_SET(ctrl, SETUP, ss_ehci64bit_en);
#endif

#if defined(BCHP_USB_CTRL_USB30_CTL1_phy3_pll_seq_start_MASK)
	/*
	 * Kick start USB3 PHY
	 * Make sure it's low to insure a rising edge.
	 */
	USB_CTRL_UNSET(ctrl, USB30_CTL1, phy3_pll_seq_start);
	USB_CTRL_SET(ctrl, USB30_CTL1, phy3_pll_seq_start);
#endif

	/* Block auto PLL suspend by USB2 PHY */
	USB_CTRL_SET(ctrl, PLL_CTL, PLL_SUSPEND_EN);

	usb_phy_ldo_fix(ctrl);
	usb2_eye_fix(ctrl);
	if (params->has_xhci)
		usb3_phy_workarounds(ctrl);

	/* Setup the endian bits */
	reg = DEV_RD(USB_CTRL_REG(ctrl, SETUP));
	reg &= ~USB_CTRL_SETUP_CONDITIONAL_BITS;
	reg |= ENDIAN_SETTINGS;

#if defined(CONFIG_BCM7364)
	if ((BDEV_RD(BCHP_SUN_TOP_CTRL_PRODUCT_ID) == 0x73640000))
		/* Suppress overcurrent indication from USB30 ports for A0 */
		reg |= USB_CTRL_MASK(SETUP, OC3_DISABLE);
#endif

#if defined(BCHP_USB_CTRL_SETUP_strap_ipp_sel_MASK)
	if (params->ipp != 2)
		/* override ipp strap pin (if it exists) */
		reg &= ~(USB_CTRL_MASK(SETUP, strap_ipp_sel));
#endif
	/*
	 * Make sure the the second and third memory controller
	 * interfaces are enabled, if they exist.
	 */
#if defined(BCHP_USB_CTRL_SETUP_scb1_en_MASK)
	reg |= USB_CTRL_MASK(SETUP, scb1_en);
#endif
#if defined(BCHP_USB_CTRL_SETUP_scb2_en_MASK)
	reg |= USB_CTRL_MASK(SETUP, scb2_en);
#endif

	/* Override the default OC and PP polarity */
	if (params->ioc)
		reg |= USB_CTRL_MASK(SETUP, IOC);
	if ((params->ipp == 1) && ((reg & USB_CTRL_MASK(SETUP, IPP)) == 0)) {
		change_ipp = 1;
		reg |= USB_CTRL_MASK(SETUP, IPP);
	}
	DEV_WR(USB_CTRL_REG(ctrl, SETUP), reg);

	/*
	 * If we're changing IPP, make sure power is off long enough
	 * to turn off any connected devices.
	 */
	if (change_ipp)
		msleep(50);
	memc_fix(ctrl);
	if (params->has_xhci) {
		xhci_soft_reset(ctrl, 0);
		usb3_otp_fix(ctrl, params->xhci_ec_regs);
	}
#ifdef BCHP_USB_CTRL_USB_DEVICE_CTL1_port_mode_MASK
	reg = DEV_RD(USB_CTRL_REG(ctrl, USB_DEVICE_CTL1));
	reg &= ~BCHP_USB_CTRL_USB_DEVICE_CTL1_port_mode_MASK;
	reg |= params->device_mode;
	DEV_WR(USB_CTRL_REG(ctrl, USB_DEVICE_CTL1), reg);
#endif
#ifdef BCHP_USB_CTRL_USB_PM_bdc_soft_resetb_MASK
	switch (params->device_mode) {
	case USB_CTLR_DEVICE_OFF:
		USB_CTRL_UNSET(ctrl, USB_PM, bdc_soft_resetb);
		break;
	default:
		USB_CTRL_SET(ctrl, USB_PM, bdc_soft_resetb);
		break;
	}
#ifdef BCHP_USB_CTRL_SETUP_strap_cc_drd_mode_enable_sel_MASK
	/* Never use the strap, it's going away. */
	USB_CTRL_UNSET(ctrl, SETUP, strap_cc_drd_mode_enable_sel);
#endif
#ifdef BCHP_USB_CTRL_SETUP_cc_drd_mode_enable_MASK
	if (params->device_mode == USB_CTLR_DEVICE_TYPEC_PD)
		USB_CTRL_SET(ctrl, SETUP, cc_drd_mode_enable);
	else
		USB_CTRL_UNSET(ctrl, SETUP, cc_drd_mode_enable);

#endif
#endif
}

EXPORT_SYMBOL(brcm_usb_common_init);
