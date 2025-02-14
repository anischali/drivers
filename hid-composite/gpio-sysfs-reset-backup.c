#include <linux/errno.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/parser.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/reset.h>

struct reset_device_data_t {
	char *name;
	struct kobject kobj;
	struct device *dev;
};


static struct reset_device_data_t *rsts_devices;

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{

	if (!len || buf[0] != '1')
		return -EINVAL;

    
	__device_reset(dev, true);
	return len;
}
static DEVICE_ATTR_WO(reset);

struct attribute *resets_dev_attrs[] = {
	&dev_attr_reset.attr,
	NULL,
};
static struct attribute *resets_attrs[] = {
	&dev_attr_reset.attr,
	NULL,
};

static struct kobj_type resets_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = resets_attrs,
};

static int gpio_resets_sysfs_add_reset(struct reset_device_data_t *rsts, struct device *dev, const char *name)
{
	int ret;

	ret = kobject_init_and_add(&rsts->kobj,
			&resets_ktype, &dev->kobj, "%s", name);
	if (ret) {
		kobject_put(&rsts->kobj);
		return ret;
	}

	kobject_uevent(&rsts->kobj, KOBJ_ADD);

	rsts->dev = dev;

	return 0;
}

static int populate_resets_devices(void) {
    struct platform_device *pdev;
    struct device_node *node;
	int count = 0, ret;
	

	for_each_compatible_node(node, NULL, "gpio-reset") {
		pdev = of_find_device_by_node(node);
		if (pdev) {
			++count;
			of_node_put(node);
		}
	}


	rsts_devices = kcalloc(count, sizeof(struct reset_device_data_t), GFP_KERNEL);
	if (!rsts_devices)
		return -ENOMEM;

	count = 0;
    for_each_compatible_node(node, NULL, "gpio-reset") {
		pdev = of_find_device_by_node(node);
		if (pdev) {

            ret = gpio_resets_sysfs_add_reset(&rsts_devices[count], &pdev->dev, dev_name(&pdev->dev));
			if (ret) {
				of_node_put(node);
				goto ret_err;
			}
			of_node_put(node);
			++count;
		}
	}

    return 0;
ret_err:
	kfree(rsts_devices);
	return ret;
}


static int __init reset_control_class_init(void)
{
    return populate_resets_devices();
}
subsys_initcall(reset_control_class_init);

static void __exit reset_control_class_exit(void)
{
	kfree(rsts_devices);
}
module_exit(reset_control_class_exit);


MODULE_DESCRIPTION("Platform resets");
MODULE_AUTHOR("Anis CHALI <anis.chali1@outlook.com>");
MODULE_LICENSE("GPL");

