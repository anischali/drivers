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


enum battery_usages {
	HID_BATTERY_USAGE = 0x850000U,
	HID_BATTERY_BATTERY_PRESENT = 0x8500D1U,
	HID_BATTERY_POWER_FAIL = 0x8500D2U,
	HID_BATTERY_ALARM_INHIBITED = 0x8500D3U,
	HID_BATTERY_THERMISTOR_HOT = 0x8500D5U,
	HID_BATTERY_THERMISTOR_COLD = 0x8500D6U,
	HID_BATTERY_VOLTAGE_OUT_OF_RANGE = 0x8500D8U,
	HID_BATTERY_CURRENT_OUT_OF_RANGE = 0x8500D9U,
};


struct hid_battery {
	struct hid_subdevice *hsdev;
	struct power_supply_desc desc;
	struct power_supply *battery;
	struct hid_composite_callbacks callbacks;
};

static int hid_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct hid_battery *bat = power_supply_get_drvdata(psy);
	
	val->intval = 0;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_FULL;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN: /* THE design voltage... */
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		break;
	case POWER_SUPPLY_PROP_TEMP:
		break;
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		break;
	default:
		break;
	}
	return 0;
}


static int hid_battery_properties_count(struct hid_device *hdev)
{
	struct hid_report *report;
	struct hid_field *field;
	int i, j;
	unsigned int count = 0;

	list_for_each_entry(report,
			    &hdev->report_enum[HID_INPUT_REPORT].report_list,
			    list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			if (field->application == HID_BATTERY_USAGE)
			{
				count += field->maxusage;
				for (j = 0; j < field->maxusage; ++j)
				{
					hid_info(hdev, "usage: 0x%08x (size: %d count: %d)\n", field->usage[j].hid, field->report_size, field->report_size);
				}
			}
		}
	}

	hid_info(hdev, "count: %d\n", count);
	return count;
}

static void hid_battery_parse(struct hid_device *hdev, 
					enum power_supply_property **properties)
{

}

static int hid_battery_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_battery *bat;
	struct power_supply_config psy_cfg = {};
	struct power_supply_desc *desc;
	enum power_supply_property *hid_battery_properties;
	int ret;

	if (!hsdev)
		return -ENODEV;

	bat = devm_kzalloc(&pdev->dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	platform_set_drvdata(pdev, bat);

	
	
	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

	hid_battery_properties = devm_kcalloc(&pdev->dev, 2,
				sizeof(enum power_supply_property), GFP_KERNEL);
	if (!hid_battery_properties)
	{
		ret = -ENOMEM;
		goto cleanup;
	}

	hid_battery_properties_count(hsdev->hdev);

	hid_battery_properties[0] = POWER_SUPPLY_PROP_STATUS;
	hid_battery_properties[0] = POWER_SUPPLY_PROP_PRESENT;

	bat->hsdev = hsdev;
	desc = &bat->desc;
	desc->name = "hid-battery";
	desc->type = POWER_SUPPLY_TYPE_BATTERY;
	desc->properties = hid_battery_properties;
	desc->num_properties = 2;
	desc->get_property = hid_battery_get_property;
	psy_cfg.drv_data = bat;
	bat->battery = devm_power_supply_register(&pdev->dev, desc, &psy_cfg);
	if (IS_ERR(bat->battery)) {
		hid_device_io_stop(hsdev->hdev);
		hid_composite_device_close(hsdev);
		return dev_err_probe(&pdev->dev, PTR_ERR(bat->battery),
				     "Unable to register battery\n");

	}
	
	ret = power_supply_powers(bat->battery, &pdev->dev);
	if (ret) {
		hid_err(hsdev->hdev, "Unable to get battery info: %d\n", ret);
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
		.name = "HID-COMPOSITE-850000",
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
