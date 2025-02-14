// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>

struct rfkill_gpio_data {
	const char		*name;
	enum rfkill_type	type;
	struct gpio_desc	*wireless_disable_gpio;

	struct rfkill		*rfkill_dev;
};

static int rfkill_gpio_set_block(void *data, bool blocked)
{
	struct rfkill_gpio_data *rfkill = data;		

	gpiod_set_value_cansleep(rfkill->wireless_disable_gpio, !blocked);

	return 0;
}

static const struct rfkill_ops rfkill_gpio_ops = {
	.set_block = rfkill_gpio_set_block,
};

static int rfkill_gpio_probe(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill;
	struct gpio_desc *gpio;
	const char *type_name;
	int ret;

	rfkill = devm_kzalloc(&pdev->dev, sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	device_property_read_string(&pdev->dev, "rfkill-name", &rfkill->name);
	device_property_read_string(&pdev->dev, "rfkill-type", &type_name);

	if (!rfkill->name)
		rfkill->name = dev_name(&pdev->dev);

	rfkill->type = rfkill_find_type(type_name);

	gpio = devm_gpiod_get(&pdev->dev, "wireless-disable", GPIOD_OUT_LOW);
	if (IS_ERR(gpio)) {
		return -EPROBE_DEFER;
	}

	rfkill->wireless_disable_gpio = gpio;

	rfkill->rfkill_dev = rfkill_alloc(rfkill->name, &pdev->dev,
					  rfkill->type, &rfkill_gpio_ops,
					  rfkill);
	if (!rfkill->rfkill_dev)
		return -ENOMEM;

	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto err_destroy;

	platform_set_drvdata(pdev, rfkill);

	dev_info(&pdev->dev, "%s device registered.\n", rfkill->name);

	return 0;

err_destroy:
	rfkill_destroy(rfkill->rfkill_dev);

	return ret;
}

static int rfkill_gpio_remove(struct platform_device *pdev)
{
	struct rfkill_gpio_data *rfkill = platform_get_drvdata(pdev);

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);

	return 0;
}


static const struct of_device_id rfkill_gpio_ids[] = {
	{ .compatible = "rfkill,gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, rfkill_gpio_ids);

static struct platform_driver rfkill_gpio_driver = {
	.probe = rfkill_gpio_probe,
	.remove = rfkill_gpio_remove,
	.driver = {
		.name = "gpio_rfkill",
		.of_match_table = of_match_ptr(rfkill_gpio_ids),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
	
module_platform_driver(rfkill_gpio_driver);


MODULE_DESCRIPTION("rfkill gpio");
MODULE_AUTHOR("Anis CHALI");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-rfkill");
