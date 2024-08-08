#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/hid-sensor-ids.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/consumer.h>


#include "hid_composite.h"
#include "hid-gpio.h"


// five usages (direction, bias, polarity, value, label)
enum hid_gpio_enum {
	HID_GPIO_DIRECTION_IDX,
	HID_GPIO_BIAS_IDX,
	HID_GPIO_POLARITY_IDX,
	HID_GPIO_VALUE_IDX,
	HID_GPIO_USAGES_MAX,
};


static uint32_t usage_addresses[HID_GPIO_USAGES_MAX] = {
	[HID_GPIO_DIRECTION_IDX] = HID_USAGE_GPIO_DIRECTION,
	[HID_GPIO_BIAS_IDX] = HID_USAGE_GPIO_BIAS,
	[HID_GPIO_POLARITY_IDX] = HID_USAGE_GPIO_POLARITY,
	[HID_GPIO_VALUE_IDX] = HID_USAGE_GPIO_VALUE,
};

#define PACKET_SIZE(sz) (HID_GPIO_USAGES_MAX * sz)


static uint32_t report_types[HID_GPIO_USAGES_MAX] = {
	[HID_GPIO_DIRECTION_IDX] = HID_INPUT_REPORT,
	[HID_GPIO_BIAS_IDX] = HID_INPUT_REPORT,
	[HID_GPIO_POLARITY_IDX] = HID_INPUT_REPORT,
	[HID_GPIO_VALUE_IDX] = HID_INPUT_REPORT,
};



struct hid_gpio_t {
    struct hid_subdevice *hsdev;
    struct hid_composite_callbacks callbacks;
	spinlock_t data_lock;

	int gpio_num;
    struct gpio_chip gpio_chip;
	char gpio_name[32];

	uint8_t *data_buf;
	uint8_t *last_buf;

	struct hid_attribute_info info[HID_GPIO_USAGES_MAX];
	int minor;
	int base;
};

static inline uint8_t * hid_gpio_get_offset(struct hid_gpio_t *gpio, int index, bool last) {
	uint8_t *ptr; 
	
	if (last)
		ptr = &gpio->last_buf[index * gpio->gpio_num];
	else
		ptr = &gpio->data_buf[index * gpio->gpio_num];

	return ptr;
}


static int hid_gpio_capture_sample(struct hid_subdevice *hsdev,
			u32 usage_id, size_t raw_len, char *raw_data,
			void *priv)
{
	struct hid_gpio_t *gpio = platform_get_drvdata(priv);
	uint8_t *ptr;

	switch (usage_id)
    {
		case HID_USAGE_GPIO_DIRECTION:
			ptr = hid_gpio_get_offset(gpio, HID_GPIO_DIRECTION_IDX, false);
			memcpy(ptr, raw_data, raw_len);
			break;
		case HID_USAGE_GPIO_BIAS:
			ptr = hid_gpio_get_offset(gpio, HID_GPIO_BIAS_IDX, false);
			memcpy(ptr, raw_data, raw_len);
			break;
		case HID_USAGE_GPIO_POLARITY:
			ptr = hid_gpio_get_offset(gpio, HID_GPIO_POLARITY_IDX, false);
			memcpy(ptr, raw_data, raw_len);
			break;
		case HID_USAGE_GPIO_VALUE:
			ptr = hid_gpio_get_offset(gpio, HID_GPIO_VALUE_IDX, false);
			memcpy(ptr, raw_data, raw_len);
			
			break;

        default:
            break;
    }
	
	return 0;
}


static int hid_gpio_proc_event(struct hid_subdevice *hsdev, u32 usage_id,
			 void *priv)
{
	struct hid_gpio_t *gpio = platform_get_drvdata(priv);
	unsigned long flags;
	spin_lock_irqsave(&gpio->data_lock, flags);
	memcpy(gpio->last_buf, gpio->data_buf, PACKET_SIZE(gpio->gpio_num));
	spin_unlock_irqrestore(&gpio->data_lock, flags);
	
	return 0;
}



static int hid_gpio_parse_report(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_gpio_t *gpio = platform_get_drvdata(pdev);
	int i, ret;

	for (i = 0; i < HID_GPIO_USAGES_MAX; ++i)
	{
		ret = hid_composite_get_attribute_info(hsdev,
					report_types[i],
					HID_USAGE_GPIO,
					usage_addresses[i],
					&gpio->info[i]);
		if (ret)
			continue;
	}

	gpio->gpio_num = gpio->info[0].size;

	return 0;
}


static int hid_gpio_hw_read(struct hid_gpio_t *gpio)
{
	int ret;	
	
	ret = hid_composite_get_report(gpio->hsdev, report_types[0],
			HID_USAGE_GPIO, usage_addresses[0],
			gpio->info[0].report_id, HID_COMPOSITE_SYNC, 
			(uint8_t *)gpio->data_buf, PACKET_SIZE(gpio->gpio_num), msecs_to_jiffies(1000));
	if (ret)
		return ret;

	return 0;
}

static int hid_gpio_hw_write(struct hid_gpio_t *gpio)
{
	int ret;

	ret = hid_composite_set_report(gpio->hsdev, report_types[0],
			gpio->info[0].report_id, gpio->info[0].index, 
			(uint8_t *)gpio->last_buf, PACKET_SIZE(gpio->gpio_num));
	if (ret)
		return ret;
	
	return hid_gpio_hw_read(gpio);
}

static int hid_gpio_get_value(struct gpio_chip *chip, unsigned off)
{
	int ret, val;
	struct hid_gpio_t *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	uint8_t *values;

	ret = hid_gpio_hw_read(gpio);
	if (ret)
		return ret;
	
	values = hid_gpio_get_offset(gpio, HID_GPIO_VALUE_IDX, true);

	spin_lock_irqsave(&gpio->data_lock, flags);
	val = !!(values[off]);
	spin_unlock_irqrestore(&gpio->data_lock, flags);

	return val;
}

static void hid_gpio_set_value(struct gpio_chip *chip,
		unsigned off, int value)
{
	int ret, old_val, val = !!value;
	struct hid_gpio_t *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	uint8_t *values;

	values = hid_gpio_get_offset(gpio, HID_GPIO_VALUE_IDX, true);
	spin_lock_irqsave(&gpio->data_lock, flags);
	old_val = !!(values[off]);
	spin_unlock_irqrestore(&gpio->data_lock, flags);
	
	if (old_val == val)
		return;

	spin_lock_irqsave(&gpio->data_lock, flags);
	values[off] = val;
	spin_unlock_irqrestore(&gpio->data_lock, flags);

	ret = hid_gpio_hw_write(gpio);
	if (ret)
	{
		hid_err(gpio->hsdev->hdev, "failed to set gpio %d to %d with errno: %d.\n", off, val, ret);
	}
}


static int hid_gpio_direction_input(struct gpio_chip *chip, unsigned off)
{
	struct hid_gpio_t *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	uint8_t *dirs, dir;
	int ret;

	dirs = hid_gpio_get_offset(gpio, HID_GPIO_DIRECTION_IDX, true);
	spin_lock_irqsave(&gpio->data_lock, flags);
	dir = !!(dirs[off]);
	spin_unlock_irqrestore(&gpio->data_lock, flags);

	if (dir == GPIO_DIRECTION_INPUT)
		return 0;
	
	spin_lock_irqsave(&gpio->data_lock, flags);
	dirs[off] = GPIO_DIRECTION_INPUT;
	spin_unlock_irqrestore(&gpio->data_lock, flags);
	
	ret = hid_gpio_hw_write(gpio);
	if (ret)
	{
		hid_err(gpio->hsdev->hdev, "failed to set direction input for gpio %d with errno: %d.\n", off, ret);
		return ret;
	}

	return 0;
}

static int hid_gpio_direction_output(struct gpio_chip *chip,
		unsigned off, int val)
{
	struct hid_gpio_t *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	uint8_t *dirs, *values, dir;
	int ret;

	dirs = hid_gpio_get_offset(gpio, HID_GPIO_DIRECTION_IDX, true);
	values = hid_gpio_get_offset(gpio, HID_GPIO_VALUE_IDX, true);
	
	spin_lock_irqsave(&gpio->data_lock, flags);
	dir = !!(dirs[off]);
	spin_unlock_irqrestore(&gpio->data_lock, flags);

	if (dir == GPIO_DIRECTION_OUTPUT)
		return 0;
	
	spin_lock_irqsave(&gpio->data_lock, flags);
	dirs[off] = GPIO_DIRECTION_OUTPUT;
	values[off] = !!val;
	spin_unlock_irqrestore(&gpio->data_lock, flags);
	
	ret = hid_gpio_hw_write(gpio);
	if (ret)
	{
		hid_err(gpio->hsdev->hdev, "failed to set direction input for gpio %d with errno: %d.\n", off, ret);
		return ret;
	}

	return 0;
}


/**
 * @brief 
 * 
 * @param chip 
 * @param offset 
 * @return int 0 for output and 1 for input 
 */
static int hid_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	int ret, val;
	struct hid_gpio_t *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	uint8_t *directions;

	ret = hid_gpio_hw_read(gpio);
	if (ret) {
		hid_err(gpio->hsdev->hdev, "failed to get direction with errno (%d)\n", ret);
		return ret;
	}

	directions = hid_gpio_get_offset(gpio, HID_GPIO_DIRECTION_IDX, true);

	spin_lock_irqsave(&gpio->data_lock, flags);
	val = !!(directions[offset]);
	spin_unlock_irqrestore(&gpio->data_lock, flags);

	return val;
}

static inline int hid_gpio_apply_config(struct hid_gpio_t *gpio, int off, int8_t cfg)
{
	unsigned long flags;
	uint8_t *bias, b;
	int ret;

	bias = hid_gpio_get_offset(gpio, HID_GPIO_BIAS_IDX, true);	
	spin_lock_irqsave(&gpio->data_lock, flags);
	b = bias[off];
	spin_unlock_irqrestore(&gpio->data_lock, flags);

	if (b == cfg)
		return 0;
	
	spin_lock_irqsave(&gpio->data_lock, flags);
	bias[off] = cfg;
	spin_unlock_irqrestore(&gpio->data_lock, flags);
	
	ret = hid_gpio_hw_write(gpio);
	if (ret)
	{
		hid_err(gpio->hsdev->hdev, "failed to set direction input for gpio %d with errno: %d.\n", off, ret);
		return ret;
	}

	return 0;
}


static int hid_gpio_set_config(struct gpio_chip *gc,
				  unsigned int offset, unsigned long config)
{
	struct hid_gpio_t *gpio = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_DISABLE:
		return hid_gpio_apply_config(gpio, offset, GPIO_BIAS_DISABLE);
	case PIN_CONFIG_BIAS_PULL_UP:
		return hid_gpio_apply_config(gpio, offset, GPIO_BIAS_PULL_UP);
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return hid_gpio_apply_config(gpio, offset, GPIO_BIAS_PULL_DOWN);
	default:
		break;
	}
	return -ENOTSUPP;
}



#define allocate_buffer(instance, dev, cnt) \
	instance = (uint8_t *) devm_kcalloc((dev), cnt, \
		sizeof(uint8_t), GFP_KERNEL); \
	if (instance == NULL) \
		return -ENOMEM;

static int hid_gpio_allocate_buffers(struct platform_device *pdev)
{		
	struct hid_gpio_t *gpio = platform_get_drvdata(pdev);
	
	allocate_buffer(gpio->data_buf, &pdev->dev, PACKET_SIZE(gpio->gpio_num));
	allocate_buffer(gpio->last_buf, &pdev->dev, PACKET_SIZE(gpio->gpio_num));

	return 0;
}

static const struct of_device_id hid_gpio_dt_match[] = {
	{ .compatible = "composite,hid-gpio" },
	{ /*sentinel*/ },
};
MODULE_DEVICE_TABLE(of, hid_gpio_dt_match);
#if defined(CONFIG_OF_GPIO)
static int hid_gpio_of_xlate(struct gpio_chip *chip,
				  const struct of_phandle_args *spec,
				  u32 *flags)
{
	struct hid_gpio_t *gpio = gpiochip_get_data(chip);

	if (WARN_ON(chip->of_gpio_n_cells < 2))
		return -EINVAL;

	if (WARN_ON(spec->args_count < chip->of_gpio_n_cells))
		return -EINVAL;

	if (spec->args[0] < 0 || spec->args[0] > gpio->gpio_num)
		return -EINVAL;

	return spec->args[0];
}
#endif

#ifdef CONFIG_PM
static int hid_gpio_suspend(struct hid_subdevice *hsdev, void *priv)
{
	return 0;
}

static int hid_gpio_resume(struct hid_subdevice *hsdev, void *priv)
{
	struct hid_gpio_t *gpio = platform_get_drvdata(priv);

	hid_gpio_hw_read(gpio);

	return 0;
}
#endif

static int hid_gpio_platform_probe(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);
	struct hid_gpio_t *gpio;
	struct gpio_chip *gc;
	int ret;

	if (!hsdev)
		return 0;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	ret = hid_composite_device_open(hsdev);
	if (ret)
		return ret;

	hid_device_io_start(hsdev->hdev);

    spin_lock_init(&gpio->data_lock);
	gpio->hsdev = hsdev;
	gpio->minor = ((struct hidraw *) hsdev->hdev->hidraw)->minor;
	gpio->base = -1;
	snprintf(gpio->gpio_name, 32, "hid-gpio-%d", gpio->minor);
	platform_set_drvdata(pdev, gpio);

	ret = hid_gpio_parse_report(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup attributes!\n");
		goto io_stop_clean;
	}

	if (gpio->gpio_num <= 0)
	{
		dev_err(&pdev->dev, "no gpio found!\n");
		goto io_stop_clean;
	}

	ret = hid_gpio_allocate_buffers(pdev);
	if (ret)
	{
		dev_err(&pdev->dev, "failed to allocate buffers!\n");
		goto io_stop_clean;
	}

    gpio->callbacks.capture_sample = hid_gpio_capture_sample;
	gpio->callbacks.send_event = hid_gpio_proc_event;
#ifdef CONFIG_PM
	gpio->callbacks.suspend = hid_gpio_suspend;
	gpio->callbacks.resume = hid_gpio_resume;
#endif
	gpio->callbacks.pdev = pdev;
	hid_composite_register_callback(hsdev,
		HID_USAGE_GPIO, &gpio->callbacks);	
    
	
	gc = &gpio->gpio_chip;

#if defined(CONFIG_OF_GPIO)
	pdev->dev.of_node = of_find_matching_node(NULL, hid_gpio_dt_match);
	if (pdev->dev.of_node) {
		gc->of_node = pdev->dev.of_node;
		gc->of_gpio_n_cells = 2;
		gc->of_xlate = hid_gpio_of_xlate;
		pdev->dev.fwnode = of_fwnode_handle(pdev->dev.of_node);
		ret = of_property_read_u32(gc->of_node, "gpio,base",
				   &gpio->base);
		if (ret)
			gpio->base = -1;

		of_node_put(pdev->dev.of_node);
		hid_info(gpio->hsdev->hdev, "hid gpio associate to devicetree node!\n");
	}
#endif

	gc->direction_input  = hid_gpio_direction_input;
	gc->direction_output = hid_gpio_direction_output;
	gc->get_direction = hid_gpio_get_direction;
	gc->get = hid_gpio_get_value;
	gc->set = hid_gpio_set_value;
	gc->set_config = hid_gpio_set_config;
	gc->can_sleep = true;

	gc->base = gpio->base;
	gc->ngpio = gpio->gpio_num;
	gc->label = gpio->gpio_name;
	gc->owner = THIS_MODULE;
 
 	gc->parent = &pdev->dev;
	ret = devm_gpiochip_add_data(&pdev->dev, &gpio->gpio_chip, gpio);
	if (ret) {
		hid_err(gpio->hsdev->hdev, "failed to add a gpiochip with %d.\n", ret);
		goto callbacks_clean;
	}

	hid_info(gpio->hsdev->hdev, "hid gpio was successfully probed!\n");
	
	return 0;

callbacks_clean:
	hid_composite_remove_callback(hsdev, HID_USAGE_GPIO);
io_stop_clean:
    hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	return ret;

}

static int hid_gpio_platform_remove(struct platform_device *pdev)
{
	struct hid_subdevice *hsdev = dev_get_platdata(&pdev->dev);

	if (!hsdev)
		return 0;

	hid_device_io_stop(hsdev->hdev);
	hid_composite_device_close(hsdev);
	hid_composite_remove_callback(hsdev, HID_USAGE_GPIO);
    return 0;
}


static const struct platform_device_id hid_gpio_ids[] = {
	{
		/* Format: HID-COMPOSITE-usage_id_in_hex_lowercase */
		.name = "HID-COMPOSITE-970000",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_gpio_ids);

static struct platform_driver hid_gpio_platform_driver = {
	.id_table = hid_gpio_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.of_match_table = of_match_ptr(hid_gpio_dt_match)
	},
	.probe		= hid_gpio_platform_probe,
	.remove		= hid_gpio_platform_remove,
};
module_platform_driver(hid_gpio_platform_driver);

MODULE_DESCRIPTION("HID USB Gpio");
MODULE_AUTHOR("Anis CHALI <anis.chali@exfo.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_HID);