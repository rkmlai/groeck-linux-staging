/*
 * Driver for the enhanced rotary controller on pxa930 and pxa935
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <linux/platform_data/keyboard-pxa930_rotary.h>

#define SBCR	(0x04)
#define ERCR	(0x0c)

#define SBCR_ERSB	(1 << 5)

struct pxa930_rotary {
	struct input_dev	*input_dev;
	void __iomem		*mmio_base;
	int			last_ercr;

	struct pxa930_rotary_platform_data *pdata;
};

static void clear_sbcr(struct pxa930_rotary *r)
{
	uint32_t sbcr = __raw_readl(r->mmio_base + SBCR);

	__raw_writel(sbcr | SBCR_ERSB, r->mmio_base + SBCR);
	__raw_writel(sbcr & ~SBCR_ERSB, r->mmio_base + SBCR);
}

static irqreturn_t rotary_irq(int irq, void *dev_id)
{
	struct pxa930_rotary *r = dev_id;
	struct pxa930_rotary_platform_data *pdata = r->pdata;
	int ercr, delta, key;

	ercr = __raw_readl(r->mmio_base + ERCR) & 0xf;
	clear_sbcr(r);

	delta = ercr - r->last_ercr;
	if (delta == 0)
		return IRQ_HANDLED;

	r->last_ercr = ercr;

	if (pdata->up_key && pdata->down_key) {
		key = (delta > 0) ? pdata->up_key : pdata->down_key;
		input_report_key(r->input_dev, key, 1);
		input_sync(r->input_dev);
		input_report_key(r->input_dev, key, 0);
	} else
		input_report_rel(r->input_dev, pdata->rel_code, delta);

	input_sync(r->input_dev);

	return IRQ_HANDLED;
}

static int pxa930_rotary_open(struct input_dev *dev)
{
	struct pxa930_rotary *r = input_get_drvdata(dev);

	clear_sbcr(r);

	return 0;
}

static void pxa930_rotary_close(struct input_dev *dev)
{
	struct pxa930_rotary *r = input_get_drvdata(dev);

	clear_sbcr(r);
}

static int pxa930_rotary_probe(struct platform_device *pdev)
{
	struct pxa930_rotary_platform_data *pdata =
			dev_get_platdata(&pdev->dev);
	struct pxa930_rotary *r;
	struct input_dev *input_dev;
	struct resource *res;
	int irq;
	int err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq for rotary controller\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no I/O memory defined\n");
		return -ENXIO;
	}

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	r = devm_kzalloc(&pdev->dev, sizeof(struct pxa930_rotary), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	r->mmio_base = devm_ioremap_nocache(&pdev->dev, res->start,
					    resource_size(res));
	if (r->mmio_base == NULL) {
		dev_err(&pdev->dev, "failed to remap IO memory\n");
		return -ENXIO;
	}

	r->pdata = pdata;

	/* allocate and register the input device */
	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->open = pxa930_rotary_open;
	input_dev->close = pxa930_rotary_close;
	input_dev->dev.parent = &pdev->dev;

	if (pdata->up_key && pdata->down_key) {
		__set_bit(pdata->up_key, input_dev->keybit);
		__set_bit(pdata->down_key, input_dev->keybit);
		__set_bit(EV_KEY, input_dev->evbit);
	} else {
		__set_bit(pdata->rel_code, input_dev->relbit);
		__set_bit(EV_REL, input_dev->evbit);
	}

	r->input_dev = input_dev;
	input_set_drvdata(input_dev, r);

	err = devm_request_irq(&pdev->dev, irq, rotary_irq, 0,
			       "enhanced rotary", r);
	if (err) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return err;
	}

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return err;
	}

	return 0;
}

static struct platform_driver pxa930_rotary_driver = {
	.driver		= {
		.name	= "pxa930-rotary",
	},
	.probe		= pxa930_rotary_probe,
};
module_platform_driver(pxa930_rotary_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for PXA93x Enhanced Rotary Controller");
MODULE_AUTHOR("Yao Yong <yaoyong@marvell.com>");
