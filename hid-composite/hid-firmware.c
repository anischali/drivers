#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include "hid_composite.h"
#include "hid-firmware.h"
#include <linux/sysfs.h>
#include <linux/delay.h>


#define HID_FIRMWARE_DATA_SIZE 56
#define HID_PACKET_DATA_SIZE 64

enum _hid_firmware_requests {
    HID_FIRMWARE_WAKEUP_REASON = 0,
    HID_FIRMWARE_BOOTUP_REASON,
    HID_FIRMWARE_RESET_REASON,
    HID_FIRMWARE_NAME,
    HID_FIRMWARE_FILE_VERSION,
    HID_FIRMWARE_PRODUCT_VERSION,
    HID_FIRMWARE_APP_ID,
    HID_FIRMWARE_SIGNATURE,
    HID_FIRMWARE_VERSION,
    HID_FIRMWARE_HW_REVISION,
    HID_FIRMWARE_BOARD_TYPE,
    HID_FIRMWARE_BOARD_ID,
    HID_FIRMWARE_UUID,
    HID_FIRMWARE_USAGES_MAX
};
typedef uint8_t firmware_request_type_t;
typedef uint8_t firmware_length_t;

struct __attribute__((packed)) hid_firmware_report_t {
    firmware_request_type_t request;
    firmware_length_t length;
    uint8_t reserved;
    uint8_t data[HID_FIRMWARE_DATA_SIZE];
};
/*
static const char *firmware_properties_name[] = {
    [HID_FIRMWARE_WAKEUP_REASON] = "wakeup_reason",
    [HID_FIRMWARE_BOOTUP_REASON] = "bootup_reason",
    [HID_FIRMWARE_RESET_REASON] = "reset_reason",
    [HID_FIRMWARE_NAME] = "firmware_name",
    [HID_FIRMWARE_FILE_VERSION] = "firmware_version",
    [HID_FIRMWARE_PRODUCT_VERSION] = "product_version",
    [HID_FIRMWARE_APP_ID] = "application_id",
    [HID_FIRMWARE_SIGNATURE] = "signature",
    [HID_FIRMWARE_VERSION] = "version",
    [HID_FIRMWARE_HW_REVISION] = "hardware_revision",
    [HID_FIRMWARE_BOARD_TYPE] = "board_type",
};
*/
struct hid_firmware_version_t {
    uint32_t major;
    uint32_t minor;
    uint32_t build_hot_fix;
    uint32_t rev_reserved;
};

struct hid_firmware_fields_t {
    uint32_t wakeup_reason;
    uint32_t bootup_reason;
    uint32_t reset_reason;
    char firmware_name[32];
    uint32_t uuid[4];
    struct hid_firmware_version_t firmware_version;
    struct hid_firmware_version_t product_version;
    uint32_t application_id;
    uint32_t signature;
    uint32_t version;
    uint32_t hardware_revision;
    uint8_t board_type;
    int board_id;
};

static uint32_t usage_addresses[HID_FIRMWARE_USAGES_MAX] = {
    [HID_FIRMWARE_WAKEUP_REASON] = HID_USAGE_FIRMWARE_WAKEUP_REASON,
    [HID_FIRMWARE_BOOTUP_REASON] = HID_USAGE_FIRMWARE_BOOTUP_REASON,
    [HID_FIRMWARE_RESET_REASON] = HID_USAGE_FIRMWARE_RESET_REASON,
    [HID_FIRMWARE_NAME] = HID_USAGE_FIRMWARE_NAME,
    [HID_FIRMWARE_FILE_VERSION] = HID_USAGE_FIRMWARE_FILE_VERSION,
    [HID_FIRMWARE_PRODUCT_VERSION] = HID_USAGE_FIRMWARE_PRODUCT_VERSION,
    [HID_FIRMWARE_APP_ID] = HID_USAGE_FIRMWARE_APP_ID,
    [HID_FIRMWARE_SIGNATURE] = HID_USAGE_FIRMWARE_SIGNATURE,
    [HID_FIRMWARE_VERSION] = HID_USAGE_FIRMWARE_VERSION,
    [HID_FIRMWARE_HW_REVISION] = HID_USAGE_FIRMWARE_HW_REVISION,
    [HID_FIRMWARE_BOARD_TYPE] = HID_USAGE_FIRMWARE_BOARD_TYPE,
    [HID_FIRMWARE_BOARD_ID] = HID_USAGE_FIRMWARE_BOARD_ID,
    [HID_FIRMWARE_UUID] = HID_USAGE_FIRMWARE_UUID,
};

struct hid_firmware_t {
    struct kobject kobj;
    struct hid_subdevice *hsdev;
    struct platform_device *pdev;
    struct hid_composite_callbacks callbacks;
	spinlock_t data_lock;
    struct completion completion;
    uint8_t buffer[HID_PACKET_DATA_SIZE];
    struct hid_firmware_fields_t firmware;
    struct hid_attribute_info info[HID_FIRMWARE_USAGES_MAX];
    char name[32];
    bool async;
    int minor;
};

static int hid_firmware_capture_sample(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv);

static int hid_firmware_parse_report(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_firmware_t *firm = platform_get_drvdata(pdev);
	int i, ret;

	for (i = 0; i < HID_FIRMWARE_USAGES_MAX; ++i)
	{
		ret = hid_composite_get_attribute_info(hsdev,
					HID_INPUT_REPORT,
					HID_USAGE_FIRMWARE,
					usage_addresses[i],
					&firm->info[i]);
		if (ret)
			continue;
	}

	return 0;
}


static int hid_firmware_hw_request(struct hid_firmware_t *firm, firmware_request_type_t request) {
    struct hid_firmware_report_t *report;
    int ret = 0;

    firm->buffer[0] = (uint8_t)firm->info[request].report_id;
    report = (struct hid_firmware_report_t *)&firm->buffer[1];
    report->request = request;
    ret = hid_composite_set_raw_value(firm->hsdev, 
                firm->info[request].report_id, 
                HID_OUTPUT_REPORT, 
                (uint8_t *)&firm->buffer[0], sizeof(struct hid_firmware_report_t) + 1);

    firm->buffer[0] = (uint8_t)firm->info[request].report_id;
    ret = hid_composite_get_raw_value(firm->hsdev, 
                firm->info[request].report_id, 
                HID_INPUT_REPORT, 
                (uint8_t *)&firm->buffer[0], sizeof(struct hid_firmware_report_t) + 1);
    if (ret < 0)
        return ret;

    if (firm->async) {
        ret = wait_for_completion_interruptible_timeout(
						&firm->completion, HZ);
        if (ret <= 0)
            return -ETIMEDOUT;
    }
    else
    {
        hid_firmware_capture_sample(firm->hsdev, request, 
                    sizeof(struct hid_firmware_report_t) + 1, 
                    &firm->buffer[1], firm->pdev);
    }

    return 0;
}


static int hid_firmware_hw_read(struct hid_firmware_t *firm) {
    int i, ret = 0;

    for (i = 0; i < HID_FIRMWARE_USAGES_MAX; ++i) {
        ret = hid_firmware_hw_request(firm, i);
        if (ret < 0)
            return ret;
    }

    return 0;
}


static int hid_firmware_capture_sample(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv)
{
	struct hid_firmware_t *firm = platform_get_drvdata(priv);
	unsigned long flags;
    firmware_length_t length;
    struct hid_firmware_report_t *report = (struct hid_firmware_report_t *)&raw_data[0];
    
    length = report->length;
    if (length <= 0)
        return 0;

    spin_lock_irqsave(&firm->data_lock, flags);
	switch (report->request)
    {
    case HID_FIRMWARE_WAKEUP_REASON:
        memcpy((uint8_t *)&firm->firmware.wakeup_reason, (uint8_t *)&report->data[0], length);
        break;    
    case HID_FIRMWARE_BOOTUP_REASON:
        memcpy((uint8_t *)&firm->firmware.bootup_reason, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_RESET_REASON:
        memcpy((uint8_t *)&firm->firmware.reset_reason, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_NAME:
        memcpy((uint8_t *)&firm->firmware.firmware_name, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_FILE_VERSION:
        memcpy((uint8_t *)&firm->firmware.firmware_version, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_PRODUCT_VERSION:
        memcpy((uint8_t *)&firm->firmware.product_version, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_APP_ID:
        memcpy((uint8_t *)&firm->firmware.application_id, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_SIGNATURE:
        memcpy((uint8_t *)&firm->firmware.signature, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_VERSION:
        memcpy((uint8_t *)&firm->firmware.version, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_HW_REVISION:
        memcpy((uint8_t *)&firm->firmware.hardware_revision, (uint8_t *)&report->data[0], length);
        break;
    case HID_FIRMWARE_BOARD_TYPE:
        memcpy((uint8_t *)&firm->firmware.board_type, (uint8_t *)&report->data[0], length);
        break;    
    case HID_FIRMWARE_BOARD_ID:
        memcpy((uint8_t *)&firm->firmware.board_id, (uint8_t *)&report->data[0], length);
        break; 
    case HID_FIRMWARE_UUID:
        memcpy((uint8_t *)&firm->firmware.uuid[0], (uint8_t *)&report->data[0], length);
        break;    
    }
    spin_unlock_irqrestore(&firm->data_lock, flags);
    return 0;
}

static int hid_firmware_proc_event(struct hid_subdevice *hsdev, u32 usage_id,
			 void *priv)
{
	struct hid_firmware_t *firm = platform_get_drvdata(priv);

    if (firm->async && !completion_done(&firm->completion))
		complete_all(&firm->completion);
	
    return 0;
}
#ifdef CONFIG_PM
static int hid_firmware_suspend(struct hid_subdevice *hsdev, void *priv)
{
	return 0;
}

static int hid_firmware_resume(struct hid_subdevice *hsdev, void *priv)
{
    struct hid_firmware_t *firm = platform_get_drvdata(priv);

    hid_firmware_hw_read(firm);
	return 0;
}
#endif

struct hid_firmware_attr_t {
    struct attribute attr;
    int request_id;
};

static ssize_t hid_firmware_show(struct hid_firmware_t *firm, struct hid_firmware_attr_t *attr, char *buf)
{
    ssize_t size = 0;
    struct hid_firmware_version_t *version;
    int ret;


    if (firm->async && attr->request_id < HID_FIRMWARE_NAME)
        reinit_completion(&firm->completion);
	
    switch (attr->request_id)
    {
    case HID_FIRMWARE_WAKEUP_REASON:
        ret = hid_firmware_hw_request(firm, HID_FIRMWARE_WAKEUP_REASON);
        if (ret < 0)
            return ret;

        size = snprintf(buf, 32, "0x%08x\n", firm->firmware.wakeup_reason);
        break;    
    case HID_FIRMWARE_BOOTUP_REASON:
        ret = hid_firmware_hw_request(firm, HID_FIRMWARE_BOOTUP_REASON);
        if (ret < 0)
            return ret;

        size = snprintf(buf, 32, "0x%08x\n", firm->firmware.bootup_reason);
        break;
    case HID_FIRMWARE_RESET_REASON:
        ret = hid_firmware_hw_request(firm, HID_FIRMWARE_RESET_REASON);
        if (ret < 0)
            return ret;

        size = snprintf(buf, 32, "0x%08x\n", firm->firmware.reset_reason);
        break;
    case HID_FIRMWARE_NAME:
        size = snprintf(buf, 32, "%s\n", firm->firmware.firmware_name);
        break;
    case HID_FIRMWARE_FILE_VERSION:
        version = &firm->firmware.firmware_version;
        size = snprintf(buf, 32, "%d.%d.%d.%d\n", version->major, version->minor, version->build_hot_fix, version->rev_reserved);
        break;
    case HID_FIRMWARE_PRODUCT_VERSION:
        version = &firm->firmware.product_version;
        size = snprintf(buf, 32, "%d.%d.%d.%d\n", version->major, version->minor, version->build_hot_fix, version->rev_reserved);
        break;
    case HID_FIRMWARE_APP_ID:
        size = snprintf(buf, 32, "%d\n", firm->firmware.application_id);
        break;
    case HID_FIRMWARE_SIGNATURE:
        size = snprintf(buf, 32, "0x%08x\n", firm->firmware.signature);
        break;
    case HID_FIRMWARE_VERSION:
        size = snprintf(buf, 32, "%d\n", firm->firmware.version);
        break;
    case HID_FIRMWARE_HW_REVISION:
        size = snprintf(buf, 32, "%c\n", firm->firmware.hardware_revision);
        break;
    case HID_FIRMWARE_BOARD_TYPE:
        size = snprintf(buf, 32, "0x%08x\n", firm->firmware.board_type);
        break;
    case HID_FIRMWARE_BOARD_ID:
        size = snprintf(buf, 32, "%d\n", firm->firmware.board_id);
        break;    
    case HID_FIRMWARE_UUID:
        size = snprintf(buf, 128, "%08x-%08x-%08x-%08x\n", firm->firmware.uuid[0], 
            firm->firmware.uuid[1], firm->firmware.uuid[2], firm->firmware.uuid[3]);
        break;
    }

	return size;
}

static ssize_t hid_firmware_attr_show(struct kobject *kobj, struct attribute *attr,
		char *buf)
{
    struct hid_firmware_t *firm;
	struct hid_firmware_attr_t *firm_attr;

	firm = container_of(kobj, struct hid_firmware_t, kobj);
	firm_attr = container_of(attr, struct hid_firmware_attr_t, attr);

	if (!firm_attr)
		return -ENOENT;

	return hid_firmware_show(firm, firm_attr, buf);
}

#define HID_FIRMWARE_ATTR(_name, id) \
    static struct hid_firmware_attr_t hid_firmware_attr_##_name = { \
        .attr = {.name = __stringify(_name), .mode = VERIFY_OCTAL_PERMISSIONS(S_IRUGO) }, \
        .request_id = id, \
    }

HID_FIRMWARE_ATTR(wakeup_reason, HID_FIRMWARE_WAKEUP_REASON);
HID_FIRMWARE_ATTR(bootup_reason, HID_FIRMWARE_BOOTUP_REASON);
HID_FIRMWARE_ATTR(reset_reason, HID_FIRMWARE_RESET_REASON);
HID_FIRMWARE_ATTR(firmware_name, HID_FIRMWARE_NAME);
HID_FIRMWARE_ATTR(firmware_version, HID_FIRMWARE_FILE_VERSION);
HID_FIRMWARE_ATTR(product_version, HID_FIRMWARE_PRODUCT_VERSION);
HID_FIRMWARE_ATTR(application_id, HID_FIRMWARE_APP_ID);
HID_FIRMWARE_ATTR(signature, HID_FIRMWARE_SIGNATURE);
HID_FIRMWARE_ATTR(version, HID_FIRMWARE_VERSION);
HID_FIRMWARE_ATTR(hardware_revision, HID_FIRMWARE_HW_REVISION);
HID_FIRMWARE_ATTR(board_type, HID_FIRMWARE_BOARD_TYPE);
HID_FIRMWARE_ATTR(board_id, HID_FIRMWARE_BOARD_ID);
HID_FIRMWARE_ATTR(uuid, HID_FIRMWARE_UUID);

static const struct attribute * const hid_firmware_sysfs_attrs[] = {
	&hid_firmware_attr_wakeup_reason.attr,
	&hid_firmware_attr_bootup_reason.attr,
    &hid_firmware_attr_reset_reason.attr,
    &hid_firmware_attr_firmware_name.attr,
    &hid_firmware_attr_firmware_version.attr,
    &hid_firmware_attr_product_version.attr,
    &hid_firmware_attr_application_id.attr,
    &hid_firmware_attr_signature.attr,
    &hid_firmware_attr_version.attr,
    &hid_firmware_attr_hardware_revision.attr,
    &hid_firmware_attr_board_type.attr,
    &hid_firmware_attr_board_id.attr,
    &hid_firmware_attr_uuid.attr,
    NULL
};

static const struct sysfs_ops hid_firmware_sysfs_ops = {
	.show = hid_firmware_attr_show,
};

static struct kobj_type hid_firmware_ktype = {
	.sysfs_ops = &hid_firmware_sysfs_ops,
};

static int hid_firmware_add_attributes(struct hid_firmware_t *firm) {
    int ret;

    switch(firm->firmware.board_type)
    {
        case BOARD_TYPE_GENERIC:
            snprintf(firm->name, 32, "hid-generic-%d", firm->minor);
            break;
	    case BOARD_TYPE_SOC:
            snprintf(firm->name, 32, "hid-soc-%d", firm->minor);
            break;
	    case BOARD_TYPE_CARRIER:
            snprintf(firm->name, 32, "hid-carrier-%d", firm->minor);
            break;
	    case BOARD_TYPE_BACKPLANE:
            snprintf(firm->name, 32, "hid-backplane-%d", firm->minor);
            break;
	    case BOARD_TYPE_EXTENTION:
            snprintf(firm->name, 32, "hid-extention-%d", firm->minor);
            break;
         case BOARD_TYPE_EVAL_KIT:
            snprintf(firm->name, 32, "hid-evalkit-%d", firm->minor);
            break;
    }

    kobject_init(&firm->kobj, &hid_firmware_ktype);
    if (kobject_add(&firm->kobj, firmware_kobj, firm->name)) {
		ret = -ENXIO;
        goto fail_add;
    }

    ret = sysfs_create_files(&firm->kobj, hid_firmware_sysfs_attrs);
    if (ret) {
        goto fail_add;
    }

    return 0;

fail_add:
    kobject_put(&firm->kobj);
    return ret;
}

static int hid_firmware_platform_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_firmware_t *firm;
	int ret;

	firm = devm_kzalloc(&pdev->dev, sizeof(*firm), GFP_KERNEL);
	if (!firm)
		return -ENOMEM;

	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

    firm->hsdev = hsdev;
    firm->pdev = pdev;
	firm->minor = hsdev->id;
    firm->async = (hsdev->quirks & HID_COMPOSITE_QUIRK_NO_IRQ_EVENTS) == 0;
    spin_lock_init(&firm->data_lock);
    init_completion(&firm->completion);
	platform_set_drvdata(pdev, firm);
    
	ret = hid_firmware_parse_report(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes!\n");
		goto io_stop_clean;
	}

    if (firm->async) {
        firm->callbacks.capture_sample = hid_firmware_capture_sample;
        firm->callbacks.send_event = hid_firmware_proc_event;
    }
#ifdef CONFIG_PM
	firm->callbacks.suspend = hid_firmware_suspend;
	firm->callbacks.resume = hid_firmware_resume;
#endif
	firm->callbacks.pdev = pdev;
	hid_composite_register_callback(hsdev,
		HID_USAGE_FIRMWARE, &firm->callbacks);	
	
    ret = hid_firmware_hw_read(firm);
    if (ret)
        goto callbacks_clean;

    msleep(HZ);

    ret = hid_firmware_add_attributes(firm);
    if (ret)
        goto callbacks_clean;

    hid_info(firm->hsdev->hdev, "hid firm was successfully probed!\n");
    	
	return 0;

callbacks_clean:
	hid_composite_remove_callback(hsdev, HID_USAGE_FIRMWARE);
io_stop_clean:
    hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return ret;

}

static int hid_firmware_platform_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
    struct hid_firmware_t *firm = platform_get_drvdata(pdev);

    sysfs_remove_files(&firm->kobj, hid_firmware_sysfs_attrs);
    kobject_put(&firm->kobj);

	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	hid_composite_remove_callback(hsdev, HID_USAGE_FIRMWARE);

    return 0;
}


static const struct platform_device_id hid_firmware_ids[] = {
	{
		/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-990000",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_firmware_ids);

static struct platform_driver hid_firmware_platform_driver = {
	.id_table = hid_firmware_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= hid_firmware_platform_probe,
	.remove		= hid_firmware_platform_remove,
};
module_platform_driver(hid_firmware_platform_driver);

MODULE_DESCRIPTION("HID Firmware data");
MODULE_AUTHOR("Anis CHALI <anis.chali@exfo.com>");
MODULE_LICENSE("GPL");