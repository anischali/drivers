// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Anis CHALI <anis.chali@exfo.com>
 * Based on the Battery driver for the Ingenic JZ47xx SoCs of Artur Rojek <contact@artur-rojek.eu>
 * based on drivers/power/supply/jz4740-battery.c
 * Based on the work of Alexander Holler <holler@ahsoftware.de> on HID Sensor HUB
 */

#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/jiffies.h>
#include "hid_composite.h"
#include "hid-power-supply.h"


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

struct hid_power_supply_usage_info {
	struct hid_sbs_property_usage *pusage;
	struct hid_attribute_info info;
	union hid_usage_propval {
		int intval;
		const char *strval;
	} value;
};

struct hid_power_supply {
	char ps_name[32];

	struct hid_subdevice *hsdev;
	struct power_supply *power_supply;
	struct power_supply_desc power_supply_desc;

	struct hid_composite_callbacks callbacks;
	struct completion data_completion;
	spinlock_t data_lock;
	struct hid_power_supply_usage_info *ps_data_last;
	struct hid_power_supply_usage_info *ps_data;

	unsigned long last_update;
	bool ready;
	size_t ps_data_size;
	int minor;
	int index;
	int battery_status;
	int usage_id;
	int type;
};


static const char *hid_power_supply_names[] = {
	[POWER_SUPPLY_TYPE_UNKNOWN] = "hid-unkown",
	[POWER_SUPPLY_TYPE_BATTERY] = "hid-battery",
	[POWER_SUPPLY_TYPE_UPS] = "hid-ups",
	[POWER_SUPPLY_TYPE_MAINS] = "hid-charger",
	[POWER_SUPPLY_TYPE_USB] = "hid-usb",
	[POWER_SUPPLY_TYPE_USB_DCP] = "hid-usb-dcp",
	[POWER_SUPPLY_TYPE_USB_CDP] = "hid-usb-cdp",
	[POWER_SUPPLY_TYPE_USB_ACA] = "hid-usb-aca",
	[POWER_SUPPLY_TYPE_USB_TYPE_C] = "hid-usbc",
	[POWER_SUPPLY_TYPE_USB_PD] = "hid-usb-pd",
	[POWER_SUPPLY_TYPE_USB_PD_DRP] = "hid-usb-pd-drp",
	[POWER_SUPPLY_TYPE_APPLE_BRICK_ID] = "hid-apple-brick-id",
	[POWER_SUPPLY_TYPE_WIRELESS] = "hid-wireless",
};

static struct ida hid_power_supply_idas[POWER_SUPPLY_TYPE_WIRELESS + 1] = {0};


static const struct hid_sbs_property_usage sbs_power_supply_prop_usages[] = {
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_STATUS, HID_SBS_POWER_SUPPLY_BATTERY_STATUS, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_ONLINE, HID_SBS_POWER_SUPPLY_CHARGER_STATUS, 1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, HID_SBS_CHARGER_CHARGING_CURRENT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, HID_SBS_CHARGER_CHARGING_VOLTAGE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_PRESENT, HID_SBS_BATTERY_PRESENT, 1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_TECHNOLOGY, HID_SBS_IDEVICE_CHEMISTRY, -1),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CYCLE_COUNT, HID_SBS_CYCLE_COUNT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CURRENT_AVG, HID_SBS_AVERAGE_CURRENT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CURRENT_NOW, HID_SBS_CURRENT, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_VOLTAGE_NOW, HID_SBS_VOLTAGE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX, HID_SBS_CHARGING_VOLTAGE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN, HID_SBS_DESIGN_VOLTAGE, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_CURRENT_MAX, HID_SBS_DESIGN_CAPACITY, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, HID_SBS_DESIGN_CHARGE_FULL, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_ENERGY_FULL, HID_SBS_CHARGE_FULL, 0),
	HID_SBS_PROPERTY_USAGE(POWER_SUPPLY_PROP_ENERGY_NOW, HID_SBS_CHARGE_NOW, 0),
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

static inline int power_supply_psp_to_usage_index(enum power_supply_property psp) {
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(sbs_power_supply_prop_usages); ++i)
		if (sbs_power_supply_prop_usages[i].psp == psp)
			return i;

	return -1;
}


static inline struct hid_power_supply_usage_info *
hid_power_supply_get_usage(struct hid_power_supply_usage_info *usages,
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
	return 	capacity <= 100 && capacity >= 90 ? POWER_SUPPLY_CAPACITY_LEVEL_FULL :
			capacity >= 75 ? POWER_SUPPLY_CAPACITY_LEVEL_HIGH :
			capacity >= 40 ? POWER_SUPPLY_CAPACITY_LEVEL_NORMAL :
			capacity >= 10 ? POWER_SUPPLY_CAPACITY_LEVEL_LOW :
			capacity >= 0 ? POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL :
			POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;

}


static int hid_power_supply_hw_read(struct hid_power_supply *ps, bool force)
{
	int ret = 0;

	if ((jiffies - ps->last_update) < msecs_to_jiffies(2000) && !force)
		return 0;

	reinit_completion(&ps->data_completion);
	/* get a report with all values through requesting one value */
	ret = hid_composite_get_report(ps->hsdev, HID_INPUT_REPORT,
			ps->usage_id, ps->ps_data[0].info.usage_id,
			ps->ps_data[0].info.report_id, HID_COMPOSITE_ASYNC,  NULL, 0, 0);
	ret = wait_for_completion_interruptible_timeout(
		&ps->data_completion, msecs_to_jiffies(1000));

	if (ret > 0) {
		ps->last_update = jiffies;
		return 0;
	}

	return -EIO;
}

static int hid_power_supply_hw_write(struct hid_power_supply *ps, uint8_t *values, size_t size)
{
	int ret;

	ret = hid_composite_set_feature(ps->hsdev, ps->ps_data[0].info.report_id, 
									0, values, size);
	if (ret)
		return ret;
	
	return hid_power_supply_hw_read(ps, true);
}

static void  sbs_unit_adjustment(enum power_supply_property psp, union power_supply_propval *val)
{
	switch (psp) {

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_MAX:	
		val->intval *= BASE_UNIT_CONVERSION;
		break;
	
	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		//val->intval /= 10;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		/* sbs provides battery temperature in 0.1K
		 * so convert it to 0.1°C
		 */
		val->intval -= TEMP_KELVIN_TO_CELSIUS;
		break;

	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		/* sbs provides time to empty and time to full in minutes.
		 * Convert to seconds
		 */
		if (val->intval != 0xFFFF)
			val->intval *= TIME_UNIT_CONVERSION;
		break;

	default:
	}
}


static int hid_power_supply_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct hid_power_supply *ps = power_supply_get_drvdata(psy);
	struct hid_power_supply_usage_info *usage;
	int psp_idx = power_supply_psp_to_usage_index(psp);
	unsigned long flags;

	if (psp_idx < 0 || psp_idx >= ARRAY_SIZE(sbs_power_supply_prop_usages))
		return -EINVAL;

	hid_power_supply_hw_read(ps, false);

	usage = hid_power_supply_get_usage(ps->ps_data,
			ps->ps_data_size, sbs_power_supply_prop_usages[psp_idx].usage);
	if (!usage)
		return -ENOTSUPP;
	

	spin_lock_irqsave(&ps->data_lock, flags);

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
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = usage->value.intval;
			ps->battery_status = val->intval;
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
		case POWER_SUPPLY_PROP_CURRENT_AVG:
			val->intval = usage->value.intval;
			sbs_unit_adjustment(psp, val);
			
			if (ps->battery_status == POWER_SUPPLY_STATUS_DISCHARGING && val->intval > 0)
				val->intval *= -1;

			break;
		default:
			val->intval = usage->value.intval;
			sbs_unit_adjustment(psp, val);
	}

	spin_unlock_irqrestore(&ps->data_lock, flags);

	return 0;
}

static int hid_power_supply_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct hid_power_supply *ps = power_supply_get_drvdata(psy);

	uint8_t online;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		online = val->intval == 1 ? 1 : 0;
		return hid_power_supply_hw_write(ps, &online, 1);
	default:
		return -EINVAL;
	}

	return 0;
}


static int hid_power_supply_prop_writeable(struct power_supply *psy,
					  enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_ONLINE;
}

static void hid_power_supply_external_changed(struct power_supply *psy)
{
	struct hid_power_supply *ps = power_supply_get_drvdata(psy);
	hid_power_supply_hw_read(ps, true);
}


static size_t hid_power_supply_usages_count(struct hid_power_supply *ps,
		const struct hid_sbs_property_usage *sbs_props, size_t sbs_props_length)
{
	int ret, i;
	size_t count = 0;
	struct hid_attribute_info info;
	for (i = 0; i < sbs_props_length; ++i)
	{
		ret = hid_composite_get_attribute_info(ps->hsdev,
				HID_INPUT_REPORT,
				ps->usage_id,
				sbs_props[i].usage, &info);
		if (!ret)
			++count;
	}

	return count;
}

static inline int hid_power_supply_allocate_properties(
		struct platform_device *pdev,
		const struct hid_sbs_property_usage *sbs,
		size_t plen,
		enum power_supply_property **properties)
{
	struct hid_power_supply *ps = platform_get_drvdata(pdev);
	enum power_supply_property *props;
	struct hid_attribute_info info;
	int i = 0, j = 0, cnt, ret;

	cnt = hid_power_supply_usages_count(ps, sbs, plen);
	if (cnt <= 0)
		return -ENOENT;

	props = devm_kcalloc(&pdev->dev, cnt,
			sizeof(enum power_supply_property), GFP_KERNEL);
	if (props == NULL)
		return -ENOMEM;

	for (i = 0; i < plen && j < cnt; i++)
	{
		ret = hid_composite_get_attribute_info(ps->hsdev,
			HID_INPUT_REPORT,
			ps->usage_id,
			sbs[i].usage,
			&info);

		if (!ret)
			props[j++] = sbs[i].psp;
	}

	*properties = props;

	return cnt;
}

static int hid_power_supply_get_available_usages(struct hid_power_supply *ps,
			struct hid_power_supply_usage_info *ps_info, size_t ps_info_len,
			const struct hid_sbs_property_usage *props, size_t props_len)
{
	struct hid_attribute_info info;
	int i, ret, cnt = 0;
	struct hid_subdevice *hsdev = ps->hsdev;

	for (i = 0; i < props_len &&
		cnt < ps_info_len; ++i) {
		ret = hid_composite_get_attribute_info(hsdev,
			HID_INPUT_REPORT,
			ps->usage_id,
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

	return cnt;
}


static int hid_power_supply_capture_sample(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv)
{
	struct hid_power_supply *ps = platform_get_drvdata(priv);
	struct hid_power_supply_usage_info *usage = NULL;

	usage = hid_power_supply_get_usage(ps->ps_data,
			ps->ps_data_size, usage_id);
	if (usage)
	{
		usage->value.intval = hid_composite_value(raw_len, raw_data);
		return 0;
	}

	return -EINVAL;
}


static int hid_power_supply_proc_event(struct hid_subdevice *hsdev, u32 usage_id,
			 void *priv)
{
	struct hid_power_supply *ps = platform_get_drvdata(priv);
	
	if (ps->power_supply && ps->ready)
		power_supply_changed(ps->power_supply);

	if (!completion_done(&ps->data_completion))
		complete_all(&ps->data_completion);

	return 0;
}

static int hid_power_supply_allocate_data(struct platform_device *pdev)
{
	struct hid_power_supply *ps = platform_get_drvdata(pdev);
	int ps_size = 0;

	ps_size += hid_power_supply_usages_count(ps,
					sbs_power_supply_prop_usages, ARRAY_SIZE(sbs_power_supply_prop_usages));

	ps->ps_data_size = ps_size;
	ps->ps_data = devm_kcalloc(&pdev->dev, ps->ps_data_size,
				 sizeof(struct hid_power_supply_usage_info), GFP_KERNEL);
	if (!ps->ps_data)
		return -ENOMEM;


	ps_size = 0;
	ps_size += hid_power_supply_get_available_usages(ps, &ps->ps_data[0],
				ps->ps_data_size, sbs_power_supply_prop_usages,
				ARRAY_SIZE(sbs_power_supply_prop_usages));

	ps->ps_data_size = ps_size;

	return 0;
}



static int hid_power_supply_add_power_supply(struct platform_device *pdev, 
				struct power_supply **ps, const struct power_supply_desc *desc)
{
	int ret;
	struct power_supply_config psy_cfg = {};
	struct hid_power_supply *hps = platform_get_drvdata(pdev);

	psy_cfg.drv_data = hps;

	*ps = power_supply_register(&pdev->dev, desc, &psy_cfg);
	if (IS_ERR(*ps)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(*ps), "Unable to register %s\n",
				hid_power_supply_names[desc->type]);
	}

	ret = power_supply_powers(*ps, &pdev->dev);
	if (ret) {
		hid_err(hps->hsdev->hdev, "Unable to get %s info: %d\n", 
				hid_power_supply_names[desc->type], ret);
		return ret;
	}

	return 0;
}

static int hid_power_supply_allocate_power_supply(struct platform_device *pdev)
{
	struct hid_power_supply *ps = platform_get_drvdata(pdev);
	enum power_supply_property *props;
	int cnt;

	if (!ps)
		return -ENODEV;

	cnt = hid_power_supply_allocate_properties(pdev,
			sbs_power_supply_prop_usages,
			ARRAY_SIZE(sbs_power_supply_prop_usages), &props);
	if	(cnt <= 0)
		return -ENODEV;

	snprintf(ps->ps_name, 20, "%s-%d:%d", hid_power_supply_names[ps->type], ps->minor, ps->index);
	ps->power_supply_desc.name = ps->ps_name;
	ps->power_supply_desc.type = ps->type,
	ps->power_supply_desc.get_property = hid_power_supply_get_property,
	ps->power_supply_desc.set_property = hid_power_supply_set_property,
	ps->power_supply_desc.external_power_changed = hid_power_supply_external_changed;
	ps->power_supply_desc.property_is_writeable = hid_power_supply_prop_writeable,
	ps->power_supply_desc.num_properties = cnt;
	ps->power_supply_desc.properties = props;
	ps->power_supply_desc.use_for_apm = 0;

	return hid_power_supply_add_power_supply(pdev, &ps->power_supply, &ps->power_supply_desc);
}


#ifdef CONFIG_PM
static int hid_power_supply_suspend(struct hid_subdevice *hsdev, void *priv)
{
	return 0;
}

static int hid_power_supply_resume(struct hid_subdevice *hsdev, void *priv)
{
	struct hid_power_supply *ps = platform_get_drvdata(priv);

	hid_power_supply_hw_read(ps, true);

	return 0;
}
#endif

static int hid_power_supply_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_power_supply *ps;
	int type = platform_get_device_id(pdev)->driver_data;

	int ret;

	if (!hsdev)
		return -ENODEV;

	ps = devm_kzalloc(&pdev->dev, sizeof(*ps), GFP_KERNEL);
	if (!ps)
		return -ENOMEM;

	ps->index = ida_simple_get(&hid_power_supply_idas[type], 0, 32, GFP_KERNEL);
	if (ps->index < 0)
		return ps->index;

	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

	spin_lock_init(&ps->data_lock);
	init_completion(&ps->data_completion);

	ps->index = ret;
	ps->hsdev = hsdev;
	ps->minor = hsdev->id;
	ps->type = type;
	ps->usage_id = HID_SBS_POWER_SUPPLY_TYPE_UNKNWON + type;

	ps->callbacks.capture_sample = hid_power_supply_capture_sample;
	ps->callbacks.send_event = hid_power_supply_proc_event;
#ifdef CONFIG_PM
	ps->callbacks.suspend = hid_power_supply_suspend;
	ps->callbacks.resume = hid_power_supply_resume;
#endif
	ps->callbacks.pdev = pdev;
	hid_composite_register_callback(hsdev,
		ps->usage_id, &ps->callbacks);

	platform_set_drvdata(pdev, ps);

	ret = hid_power_supply_allocate_data(pdev);
	if (ret)
		goto cleanup;

	ret = hid_power_supply_allocate_power_supply(pdev);
	if (ret)
		goto cleanup;
	
	hid_power_supply_hw_read(ps, true);

	ps->ready = true;

	return 0;

cleanup:
	ida_simple_remove(&hid_power_supply_idas[type], ps->index);
	if (ps->power_supply)
		power_supply_unregister(ps->power_supply);
	
	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	hid_composite_remove_callback(hsdev, ps->usage_id);

	return ret;
}


static int hid_power_supply_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_power_supply *ps = platform_get_drvdata(pdev);

	complete_all(&ps->data_completion);

	if (ps->power_supply)
		power_supply_unregister(ps->power_supply);

	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	hid_composite_remove_callback(hsdev, ps->usage_id);
	ida_simple_remove(&hid_power_supply_idas[ps->type], ps->index);
	
	return 0;
}


static const struct platform_device_id hid_power_supply_platform_ids[] = {
	/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
	{ .name = "HID-COMPOSITE-8500a0", .driver_data = POWER_SUPPLY_TYPE_UNKNOWN },
	{ .name = "HID-COMPOSITE-8500a1", .driver_data = POWER_SUPPLY_TYPE_BATTERY },
	{ .name = "HID-COMPOSITE-8500a2", .driver_data = POWER_SUPPLY_TYPE_UPS },
	{ .name = "HID-COMPOSITE-8500a3", .driver_data = POWER_SUPPLY_TYPE_MAINS },
	{ .name = "HID-COMPOSITE-8500a4", .driver_data = POWER_SUPPLY_TYPE_USB },
	{ .name = "HID-COMPOSITE-8500a5", .driver_data = POWER_SUPPLY_TYPE_USB_DCP },
	{ .name = "HID-COMPOSITE-8500a6", .driver_data = POWER_SUPPLY_TYPE_USB_CDP },
	{ .name = "HID-COMPOSITE-8500a7", .driver_data = POWER_SUPPLY_TYPE_USB_ACA },
	{ .name = "HID-COMPOSITE-8500a8", .driver_data = POWER_SUPPLY_TYPE_USB_TYPE_C }, 
	{ .name = "HID-COMPOSITE-8500a9", .driver_data = POWER_SUPPLY_TYPE_USB_PD },
	{ .name = "HID-COMPOSITE-8500aa", .driver_data = POWER_SUPPLY_TYPE_USB_PD_DRP },
	{ .name = "HID-COMPOSITE-8500ab", .driver_data = POWER_SUPPLY_TYPE_APPLE_BRICK_ID },
	{ .name = "HID-COMPOSITE-8500ac", .driver_data = POWER_SUPPLY_TYPE_WIRELESS },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_power_supply_platform_ids);

static struct platform_driver hid_power_supply_driver = {
	.id_table = hid_power_supply_platform_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe = hid_power_supply_probe,
	.remove = hid_power_supply_remove,
};

MODULE_DESCRIPTION("HID Battery driver for USB HID based batteries");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_LICENSE("GPL");



static int __init hid_power_supply_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hid_power_supply_idas); i++)
		ida_init(&hid_power_supply_idas[i]);

	return platform_driver_register(&hid_power_supply_driver);
}

static void hid_power_supply_exit(void)
{
	int i;

	platform_driver_unregister(&hid_power_supply_driver);

	for (i = 0; i < ARRAY_SIZE(hid_power_supply_idas); i++)
		ida_destroy(&hid_power_supply_idas[i]);
}

module_init(hid_power_supply_init);
module_exit(hid_power_supply_exit);