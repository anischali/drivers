// SPDX-License-Identifier: GPL-2.0
/*
 * chargertery driver for the Ingenic JZ47xx SoCs
 * Copyright (c) 2019 Artur Rojek <contact@artur-rojek.eu>
 *
 * based on drivers/power/supply/jz4740-chargertery.c
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include "hid_composite.h"


enum hid_charger_usages {
	HID_CHARGER_USAGE = 0x850000U,
};


struct hid_charger {
	struct hid_subdevice *hsdev;
	struct power_supply_desc desc;
	struct power_supply *charger;
	struct hid_composite_callbacks callbacks;
};

static int hid_charger_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct hid_charger *charger = power_supply_get_drvdata(psy);
	
	val->intval = 0;
	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Texas Instruments";
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "bq25...";
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;

	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
/*
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			val->intval = POWER_SUPPLY_HEALTH_OVERCURRENT;
			val->intval =
				POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE;*/
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;

			/*val->intval = POWER_SUPPLY_STATUS_CHARGING;
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;*/
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
/*
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;*/
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = 0;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}


static int hid_charger_properties_count(struct hid_device *hdev)
{
	struct hid_report *report;
	struct hid_field *field;
	int i;
	unsigned int count = 0;
	
	list_for_each_entry(report,
			    &hdev->report_enum[HID_INPUT_REPORT].report_list,
			    list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
				if (field->application == HID_CHARGER_USAGE)
					count += 1;
		}
	}
	return count;
}

static void hid_charger_parse(struct hid_device *hdev, 
					enum power_supply_property **properties)
{

}

static int hid_charger_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_charger *charger;
	struct power_supply_config psy_cfg = {};
	struct power_supply_desc *desc;
	enum power_supply_property *hid_charger_properties;
	int ret;

	if (!hsdev)
		return -ENODEV;

	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	platform_set_drvdata(pdev, charger);

	
	
	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

	hid_charger_properties = devm_kcalloc(&pdev->dev, 2,
				sizeof(enum power_supply_property), GFP_KERNEL);
	if (!hid_charger_properties)
	{
		ret = -ENOMEM;
		goto cleanup;
	}

	hid_charger_properties[0] = POWER_SUPPLY_PROP_STATUS;
	hid_charger_properties[0] = POWER_SUPPLY_PROP_PRESENT;

	charger->hsdev = hsdev;
	desc = &charger->desc;
	desc->name = "hid-charger";
	desc->type = POWER_SUPPLY_TYPE_USB;
	desc->properties = hid_charger_properties;
	desc->num_properties = 2;
	desc->get_property = hid_charger_get_property;
	psy_cfg.drv_data = charger;
	charger->charger = devm_power_supply_register(&pdev->dev, desc, &psy_cfg);
	if (IS_ERR(charger->charger)) {
		hid_device_io_stop(hsdev->hdev);
		hid_composite_device_close(hsdev);
		return dev_err_probe(&pdev->dev, PTR_ERR(charger->charger),
				     "Unable to register chargertery\n");

	}
	ret = power_supply_powers(charger->charger, &pdev->dev);
	if (ret) {
		hid_err(hsdev->hdev, "Unable to get chargertery info: %d\n", ret);
		goto cleanup;
	}

	return 0;
cleanup:
	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);

	return ret;
}


static int hid_charger_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);	

	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return 0;
}

static const struct platform_device_id hid_charger_platform_ids[] = {
	{
		/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-850000",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_charger_platform_ids);

static struct platform_driver hid_charger_driver = {
	.id_table = hid_charger_platform_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe = hid_charger_probe,
	.remove = hid_charger_remove,
};
module_platform_driver(hid_charger_driver);

MODULE_DESCRIPTION("HID chargertery driver USBHID based chargerteries");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_LICENSE("GPL");
