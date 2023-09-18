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
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/list.h>

#include "hid-ids.h"

struct hid_subdevice_pending {
	bool status;
	struct completion ready;
	u32 usage_id;
	u32 attr_usage_id;
	int raw_size;
	u8  *raw_data;
};

struct hid_subdevice {
	struct hid_device *hdev;
	u32 vendor_id;
	u32 product_id;
	u32 usage;
	int start_collection_index;
	int end_collection_index;
	struct mutex *mutex_ptr;
	struct hid_subdevice_pending pending;
};


struct hid_composite_device {
	struct mutex mutex;
	spinlock_t lock;
	struct hid_device *hdev;
	struct mfd_cell *hid_composite_client_devs;
	int hid_composite_client_cnt;
	int ref_cnt;
};

static int hid_composite_get_physical_device_count(struct hid_device *hdev)
{
	int i;
	int count = 0;

	for (i = 0; i < hdev->maxcollection; ++i) {
		struct hid_collection *collection = &hdev->collection[i];
		if (collection->type == HID_COLLECTION_PHYSICAL ||
		    collection->type == HID_COLLECTION_APPLICATION)
			++count;
	}

	return count;
}

static int hid_composite_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hid_composite_device *ldev;
	unsigned int minor;
	int ret, i, dev_cnt;
	struct hid_subdevice *hsdev, *last_hsdev = NULL, *collection_hsdev = NULL;
	char *name;

	ldev = devm_kzalloc(&hdev->dev, sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	hid_set_drvdata(hdev, ldev);

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ldev->hdev = hdev;
	mutex_init(&ldev->mutex);
	spin_lock_init(&ldev->lock);

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	dev_cnt = hid_composite_get_physical_device_count(hdev);
	if (dev_cnt > 0xFFU) {
		hid_err(hdev, "Invalid Physical device count\n");
		ret = -EINVAL;
		goto err_stop_hw;
	}
	ldev->hid_composite_client_devs = devm_kcalloc(&hdev->dev,
						      dev_cnt,
						      sizeof(struct mfd_cell),
						      GFP_KERNEL);
	if (ldev->hid_composite_client_devs == NULL) {
		hid_err(hdev, "Failed to allocate memory for mfd cells\n");
		ret = -ENOMEM;
		goto err_stop_hw;
	}

	minor = ((struct hidraw *) hdev->hidraw)->minor;

	for (i = 1; i < hdev->maxcollection; ++i) {
		
		struct hid_collection *collection = &hdev->collection[i];

		if (collection->type == HID_COLLECTION_PHYSICAL ||
		    collection->type == HID_COLLECTION_APPLICATION) {
			hsdev = devm_kzalloc(&hdev->dev, sizeof(*hsdev), 
									GFP_KERNEL);
			if (!hsdev) 
			{
				hid_err(hdev, "cannot allocate hid_subdevice\n");
				ret = -ENOMEM;
				goto err_stop_hw;
			}				

			hsdev->hdev = hdev;
			hsdev->vendor_id = hdev->vendor;
			hsdev->product_id = hdev->product;
			hsdev->usage = collection->usage;
			hsdev->mutex_ptr = devm_kzalloc(&hdev->dev,
							sizeof(struct mutex),
							GFP_KERNEL);
			if (!hsdev->mutex_ptr) {
				ret = -ENOMEM;
				goto err_stop_hw;
			}

			mutex_init(hsdev->mutex_ptr);
			hsdev->start_collection_index = i;
			if (last_hsdev)
				last_hsdev->end_collection_index = i;
			last_hsdev = hsdev;
			name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
					      "HID-COMPOSITE-%x",
					      collection->usage);
			if (name == NULL) {
				hid_err(hdev, "Failed MFD device name\n");
				ret = -ENOMEM;
				goto err_stop_hw;
			}
			ldev->hid_composite_client_devs[
				ldev->hid_composite_client_cnt].name = name;
			ldev->hid_composite_client_devs[
				ldev->hid_composite_client_cnt].platform_data =
							hsdev;
			ldev->hid_composite_client_devs[
				ldev->hid_composite_client_cnt].pdata_size =
							sizeof(*hsdev);
			hid_dbg(hdev, "Adding %s:%d\n", name,
					hsdev->start_collection_index);
			ldev->hid_composite_client_cnt++;

			if (collection_hsdev)
				collection_hsdev->end_collection_index = i;
			if (collection->type == HID_COLLECTION_APPLICATION &&
			    collection->usage == 0x0)
				collection_hsdev = hsdev;
		}
	}

	if (last_hsdev)
		last_hsdev->end_collection_index = i;
	if (collection_hsdev)
		collection_hsdev->end_collection_index = i;

	ret = mfd_add_hotplug_devices(&hdev->dev,
			ldev->hid_composite_client_devs,
			ldev->hid_composite_client_cnt);
	if (ret < 0)
		goto err_stop_hw;

	hid_info(hdev, "HID Composite initialized\n");

	return 0;

err_stop_hw:
	hid_hw_stop(hdev);
	return ret;
}


static void hid_composite_remove(struct hid_device *hdev)
{
	struct hid_composite_device *ldev = hid_get_drvdata(hdev);
	unsigned long flags;
	int i;

	hid_dbg(hdev, " hardware removed\n");
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	spin_lock_irqsave(&ldev->lock, flags);
	for (i = 0; i < ldev->hid_composite_client_cnt; ++i) {
		struct hid_subdevice *hsdev =
			ldev->hid_composite_client_devs[i].platform_data;
		if (hsdev->pending.status)
			complete(&hsdev->pending.ready);
	}
	spin_unlock_irqrestore(&ldev->lock, flags);
	mfd_remove_devices(&hdev->dev);
	mutex_destroy(&ldev->mutex);
}



static const struct hid_device_id hid_composite_table[] = {
    { HID_USB_DEVICE(0x18C9, 0x1044)},
	{ }
};
MODULE_DEVICE_TABLE(hid, hid_composite_table);

static struct hid_driver hid_composite_driver = {
	.name = "hid-composite",
	.probe = hid_composite_probe,
	.remove = hid_composite_remove,
	.id_table = hid_composite_table,
};

module_hid_driver(hid_composite_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_DESCRIPTION("USB HID Composite bridge driver");