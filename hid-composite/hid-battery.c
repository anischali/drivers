// SPDX-License-Identifier: GPL-2.0
/*
 * Battery driver for the Ingenic JZ47xx SoCs
 * Copyright (c) 2019 Artur Rojek <contact@artur-rojek.eu>
 *
 * based on drivers/power/supply/jz4740-battery.c
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include "hid_composite.h"


struct hid_battery {
	struct hid_subdevice *hsdev;
	struct power_supply_desc desc;
	struct power_supply *battery;
	struct power_supply_battery_info *info;
	struct hid_composite_callbacks callbacks;
};

static int hid_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct hid_battery *bat = power_supply_get_drvdata(psy);
	struct power_supply_battery_info *info = bat->info;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		//ret = iio_read_channel_processed(bat->channel, &val->intval);
		val->intval *= 1000;
		if (val->intval < info->voltage_min_design_uv)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (val->intval > info->voltage_max_design_uv)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return ret;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		//ret = iio_read_channel_processed(bat->channel, &val->intval);
		val->intval *= 1000;
		return ret;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = info->voltage_min_design_uv;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = info->voltage_max_design_uv;
		return 0;
	default:
		return -EINVAL;
	}
}


static enum power_supply_property hid_battery_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static int hid_battery_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_battery *bat;
	struct power_supply_config psy_cfg = {};
	struct power_supply_desc *desc;
	int ret;

	if (!hsdev)
		return -ENODEV;

	bat = devm_kzalloc(&pdev->dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	platform_set_drvdata(pdev, bat);

	bat->hsdev = hsdev;
	desc = &bat->desc;
	desc->name = "hid-battery";
	desc->type = POWER_SUPPLY_TYPE_BATTERY;
	desc->properties = hid_battery_properties;
	desc->num_properties = ARRAY_SIZE(hid_battery_properties);
	desc->get_property = hid_battery_get_property;
	psy_cfg.drv_data = bat;
	
	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

	bat->battery = devm_power_supply_register(&pdev->dev, desc, &psy_cfg);
	if (IS_ERR(bat->battery)) {
		hid_device_io_stop(hsdev->hdev);
		hid_composite_device_close(hsdev);
		return dev_err_probe(&pdev->dev, PTR_ERR(bat->battery),
				     "Unable to register battery\n");

	}
	ret = power_supply_get_battery_info(bat->battery, &bat->info);
	if (ret) {
		hid_err(hsdev->hdev, "Unable to get battery info: %d\n", ret);
		goto cleanup;
	}
	if (bat->info->voltage_min_design_uv < 0) {
		hid_err(hsdev->hdev, "Unable to get voltage min design\n");
		ret = bat->info->voltage_min_design_uv;
		goto cleanup;
	}
	if (bat->info->voltage_max_design_uv < 0) {
		hid_err(hsdev->hdev, "Unable to get voltage max design\n");
		ret = bat->info->voltage_max_design_uv;
		goto cleanup;
	}

	return 0;
cleanup:
	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);

	return ret;
}


static int hid_battery_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);	

	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return 0;
}

static const struct platform_device_id hid_battery_platform_ids[] = {
	{
		/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-10070",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_battery_platform_ids);

static struct platform_driver hid_battery_driver = {
	.id_table = hid_battery_platform_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe = hid_battery_probe,
	.remove = hid_battery_remove,
};
module_platform_driver(hid_battery_driver);

MODULE_DESCRIPTION("HID Battery driver USBHID based batteries");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_LICENSE("GPL");
