#ifndef __HID_COMPOSITE_H__
#define __HID_COMPOSITE_H__
#include <linux/hid.h>
#include <linux/hidraw.h>

struct hid_subdevice_pending {
	bool status;
	struct completion ready;
	u32 usage_id;
	u32 attr_usage_id;
	int raw_size;
	u8  *raw_data;
};



/**
 * struct hid_sensor_hub_attribute_info - Attribute info
 * @usage_id:		Parent usage id of a physical device.
 * @attrib_id:		Attribute id for this attribute.
 * @report_id:		Report id in which this information resides.
 * @index:		Field index in the report.
 * @units:		Measurment unit for this attribute.
 * @unit_expo:		Exponent used in the data.
 * @size:		Size in bytes for data size.
 * @logical_minimum:	Logical minimum value for this attribute.
 * @logical_maximum:	Logical maximum value for this attribute.
 */
struct hid_attribute_info {
	u32 usage_id;
	u32 attrib_id;
	s32 report_id;
	s32 index;
	s32 units;
	s32 unit_expo;
	s32 size;
	s32 logical_minimum;
	s32 logical_maximum;
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
	struct hid_attribute_info *info;
};

int hid_composite_device_open(struct hid_subdevice *sdev);
void hid_composite_device_close(struct hid_subdevice *sdev);
int hid_composite_get_attribute_info(struct hid_subdevice *hsdev,
				u8 type,
				u32 usage_id,
				u32 attr_usage_id,
				struct hid_attribute_info *info);
#endif /* __HID_COMPOSITE_H__ */