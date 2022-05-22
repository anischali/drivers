#include <linux/version.h>
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(ver, rel, seq) ((ver << 16) | (rel << 8) | (seq))
#endif

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/firewire.h>
#include <uapi/linux/usb/ch9.h>
#include <linux/delay.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
#include <linux/signal.h>
#else
#include <linux/sched/signal.h>
#endif

#include "fx2lafw.h"

#define DRIVER_DESC "FX2LAFW USB Logic analyzer driver"
#define DRIVER_AUTHOR "<anis.chali1@outlook.com>"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);

#define FX2LAFW_VENDOR_ID 0x08A9
#define FX2LAFW_PRODUCT_ID 0x0014

#define FX2LAFW_MANUF "sigrok"
#define FX2LAFW_PRODUCT "fx2lafw"

#define DRIVER_NAME "fx2lafw"
#define FW_NAME "fx2lafw/fx2lafw-cwav-usbeeax.fw"

#define FX2LAFW_BUF_SIZE 1024UL
#define FW_CHUNKSIZE 4096UL


struct fw_version {
	u8 major;
	u8 minor;
};


struct cmd_acq {
	uint8_t flags;
	uint8_t sample_delay_h;
	uint8_t sample_delay_l;
};



struct fx2lafw_private
{
	bool sample_wide;
	bool enable_analog;
	spinlock_t lock;
	struct usb_device *usbdev;
	struct fw_version firmware_version;
	uint64_t samplerate;
	uint8_t logic_channels[FX2LAFW_LOGIC_CHANNELS_SIZE];
	uint8_t analog_channels[FX2LAFW_ANALOG_CHANNELS_SIZE];
};




/**
 * @brief 
 * 
 * @param ldev 
 * @param clear 
 * @return int 
 */
static int fx2lafw_reset(struct fx2lafw_private *ldev, bool clear)
{
	int retval = -EINVAL;
	struct usb_device *dev = ldev->usbdev;
	__u8 value[1];

	value[0] = clear ? 1 : 0;
	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
							 0xa0, USB_TYPE_VENDOR | USB_DIR_OUT, 0xe600, 0x0,
							 (void *)value, 1, 100);
	if (retval <= 0)
	{
		dev_err(&dev->dev, "Failed to reset the device (errno: %d)\n", retval);
		return retval;
	}

	dev_info(&dev->dev, "Device reseted succesfully\n");
	return 0;
}


/**
 * @brief 
 * 
 * @param ldev 
 * @return true 
 * @return false 
 */
static bool fx2lafw_has_fw(struct fx2lafw_private *ldev)
{
	struct usb_device *dev = ldev->usbdev;

	if (!dev->product || !dev->manufacturer)
		return false;

	return !strncmp(FX2LAFW_PRODUCT,
					dev->product,
					sizeof(FX2LAFW_PRODUCT)) && 
			!strncmp(FX2LAFW_MANUF,
					dev->manufacturer,
					sizeof(FX2LAFW_MANUF));
		   
}


/**
 * @brief 
 * 
 * @param ldev 
 * @return int 
 */
static int fx2lafw_get_firmware_version(struct fx2lafw_private *ldev)
{
	int retval = -EINVAL;
	struct usb_device *dev = ldev->usbdev;

	retval = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0x0),
							 CMD_GET_FW_VERSION, USB_TYPE_VENDOR | USB_DIR_IN, 0x0, 0x0,
							 (void *)&ldev->firmware_version, sizeof(struct fw_version), 100);
	if (retval <= 0)
	{
		dev_err(&dev->dev, "Failed to reset the device (errno: %d)\n", retval);
		return retval;
	}

	return 0;	
}


/**
 * @brief 
 * 
 * @param ldev 
 * @return int 
 */
static int fx2lafw_upload_firmware(struct fx2lafw_private *ldev)
{
	int retval = -EINVAL;
	struct usb_device *dev = ldev->usbdev;
	const struct firmware *fw;
	size_t offset = 0;
	size_t chuncksize = 0;
	char *fw_data;

	retval = request_firmware(&fw, FW_NAME, &dev->dev);
	if (retval)
	{
		dev_err(&dev->dev, "Failed to load firmware (errno: %d)\n", retval);
		goto error;
	}

	while (offset < fw->size)
	{
		chuncksize = min(fw->size - offset, FW_CHUNKSIZE);
		fw_data = kmalloc(chuncksize, GFP_KERNEL);
		if (!fw_data)
		{
			dev_err(&dev->dev, "Failed to allocate memory for fw data\n");
			retval = -ENOMEM;
			goto error;
		}

		memset(fw_data, 0xff, chuncksize);
		memcpy(fw_data, fw->data + offset, chuncksize);
		retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0x0),
								 0xa0, USB_TYPE_VENDOR | USB_DIR_OUT, offset, 0x0,
								 (void *)fw_data, chuncksize, 1000);
		if (retval <= 0)
		{
			dev_err(&dev->dev, "Failed to upload firmware to the device (errno: %d)\n", retval);
			goto error;
		}

		kfree(fw_data);
		fw_data = NULL;
		offset += chuncksize;
	}

	dev_info(&dev->dev, "Firmware loaded succesfully (size: %ld)\n", fw->size);
	release_firmware(fw);

	return 0;
error:
	if (fw != NULL)
		release_firmware(fw);
	if (fw_data)
		kfree(fw_data);

	return retval;
}




static int fx2lafw_start_aquisition(struct fx2lafw_private *dev) {
	struct usb_device *usbdev;
	int retval = 0, delay = 0;
	unsigned long flags;
	uint64_t samplerate;
	struct cmd_acq cmd;
	bool sample_wide, enable_analog;

	if (!dev)
		return -EINVAL;

	spin_lock_irqsave(&dev->lock, flags);
	
	samplerate = dev->samplerate;
	usbdev = dev->usbdev;
	sample_wide = dev->sample_wide;
	enable_analog = dev->enable_analog;

	spin_unlock_irqrestore(&dev->lock, flags);

	if (sample_wide && samplerate > MAX_16BIT_SAMPLE_RATE) {
		dev_err(&usbdev->dev, "Invalid samplerate value %ld\n", samplerate);
		return -EINVAL;
	}

	delay = 0;
	cmd.flags = 0;
	cmd.sample_delay_h = cmd.sample_delay_l = 0;	

	if ((SR_MHZ(48) % samplerate) == 0) 
	{
		cmd.flags = CMD_START_FLAGS_CLK_48MHZ;
		delay = SR_MHZ(48) / samplerate - 1;
		if (delay > MAX_SAMPLE_DELAY)
			delay = 0;
	}

	if (delay == 0 && (SR_MHZ(30) % samplerate) == 0) 
	{
		cmd.flags = CMD_START_FLAGS_CLK_30MHZ;
		delay = SR_MHZ(30) / samplerate - 1;
	}

	if (delay < 0 || delay > MAX_SAMPLE_DELAY)
	{
		dev_err(&usbdev->dev, "Invalid delay value %d\n", delay);
		return -EINVAL;
	}

	cmd.sample_delay_h = (delay >> 8) & 0xff;
	cmd.sample_delay_l = delay & 0xff;

	/* Select the sampling width. */
	cmd.flags |= sample_wide ? CMD_START_FLAGS_SAMPLE_16BIT :
		CMD_START_FLAGS_SAMPLE_8BIT;
	
	/* Enable CTL2 clock. */
	cmd.flags |= enable_analog > 0 ? CMD_START_FLAGS_CLK_CTL2 : 0;

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0x0),
		CMD_START, USB_TYPE_VENDOR | USB_DIR_OUT, 0x0, 0x0,
		(void *)&cmd, sizeof(struct cmd_acq), 1000);
}



/**
 * @brief 
 * 
 * @param interface 
 * @param cmd 
 * @param data 
 * @return int 
 */
int fx2lafw_ioctl(struct usb_interface *interface, unsigned int cmd,
			void *data) {
	struct fx2lafw_private *dev;
	struct fw_version version;
	unsigned long flags, len;
	uint64_t samplerate;
	int enable;

	dev = usb_get_intfdata(interface);
	if (spin_is_locked(&dev->lock))
		return -ERESTART;
	
	switch (cmd)
	{
	case IOGFWVERSION:
		spin_lock_irqsave(&dev->lock, flags);
		version.major = dev->firmware_version.major;
		version.minor = dev->firmware_version.minor;
		spin_unlock_irqrestore(&dev->lock, flags);
		len = copy_to_user(data, &version, sizeof(struct fw_version));
		break;
	
	case IOGSAMPLERATE:
		spin_lock_irqsave(&dev->lock, flags);
		samplerate = dev->samplerate;
		spin_unlock_irqrestore(&dev->lock, flags);
		len = copy_to_user(data, &samplerate, sizeof(uint64_t));
		break;

	case IOSSAMPLERATE:
		len = copy_from_user(&samplerate, data, sizeof(uint64_t));
		spin_lock_irqsave(&dev->lock, flags);
		dev->samplerate = samplerate;
		spin_unlock_irqrestore(&dev->lock, flags);
		break;
	
	case IODISABLEANALOG:
	case IOENABLEANALOG:
		len = copy_from_user(&enable, data, sizeof(int));
		spin_lock_irqsave(&dev->lock, flags);
		dev->enable_analog = enable ? true : false;
		spin_unlock_irqrestore(&dev->lock, flags);
		break;

	case IOGANALOG:
		spin_lock_irqsave(&dev->lock, flags);
		enable = dev->enable_analog ? 1 : 0;
		spin_unlock_irqrestore(&dev->lock, flags);
		len = copy_to_user(data, &enable, sizeof(int));
		break;

	default:
		return -EINVAL;
		break;
	}
	return len;
}

/**
 * @brief 
 * 
 * @param interface 
 * @param id 
 * @return int 
 */
static int fx2lafw_probe(struct usb_serial *serial,
						  const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct fx2lafw_private *dev = NULL;
	int retval = 0;
	bool has_firmware = false;

	dev = kmalloc(sizeof(struct fx2lafw_private), GFP_KERNEL);
	if (dev == NULL)
	{
		dev_err(&interface->dev, "Out of memory\n");
		return -ENOMEM;
	}

	memset(dev, 0x00, sizeof(*dev));
	dev->usbdev = usb_get_dev(usbdev);
	usb_set_interface(dev->usbdev, 0, 1);
	usb_set_intfdata(interface, dev);
	dev->samplerate = 1.0;
	dev->sample_wide = false;

	has_firmware = fx2lafw_has_fw(dev);
	if (!has_firmware)
	{
		usb_driver_set_configuration(dev->usbdev, 1);
		fx2lafw_reset(dev, true);
		retval = fx2lafw_upload_firmware(dev);
		if (retval)
		{
			dev_err(&interface->dev, "USB FX2LAFW probe failed\n");
			goto error;
		}
		mdelay(400);
		fx2lafw_reset(dev, false);
		mdelay(100);
	} else {
		retval = fx2lafw_get_firmware_version(dev);
		if (retval) 
		{
			dev_err(&interface->dev, "USB FX2LAFW failed to get firmware version\n");
			goto error;
		}
		dev_info(&interface->dev, "firmware major: %d firmware minor: %d\n", 
			dev->firmware_version.major, dev->firmware_version.minor);
	}
	dev_info(&interface->dev, "USB FX2LAFW now connected %d\n", retval);

	spin_lock_init(&dev->lock);

	return 0;
error:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->usbdev);
	kfree(dev);
	return retval;
}





static void fx2lafw_disconnect(struct usb_interface *interface)
{
	struct fx2lafw_private *dev;

	dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->usbdev);

	kfree(dev);
	dev_info(&interface->dev, "USB FX2LAFW now disconnected\n");
}

static const struct usb_device_id id_table[] = {
	{USB_DEVICE(FX2LAFW_VENDOR_ID, FX2LAFW_PRODUCT_ID)},
	{} //End
};
MODULE_DEVICE_TABLE(usb, id_table);

/*static struct usb_driver_serial fx2lafw = {
	.name = DRIVER_NAME,
	.probe = fx2lafw_probe,
	.disconnect = fx2lafw_disconnect,
	.unlocked_ioctl = fx2lafw_ioctl,
	.id_table = id_table,
};*/
static struct usb_serial_driver fx2lafw = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRIVER_NAME,
	},
	.id_table          = id_table,
	.num_ports         = 1,
	// .open              = ch341_open,
	// .dtr_rts	   = ch341_dtr_rts,
	// .carrier_raised	   = ch341_carrier_raised,
	// .close             = ch341_close,
	// .set_termios       = ch341_set_termios,
	// .break_ctl         = ch341_break_ctl,
	// .tiocmget          = ch341_tiocmget,
	// .tiocmset          = ch341_tiocmset,
	// .tiocmiwait        = usb_serial_generic_tiocmiwait,
	// .read_int_callback = ch341_read_int_callback,
	.port_probe        = fx2lafw_probe,
	.port_remove       = fx2lafw_disconnect,
	.reset_resume      = ch341_reset_resume,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&fx2lafw, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR("Anis CHALI");
MODULE_LICENSE("GPL");