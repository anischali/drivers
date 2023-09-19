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

int hid_composite_device_open(struct hid_subdevice *sdev);
void hid_composite_device_close(struct hid_subdevice *sdev);
#endif /* __HID_COMPOSITE_H__ */