/*
 * SAMSUNG EXYNOS USB HOST EHCI Controller
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 *	Vivek Gautam <gautam.vivek@samsung.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <fdtdec.h>
#include <libfdt.h>
#include <malloc.h>
#include <usb.h>
#include <asm/arch/ehci.h>
#include <asm/gpio.h>
#if !defined(CONFIG_ARCH_NEXELL)
#include <asm/arch/cpu.h>
#include <asm/arch/system.h>
#include <asm/arch/power.h>
#else
#include <asm/arch/reset.h>
#include <asm/arch/nexell.h>
#include <asm/io.h>
#endif
#include <asm-generic/errno.h>
#include <linux/compat.h>
#include "ehci.h"

/* Declare global data pointer */
DECLARE_GLOBAL_DATA_PTR;

struct exynos_ehci_platdata {
	struct usb_platdata usb_plat;
	fdt_addr_t hcd_base;
	fdt_addr_t phy_base;
	struct gpio_desc vbus_gpio;
};

/**
 * Contains pointers to register base addresses
 * for the usb controller.
 */
struct exynos_ehci {
	struct ehci_ctrl ctrl;
	struct exynos_usb_phy *usb;
	struct ehci_hccr *hcd;
};

static int ehci_usb_ofdata_to_platdata(struct udevice *dev)
{
	struct exynos_ehci_platdata *plat = dev_get_platdata(dev);
	const void *blob = gd->fdt_blob;
	unsigned int node;
	int depth;

	/*
	 * Get the base address for XHCI controller from the device node
	 */
	plat->hcd_base = dev_get_addr(dev);
	if (plat->hcd_base == FDT_ADDR_T_NONE) {
		debug("Can't get the XHCI register base address\n");
		return -ENXIO;
	}

	depth = 0;
	node = fdtdec_next_compatible_subnode(blob, dev->of_offset,
				COMPAT_SAMSUNG_EXYNOS_USB_PHY, &depth);
	if (node <= 0) {
		debug("XHCI: Can't get device node for usb3-phy controller\n");
		return -ENODEV;
	}

	/*
	 * Get the base address for usbphy from the device node
	 */
	plat->phy_base = fdtdec_get_addr(blob, node, "reg");
	if (plat->phy_base == FDT_ADDR_T_NONE) {
		debug("Can't get the usbphy register address\n");
		return -ENXIO;
	}
	/* Vbus gpio */
	gpio_request_by_name(dev, "samsung,vbus-gpio", 0,
			     &plat->vbus_gpio, GPIOD_IS_OUT);

	return 0;
}

#if defined(CONFIG_ARCH_NEXELL)
static void nx_setup_usb_phy(struct nx_usb_phy *usb)
{
	u32 reg;
	u32 reg1, reg2, reg3;
	u32 fladj_val, bit_num, bit_pos = NX_HOST_CON2_SS_FLADJ_VAL_0_OFFSET;

	fladj_val = NX_HOST_CON2_SS_FLADJ_VAL_0_SEL;

	reg = fladj_val;

	for (bit_num = 0; bit_num < NX_HOST_CON2_SS_FLADJ_VAL_NUM; bit_num++) {
		if (fladj_val & (1 << bit_num))
			reg |= (NX_HOST_CON2_SS_FLADJ_VAL_MAX << bit_pos);
		bit_pos -= NX_HOST_CON2_SS_FLADJ_VAL_OFFSET;
	}
	writel(reg, (void *)(&usb->usbhost_con[2]));

	nx_rstcon_setrst(RESET_ID_USB20HOST, RSTCON_ASSERT);
	nx_rstcon_setrst(RESET_ID_USB20HOST, RSTCON_NEGATE);

	writel(readl((void *)(&usb->usbhost_con[0])) &
	       ~NX_HOST_CON0_HSIC_CLK_MASK,
	       (void *)(&usb->usbhost_con[0]));
	writel(readl((void *)(&usb->usbhost_con[0])) |
	       NX_HOST_CON0_HSIC_480M_FROM_OTG_PHY,
	       (void *)(&usb->usbhost_con[0]));

	writel(readl((void *)(&usb->usbhost_con[0])) &
	       ~NX_HOST_CON0_HSIC_EN_MASK,
	       (void *)(&usb->usbhost_con[0]));
	writel(readl((void *)(&usb->usbhost_con[0])) |
	       NX_HOST_CON0_HSIC_EN_PORT1,
	       (void *)(&usb->usbhost_con[0]));

	reg = readl((void *)(&usb->usbhost_con[2])) &
		~NX_HOST_CON2_SS_DMA_BURST_MASK;
	writel(reg | NX_HOST_CON2_EHCI_SS_ENABLE_DMA_BURST,
	       (void *)(&usb->usbhost_con[2]));

	reg1 = readl((void *)(&usb->usbhost_con[0])) |
		NX_HOST_CON0_SS_WORD_IF_16;
	reg2 = readl((void *)(&usb->usbhost_con[4])) |
		NX_HOST_CON4_WORDINTERFACE_16;
	reg3 = readl((void *)(&usb->usbhost_con[6])) |
		NX_HOST_CON6_HSIC_WORDINTERFACE_16;

	writel(reg1, (void *)(&usb->usbhost_con[0]));
	writel(reg2, (void *)(&usb->usbhost_con[4]));
	writel(reg3, (void *)(&usb->usbhost_con[6]));

	reg   = readl((void *)(&usb->usbhost_con[3]));
	reg  &= ~NX_HOST_CON3_POR_MASK;
	reg  |=  NX_HOST_CON3_POR_ENB;
	writel(reg, (void *)(&usb->usbhost_con[3]));
	udelay(10);

	writel(readl((void *)(&usb->usbhost_con[0])) |
	       NX_HOST_CON0_HSIC_CLK_MASK,
	       (void *)(&usb->usbhost_con[0]));

	writel(readl((void *)(&usb->usbhost_con[5])) &
	       ~NX_HOST_CON5_HSIC_POR_MASK,
	       (void *)(&usb->usbhost_con[5]));
	writel(readl((void *)(&usb->usbhost_con[5])) |
	       NX_HOST_CON5_HSIC_POR_ENB,
	       (void *)(&usb->usbhost_con[5]));

	udelay(100);

	reg = readl((void *)(&usb->usbhost_con[0])) |
		NX_HOST_CON0_UTMI_RESET_SYNC;
	writel(reg, (void *)(&usb->usbhost_con[0]));

	writel(readl((void *)(&usb->usbhost_con[0])) |
	       NX_HOST_CON0_AHB_RESET_SYNC,
	       (void *)(&usb->usbhost_con[0]));
}
#else
static void exynos5_setup_usb_phy(struct exynos_usb_phy *usb)
{
	u32 hsic_ctrl;

	clrbits_le32(&usb->usbphyctrl0,
			HOST_CTRL0_FSEL_MASK |
			HOST_CTRL0_COMMONON_N |
			/* HOST Phy setting */
			HOST_CTRL0_PHYSWRST |
			HOST_CTRL0_PHYSWRSTALL |
			HOST_CTRL0_SIDDQ |
			HOST_CTRL0_FORCESUSPEND |
			HOST_CTRL0_FORCESLEEP);

	setbits_le32(&usb->usbphyctrl0,
			/* Setting up the ref freq */
			(CLK_24MHZ << 16) |
			/* HOST Phy setting */
			HOST_CTRL0_LINKSWRST |
			HOST_CTRL0_UTMISWRST);
	udelay(10);
	clrbits_le32(&usb->usbphyctrl0,
			HOST_CTRL0_LINKSWRST |
			HOST_CTRL0_UTMISWRST);

	/* HSIC Phy Setting */
	hsic_ctrl = (HSIC_CTRL_FORCESUSPEND |
			HSIC_CTRL_FORCESLEEP |
			HSIC_CTRL_SIDDQ);

	clrbits_le32(&usb->hsicphyctrl1, hsic_ctrl);
	clrbits_le32(&usb->hsicphyctrl2, hsic_ctrl);

	hsic_ctrl = (((HSIC_CTRL_REFCLKDIV_12 & HSIC_CTRL_REFCLKDIV_MASK)
				<< HSIC_CTRL_REFCLKDIV_SHIFT)
			| ((HSIC_CTRL_REFCLKSEL & HSIC_CTRL_REFCLKSEL_MASK)
				<< HSIC_CTRL_REFCLKSEL_SHIFT)
			| HSIC_CTRL_UTMISWRST);

	setbits_le32(&usb->hsicphyctrl1, hsic_ctrl);
	setbits_le32(&usb->hsicphyctrl2, hsic_ctrl);

	udelay(10);

	clrbits_le32(&usb->hsicphyctrl1, HSIC_CTRL_PHYSWRST |
					HSIC_CTRL_UTMISWRST);

	clrbits_le32(&usb->hsicphyctrl2, HSIC_CTRL_PHYSWRST |
					HSIC_CTRL_UTMISWRST);

	udelay(20);

	/* EHCI Ctrl setting */
	setbits_le32(&usb->ehcictrl,
			EHCICTRL_ENAINCRXALIGN |
			EHCICTRL_ENAINCR4 |
			EHCICTRL_ENAINCR8 |
			EHCICTRL_ENAINCR16);
}

static void exynos4412_setup_usb_phy(struct exynos4412_usb_phy *usb)
{
	writel(CLK_24MHZ, &usb->usbphyclk);

	clrbits_le32(&usb->usbphyctrl, (PHYPWR_NORMAL_MASK_HSIC0 |
		PHYPWR_NORMAL_MASK_HSIC1 | PHYPWR_NORMAL_MASK_PHY1 |
		PHYPWR_NORMAL_MASK_PHY0));

	setbits_le32(&usb->usbphyrstcon, (RSTCON_HOSTPHY_SWRST | RSTCON_SWRST));
	udelay(10);
	clrbits_le32(&usb->usbphyrstcon, (RSTCON_HOSTPHY_SWRST | RSTCON_SWRST));
}
#endif

static void setup_usb_phy(struct exynos_usb_phy *usb)
{
#if defined(CONFIG_ARCH_NEXELL)
	nx_setup_usb_phy((struct nx_usb_phy *)usb);
#else
	set_usbhost_mode(USB20_PHY_CFG_HOST_LINK_EN);

	set_usbhost_phy_ctrl(POWER_USB_HOST_PHY_CTRL_EN);

	if (cpu_is_exynos5())
		exynos5_setup_usb_phy(usb);
	else if (cpu_is_exynos4())
		if (proid_is_exynos4412())
			exynos4412_setup_usb_phy((struct exynos4412_usb_phy *)
						 usb);
#endif
}

#if defined(CONFIG_ARCH_NEXELL)
static void nx_reset_usb_phy(struct nx_usb_phy *usb)
{
	u32 reg;

	writel(readl((void *)(&usb->usbhost_con[0])) &
	       ~NX_HOST_CON0_AHB_RESET_SYNC,
	       (void *)(&usb->usbhost_con[0]));

	writel(readl((void *)(&usb->usbhost_con[0])) &
	       ~NX_HOST_CON0_UTMI_RESET_SYNC,
	       (void *)(&usb->usbhost_con[0]));

	reg    = readl((void *)(&usb->usbhost_con[3]));
	reg   &= ~NX_HOST_CON3_POR_MASK;
	reg   |=  NX_HOST_CON3_POR_ENB;
	writel(reg, (void *)(&usb->usbhost_con[3]));
	udelay(1);
	reg   |=  NX_HOST_CON3_POR_MASK;
	writel(reg, (void *)(&usb->usbhost_con[3]));
	udelay(1);
	reg   &= ~(NX_HOST_CON3_POR);
	writel(reg, (void *)(&usb->usbhost_con[3]));

	writel(readl((void *)(&usb->usbhost_con[0])) &
	       ~NX_HOST_CON0_HSIC_CLK_MASK,
	       (void *)(&usb->usbhost_con[0]));

	writel(readl((void *)(&usb->usbhost_con[5])) |
	       NX_HOST_CON5_HSIC_POR_MASK,
	       (void *)(&usb->usbhost_con[5]));

	udelay(10);
}
#else
static void exynos5_reset_usb_phy(struct exynos_usb_phy *usb)
{
	u32 hsic_ctrl;

	/* HOST_PHY reset */
	setbits_le32(&usb->usbphyctrl0,
			HOST_CTRL0_PHYSWRST |
			HOST_CTRL0_PHYSWRSTALL |
			HOST_CTRL0_SIDDQ |
			HOST_CTRL0_FORCESUSPEND |
			HOST_CTRL0_FORCESLEEP);

	/* HSIC Phy reset */
	hsic_ctrl = (HSIC_CTRL_FORCESUSPEND |
			HSIC_CTRL_FORCESLEEP |
			HSIC_CTRL_SIDDQ |
			HSIC_CTRL_PHYSWRST);

	setbits_le32(&usb->hsicphyctrl1, hsic_ctrl);
	setbits_le32(&usb->hsicphyctrl2, hsic_ctrl);
}

static void exynos4412_reset_usb_phy(struct exynos4412_usb_phy *usb)
{
	setbits_le32(&usb->usbphyctrl, (PHYPWR_NORMAL_MASK_HSIC0 |
		PHYPWR_NORMAL_MASK_HSIC1 | PHYPWR_NORMAL_MASK_PHY1 |
		PHYPWR_NORMAL_MASK_PHY0));
}
#endif

/* Reset the EHCI host controller. */
static void reset_usb_phy(struct exynos_usb_phy *usb)
{
#if defined(CONFIG_ARCH_NEXELL)
	nx_reset_usb_phy((struct nx_usb_phy *)usb);
#else
	if (cpu_is_exynos5())
		exynos5_reset_usb_phy(usb);
	else if (cpu_is_exynos4())
		if (proid_is_exynos4412())
			exynos4412_reset_usb_phy((struct exynos4412_usb_phy *)
						 usb);

	set_usbhost_phy_ctrl(POWER_USB_HOST_PHY_CTRL_DISABLE);
#endif
}

static int ehci_usb_probe(struct udevice *dev)
{
	struct exynos_ehci_platdata *plat = dev_get_platdata(dev);
	struct exynos_ehci *ctx = dev_get_priv(dev);
	struct ehci_hcor *hcor;

	ctx->hcd = (struct ehci_hccr *)plat->hcd_base;
	ctx->usb = (struct exynos_usb_phy *)plat->phy_base;

	/* setup the Vbus gpio here */
	if (dm_gpio_is_valid(&plat->vbus_gpio))
		dm_gpio_set_value(&plat->vbus_gpio, 1);

	setup_usb_phy(ctx->usb);
	hcor = (struct ehci_hcor *)(ctx->hcd +
			HC_LENGTH(ehci_readl(&ctx->hcd->cr_capbase)));

	return ehci_register(dev, ctx->hcd, hcor, NULL, 0, USB_INIT_HOST);
}

static int ehci_usb_remove(struct udevice *dev)
{
	struct exynos_ehci *ctx = dev_get_priv(dev);
	int ret;

	ret = ehci_deregister(dev);
	if (ret)
		return ret;
	reset_usb_phy(ctx->usb);

	return 0;
}

static const struct udevice_id ehci_usb_ids[] = {
	{ .compatible = "samsung,exynos-ehci" },
	{ .compatible = "nexell,nexell-ehci" },
	{ }
};

U_BOOT_DRIVER(usb_ehci) = {
	.name	= "ehci_exynos",
	.id	= UCLASS_USB,
	.of_match = ehci_usb_ids,
	.ofdata_to_platdata = ehci_usb_ofdata_to_platdata,
	.probe = ehci_usb_probe,
	.remove = ehci_usb_remove,
	.ops	= &ehci_usb_ops,
	.priv_auto_alloc_size = sizeof(struct exynos_ehci),
	.platdata_auto_alloc_size = sizeof(struct exynos_ehci_platdata),
	.flags	= DM_FLAG_ALLOC_PRIV_DMA,
};
