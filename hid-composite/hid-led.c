// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple USB RGB LED driver
 *
 * Copyright 2016 Heiner Kallweit <hkallweit1@gmail.com>
 * Based on drivers/hid/hid-thingm.c and
 * drivers/usb/misc/usbled.c
 */

#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include "hid-ids.h"
#include "hid_composite.h"
#include <linux/iio/iio.h>

enum hidled_report_type {
	RAW_REQUEST,
	OUTPUT_REPORT
};

enum hidled_usages {
	HID_RGB_LED_USAGE = 0x00080052,
	HID_RGB_RED_LED_USAGE = 0x00080053,
	HID_RGB_BLUE_LED_USAGE = 0x00080054,
	HID_RGB_GREEN_LED_USAGE = 0x00080055,
	HID_RGB_INTENSITY_LED_USAGE = 0x00080055,
};

static const uint32_t hidled_addresses[] = {
	HID_RGB_RED_LED_USAGE,
	HID_RGB_BLUE_LED_USAGE,
	HID_RGB_GREEN_LED_USAGE,
};

#define RISO_KAGAKU_IX(r, g, b) riso_kagaku_tbl[((r)?1:0)+((g)?2:0)+((b)?4:0)]

struct hidled_device;
struct hidled_rgb;

struct hidled_config {
	const char		*name;
	const char		*short_name;
	enum led_brightness	max_brightness;
	int			num_leds;
	size_t			report_size;
	enum hidled_report_type	report_type;
	int (*init)(struct hidled_device *ldev);
	int (*write)(struct led_classdev *cdev, enum led_brightness br);
};

struct hidled_led {
	struct led_classdev	cdev;
	struct hidled_rgb	*rgb;
	char			name[32];
};

struct hidled_rgb {
	struct hidled_device	*ldev;
	struct hidled_led	red;
	struct hidled_led	green;
	struct hidled_led	blue;
	u8			num;
};

struct hidled_device {
	struct hid_attribute_info *info;
	struct hidled_config *config;
	struct hid_device       *hdev;
	struct hidled_rgb	*rgb;
	u8			*buf;
	struct mutex		lock;
};

#define MAX_REPORT_SIZE		16

#define to_hidled_led(arg) container_of(arg, struct hidled_led, cdev)

static int hidled_send(struct hidled_device *ldev, __u8 *buf)
{
	int ret;

	mutex_lock(&ldev->lock);

	/*
	 * buffer provided to hid_hw_raw_request must not be on the stack
	 * and must not be part of a data structure
	 */
	memcpy(ldev->buf, buf, ldev->config->report_size);

	if (ldev->config->report_type == RAW_REQUEST)
		ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
					 ldev->config->report_size,
					 HID_FEATURE_REPORT,
					 HID_REQ_SET_REPORT);
	else if (ldev->config->report_type == OUTPUT_REPORT)
		ret = hid_hw_output_report(ldev->hdev, ldev->buf,
					   ldev->config->report_size);
	else
		ret = -EINVAL;

	mutex_unlock(&ldev->lock);

	if (ret < 0)
		return ret;

	return ret == ldev->config->report_size ? 0 : -EMSGSIZE;
}

/* reading data is supported for report type RAW_REQUEST only */
static int hidled_recv(struct hidled_device *ldev, __u8 *buf)
{
	int ret;

	if (ldev->config->report_type != RAW_REQUEST)
		return -EINVAL;

	mutex_lock(&ldev->lock);

	memcpy(ldev->buf, buf, ldev->config->report_size);

	ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
				 ldev->config->report_size,
				 HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	if (ret < 0)
		goto err;

	ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
				 ldev->config->report_size,
				 HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);

	memcpy(buf, ldev->buf, ldev->config->report_size);
err:
	mutex_unlock(&ldev->lock);

	return ret < 0 ? ret : 0;
}


static int hidled_write(struct led_classdev *cdev, enum led_brightness br)
{
	struct hidled_led *led = to_hidled_led(cdev);
	struct hidled_device *ldev = led->rgb->ldev;

	__u8 buf[6] = { [1] = 1 };

	buf[0] = (uint8_t)ldev->info[led->rgb->num].report_id;
	buf[1] = led->rgb->num;
	buf[2] = led->rgb->red.cdev.brightness;
	buf[3] = led->rgb->green.cdev.brightness;
	buf[4] = led->rgb->blue.cdev.brightness;
	
	return hidled_send(ldev, buf);
}

static struct hidled_config hidled_config = {
		.name = "HID Leds",
		.short_name = "HID_LED",
		.max_brightness = 255,
		.report_size = 6,
		.report_type = RAW_REQUEST,
		.write = hidled_write,
};

unsigned int hidled_count_leds(struct hid_device *hdev)
{
	struct hid_report *report;
	struct hid_field *field;
	int i;
	unsigned int count = 0;

	list_for_each_entry(report,
			    &hdev->report_enum[HID_OUTPUT_REPORT].report_list,
			    list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
				if (field->application == HID_RGB_LED_USAGE)
					count += 1;
		}
	}
	return count;
}



static int hidled_parse_report(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hidled_device *ldev = platform_get_drvdata(pdev);
	int i, ret;

	for (i = 0; i < ldev->config->num_leds; i++)
	{
		ret = hid_composite_get_attribute_info(hsdev, 
				HID_OUTPUT_REPORT, 
				HID_RGB_LED_USAGE, 
				HID_RGB_LED_USAGE, 
				&ldev->info[i]);
		if (ret)
			return -EINVAL;
		
	}

	return 0;
}

static int hidled_init_led(struct hidled_led *led, const char *color_name,
			   struct hidled_rgb *rgb, unsigned int minor)
{
	const struct hidled_config *config = rgb->ldev->config;

	if (config->num_leds > 1)
		snprintf(led->name, sizeof(led->name), "%s%u:%s:led%u",
			 config->short_name, minor, color_name, rgb->num);
	else
		snprintf(led->name, sizeof(led->name), "%s%u:%s",
			 config->short_name, minor, color_name);
	led->cdev.name = led->name;
	led->cdev.max_brightness = config->max_brightness;
	led->cdev.brightness_set_blocking = config->write;
	led->cdev.flags = LED_HW_PLUGGABLE;
	led->rgb = rgb;

	return devm_led_classdev_register(&rgb->ldev->hdev->dev, &led->cdev);
}

static int hidled_init_rgb(struct hidled_rgb *rgb, unsigned int minor)
{
	int ret;

	/* Register the red diode */
	ret = hidled_init_led(&rgb->red, "red", rgb, minor);
	if (ret)
		return ret;

	/* Register the green diode */
	ret = hidled_init_led(&rgb->green, "green", rgb, minor);
	if (ret)
		return ret;

	/* Register the blue diode */
	return hidled_init_led(&rgb->blue, "blue", rgb, minor);
}


static void hidled_remove_rgb(struct hidled_device *ldev)
{
	int i;
	for (i = 0; i < ldev->config->num_leds; i++) {
		devm_led_classdev_unregister(&ldev->hdev->dev, &ldev->rgb[i].red.cdev);
		devm_led_classdev_unregister(&ldev->hdev->dev, &ldev->rgb[i].green.cdev);
		devm_led_classdev_unregister(&ldev->hdev->dev, &ldev->rgb[i].blue.cdev);
	}
}

static int hidled_platform_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hidled_device *ldev;
	unsigned int minor;
	int i;

	if (!hsdev)
		return -ENODEV;

	ldev = devm_kzalloc(&pdev->dev, sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	ldev->buf = devm_kmalloc(&pdev->dev, MAX_REPORT_SIZE, GFP_KERNEL);
	if (!ldev->buf)
		return -ENOMEM;

	ldev->hdev = hsdev->hdev;
	mutex_init(&ldev->lock);
	ldev->config = &hidled_config;

	if (!ldev->config)
		return -EINVAL;

	if (ldev->config->init) {
		ret = ldev->config->init(ldev);
		if (ret)
			return ret;
	}
	ldev->config->num_leds = hidled_count_leds(hsdev->hdev);
	ldev->rgb = devm_kcalloc(&pdev->dev, ldev->config->num_leds,
				 sizeof(struct hidled_rgb), GFP_KERNEL);
	if (!ldev->rgb)
		return -ENOMEM;
	
	ldev->info = devm_kcalloc(&pdev->dev, ldev->config->num_leds,
				 sizeof(struct hidled_rgb), GFP_KERNEL);
	if (!ldev->info)
		return -ENOMEM;

	platform_set_drvdata(pdev, ldev);
	
	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

	ret = hidled_parse_report(pdev);
	if (ret)
		return ret;
	
	minor = ((struct hidraw *) hsdev->hdev->hidraw)->minor;

	for (i = 0; i < ldev->config->num_leds; i++) {
		ldev->rgb[i].ldev = ldev;
		ldev->rgb[i].num = i;
		ret = hidled_init_rgb(&ldev->rgb[i], minor);
		if (ret) {
			hid_device_io_stop(hsdev->hdev);
			hid_composite_device_close(hsdev);
			return ret;
		}
	}

	return 0;
}

static int hidled_platform_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hidled_device *ldev = platform_get_drvdata(pdev);

	hidled_remove_rgb(ldev);
	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return 0;
}

static const struct platform_device_id hidled_platform_ids[] = {
	{
		/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-80052",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hidled_platform_ids);

static struct platform_driver hidled_platform_platform_driver = {
	.id_table = hidled_platform_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= hidled_platform_probe,
	.remove		= hidled_platform_remove,
};
module_platform_driver(hidled_platform_platform_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_DESCRIPTION("Simple USB HID Composite RGB LED driver");