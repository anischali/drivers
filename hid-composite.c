// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID Composite driver
 * Copyright 2024 Anis CHALI <anis.chali1@outlook.com>
 * Based on the work of Alexander Holler <holler@ahsoftware.de> on HID Sensor HUB drivers/hid/hid-sensor-hub.c
 */
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/list.h>
#include "hid_composite.h"
#include "hid-ids.h"


/**
 * struct hid_sensor_hub_callbacks_list - Stores callback list
 * @list:		list head.
 * @usage_id:		usage id for a physical device.
 * @hsdev:		Stored hid instance for current hub device.
 * @usage_callback:	Stores registered callback functions.
 * @priv:		Private data for a physical device.
 */
struct hid_composite_callbacks_list {
	struct list_head list;
	u32 usage_id;
	struct hid_subdevice *hsdev;
	struct hid_composite_callbacks *usage_callback;
	void *priv;
};

struct hid_composite_device {
	struct mutex mutex;
	spinlock_t lock;
	struct hid_device *hdev;
	struct mfd_cell *hid_composite_client_devs;
	struct list_head dyn_callback_list;
	spinlock_t dyn_callback_lock;
	int hid_composite_client_cnt;
	int ref_cnt;
};

struct hid_report *hid_composite_report(int id, struct hid_device *hdev,
						int dir)
{
	struct hid_report *report;

	list_for_each_entry(report, &hdev->report_enum[dir].report_list, list) {
		if (report->id == id)
			return report;
	}
	hid_err(hdev, "No report with id 0x%x found\n", id);

	return NULL;
}



/**
 * @brief 
 * 
 * @param info 
 * @param index 
 * @param report_id 
 * @param field 
 */
static void hid_composite_fill_attr_info(
		struct hid_attribute_info *info,
		s32 index, s32 report_id, struct hid_field *field)
{
	info->index = index;
	info->report_id = report_id;
	info->units = field->unit;
	info->unit_expo = field->unit_exponent;
	info->size = (field->report_size * field->report_count)/8;
	info->logical_minimum = field->logical_minimum;
	info->logical_maximum = field->logical_maximum;
	info->report_type = field->report_type;
	info->usage_hid = field->usage->hid;
	info->usage_logical = field->logical;
}

/**
 * @brief 
 * 
 * @param hsdev 
 * @param type 
 * @param usage_id 
 * @param attr_usage_id 
 * @param info 
 * @return int 
 */

int hid_composite_get_attribute_info(struct hid_subdevice *hsdev,
				u8 type,
				u32 usage_id,
				u32 attr_usage_id,
				struct hid_attribute_info *info)
{
	int ret = -1;
	int i;
	struct hid_report *report;
	struct hid_field *field;
	struct hid_report_enum *report_enum;
	struct hid_device *hdev = hsdev->hdev;

	/* Initialize with defaults */
	info->usage_id = usage_id;
	info->attrib_id = attr_usage_id;
	info->report_id = -1;
	info->index = -1;
	info->units = -1;
	info->unit_expo = -1;

	report_enum = &hdev->report_enum[type];
	list_for_each_entry(report, &report_enum->report_list, list) {
		for (i = 0; i < report->maxfield; ++i) {
			field = report->field[i];
			if (field->maxusage) {
				if ((field->application == usage_id || field->physical == usage_id) &&
					(field->logical == attr_usage_id || field->usage[0].hid == attr_usage_id) &&
					(field->usage[0].collection_index >= hsdev->start_collection_index) &&
					(field->usage[0].collection_index < hsdev->end_collection_index)) {
						
					hid_composite_fill_attr_info(info, i,
								report->id,
								field);
					ret = 0;
					break;
				}
			}
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(hid_composite_get_attribute_info);



static struct hid_composite_callbacks *hid_composite_get_callback(
					struct hid_device *hdev,
					u32 usage_id,
					int collection_index,
					struct hid_subdevice **hsdev,
					void **priv)
{
	struct hid_composite_callbacks_list *callback;
	struct hid_composite_device *cdev = hid_get_drvdata(hdev);
	unsigned long flags;

	spin_lock_irqsave(&cdev->dyn_callback_lock, flags);
	list_for_each_entry(callback, &cdev->dyn_callback_list, list)
		if ((callback->usage_id == usage_id) &&
			(collection_index >=
				callback->hsdev->start_collection_index) &&
			(collection_index <
				callback->hsdev->end_collection_index)) {
			*priv = callback->priv;
			*hsdev = callback->hsdev;
			spin_unlock_irqrestore(&cdev->dyn_callback_lock,
					       flags);
			return callback->usage_callback;
		}
	spin_unlock_irqrestore(&cdev->dyn_callback_lock, flags);

	return NULL;
}

int hid_composite_register_callback(struct hid_subdevice *hsdev,
			u32 usage_id,
			struct hid_composite_callbacks *usage_callback)
{
	struct hid_composite_callbacks_list *callback;
	struct hid_composite_device *cdev = hid_get_drvdata(hsdev->hdev);
	unsigned long flags;

	spin_lock_irqsave(&cdev->dyn_callback_lock, flags);
	list_for_each_entry(callback, &cdev->dyn_callback_list, list)
		if (callback->usage_id == usage_id &&
						callback->hsdev == hsdev) {
			spin_unlock_irqrestore(&cdev->dyn_callback_lock, flags);
			return -EINVAL;
		}
	callback = kzalloc(sizeof(*callback), GFP_ATOMIC);
	if (!callback) {
		spin_unlock_irqrestore(&cdev->dyn_callback_lock, flags);
		return -ENOMEM;
	}
	callback->hsdev = hsdev;
	callback->usage_callback = usage_callback;
	callback->usage_id = usage_id;
	callback->priv = NULL;
	/*
	 * If there is a handler registered for the collection type, then
	 * it will handle all reports for sensors in this collection. If
	 * there is also an individual sensor handler registration, then
	 * we want to make sure that the reports are directed to collection
	 * handler, as this may be a fusion sensor. So add collection handlers
	 * to the beginning of the list, so that they are matched first.
	 */
	list_add(&callback->list, &cdev->dyn_callback_list);
	spin_unlock_irqrestore(&cdev->dyn_callback_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(hid_composite_register_callback);

int hid_composite_remove_callback(struct hid_subdevice *hsdev,
				u32 usage_id)
{
	struct hid_composite_callbacks_list *callback;
	struct hid_composite_device *cdev = hid_get_drvdata(hsdev->hdev);
	unsigned long flags;

	spin_lock_irqsave(&cdev->dyn_callback_lock, flags);
	list_for_each_entry(callback, &cdev->dyn_callback_list, list)
		if (callback->usage_id == usage_id &&
						callback->hsdev == hsdev) {
			list_del(&callback->list);
			kfree(callback);
			break;
		}
	spin_unlock_irqrestore(&cdev->dyn_callback_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(hid_composite_remove_callback);

int hid_composite_set_feature(struct hid_subdevice *hsdev, u32 report_id,
			   u32 field_index, void *buffer, int buffer_size)
{
	return hid_composite_set_report(hsdev, HID_FEATURE_REPORT, report_id, field_index, buffer, buffer_size);
}
EXPORT_SYMBOL_GPL(hid_composite_set_feature);

int hid_composite_get_feature(struct hid_subdevice *hsdev, u32 report_id,
			   u32 field_index, void *buffer, int buffer_size)
{
	struct hid_report *report;
	struct hid_composite_device *cdev = hid_get_drvdata(hsdev->hdev);
	int report_size;
	int ret = 0;
	u8 *val_ptr;
	int buffer_index = 0;
	int i;

	memset(buffer, 0, buffer_size);

	mutex_lock(&cdev->mutex);
	report = hid_composite_report(report_id, hsdev->hdev, HID_FEATURE_REPORT);
	if (!report || (field_index >= report->maxfield) ||
	    report->field[field_index]->report_count < 1) {
		ret = -EINVAL;
		goto done_proc;
	}
	hid_hw_request(hsdev->hdev, report, HID_REQ_GET_REPORT);
	hid_hw_wait(hsdev->hdev);

	/* calculate number of bytes required to read this field */
	report_size = DIV_ROUND_UP(report->field[field_index]->report_size,
				   8) *
				   report->field[field_index]->report_count;
	if (!report_size) {
		ret = -EINVAL;
		goto done_proc;
	}
	ret = min(report_size, buffer_size);

	val_ptr = (u8 *)report->field[field_index]->value;
	for (i = 0; i < report->field[field_index]->report_count; ++i) {
		if (buffer_index >= ret)
			break;

		memcpy(&((u8 *)buffer)[buffer_index], val_ptr,
		       report->field[field_index]->report_size / 8);
		val_ptr += sizeof(__s32);
		buffer_index += (report->field[field_index]->report_size / 8);
	}

done_proc:
	mutex_unlock(&cdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(hid_composite_get_feature);



int hid_composite_input_attr_get_raw_value(struct hid_subdevice *hsdev,
					u32 usage_id,
					u32 attr_usage_id, u32 report_id,
					enum hid_composite_read_flags flag,
					bool is_signed)
{
	struct hid_composite_device *cdev = hid_get_drvdata(hsdev->hdev);
	unsigned long flags;
	struct hid_report *report;
	int ret_val = 0;

	report = hid_composite_report(report_id, hsdev->hdev,
				   HID_INPUT_REPORT);
	if (!report)
		return -EINVAL;

	mutex_lock(hsdev->mutex_ptr);
	if (flag == HID_COMPOSITE_SYNC) {
		memset(&hsdev->pending, 0, sizeof(hsdev->pending));
		init_completion(&hsdev->pending.ready);
		hsdev->pending.usage_id = usage_id;
		hsdev->pending.attr_usage_id = attr_usage_id;
		hsdev->pending.raw_size = 0;

		spin_lock_irqsave(&cdev->lock, flags);
		hsdev->pending.status = true;
		spin_unlock_irqrestore(&cdev->lock, flags);
	}
	mutex_lock(&cdev->mutex);
	hid_hw_request(hsdev->hdev, report, HID_REQ_GET_REPORT);
	mutex_unlock(&cdev->mutex);	
	if (flag == HID_COMPOSITE_SYNC) {
		wait_for_completion_interruptible_timeout(
						&hsdev->pending.ready, HZ*5);
		switch (hsdev->pending.raw_size) {
		case 1:
			if (is_signed)
				ret_val = *(s8 *)hsdev->pending.raw_data;
			else
				ret_val = *(u8 *)hsdev->pending.raw_data;
			break;
		case 2:
			if (is_signed)
				ret_val = *(s16 *)hsdev->pending.raw_data;
			else
				ret_val = *(u16 *)hsdev->pending.raw_data;
			break;
		case 4:
			ret_val = *(u32 *)hsdev->pending.raw_data;
			break;
		default:
			ret_val = 0;
		}
		kfree(hsdev->pending.raw_data);
		hsdev->pending.status = false;
	}
	mutex_unlock(hsdev->mutex_ptr);

	return ret_val;
}
EXPORT_SYMBOL_GPL(hid_composite_input_attr_get_raw_value);


int hid_composite_get_report(struct hid_subdevice *hsdev,
					int report_type, u32 usage_id,
					u32 attr_usage_id, u32 report_id,
					enum hid_composite_read_flags flag,
					uint8_t *buffer, size_t size, int timeout)
{
	struct hid_composite_device *cdev = hid_get_drvdata(hsdev->hdev);
	unsigned long flags;
	struct hid_report *report;
	int ret = 0;

	report = hid_composite_report(report_id, hsdev->hdev,
				   report_type);
	if (!report)
		return -EINVAL;

	mutex_lock(hsdev->mutex_ptr);
	if (flag == HID_COMPOSITE_SYNC) {
		memset(&hsdev->pending, 0, sizeof(hsdev->pending));
		init_completion(&hsdev->pending.ready);
		hsdev->pending.usage_id = usage_id;
		hsdev->pending.attr_usage_id = attr_usage_id;
		hsdev->pending.raw_size = 0;

		spin_lock_irqsave(&cdev->lock, flags);
		hsdev->pending.status = true;
		spin_unlock_irqrestore(&cdev->lock, flags);
	}
	mutex_lock(&cdev->mutex);
	hid_hw_request(hsdev->hdev, report, HID_REQ_GET_REPORT);
	mutex_unlock(&cdev->mutex);	
	if (flag == HID_COMPOSITE_SYNC) {
		ret = wait_for_completion_interruptible_timeout(
						&hsdev->pending.ready, timeout);
		if (ret <= 0) {
			kfree(hsdev->pending.raw_data);
			goto out;
		}
		
		memcpy(buffer, hsdev->pending.raw_data, size);

		kfree(hsdev->pending.raw_data);
		hsdev->pending.status = false;
		ret = 0;
	}

out:
	mutex_unlock(hsdev->mutex_ptr);
	return ret;
}
EXPORT_SYMBOL_GPL(hid_composite_get_report);

int hid_composite_set_report(struct hid_subdevice *hsdev, int report_type,
			u32 report_id, u32 field_index, void *buffer, int buffer_size)
{
	struct hid_report *report;
	struct hid_composite_device *cdev = hid_get_drvdata(hsdev->hdev);
	int i = 0, j = 0, cnt = 0, ret = 0;
	char *ptr = buffer;
	
	mutex_lock(&cdev->mutex);
	report = hid_composite_report(report_id, hsdev->hdev, report_type);
	if (!report || (field_index >= report->maxfield)) {
		ret = -EINVAL;
		hid_err(hsdev->hdev, "failed to get hid report (type: %d)\n", report_type);
		goto done_proc;
	}

	for (i = 0; i < report->maxfield; ++i)
	{
		for (j = 0; j < report->field[i]->report_count; ++j)
		{
			ret = hid_set_field(report->field[i], j, 
					cnt < buffer_size ? hid_composite_value(report->field[i]->report_size / 8, &ptr[cnt]) : 0);
			if (ret)
				goto done_proc;
			
			++cnt;
		}
	}
	
	hid_hw_request(hsdev->hdev, report, HID_REQ_SET_REPORT);
	hid_hw_wait(hsdev->hdev);

done_proc:
	mutex_unlock(&cdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(hid_composite_set_report);


int hid_composite_output_attr_set_raw_value(struct hid_subdevice *hsdev, u32 report_id, 
											void *data, size_t size)
{
	struct hid_composite_device *cdev = hid_get_drvdata(hsdev->hdev);
	int ret = 0, sz = size;
	uint8_t *buf, *ptr;

	mutex_lock(&cdev->mutex);

	buf = kmalloc(size + ((report_id > 0) ? 1 : 0), GFP_ATOMIC);
	if (!buf) {
		ret = -ENOMEM;
		goto done;
	}
	ptr = buf;
	if (report_id > 0) {
		*ptr++ = (report_id & 0xff);
		++sz;
	} 

	memcpy(ptr, data, size);

	hid_hw_raw_request(hsdev->hdev, report_id, 
	 			buf, sz, 
	 			HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	hid_hw_wait(hsdev->hdev);

	kfree(buf);
done:
	mutex_unlock(&cdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(hid_composite_output_attr_set_raw_value);

/*
 * Handle raw report as sent by device
 */
static int hid_composite_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *raw_data, int size)
{
	int i;
	u8 *ptr;
	int sz;
	struct hid_composite_device *cdev = hid_get_drvdata(hdev);
	unsigned long flags;
	struct hid_composite_callbacks *callback = NULL;
	struct hid_collection *collection = NULL;
	void *priv = NULL;
	struct hid_subdevice *hsdev = NULL;
	int usage_id = 0;

	if (report->type != HID_INPUT_REPORT)
		return 1;

	ptr = raw_data;
	if (report->id)
		ptr++; /* Skip report id */

	spin_lock_irqsave(&cdev->lock, flags);

	for (i = 0; i < report->maxfield; ++i) {
		hid_dbg(hdev, "CP: (0x%08x) CA:(0x%08x) %d collection_index:%x hid:%x sz:%x\n",
				report->field[i]->physical, report->field[i]->application, i, report->field[i]->usage->collection_index,
				report->field[i]->usage->hid,
				(report->field[i]->report_size *
					report->field[i]->report_count)/8);
		sz = (report->field[i]->report_size *
					report->field[i]->report_count)/8;
		collection = &hdev->collection[
				report->field[i]->usage->collection_index];
		hid_dbg(hdev, "collection->usage %x\n",
					collection->usage);

		usage_id = report->field[i]->physical > 0 ? report->field[i]->physical :
								report->field[i]->application;
		callback = hid_composite_get_callback(hdev,
				usage_id,
				report->field[i]->usage[0].collection_index,
				&hsdev, &priv);
		if (!callback) {
			ptr += sz;
			continue;
		}
		if (hsdev->pending.status && (hsdev->pending.attr_usage_id ==
					      report->field[i]->usage->hid ||
					      hsdev->pending.attr_usage_id ==
					      report->field[i]->logical)) {
			hid_dbg(hdev, "data was pending ...\n");
			hsdev->pending.raw_data = kmemdup(ptr, sz, GFP_ATOMIC);
			if (hsdev->pending.raw_data)
				hsdev->pending.raw_size = sz;
			else
				hsdev->pending.raw_size = 0;
			complete(&hsdev->pending.ready);
		}
		if (callback->capture_sample) {
			if (report->field[i]->logical)
				callback->capture_sample(hsdev,
					report->field[i]->logical, sz, ptr,
					callback->pdev);
			else
				callback->capture_sample(hsdev,
					report->field[i]->usage->hid, sz, ptr,
					callback->pdev);
		}
		ptr += sz;
	}
	if (callback && collection && callback->send_event)
		callback->send_event(hsdev, collection->usage,
				callback->pdev);
	spin_unlock_irqrestore(&cdev->lock, flags);

	return 1;
}


/**
 * @brief 
 * 
 * @param sdev 
 * @return int 
 */
int hid_composite_device_open(struct hid_subdevice *sdev)
{
	int ret = 0;
	struct hid_composite_device *hsdev =  hid_get_drvdata(sdev->hdev);

	mutex_lock(&hsdev->mutex);
	if (!hsdev->ref_cnt) {
		ret = hid_hw_open(hsdev->hdev);
		if (ret) {
			hid_err(hsdev->hdev, "failed to open hid device\n");
			mutex_unlock(&hsdev->mutex);
			return ret;
		}
	}
	hsdev->ref_cnt++;
	mutex_unlock(&hsdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(hid_composite_device_open);


/**
 * @brief 
 * 
 * @param sdev 
 */
void hid_composite_device_close(struct hid_subdevice *sdev)
{
	struct hid_composite_device *hsdev =  hid_get_drvdata(sdev->hdev);

	mutex_lock(&hsdev->mutex);
	hsdev->ref_cnt--;
	if (!hsdev->ref_cnt)
		hid_hw_close(hsdev->hdev);
	mutex_unlock(&hsdev->mutex);
}
EXPORT_SYMBOL_GPL(hid_composite_device_close);


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


#ifdef CONFIG_PM
static int hid_composite_suspend(struct hid_device *hdev, pm_message_t message)
{
	struct hid_composite_device *cdev = hid_get_drvdata(hdev);
	struct hid_composite_callbacks_list *callback;
	unsigned long flags;

	hid_dbg(hdev, " sensor_hub_suspend\n");
	spin_lock_irqsave(&cdev->dyn_callback_lock, flags);
	list_for_each_entry(callback, &cdev->dyn_callback_list, list) {
		if (callback->usage_callback->suspend)
			callback->usage_callback->suspend(
					callback->hsdev, callback->priv);
	}
	spin_unlock_irqrestore(&cdev->dyn_callback_lock, flags);

	return 0;
}

static int hid_composite_resume(struct hid_device *hdev)
{
	struct hid_composite_device *cdev = hid_get_drvdata(hdev);
	struct hid_composite_callbacks_list *callback;
	unsigned long flags;

	hid_dbg(hdev, " sensor_hub_resume\n");
	spin_lock_irqsave(&cdev->dyn_callback_lock, flags);
	list_for_each_entry(callback, &cdev->dyn_callback_list, list) {
		if (callback->usage_callback->resume)
			callback->usage_callback->resume(
					callback->hsdev, callback->priv);
	}
	spin_unlock_irqrestore(&cdev->dyn_callback_lock, flags);

	return 0;
}

static int hid_composite_reset_resume(struct hid_device *hdev)
{
	return 0;
}
#endif


/**
 * @brief 
 * 
 * @param hdev 
 * @param id 
 * @return int 
 */
static int hid_composite_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hid_composite_device *ldev;
	int ret, i, dev_cnt;
	struct hid_subdevice *hsdev = NULL, *last_hsdev = NULL, *collection_hsdev = NULL;
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
	spin_lock_init(&ldev->dyn_callback_lock);
	spin_lock_init(&ldev->lock);
	
	INIT_LIST_HEAD(&hdev->inputs);

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;
	
	INIT_LIST_HEAD(&ldev->dyn_callback_list);
	ldev->hid_composite_client_cnt = 0;

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
			hid_dbg(hdev, "Usage: 0x%08x Adding %s:%d-%d\n", hsdev->usage, name,
					hsdev->start_collection_index, hsdev->end_collection_index);
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

/**
 * @brief 
 * 
 * @param hdev 
 */
static void hid_composite_remove(struct hid_device *hdev)
{
	struct hid_composite_device *cdev = hid_get_drvdata(hdev);
	struct hid_subdevice *hsdev;
	unsigned long flags;
	int i;

	hid_hw_close(hdev);
	hid_hw_stop(hdev);

	spin_lock_irqsave(&cdev->lock, flags);
	for (i = 0; i < cdev->hid_composite_client_cnt; ++i) {
		hsdev = cdev->hid_composite_client_devs[i].platform_data;
		if (hsdev->pending.status)
			complete(&hsdev->pending.ready);
	}
	spin_unlock_irqrestore(&cdev->lock, flags);
	mfd_remove_devices(&hdev->dev);
}



static const struct hid_device_id hid_composite_table[] = {
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_COMPOSITE, HID_ANY_ID, HID_ANY_ID)},
	{}
};
MODULE_DEVICE_TABLE(hid, hid_composite_table);

static struct hid_driver hid_composite_driver = {
	.name = "hid-composite",
	.probe = hid_composite_probe,
	.remove = hid_composite_remove,
	.id_table = hid_composite_table,
	.raw_event = hid_composite_raw_event,
#ifdef CONFIG_PM
	.suspend = hid_composite_suspend,
	.resume = hid_composite_resume,
	.reset_resume = hid_composite_reset_resume,
#endif
};

module_hid_driver(hid_composite_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_DESCRIPTION("USB HID Composite driver");