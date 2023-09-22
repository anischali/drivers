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
#include "hid-battery-system.h"


#define HID_SBS_PROPERTY_USAGE(_psp, _usage, _default_value) { \
	.psp = _psp, \
	.usage = _usage, \
	.default_value = _default_value \
}

struct hid_sbs_property_usage {
	int psp;
	int usage;
	int default_value;
};


static const struct hid_sbs_property_usage sbs_battery_prop_usages[] = {
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_STATUS, HID_SBS_SMART_BATTERY_BATTERY_STATUS, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_PRESENT, HID_SBS_SMART_BATTERY_SELECTOR_STATE, 1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_ONLINE, HID_SBS_SMART_BATTERY_SELECTOR_STATE, 1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_HEALTH, HID_SBS_SMART_BATTERY_BATTERY_STATUS, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_TECHNOLOGY, HID_SBS_IDEVICE_CHEMISTRY, -1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CYCLE_COUNT, HID_SBS_CYCLE_COUNT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CURRENT_AVG, HID_SBS_AVERAGE_CURRENT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CURRENT_NOW, HID_SBS_CURRENT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_VOLTAGE_NOW, HID_SBS_VOLTAGE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX, HID_SBS_CHARGING_VOLTAGE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN, HID_SBS_DESIGN_VOLTAGE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, HID_SBS_DESIGN_CAPACITY, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, HID_SBS_CHARGING_CURRENT, 0),	
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CAPACITY, HID_SBS_RELATIVE_STATE_OF_CHARGE, 50),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN, HID_SBS_MAX_ERROR, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_TEMP, HID_SBS_TEMPERATURE, 25),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG, HID_SBS_AVERAGE_TIME_TO_EMPTY, 100),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_TIME_TO_FULL_AVG, HID_SBS_AVERAGE_TIME_TO_FULL, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_MANUFACTURE_YEAR, HID_SBS_MANUFACTURE_DATE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_MANUFACTURE_MONTH, HID_SBS_MANUFACTURE_DATE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_MANUFACTURE_DAY, HID_SBS_MANUFACTURE_DATE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_MODEL_NAME, HID_SBS_IDEVICE_NAME, -1),	
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_MANUFACTURER, HID_SBS_IMANUFACTURER_NAME, -1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_SERIAL_NUMBER, HID_SBS_SERIAL_NUMBER, 12568762),
};


static const struct hid_sbs_property_usage sbs_charger_prop_usages[] = {
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_STATUS, HID_SBS_SMART_BATTERY_CHARGER_STATUS, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_PRESENT, HID_SBS_SMART_BATTERY_SELECTOR_STATE, 1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_ONLINE, HID_SBS_SMART_BATTERY_SELECTOR_STATE, 1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_HEALTH, HID_SBS_SMART_BATTERY_CHARGER_STATUS, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, HID_SBS_CHARGER_CHARGING_CURRENT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, HID_SBS_CHARGER_CHARGING_VOLTAGE, 0),
};


struct hid_battery {
	struct hid_subdevice *hsdev;
	struct power_supply *battery;
	struct power_supply *charger;
	struct hid_composite_callbacks callbacks;
	struct uint8_t *battery_data;
};


enum power_supply_property hid_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_MANUFACTURE_YEAR,
	POWER_SUPPLY_PROP_MANUFACTURE_MONTH,
	POWER_SUPPLY_PROP_MANUFACTURE_DAY,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER
};





enum power_supply_property hid_battery_charger_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};



static inline int battery_psp_to_usage_index(enum power_supply_property psp) {
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(sbs_battery_prop_usages); ++i)
		if (sbs_battery_prop_usages[i].psp == psp)
			return i;
	
	return -1;
}


static inline int charger_psp_to_usage_index(enum power_supply_property psp) {
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(sbs_charger_prop_usages); ++i)
		if (sbs_charger_prop_usages[i].psp == psp)
			return i;
	
	return -1;
}

static inline int capacity_to_capacity_level(int capacity)
{
	return 	capacity >= 90 ? POWER_SUPPLY_CAPACITY_LEVEL_FULL :
			capacity >= 75 ? POWER_SUPPLY_CAPACITY_LEVEL_HIGH :
			capacity >= 40 ? POWER_SUPPLY_CAPACITY_LEVEL_NORMAL :
			capacity >= 20 ? POWER_SUPPLY_CAPACITY_LEVEL_LOW : 
			capacity >= 10 ? POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL :
			POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;

}		

/*
static int hid_battery_get_usage_value(struct hid_device *hdev, enum power_supply_property psp)
{
	struct hid_report *report;
	struct hid_field *field;
	int i, j;
	unsigned int count = 0;
	int index_val = battery_psp_to_usage_index(psp);

	list_for_each_entry(report,
			    &hdev->report_enum[HID_INPUT_REPORT].report_list,
			    list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			if (field->application == HID_SBS_BATTERY_SYSTEM_USAGE)
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

static int hid_battery_get_property_from_hid_usage(struct hid_battery *bat,
													enum hid_battery_system_usage usage, 
													union power_supply_propval *val)
{	
	return 0;
}

*/

static int hid_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	//struct hid_battery *bat = power_supply_get_drvdata(psy);
	int psp_idx = battery_psp_to_usage_index(psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
	case POWER_SUPPLY_PROP_MANUFACTURER:
		if (psp_idx < 0)
			val->strval = "Unkown";
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		psp_idx = battery_psp_to_usage_index(POWER_SUPPLY_PROP_CAPACITY_LEVEL);
		if (psp_idx < 0)
			val->intval = 0;
		else	
			val->intval = capacity_to_capacity_level(sbs_battery_prop_usages[psp_idx].default_value);
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = "0000000";
		break;
	default:
		if (psp_idx < 0 || psp_idx >= ARRAY_SIZE(sbs_battery_prop_usages))
			val->intval = 0;
		else
			val->intval = sbs_battery_prop_usages[psp_idx].default_value;
		break;
	}
	return 0;
}



static int hid_battery_charger_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;
	default:
	}
	return 0;
}


/*
static int hid_battery_report_size(struct hid_device *hdev)
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
			if (field->application == HID_SBS_BATTERY_SYSTEM_USAGE)
			{
				count += field->maxusage;
				for (j = 0; j < field->maxusage; ++j)
				{
					hid_info(hdev, "usage: 0x%08x (size: %d count: %d)\n", field->usage[j].hid, field->report_size, field->report_count);
				}
			}
		}
	}

	hid_info(hdev, "count: %d\n", count);
	return count;
}

*/


static const struct power_supply_desc hid_battery_desc = {
	.name = "hid-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.num_properties = ARRAY_SIZE(hid_battery_properties),
	.properties = hid_battery_properties,
	.get_property = hid_battery_get_property,

};
static const struct power_supply_desc hid_battery_charger_desc = {
	.name = "hid-charger",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.num_properties = ARRAY_SIZE(hid_battery_charger_properties),
	.properties = hid_battery_charger_properties,
	.get_property = hid_battery_charger_get_property,
};


static inline bool hsdev_has_charger(struct hid_subdevice *hsdev)
{
	int ret, i;
	bool found = false;
	struct hid_attribute_info info;
	int hid_charger_usages[] = {
		HID_SBS_SMART_BATTERY_CHARGER_MODE,
		HID_SBS_SMART_BATTERY_CHARGER_STATUS,
		HID_SBS_SMART_BATTERY_CHARGER_SPEC_INFO
	};

	for (i = 0; i < 3; ++i)
	{
		ret = hid_composite_get_attribute_info(hsdev, 
						HID_INPUT_REPORT, 
						HID_SBS_BATTERY_SYSTEM_USAGE,
						hid_charger_usages[i], &info);
		if (!ret)
		{
			found = true;
			break;
		}
	}

	return found;
}

static inline bool hsdev_has_battery(struct hid_subdevice *hsdev)
{
	int ret, i;
	bool found = false;
	struct hid_attribute_info info;
	int hid_battery_usages[] = {
		HID_SBS_SMART_BATTERY_BATTERY_MODE,
		HID_SBS_SMART_BATTERY_BATTERY_STATUS,
		HID_SBS_SMART_BATTERY_ALARM_WARNING,		
	};

	for (i = 0; i < 3; ++i)
	{
		ret = hid_composite_get_attribute_info(hsdev, 
				HID_INPUT_REPORT, 
				HID_SBS_BATTERY_SYSTEM_USAGE,
				hid_battery_usages[i], &info);
		if (!ret)
		{
			found = true;
			break;
		}
		
	}

	return found;
}



static int hid_battery_add_power_supply(struct platform_device *pdev, 
				struct power_supply **ps, const struct power_supply_desc *desc)
{
	int ret;
	struct power_supply_config psy_cfg = {};
	struct hid_battery *bat = platform_get_drvdata(pdev);

	psy_cfg.drv_data = bat;

	*ps = devm_power_supply_register(&pdev->dev, desc, &psy_cfg);
	if (IS_ERR(*ps)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(*ps), "Unable to register %s\n", 
				desc->type == POWER_SUPPLY_TYPE_BATTERY ? "battery" : "charger");
	}
	
	ret = power_supply_powers(*ps, &pdev->dev);
	if (ret) {
		hid_err(bat->hsdev->hdev, "Unable to get %s info: %d\n", 
				desc->type == POWER_SUPPLY_TYPE_BATTERY ? "battery" : "charger", ret);
		return ret;
	}

	return 0;
}

static int hid_battery_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_battery *bat;
	
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

	bat->hsdev = hsdev;
	
	if (hsdev_has_battery(hsdev))
	{
		ret = hid_battery_add_power_supply(pdev, &bat->battery, &hid_battery_desc);
		if (IS_ERR(bat->battery)) {
			goto cleanup;
		}
	}

	if (hsdev_has_charger(hsdev))
	{
		ret = hid_battery_add_power_supply(pdev, &bat->charger, &hid_battery_charger_desc);
		if (IS_ERR(bat->battery)) {
			goto cleanup;
		}		
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
