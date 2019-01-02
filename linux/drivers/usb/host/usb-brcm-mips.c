/*
 * Copyright (C) 2010 Broadcom Corporation
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/usb.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/brcmstb/brcmstb.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
#include <linux/usb/hcd.h>
#else
#include "../core/hcd.h"
#endif

#define MAX_HCD			8

static struct clk *usb_clk;

/***********************************************************************
 * Library functions
 ***********************************************************************/

int brcm_usb_probe(struct platform_device *pdev,
		const struct hc_driver *hc_driver,
		struct usb_hcd **hcdptr,
		struct clk **hcd_clk_ptr)
{
	struct resource *res_mem;
	struct usb_hcd *hcd;
	int irq;
	struct clk *clk;
	int err;

	if (usb_disabled())
		return -ENODEV;

	if (!usb_clk) {
		clk = clk_get(NULL, "usb");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Error getting Clock\n");
			return PTR_ERR(clk);
		}
		usb_clk = clk;
	}
	*hcd_clk_ptr = usb_clk;
	clk_enable(usb_clk);

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		dev_err(&pdev->dev, "platform_get_resource error.\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq error.\n");
		return -ENODEV;
	}

	/* initialize hcd */
	hcd = usb_create_hcd(hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Failed to create hcd\n");
		clk_disable(usb_clk);
		return -ENOMEM;
	}
	*hcdptr = hcd;
	hcd->rsrc_start = res_mem->start;
	hcd->rsrc_len = resource_size(res_mem);
	hcd->regs = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto err_put_hcd;
	}
	err = usb_add_hcd(hcd, irq, IRQF_DISABLED);
	if (err)
		goto err_put_hcd;

	usb_disable_autosuspend(hcd->self.root_hub);
	platform_set_drvdata(pdev, hcd);
	return err;

err_put_hcd:
	clk_disable(usb_clk);
	usb_put_hcd(hcd);

	return err;
}
EXPORT_SYMBOL(brcm_usb_probe);

int brcm_usb_remove(struct platform_device *pdev, struct clk *hcd_clk)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	clk_disable(hcd_clk);

	return 0;
}
EXPORT_SYMBOL(brcm_usb_remove);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom USB common functions");
