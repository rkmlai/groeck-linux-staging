/*
 * ON pin driver for Dialog DA9055 PMICs
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/da9055/core.h>
#include <linux/mfd/da9055/reg.h>

struct da9055_onkey {
	struct da9055 *da9055;
	struct input_dev *input;
	struct delayed_work work;
};

static void da9055_onkey_query(struct da9055_onkey *onkey)
{
	int key_stat;

	key_stat = da9055_reg_read(onkey->da9055, DA9055_REG_STATUS_A);
	if (key_stat < 0) {
		dev_err(onkey->da9055->dev,
			"Failed to read onkey event %d\n", key_stat);
	} else {
		key_stat &= DA9055_NOKEY_STS;
		/*
		 * Onkey status bit is cleared when onkey button is released.
		 */
		if (!key_stat) {
			input_report_key(onkey->input, KEY_POWER, 0);
			input_sync(onkey->input);
		}
	}

	/*
	 * Interrupt is generated only when the ONKEY pin is asserted.
	 * Hence the deassertion of the pin is simulated through work queue.
	 */
	if (key_stat)
		schedule_delayed_work(&onkey->work, msecs_to_jiffies(10));

}

static void da9055_onkey_work(struct work_struct *work)
{
	struct da9055_onkey *onkey = container_of(work, struct da9055_onkey,
						  work.work);

	da9055_onkey_query(onkey);
}

static irqreturn_t da9055_onkey_irq(int irq, void *data)
{
	struct da9055_onkey *onkey = data;

	input_report_key(onkey->input, KEY_POWER, 1);
	input_sync(onkey->input);

	da9055_onkey_query(onkey);

	return IRQ_HANDLED;
}

static void da9055_onkey_probe_work_cb(void *w) {
	cancel_delayed_work_sync(w);
}

static int da9055_onkey_probe(struct platform_device *pdev)
{
	struct da9055 *da9055 = dev_get_drvdata(pdev->dev.parent);
	struct da9055_onkey *onkey;
	struct input_dev *input_dev;
	int irq, err;

	irq = platform_get_irq_byname(pdev, "ONKEY");
	if (irq < 0) {
		dev_err(&pdev->dev,
			"Failed to get an IRQ for input device, %d\n", irq);
		return -EINVAL;
	}

	onkey = devm_kzalloc(&pdev->dev, sizeof(*onkey), GFP_KERNEL);
	if (!onkey)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	onkey->input = input_dev;
	onkey->da9055 = da9055;
	input_dev->name = "da9055-onkey";
	input_dev->phys = "da9055-onkey/input0";
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	__set_bit(KEY_POWER, input_dev->keybit);

	INIT_DELAYED_WORK(&onkey->work, da9055_onkey_work);
	err = devm_add_action_or_reset(&pdev->dev, da9055_onkey_probe_work_cb,
				       &onkey->work);
	if (err)
		return err;

	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					da9055_onkey_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"ONKEY", onkey);
	if (err < 0) {
		dev_err(&pdev->dev,
			"Failed to register ONKEY IRQ %d, error = %d\n",
			irq, err);
		return err;
	}

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&pdev->dev, "Unable to register input device, %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, onkey);

	return 0;
}

static int da9055_onkey_remove(struct platform_device *pdev)
{
	struct da9055_onkey *onkey = platform_get_drvdata(pdev);
	int irq = platform_get_irq_byname(pdev, "ONKEY");

	irq = regmap_irq_get_virq(onkey->da9055->irq_data, irq);

	return 0;
}

static struct platform_driver da9055_onkey_driver = {
	.probe	= da9055_onkey_probe,
	.remove	= da9055_onkey_remove,
	.driver = {
		.name	= "da9055-onkey",
	},
};

module_platform_driver(da9055_onkey_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("Onkey driver for DA9055");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9055-onkey");
