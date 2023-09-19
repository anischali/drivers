// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple USB HID Composite for System Control driver
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

static int hidinput_platform_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);

	if (!hsdev)
		return -ENODEV;

	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);
    ret = hidinput_connect(hsdev->hdev, false);
	if (ret) {
		hid_device_io_stop(hsdev->hdev);
		hid_composite_device_close(hsdev);
		return ret;
	}
	hid_info(hsdev->hdev, "hidinput platform device probed\n");
	return 0;
}

static int hidinput_platform_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);

	hidinput_disconnect(hsdev->hdev);
	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return 0;
}

static const struct platform_device_id hidinput_platform_ids[] = {
	{
		/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-80052",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hidinput_platform_ids);

static struct platform_driver hidinput_platform_platform_driver = {
	.id_table = hidinput_platform_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= hidinput_platform_probe,
	.remove		= hidinput_platform_remove,
};
module_platform_driver(hidinput_platform_platform_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_DESCRIPTION("Simple USB HID Composite input driver");