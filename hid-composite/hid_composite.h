#ifndef __HID_COMPOSITE_H__
#define __HID_COMPOSITE_H__
#include <linux/hid.h>
#include <linux/hidraw.h>

#ifndef USB_SUBCLASS_HID_COMPOSITE
#define USB_SUBCLASS_HID_COMPOSITE 0x02U
#endif
#ifndef HID_GROUP_COMPOSITE
#define HID_GROUP_COMPOSITE 0x0005U
#endif


#ifndef EXAMPLE_VENDOR_ID
#define EXAMPLE_VENDOR_ID 0x1DC0
#endif

enum _quirks {
	HID_COMPOSITE_QUIRK_NO_IRQ_EVENTS = BIT(0),
};


#define _min(x, y) ((x < y) ? x : y)
#define _max(x, y) ((x > y) ? x : y)

enum hid_composite_read_flags {
	HID_COMPOSITE_SYNC,
	HID_COMPOSITE_ASYNC,
};


struct hid_subdevice_pending {
	bool status;
	struct completion ready;
	u32 usage_id;
	u32 attr_usage_id;
	int raw_size;
	u8  *raw_data;
};



/**
 * struct hid_composite_attribute_info - Attribute info
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
	u32 report_type;
	u32 attrib_id;
	s32 report_id;
	s32 index;
	s32 units;
	s32 unit_expo;
	s32 size;
	s32 logical_minimum;
	s32 logical_maximum;
	s32 usage_hid;
	s32 usage_logical;
};

struct hid_subdevice {
	int id;
	int start_collection_index;
	int end_collection_index;
	u32 vendor_id;
	u32 product_id;
	u32 usage;
	u32 quirks;
	struct hid_device *hdev;
	struct mutex *mutex_ptr;
	struct hid_subdevice_pending pending;
	struct hid_attribute_info *info;
};



/**
 * struct hid_composite_callbacks - Client callback functions
 * @pdev:		Platform device instance of the client driver.
 * @suspend:		Suspend callback.
 * @resume:		Resume callback.
 * @capture_sample:	Callback to get a sample.
 * @send_event:		Send notification to indicate all samples are
 *			captured, process and send event
 */
struct hid_composite_callbacks {
	struct platform_device *pdev;
	int (*suspend)(struct hid_subdevice *hsdev, void *priv);
	int (*resume)(struct hid_subdevice *hsdev, void *priv);
	int (*capture_sample)(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv);
	int (*send_event)(struct hid_subdevice *hsdev, u32 usage_id,
			 void *priv);
};


int hid_composite_device_open(struct hid_subdevice *sdev);
void hid_composite_device_close(struct hid_subdevice *sdev);
int hid_composite_get_attribute_info(struct hid_subdevice *hsdev,
				u8 type,
				u32 usage_id,
				u32 attr_usage_id,
				struct hid_attribute_info *info);
int hid_composite_get_feature(struct hid_subdevice *hsdev, u32 report_id,
			   u32 field_index,  void *buffer, int buffer_size);
int hid_composite_set_feature(struct hid_subdevice *hsdev, u32 report_id,
			   u32 field_index, void *buffer, int buffer_size);
int hid_composite_remove_callback(struct hid_subdevice *hsdev,
				u32 usage_id);
int hid_composite_register_callback(struct hid_subdevice *hsdev,
			u32 usage_id,
			struct hid_composite_callbacks *usage_callback);
int hid_composite_input_attr_get_raw_value(struct hid_subdevice *hsdev,
					u32 usage_id,
					u32 attr_usage_id, u32 report_id,
					enum hid_composite_read_flags flag,
					bool is_signed);
int hid_composite_output_attr_set_raw_value(struct hid_subdevice *hsdev, u32 report_id, 
					void *buffer_value, size_t buffer_size);


int hid_composite_get_report(struct hid_subdevice *hsdev,
					int report_type, u32 usage_id,
					u32 attr_usage_id, u32 report_id,
					enum hid_composite_read_flags flag,
					uint8_t *buffer, size_t size, int timeout);

int hid_composite_set_report(struct hid_subdevice *hsdev, int report_type,
			u32 report_id, u32 field_index, void *buffer, int buffer_size);
	
int hid_composite_get_raw_value(struct hid_subdevice *hsdev,  int report_id, 
						int report_type, void *data, size_t size);

int hid_composite_set_raw_value(struct hid_subdevice *hsdev,  int report_id, 
						int report_type, void *data, size_t size);

static inline u32 hid_composite_value(size_t raw_len, char *raw_data)
{
	switch (raw_len) {
	case 1:
		return *(u8 *)raw_data;
	case 2:
		return *(u16 *)raw_data;
	case 4:
		return *(u32 *)raw_data;
	default:
		return (u32)(~0U); /* 0xff... or -1 to denote an error */
	}
}
#endif /* __HID_COMPOSITE_H__ */