#include <linux/errno.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include "hid_composite.h"
#include <linux/iio/iio.h>
#include <linux/hid-sensor-ids.h>
#include <linux/usb/role.h>
#include <linux/usb/typec.h>


#define HID_USAGE_TYPEC				        0x860000U
#define HID_USAGE_TYPEC_PORT_TYPE	        0x860001U
#define HID_USAGE_TYPEC_PORT_DATA_TYPE	    0x860002U
#define HID_USAGE_TYPEC_PORT_PLUG_TYPE	    0x860003U
#define HID_USAGE_TYPEC_TYPEC_ROLE	        0x860004U
#define HID_USAGE_TYPEC_DATA_ROLE	        0x860005U
#define HID_USAGE_TYPEC_OPMODE	            0x860006U
#define HID_USAGE_TYPEC_ACCESSORY	        0x860007U
#define HID_USAGE_TYPEC_ORIENTATION	        0x860008U

enum hid_typec_enum {
	CHANNEL_SCAN_INDEX_PORT_TYPE,
	CHANNEL_SCAN_INDEX_PORT_DATA_TYPE,
	CHANNEL_SCAN_INDEX_PLUG_TYPE,
	CHANNEL_SCAN_INDEX_TYPEC_ROLE,
	CHANNEL_SCAN_INDEX_DATA_ROLE,
	CHANNEL_SCAN_INDEX_OPMODE,
	CHANNEL_SCAN_INDEX_ACCESSORY,
	CHANNEL_SCAN_INDEX_ORIENTATION,
	HID_TYPEC_CHANNEL_MAX,
};


static uint32_t usage_addresses[HID_TYPEC_CHANNEL_MAX] = {
	[CHANNEL_SCAN_INDEX_PORT_TYPE] = HID_USAGE_TYPEC_PORT_TYPE,
	[CHANNEL_SCAN_INDEX_PORT_DATA_TYPE] = HID_USAGE_TYPEC_PORT_DATA_TYPE,
	[CHANNEL_SCAN_INDEX_PLUG_TYPE] = HID_USAGE_TYPEC_PORT_PLUG_TYPE,
	[CHANNEL_SCAN_INDEX_TYPEC_ROLE] = HID_USAGE_TYPEC_TYPEC_ROLE,
	[CHANNEL_SCAN_INDEX_DATA_ROLE] = HID_USAGE_TYPEC_DATA_ROLE,
	[CHANNEL_SCAN_INDEX_OPMODE] = HID_USAGE_TYPEC_OPMODE,
	[CHANNEL_SCAN_INDEX_ACCESSORY] = HID_USAGE_TYPEC_ACCESSORY,
	[CHANNEL_SCAN_INDEX_ORIENTATION] = HID_USAGE_TYPEC_ORIENTATION,
};

struct hid_typec_info_t {
	enum typec_port_type port_type;
	enum typec_port_data data_type;
	enum typec_plug_type plug_type;
	enum typec_data_role data_role;
	enum typec_role typec_role;
	enum typec_pwr_opmode opmode;
	enum typec_accessory accessory;
	enum typec_orientation orientation;
};


struct hid_typec_t {
    struct hid_subdevice *hsdev;
    struct hid_composite_callbacks callbacks;
	struct completion data_completion;
	spinlock_t data_lock;

    struct usb_role_switch	*role_sw;
    struct typec_port *port;

	struct hid_typec_info_t last_data;
	struct hid_typec_info_t data_buf;
	struct hid_attribute_info info[HID_TYPEC_CHANNEL_MAX];
	struct work_struct work;
};

static const struct of_device_id usbc_connector_dt_match[] = {
	{ .compatible = "usb-c-connector" },
	{ /*sentinel*/ },
};


static int hid_typec_dr_set(struct typec_port *port, enum typec_data_role role)
{
	struct hid_typec_t *usb = typec_get_drvdata(port);
	enum usb_role role_val;
	int ret = 0;

	if (role == TYPEC_HOST) {
		role_val = USB_ROLE_HOST;
	} else {
		role_val = USB_ROLE_DEVICE;
	}

	usb_role_switch_set_role(usb->role_sw, role_val);

	return ret;
}

static const struct typec_operations hid_typec_ops = {
	.dr_set = hid_typec_dr_set
};


static int hid_typec_set_usb_role(struct hid_typec_t *usb)
{
	enum usb_role role_state;
	role_state = usb->last_data.data_role;
	usb_role_switch_set_role(usb->role_sw, role_state);

	switch (role_state) {
	case USB_ROLE_HOST:
		typec_set_data_role(usb->port, TYPEC_HOST);
		break;
	case USB_ROLE_DEVICE:
		typec_set_data_role(usb->port, TYPEC_DEVICE);
		break;
	default:
		break;
	}

	return 0;
}


static int hid_typec_capture_sample(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv)
{
	struct hid_typec_t *usb = platform_get_drvdata(priv);
	struct hid_typec_info_t *info = &usb->data_buf;
	printk("hid-typec: usage: 0x%08x val: %d\n", usage_id, hid_composite_value(raw_len, raw_data));
    switch (usage_id)
    {
    case HID_USAGE_TYPEC_PORT_TYPE:
        info->port_type = hid_composite_value(raw_len, raw_data);
        break;
	case HID_USAGE_TYPEC_PORT_DATA_TYPE:
		info->data_type = hid_composite_value(raw_len, raw_data);
		break;
	case HID_USAGE_TYPEC_PORT_PLUG_TYPE:
		info->plug_type = hid_composite_value(raw_len, raw_data);
		break;
	case HID_USAGE_TYPEC_TYPEC_ROLE:
		info->typec_role = hid_composite_value(raw_len, raw_data);
		break;
	case HID_USAGE_TYPEC_DATA_ROLE:
		info->data_role = hid_composite_value(raw_len, raw_data);
		break;
	case HID_USAGE_TYPEC_OPMODE:
		info->opmode = hid_composite_value(raw_len, raw_data);
		break;
	case HID_USAGE_TYPEC_ACCESSORY:
		info->accessory = hid_composite_value(raw_len, raw_data);
		break;
	case HID_USAGE_TYPEC_ORIENTATION:
		info->orientation = hid_composite_value(raw_len, raw_data);
		break;
    default:
        break;
    }

	return 0;
}


static int hid_typec_proc_event(struct hid_subdevice *hsdev, u32 usage_id,
			 void *priv)
{
	struct hid_typec_t *usb = platform_get_drvdata(priv);
	unsigned long flags;

	spin_lock_irqsave(&usb->data_lock, flags);
	if (usb->data_buf.data_role != usb->last_data.data_role)
		schedule_work(&usb->work);

	usb->last_data = usb->data_buf;

	spin_unlock_irqrestore(&usb->data_lock, flags);
	complete(&usb->data_completion);
	return 0;
}


static int hid_typec_parse_report(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_typec_t *usb = platform_get_drvdata(pdev);
	int i, ret;

	for (i = 0; i < HID_TYPEC_CHANNEL_MAX; ++i)
	{
		ret = hid_composite_get_attribute_info(hsdev,
					HID_INPUT_REPORT,
					HID_USAGE_TYPEC,
					usage_addresses[i],
					&usb->info[i]);
		if (ret)
			continue;
	}

	return 0;
}

static int hid_typec_hw_read(struct hid_typec_t *usb, uint32_t channel_idx)
{
	int ret;
	reinit_completion(&usb->data_completion);
	/* get a report with all values through requesting one value */
	hid_composite_input_attr_get_raw_value(usb->hsdev,
			HID_USAGE_TYPEC, usage_addresses[channel_idx],
			usb->info[channel_idx].report_id, HID_COMPOSITE_ASYNC, false);
	/* wait for all values (event) */
	ret = wait_for_completion_killable_timeout(
			&usb->data_completion,  msecs_to_jiffies(1000));

	if (ret > 0) //no error
		return 0;
	
	return ret;
}


static void hid_typec_work(struct work_struct *work)
{
	struct hid_typec_t *usb = container_of(work, struct hid_typec_t, work);
	unsigned long flags;

	spin_lock_irqsave(&usb->data_lock, flags);

	hid_typec_set_usb_role(usb);

	spin_unlock_irqrestore(&usb->data_lock, flags);
}

#ifdef CONFIG_PM
static int hid_typec_suspend(struct hid_subdevice *hsdev, void *priv)
{
	return 0;
}

static int hid_typec_resume(struct hid_subdevice *hsdev, void *priv)
{
	struct hid_typec_t *usb = platform_get_drvdata(priv);

	return hid_typec_hw_read(usb, CHANNEL_SCAN_INDEX_DATA_ROLE);
}
#endif


static int hid_typec_platform_probe(struct platform_device *pdev)
{
    struct typec_capability typec_cap = { };
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_typec_t *usb;
    struct fwnode_handle *connector;
    struct device_node *node;
	int ret;

	if (!hsdev)
		return -ENODEV;

	usb = devm_kzalloc(&pdev->dev, sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return -ENOMEM;

	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

    spin_lock_init(&usb->data_lock);
	init_completion(&usb->data_completion);
	INIT_WORK(&usb->work, hid_typec_work);
	usb->hsdev = hsdev;
	platform_set_drvdata(pdev, usb);

	ret = hid_typec_parse_report(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes!\n");
		return ret;
	}

    usb->callbacks.capture_sample = hid_typec_capture_sample;
	usb->callbacks.send_event = hid_typec_proc_event;
#ifdef CONFIG_PM
	usb->callbacks.suspend = hid_typec_suspend;
	usb->callbacks.resume = hid_typec_resume;
#endif
	usb->callbacks.pdev = pdev;
	hid_composite_register_callback(hsdev,
		HID_USAGE_TYPEC, &usb->callbacks);

#if defined(CONFIG_OF)
    node = of_find_matching_node(NULL, usbc_connector_dt_match);
    if (!node) {
        ret = -ENODEV;
        goto ret_err;
    }

    connector = of_fwnode_handle(node);
    if (!connector) {
        hid_err(usb->hsdev->hdev, "Unable to get the usb-c connector\n");
        ret = -ENODEV;
        goto of_node_err;
    }
    
	usb->role_sw = fwnode_usb_role_switch_get(connector);
	of_node_put(node);
#endif

    typec_cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
	typec_cap.driver_data = usb;
	typec_cap.type = TYPEC_PORT_DRP;
	typec_cap.data = TYPEC_PORT_DRD;
	typec_cap.ops = &hid_typec_ops;
	typec_cap.fwnode = connector;
    usb->port = typec_register_port(&pdev->dev, &typec_cap);
	if (IS_ERR(usb->port)) {
		ret = PTR_ERR(usb->port);
		goto fwnode_err;
	}

	ret = hid_typec_hw_read(usb, CHANNEL_SCAN_INDEX_DATA_ROLE);
	if (ret)
		hid_err(usb->hsdev->hdev, "Unable to get the usb role.\n");

	hid_info(usb->hsdev->hdev, "hid typec was successfully probed!\n");
	return 0;

fwnode_err:
    usb_role_switch_put(usb->role_sw);
#ifdef CONFIG_OF
of_node_err:
	of_node_put(node);
ret_err:
#endif
	hid_composite_remove_callback(hsdev, HID_USAGE_TYPEC);
    hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return ret;
}

static int hid_typec_platform_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_typec_t *usb = platform_get_drvdata(pdev);

    complete_all(&usb->data_completion);
	cancel_work_sync(&usb->work);

    typec_unregister_port(usb->port);
    usb_role_switch_put(usb->role_sw);
	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	hid_composite_remove_callback(hsdev, HID_USAGE_TYPEC);
    return 0;
}


static const struct platform_device_id hid_typec_ids[] = {
	{
		/* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-860000",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_typec_ids);

static struct platform_driver hid_typec_platform_driver = {
	.id_table = hid_typec_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= hid_typec_platform_probe,
	.remove		= hid_typec_platform_remove,
};
module_platform_driver(hid_typec_platform_driver);

MODULE_DESCRIPTION("HID USB Typec");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_HID);
