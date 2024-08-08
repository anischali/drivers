#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include "hid_composite.h"
#include "hid-i2c.h"

#define HID_I2C_PACKET_SIZE 60
#define HID_I2C_DATA_SIZE 32

enum _i2c_requests {
    HID_I2C_BUS_SETTINGS_REQUEST,
    HID_I2C_TX_REPORT_REQUEST,
    HID_I2C_RX_REPORT_REQUEST,
};


enum _i2c_direction_t {
    I2C_WRITE = 0,
    I2C_READ = 1
};

enum _i2c_flags_t {
    I2C_FLAGS_DEFAULT = 0x0,
    I2C_FLAGS_NO_START = 0x1,
    I2C_FLAGS_REPEATED_START = 0x2,
    I2C_FLAGS_NO_STOP = 0x4,
};


typedef uint8_t i2c_xfer_dir_t;
typedef uint8_t i2c_xfer_flags_t;
typedef uint8_t i2c_xfer_slave_addr_t;
typedef uint8_t i2c_length_t;
typedef uint32_t i2c_xfer_sub_addr_t;
typedef uint32_t i2c_xfer_status_t;
typedef uint8_t i2c_request_type_t;


struct __attribute__((packed)) usbhid_i2c_settings_report_t {
    i2c_request_type_t request;
    uint8_t reset;
    uint8_t _reserved[58];
};

struct __attribute__((packed)) hid_i2c_rx_report_t {
    i2c_request_type_t request;
    i2c_xfer_status_t status;
    i2c_length_t length;
    uint8_t _reserved[22];
    uint8_t data[HID_I2C_DATA_SIZE];
};
struct __attribute__((packed)) hid_i2c_tx_report_t
{
    i2c_request_type_t request;
    i2c_xfer_flags_t flags;
    i2c_xfer_slave_addr_t addr;
    i2c_xfer_dir_t dir; 
    i2c_xfer_sub_addr_t subaddr;
    i2c_length_t subaddr_size;
    i2c_length_t length;
    uint8_t _reserved[18];
    uint8_t data[HID_I2C_DATA_SIZE];
};

enum hid_i2c_enum {
    HID_I2C_SETTING_REPORT_IDX = 0,
    HID_I2C_TX_REPORT_IDX,
    HID_I2C_RX_REPORT_IDX,
	HID_I2C_USAGES_MAX,
};

static uint32_t usage_addresses[HID_I2C_USAGES_MAX] = {
    [HID_I2C_SETTING_REPORT_IDX] = HID_USAGE_I2C_SETTING_REPORT,
    [HID_I2C_TX_REPORT_IDX] = HID_USAGE_I2C_TX_REPORT,
    [HID_I2C_RX_REPORT_IDX] = HID_USAGE_I2C_RX_REPORT,  
};


static uint32_t report_types[HID_I2C_USAGES_MAX] = {
    [HID_I2C_SETTING_REPORT_IDX] = HID_FEATURE_REPORT,
    [HID_I2C_TX_REPORT_IDX] = HID_OUTPUT_REPORT,
    [HID_I2C_RX_REPORT_IDX] = HID_INPUT_REPORT,
};


struct hid_i2c_t {
    struct hid_subdevice *hsdev;
    struct hid_composite_callbacks callbacks;
	struct completion completion;
	spinlock_t data_lock;
    struct i2c_adapter adapter;
    uint8_t settings_buffer[HID_I2C_PACKET_SIZE];
    uint8_t rx_buffer[HID_I2C_PACKET_SIZE];
    uint8_t tx_buffer[HID_I2C_PACKET_SIZE];
	struct hid_attribute_info info[HID_I2C_USAGES_MAX];
    int minor;
};


static inline size_t SUBADDR_SIZE(i2c_xfer_sub_addr_t address)
{
    return address <= 0xFF ? sizeof(uint8_t) : address <= 0xFFFF ? sizeof(uint16_t) : sizeof(uint32_t);
}

static int hid_i2c_parse_report(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_i2c_t *adapt = platform_get_drvdata(pdev);
	int i, ret;

	for (i = 0; i < HID_I2C_USAGES_MAX; ++i)
	{
		ret = hid_composite_get_attribute_info(hsdev,
					report_types[i],
					HID_USAGE_I2C,
					usage_addresses[i],
					&adapt->info[i]);
		if (ret)
			continue;
	}

	return 0;
}


static int hid_i2c_hw_transaction(struct hid_i2c_t *adapt, i2c_xfer_slave_addr_t addr, 
        i2c_xfer_flags_t flags, i2c_xfer_dir_t dir, uint8_t *buf,  i2c_length_t length)
{
    struct hid_i2c_rx_report_t *rx;
    struct hid_i2c_tx_report_t *tx;
    int ret;

    memset(adapt->tx_buffer, 0x0, HID_I2C_PACKET_SIZE);
    memset(adapt->rx_buffer, 0x0, HID_I2C_PACKET_SIZE);

    tx = (struct hid_i2c_tx_report_t *)adapt->tx_buffer;
    tx->request = HID_I2C_TX_REPORT_REQUEST;
    tx->flags = flags;
    tx->dir = dir;
    tx->addr = addr;
    tx->subaddr = 0;
    tx->subaddr_size = 0;
    tx->length = length;

    if (tx->length > 0)
        memcpy(&tx->data[0], &buf[0], length);

    ret = hid_composite_output_attr_set_raw_value(adapt->hsdev,  
        adapt->info[HID_I2C_TX_REPORT_IDX].report_id,
		(uint8_t *)&adapt->tx_buffer[0], HID_I2C_PACKET_SIZE);
	if (ret)
		return ret;
    
    ret = hid_composite_get_report(adapt->hsdev, report_types[HID_I2C_RX_REPORT_IDX],
        HID_USAGE_I2C,
        usage_addresses[HID_I2C_RX_REPORT_IDX], 
        adapt->info[HID_I2C_RX_REPORT_IDX].report_id, 
        HID_COMPOSITE_SYNC, (uint8_t *)&adapt->rx_buffer[0], HID_I2C_PACKET_SIZE, 30);
    if(ret)
        return -ETIMEDOUT;

    rx = (struct hid_i2c_rx_report_t *)adapt->rx_buffer;
    if (rx->status != 0)
        return rx->status;

    if (rx->length > 0 && dir == I2C_READ)
        memcpy(buf, rx->data, rx->length);

    return 0;
}


static int hid_i2c_capture_sample(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv)
{
	struct hid_i2c_t *adapt = platform_get_drvdata(priv);
	unsigned long flags;
    int length;
    i2c_request_type_t type = raw_data[0];
    struct hid_i2c_rx_report_t *rx;

	spin_lock_irqsave(&adapt->data_lock, flags);
	switch (type)
    {
    case HID_I2C_BUS_SETTINGS_REQUEST:
        length = _min(_max(0, raw_len), HID_I2C_PACKET_SIZE);
        memcpy(&adapt->settings_buffer[0], raw_data, length);
        break;    
    case HID_I2C_RX_REPORT_REQUEST:
        length = _min(_max(0, raw_len), HID_I2C_PACKET_SIZE);
        rx = (struct hid_i2c_rx_report_t *)&adapt->rx_buffer[0];
        memcpy(&adapt->rx_buffer[0], raw_data, length);
        break;
    
    default:
        break;
    }
    spin_unlock_irqrestore(&adapt->data_lock, flags);
	return 0;
}


static int hid_i2c_proc_event(struct hid_subdevice *hsdev, u32 usage_id,
			 void *priv)
{
    return 0;
}

#ifdef CONFIG_PM
static int hid_i2c_suspend(struct hid_subdevice *hsdev, void *priv)
{
	return 0;
}

static int hid_i2c_resume(struct hid_subdevice *hsdev, void *priv)
{
	return 0;
}
#endif



/*
 * Initialize the transfer information and start the I2C bus transfer.
 * We only configure the transfer and do some pre/post works here, and
 * wait for the transfer done. The major transfer process is performed
 * in the IRQ handler.
 */
static int hid_i2c_master_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
				int num)
{
    struct hid_i2c_t *adapt = i2c_get_adapdata(adapter);
    int ret, i, remaining, offset = 0, length;
    i2c_xfer_flags_t flags = I2C_FLAGS_DEFAULT;
    i2c_xfer_dir_t dir;
    uint8_t *buf;

    if (!adapt)
        return -ENODEV;

    for (i = 0; i < num; ++i)
    {
        offset = 0;
        flags = I2C_FLAGS_DEFAULT;
        remaining = msgs[i].len;
        do {
            length = _min(_max(0, remaining), HID_I2C_DATA_SIZE);
            remaining = _max(remaining - length, 0);
            
            if (msgs[i].flags & I2C_M_NOSTART)
                flags |= I2C_FLAGS_NO_START;
        
            if (remaining > 0)
                flags |= I2C_FLAGS_NO_STOP;

            dir = ((msgs[i].flags & I2C_M_RD) != 0) ? I2C_READ : I2C_WRITE;
        
            buf = &msgs[i].buf[offset];
            ret = hid_i2c_hw_transaction(adapt, msgs[i].addr, flags, dir, buf, length);
            if (ret)
                return ret;
            //printk("ret: %d i: %d dir: %d flags: %d length: %d remain: %d\n", ret, i, dir, flags, length, remaining);
            offset += length;

        } while (remaining > 0);
    }
    return num;
}

static u32 hid_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm hid_i2c_algo = {
	.master_xfer	= hid_i2c_master_xfer,
	.functionality	= hid_i2c_functionality,
};


static int hid_i2c_platform_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_i2c_t *adapt;
	struct i2c_adapter *adapter;
	int ret;

	adapt = devm_kzalloc(&pdev->dev, sizeof(*adapt), GFP_KERNEL);
	if (!adapt)
		return -ENOMEM;

	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

    adapt->hsdev = hsdev;
	adapt->minor = ((struct hidraw *) hsdev->hdev->hidraw)->minor;
    spin_lock_init(&adapt->data_lock);
    init_completion(&adapt->completion);
	platform_set_drvdata(pdev, adapt);

	ret = hid_i2c_parse_report(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes!\n");
		goto io_stop_clean;
	}

    adapt->callbacks.capture_sample = hid_i2c_capture_sample;
    adapt->callbacks.send_event = hid_i2c_proc_event;
#ifdef CONFIG_PM
	adapt->callbacks.suspend = hid_i2c_suspend;
	adapt->callbacks.resume = hid_i2c_resume;
#endif
	adapt->callbacks.pdev = pdev;
	hid_composite_register_callback(hsdev,
		HID_USAGE_I2C, &adapt->callbacks);	

	adapter = &adapt->adapter;
 	adapter->owner = THIS_MODULE;
	adapter->algo = &hid_i2c_algo;
	adapter->dev.parent = &pdev->dev;
    adapter->timeout = HZ;
    adapter->retries = 5;
    snprintf(adapter->name, sizeof(adapter->name),
		 "HID I2C Controller %s", dev_name(&pdev->dev));
    i2c_set_adapdata(adapter, adapt);

	ret = devm_i2c_add_adapter(&pdev->dev, adapter);
    if (ret) {
        dev_err(&pdev->dev, "failed to add i2c adapter with %d\n", ret);
        goto callbacks_clean;
    }
	
    hid_info(adapt->hsdev->hdev, "hid adapt was successfully probed!\n");
	
	return 0;

callbacks_clean:
	hid_composite_remove_callback(hsdev, HID_USAGE_I2C);
io_stop_clean:
    hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return ret;

}

static int hid_i2c_platform_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
    struct hid_i2c_t *adapt = platform_get_drvdata(pdev);

    complete_all(&adapt->completion);

	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	hid_composite_remove_callback(hsdev, HID_USAGE_I2C);
    return 0;
}


static const struct platform_device_id hid_i2c_ids[] = {
	{
		/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-980000",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_i2c_ids);

static struct platform_driver hid_i2c_platform_driver = {
	.id_table = hid_i2c_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
	},
	.probe		= hid_i2c_platform_probe,
	.remove		= hid_i2c_platform_remove,
};
module_platform_driver(hid_i2c_platform_driver);

MODULE_DESCRIPTION("HID USB I2C bridge");
MODULE_AUTHOR("Anis CHALI <anis.chali@exfo.com>");
MODULE_LICENSE("GPL");