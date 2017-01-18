/*
 *  Input driver for PCAP events:
 *   * Power key
 *   * Headphone button
 *
 *  Copyright (c) 2008,2009 Ilya Petrov <ilya.muromec@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/mfd/ezx-pcap.h>
#include <linux/slab.h>

struct pcap_keys {
	struct pcap_chip *pcap;
	struct input_dev *input;
};

/* PCAP2 interrupts us on keypress */
static irqreturn_t pcap_keys_handler(int irq, void *_pcap_keys)
{
	struct pcap_keys *pcap_keys = _pcap_keys;
	int pirq = irq_to_pcap(pcap_keys->pcap, irq);
	u32 pstat;

	ezx_pcap_read(pcap_keys->pcap, PCAP_REG_PSTAT, &pstat);
	pstat &= 1 << pirq;

	switch (pirq) {
	case PCAP_IRQ_ONOFF:
		input_report_key(pcap_keys->input, KEY_POWER, !pstat);
		break;
	case PCAP_IRQ_MIC:
		input_report_key(pcap_keys->input, KEY_HP, !pstat);
		break;
	}

	input_sync(pcap_keys->input);

	return IRQ_HANDLED;
}

static int pcap_keys_probe(struct platform_device *pdev)
{
	int err = -ENOMEM;
	struct pcap_keys *pcap_keys;
	struct input_dev *input_dev;

	pcap_keys = devm_kmalloc(&pdev->dev, sizeof(struct pcap_keys),
				 GFP_KERNEL);
	if (!pcap_keys)
		return err;

	pcap_keys->pcap = dev_get_drvdata(pdev->dev.parent);

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return err;

	pcap_keys->input = input_dev;

	input_dev->name = pdev->name;
	input_dev->phys = "pcap-keys/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->dev.parent = &pdev->dev;

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(KEY_POWER, input_dev->keybit);
	__set_bit(KEY_HP, input_dev->keybit);

	err = input_register_device(input_dev);
	if (err)
		return err;

	err = devm_request_irq(&pdev->dev,
			       pcap_to_irq(pcap_keys->pcap, PCAP_IRQ_ONOFF),
			       pcap_keys_handler, 0, "Power key", pcap_keys);
	if (err)
		return err;

	return devm_request_irq(&pdev->dev,
				pcap_to_irq(pcap_keys->pcap, PCAP_IRQ_MIC),
				pcap_keys_handler, 0, "Headphone button",
				pcap_keys);

	return err;
}

static struct platform_driver pcap_keys_device_driver = {
	.probe		= pcap_keys_probe,
	.driver		= {
		.name	= "pcap-keys",
	}
};
module_platform_driver(pcap_keys_device_driver);

MODULE_DESCRIPTION("Motorola PCAP2 input events driver");
MODULE_AUTHOR("Ilya Petrov <ilya.muromec@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pcap_keys");
