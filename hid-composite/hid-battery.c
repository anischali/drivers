// SPDX-License-Identifier: GPL-2.0
/*
 * Battery driver for the Ingenic JZ47xx SoCs
 * Copyright (c) 2019 Artur Rojek <contact@artur-rojek.eu>
 *
 * based on drivers/power/supply/jz4740-battery.c
 */

#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include "hid_composite.h"
#include "hid-battery-system.h"

static int instance = 0;

#define HID_SBS_PROPERTY_USAGE(_psp, _usage, _default_value) { \
	.psp = _psp, \
	.usage = _usage, \
	.default_value = _default_value, \
}

struct hid_sbs_property_usage {
	int psp;
	int usage;
	int default_value;
};

struct hid_battery_usage_info {
	struct hid_sbs_property_usage *pusage;
	struct hid_attribute_info info;
	union hid_usage_propval {
		int intval;
		const char *strval;
	} value;
};



struct hid_battery {
	char battery_name[32];
	char charger_name[32];
	struct hid_subdevice *hsdev;
	struct power_supply *battery;
	struct power_supply_desc battery_desc;
	enum power_supply_property *battery_properties;
	struct power_supply *charger;
	enum power_supply_property *charger_properties;
	struct power_supply_desc charger_desc;
	struct hid_composite_callbacks callbacks;
	struct hid_battery_usage_info *battery_data;
	struct hid_battery_usage_info *charger_data;
	spinlock_t data_lock;
	struct completion data_completion;
	size_t battery_data_size;
	size_t charger_data_size;
	bool has_battery;
	bool has_charger;
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
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CAPACITY_LEVEL, HID_SBS_RELATIVE_STATE_OF_CHARGE, 0),
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
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
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

static inline struct hid_battery_usage_info * 
hid_battery_get_usage(struct hid_battery_usage_info *usages, 
			size_t size, uint32_t usage_id)
{
	int i = 0;
	for (i = 0; i < size; ++i)
	{
		if (usages[i].info.usage_hid == usage_id ||
			usages[i].info.usage_logical == usage_id)
			return &usages[i];
	}

	return NULL;
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

static int hid_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct hid_battery *bat = power_supply_get_drvdata(psy);
	struct hid_battery_usage_info *usage;
	int psp_idx = battery_psp_to_usage_index(psp);
	unsigned long flags;
	int ret;

	if (psp_idx < 0 || psp_idx >= ARRAY_SIZE(sbs_battery_prop_usages))
		return -EINVAL;

	reinit_completion(&bat->data_completion);
	/* get a report with all values through requesting one value */
	hid_composite_input_attr_get_raw_value(bat->hsdev,
			HID_SBS_BATTERY_SYSTEM_USAGE, 
			sbs_battery_prop_usages[0].usage,
			bat->battery_data[0].info.report_id, HID_COMPOSITE_SYNC, false);
	ret = wait_for_completion_killable_timeout(
			&bat->data_completion, msecs_to_jiffies(1000));
	if (ret > 0) {
		usage = hid_battery_get_usage(bat->battery_data, 
				bat->battery_data_size, sbs_battery_prop_usages[psp_idx].usage);
		if (!usage)
			return -ENOTSUPP;

		spin_lock_irqsave(&bat->data_lock, flags);

		switch (psp) {
		case POWER_SUPPLY_PROP_MODEL_NAME:
		case POWER_SUPPLY_PROP_MANUFACTURER:
			val->strval = "Unkown";
			break;
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			val->intval = capacity_to_capacity_level(usage->value.intval);
			break;
		case POWER_SUPPLY_PROP_SERIAL_NUMBER:
			val->strval = "0000000";
			break;
		default:
			val->intval = usage->value.intval;
		}

		spin_unlock_irqrestore(&bat->data_lock, flags);
		return 0;
	}

	if (!ret)
		return -EIO;

	return ret;
}



static int hid_battery_charger_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct hid_battery *bat = power_supply_get_drvdata(psy);
	struct hid_battery_usage_info *usage;
	int psp_idx = charger_psp_to_usage_index(psp);
	unsigned long flags;
	int ret;

	if (psp_idx < 0 || psp_idx >= ARRAY_SIZE(sbs_charger_prop_usages))
		return -EINVAL;

	reinit_completion(&bat->data_completion);
	hid_composite_input_attr_get_raw_value(bat->hsdev,
			HID_SBS_BATTERY_SYSTEM_USAGE, 
			sbs_charger_prop_usages[0].usage,
			bat->charger_data[0].info.report_id, HID_COMPOSITE_SYNC, false);
	ret = wait_for_completion_killable_timeout(
			&bat->data_completion, msecs_to_jiffies(1000));
	if (ret > 0) {
		usage = hid_battery_get_usage(bat->charger_data, 
				bat->charger_data_size, sbs_charger_prop_usages[psp_idx].usage);
		if (!usage)
			return -ENOTSUPP;

		spin_lock_irqsave(&bat->data_lock, flags);
		switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = 1;
			break;
		default:
		}
		spin_unlock_irqrestore(&bat->data_lock, flags);
		return 0;
	}
	if (!ret)
		return -EIO;

	return ret;
}


static inline int hid_battery_allocate_properties(
		struct platform_device *pdev,
		struct hid_battery_usage_info *usages,
		enum power_supply_property **properties, 
		size_t length)
{
	int i = 0;
	enum power_supply_property *props;
	
	props = devm_kcalloc(&pdev->dev, length, 
			sizeof(enum power_supply_property), GFP_KERNEL);
	if (props == NULL)
		return -ENOMEM;

	for (i = 0; i < length; i++)
	{
		props[i] = usages[i].pusage->psp;
	}

	*properties = props;

	return 0;
}

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

static size_t hid_battery_usages_count(struct hid_battery *battery, 
		const struct hid_sbs_property_usage *sbs_props, size_t sbs_props_length)
{
	int ret, i;
	size_t count = 0;
	struct hid_attribute_info info;
	for (i = 0; i < sbs_props_length; ++i)
	{
		ret = hid_composite_get_attribute_info(battery->hsdev, 
				HID_INPUT_REPORT, 
				HID_SBS_BATTERY_SYSTEM_USAGE,
				sbs_props[i].usage, &info);
		if (!ret)
			++count;
	}

	return count;
}



static void hid_battery_get_available_usages(struct hid_battery *battery,
			struct hid_battery_usage_info *ps_info, size_t *ps_info_len,
			const struct hid_sbs_property_usage *props, size_t props_len)
{
	struct hid_attribute_info info;
	int i, ret, cnt = 0;
	struct hid_subdevice *hsdev = battery->hsdev;

	for (i = 0; i < props_len && 
		cnt < *ps_info_len; ++i) {
		ret = hid_composite_get_attribute_info(hsdev, 
			HID_INPUT_REPORT, 
			HID_SBS_BATTERY_SYSTEM_USAGE,
			props[i].usage, 
			&info);
			
		if (!ret) {
			memcpy(&ps_info[cnt].info, 
				&info, sizeof(struct hid_attribute_info));
			ps_info[cnt].pusage = 
					(struct hid_sbs_property_usage *)&props[i];
			++cnt;
		}
	}

	*ps_info_len = cnt;
}


static int hid_battery_capture_sample(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv)
{
	struct hid_battery *bat = platform_get_drvdata(priv);
	struct hid_battery_usage_info *usage = NULL;

	usage = hid_battery_get_usage(bat->battery_data, 
			bat->battery_data_size, usage_id);
	if (usage)
	{	
		usage->value.intval = hid_composite_value(raw_len, raw_data);
		return 0;
	}

	usage = hid_battery_get_usage(bat->charger_data, 
			bat->charger_data_size, usage_id);
	if (usage)
	{	
		usage->value.intval = hid_composite_value(raw_len, raw_data);
		return 0;
	}	

	return -EINVAL;
}


static int hid_battery_proc_event(struct hid_subdevice *hsdev, u32 usage_id,
			 void *priv)
{
	struct hid_battery *bat = platform_get_drvdata(priv);
	complete(&bat->data_completion);
	
	return 0;
}



static int hid_battery_add_power_supply(struct platform_device *pdev, 
				struct power_supply **ps, const struct power_supply_desc *desc)
{
	int ret;
	struct power_supply_config psy_cfg = {};
	struct hid_battery *bat = platform_get_drvdata(pdev);

	psy_cfg.drv_data = bat;

	*ps = power_supply_register(&pdev->dev, desc, &psy_cfg);
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


static int hid_battery_allocate_battery(struct platform_device *pdev)
{
	struct hid_battery *bat = platform_get_drvdata(pdev);
	struct hid_subdevice *hsdev = bat->hsdev;
	int ret;

	if (!bat)
		return -ENODEV;

	bat->battery_data_size = hid_battery_usages_count(bat, 
				sbs_battery_prop_usages, ARRAY_SIZE(sbs_battery_prop_usages));
	bat->battery_data = devm_kcalloc(&pdev->dev, bat->battery_data_size,
				 sizeof(struct hid_battery_usage_info), GFP_KERNEL);
	if (!bat->battery_data)
		return -ENOMEM;

	hid_battery_get_available_usages(bat, bat->battery_data, 
				&bat->battery_data_size, sbs_battery_prop_usages, 
				ARRAY_SIZE(sbs_battery_prop_usages));

	hid_info(hsdev->hdev, "pdev: %p bat: %p bat->battery_data: %p", 
				pdev, bat, bat->battery_data);
	
	ret = hid_battery_allocate_properties(pdev, 
			bat->battery_data, &bat->battery_properties, 
			bat->battery_data_size);
	if (ret)
		return ret;
	
	hid_info(hsdev->hdev, "pdev: %p bat: %p bat->battery_data: %p", 
				pdev, bat, bat->battery_data);
	
	snprintf(bat->battery_name, 32, "hid-battery-%d", instance);
	bat->battery_desc.name = bat->battery_name;
	bat->battery_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	bat->battery_desc.get_property = hid_battery_get_property;
	bat->battery_desc.external_power_changed = power_supply_changed;
	bat->battery_desc.num_properties = bat->battery_data_size;
	bat->battery_desc.properties = bat->battery_properties;
	bat->battery_desc.use_for_apm = 0;

	return hid_battery_add_power_supply(pdev, &bat->battery, &bat->battery_desc);
}



static int hid_battery_allocate_charger(struct platform_device *pdev)
{
	struct hid_battery *bat = platform_get_drvdata(pdev);
	int ret;

	if (!bat)
		return -ENODEV;

	bat->charger_data_size = hid_battery_usages_count(bat, 
				sbs_charger_prop_usages, ARRAY_SIZE(sbs_charger_prop_usages));
	bat->charger_data = devm_kcalloc(&pdev->dev, bat->charger_data_size,
				 sizeof(struct hid_battery_usage_info), GFP_KERNEL);
	if (!bat->charger_data)
		return -ENOMEM;
	
	hid_battery_get_available_usages(bat, bat->charger_data, 
				&bat->charger_data_size, sbs_charger_prop_usages, 
				ARRAY_SIZE(sbs_charger_prop_usages));

	ret = hid_battery_allocate_properties(pdev, 
			bat->charger_data, &bat->charger_properties, 
			bat->charger_data_size);
	if (ret)
		return ret;

	snprintf(bat->charger_name, 32, "hid-charger-%d", instance);
	bat->charger_desc.name = bat->charger_name;
	bat->charger_desc.type = POWER_SUPPLY_TYPE_MAINS,
	bat->charger_desc.get_property = hid_battery_charger_get_property,
	bat->charger_desc.num_properties = bat->charger_data_size;
	bat->charger_desc.properties = bat->charger_properties;
	bat->charger_desc.use_for_apm = 0;

	return hid_battery_add_power_supply(pdev, &bat->charger, &bat->charger_desc);
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

	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

	spin_lock_init(&bat->data_lock);
	init_completion(&bat->data_completion);
	
	bat->hsdev = hsdev;
	bat->has_charger = hsdev_has_charger(hsdev);
	bat->has_battery = hsdev_has_battery(hsdev);

	bat->callbacks.capture_sample = hid_battery_capture_sample;
	bat->callbacks.send_event = hid_battery_proc_event;
	bat->callbacks.pdev = pdev;
	hid_composite_register_callback(hsdev, 
		HID_SBS_BATTERY_SYSTEM_USAGE, &bat->callbacks);

	platform_set_drvdata(pdev, bat);

	if (bat->has_battery)
	{
		ret = hid_battery_allocate_battery(pdev);
		if (ret)
			goto cleanup;
	}

	if (bat->has_charger)
	{
		ret = hid_battery_allocate_charger(pdev);
		if (ret)
			goto cleanup;
	}

	hid_info(hsdev->hdev, 
		"battery implemented usages props: %ld charger implemented usages props: %ld\n", 
		bat->battery_data_size, bat->charger_data_size);
	
	++instance;

	return 0;

cleanup:
	hid_composite_remove_callback(hsdev, HID_SBS_BATTERY_SYSTEM_USAGE);
	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);

	return ret;
}


static int hid_battery_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);	
	struct hid_battery *bat = platform_get_drvdata(pdev);

	complete_all(&bat->data_completion);

	if (bat->has_battery && bat->battery)
		power_supply_unregister(bat->battery);

	if (bat->has_charger && bat->charger)
		power_supply_unregister(bat->charger);

	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	hid_composite_remove_callback(hsdev, HID_SBS_BATTERY_SYSTEM_USAGE);
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

MODULE_DESCRIPTION("HID Battery driver for USB HID based batteries");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_LICENSE("GPL");
