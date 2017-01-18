/*
 * DaVinci Key Scan Driver for TI platforms
 *
 * Copyright (C) 2009 Texas Instruments, Inc
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 *
 * Initial Code: Sandeep Paulraj <s-paulraj@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <linux/platform_data/keyscan-davinci.h>

/* Key scan registers */
#define DAVINCI_KEYSCAN_KEYCTRL		0x0000
#define DAVINCI_KEYSCAN_INTENA		0x0004
#define DAVINCI_KEYSCAN_INTFLAG		0x0008
#define DAVINCI_KEYSCAN_INTCLR		0x000c
#define DAVINCI_KEYSCAN_STRBWIDTH	0x0010
#define DAVINCI_KEYSCAN_INTERVAL	0x0014
#define DAVINCI_KEYSCAN_CONTTIME	0x0018
#define DAVINCI_KEYSCAN_CURRENTST	0x001c
#define DAVINCI_KEYSCAN_PREVSTATE	0x0020
#define DAVINCI_KEYSCAN_EMUCTRL		0x0024
#define DAVINCI_KEYSCAN_IODFTCTRL	0x002c

/* Key Control Register (KEYCTRL) */
#define DAVINCI_KEYSCAN_KEYEN		0x00000001
#define DAVINCI_KEYSCAN_PREVMODE	0x00000002
#define DAVINCI_KEYSCAN_CHATOFF		0x00000004
#define DAVINCI_KEYSCAN_AUTODET		0x00000008
#define DAVINCI_KEYSCAN_SCANMODE	0x00000010
#define DAVINCI_KEYSCAN_OUTTYPE		0x00000020

/* Masks for the interrupts */
#define DAVINCI_KEYSCAN_INT_CONT	0x00000008
#define DAVINCI_KEYSCAN_INT_OFF		0x00000004
#define DAVINCI_KEYSCAN_INT_ON		0x00000002
#define DAVINCI_KEYSCAN_INT_CHANGE	0x00000001
#define DAVINCI_KEYSCAN_INT_ALL		0x0000000f

struct davinci_ks {
	struct input_dev		*input;
	struct davinci_ks_platform_data	*pdata;
	int				irq;
	void __iomem			*base;
	resource_size_t			pbase;
	size_t				base_size;
	unsigned short			keymap[];
};

/* Initializing the kp Module */
static int __init davinci_ks_initialize(struct davinci_ks *davinci_ks)
{
	struct device *dev = &davinci_ks->input->dev;
	struct davinci_ks_platform_data *pdata = davinci_ks->pdata;
	u32 matrix_ctrl;

	/* Enable all interrupts */
	__raw_writel(DAVINCI_KEYSCAN_INT_ALL,
		     davinci_ks->base + DAVINCI_KEYSCAN_INTENA);

	/* Clear interrupts if any */
	__raw_writel(DAVINCI_KEYSCAN_INT_ALL,
		     davinci_ks->base + DAVINCI_KEYSCAN_INTCLR);

	/* Setup the scan period = strobe + interval */
	__raw_writel(pdata->strobe,
		     davinci_ks->base + DAVINCI_KEYSCAN_STRBWIDTH);
	__raw_writel(pdata->interval,
		     davinci_ks->base + DAVINCI_KEYSCAN_INTERVAL);
	__raw_writel(0x01,
		     davinci_ks->base + DAVINCI_KEYSCAN_CONTTIME);

	/* Define matrix type */
	switch (pdata->matrix_type) {
	case DAVINCI_KEYSCAN_MATRIX_4X4:
		matrix_ctrl = 0;
		break;
	case DAVINCI_KEYSCAN_MATRIX_5X3:
		matrix_ctrl = (1 << 6);
		break;
	default:
		dev_err(dev->parent, "wrong matrix type\n");
		return -EINVAL;
	}

	/* Enable key scan module and set matrix type */
	__raw_writel(DAVINCI_KEYSCAN_AUTODET | DAVINCI_KEYSCAN_KEYEN |
		     matrix_ctrl, davinci_ks->base + DAVINCI_KEYSCAN_KEYCTRL);

	return 0;
}

static irqreturn_t davinci_ks_interrupt(int irq, void *dev_id)
{
	struct davinci_ks *davinci_ks = dev_id;
	struct device *dev = &davinci_ks->input->dev;
	unsigned short *keymap = davinci_ks->keymap;
	int keymapsize = davinci_ks->pdata->keymapsize;
	u32 prev_status, new_status, changed;
	bool release;
	int keycode = KEY_UNKNOWN;
	int i;

	/* Disable interrupt */
	__raw_writel(0x0, davinci_ks->base + DAVINCI_KEYSCAN_INTENA);

	/* Reading previous and new status of the key scan */
	prev_status = __raw_readl(davinci_ks->base + DAVINCI_KEYSCAN_PREVSTATE);
	new_status = __raw_readl(davinci_ks->base + DAVINCI_KEYSCAN_CURRENTST);

	changed = prev_status ^ new_status;

	if (changed) {
		/*
		 * It goes through all bits in 'changed' to ensure
		 * that no key changes are being missed
		 */
		for (i = 0 ; i < keymapsize; i++) {
			if ((changed>>i) & 0x1) {
				keycode = keymap[i];
				release = (new_status >> i) & 0x1;
				dev_dbg(dev->parent, "key %d %s\n", keycode,
					release ? "released" : "pressed");
				input_report_key(davinci_ks->input, keycode,
						 !release);
				input_sync(davinci_ks->input);
			}
		}
		/* Clearing interrupt */
		__raw_writel(DAVINCI_KEYSCAN_INT_ALL,
			     davinci_ks->base + DAVINCI_KEYSCAN_INTCLR);
	}

	/* Enable interrupts */
	__raw_writel(0x1, davinci_ks->base + DAVINCI_KEYSCAN_INTENA);

	return IRQ_HANDLED;
}

static int __init davinci_ks_probe(struct platform_device *pdev)
{
	struct davinci_ks *davinci_ks;
	struct input_dev *key_dev;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct davinci_ks_platform_data *pdata = dev_get_platdata(dev);
	int error, i;

	if (pdata->device_enable) {
		error = pdata->device_enable(dev);
		if (error < 0) {
			dev_dbg(dev, "device enable function failed\n");
			return error;
		}
	}

	if (!pdata->keymap) {
		dev_dbg(dev, "no keymap from pdata\n");
		return -EINVAL;
	}

	davinci_ks = devm_kzalloc(dev,
				  sizeof(struct davinci_ks) + sizeof(unsigned short) * pdata->keymapsize,
				  GFP_KERNEL);
	if (!davinci_ks)
		return -ENOMEM;

	memcpy(davinci_ks->keymap, pdata->keymap,
		sizeof(unsigned short) * pdata->keymapsize);

	key_dev = devm_input_allocate_device(dev);
	if (!key_dev) {
		dev_dbg(dev, "could not allocate input device\n");
		return -ENOMEM;
	}

	davinci_ks->input = key_dev;

	davinci_ks->irq = platform_get_irq(pdev, 0);
	if (davinci_ks->irq < 0) {
		dev_err(dev, "no key scan irq\n");
		return davinci_ks->irq;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "no mem resource\n");
		return -EINVAL;
	}

	davinci_ks->base_size = resource_size(res);

	davinci_ks->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(davinci_ks->base)) {
		dev_err(dev, "can't ioremap MEM resource.\n");
		return PTR_ERR(davinci_ks->base);
	}

	/* Enable auto repeat feature of Linux input subsystem */
	if (pdata->rep)
		__set_bit(EV_REP, key_dev->evbit);

	/* Setup input device */
	__set_bit(EV_KEY, key_dev->evbit);

	/* Setup the platform data */
	davinci_ks->pdata = pdata;

	for (i = 0; i < davinci_ks->pdata->keymapsize; i++)
		__set_bit(davinci_ks->pdata->keymap[i], key_dev->keybit);

	key_dev->name = "davinci_keyscan";
	key_dev->phys = "davinci_keyscan/input0";
	key_dev->dev.parent = dev;
	key_dev->id.bustype = BUS_HOST;
	key_dev->id.vendor = 0x0001;
	key_dev->id.product = 0x0001;
	key_dev->id.version = 0x0001;
	key_dev->keycode = davinci_ks->keymap;
	key_dev->keycodesize = sizeof(davinci_ks->keymap[0]);
	key_dev->keycodemax = davinci_ks->pdata->keymapsize;

	error = input_register_device(davinci_ks->input);
	if (error < 0) {
		dev_err(dev, "unable to register davinci key scan device\n");
		return error;
	}

	error = devm_request_irq(dev, davinci_ks->irq, davinci_ks_interrupt,
				 0, pdev->name, davinci_ks);
	if (error < 0) {
		dev_err(dev,
			"unable to register davinci key scan interrupt\n");
		return error;
	}

	error = davinci_ks_initialize(davinci_ks);
	if (error < 0) {
		dev_err(dev, "unable to initialize davinci key scan device\n");
		return error;
	}

	return 0;
}

static struct platform_driver davinci_ks_driver = {
	.driver	= {
		.name = "davinci_keyscan",
	},
};

module_platform_driver_probe(davinci_ks_driver, davinci_ks_probe);

MODULE_AUTHOR("Miguel Aguilar");
MODULE_DESCRIPTION("Texas Instruments DaVinci Key Scan Driver");
MODULE_LICENSE("GPL");
