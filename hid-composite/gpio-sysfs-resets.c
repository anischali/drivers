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

struct reset_device_t {
	char name[32];
	struct kobject kobj;
	struct device *dev;
	struct reset_control *reset;
	int delay_ms;
	struct list_head list;
};

struct reset_controller_t {
	int count;
	struct class *resets_sysfs_class;
	struct mutex list_mutex;
	struct list_head rdevices_list;
};


struct reset_attribute_t {
	struct attribute attr;
	ssize_t (*show)(struct reset_device_t *, char *);
	ssize_t (*store)(struct reset_device_t *, const char *, size_t);
};

static struct reset_controller_t rsts_devices;

static ssize_t reset_attr_store(struct reset_device_t *rdev, const char *buf, size_t len)
{
	int ret = -1;

	if (len <= 0 || buf[0] != '1')
		return -EINVAL;
		
	ret = reset_control_reset(rdev->reset);	

	printk("resets: return %d\n", ret);

	return (!ret) ? len : ret;
}

static ssize_t delay_ms_attr_store(struct reset_device_t *rdev, const char *buf, size_t len)
{
	int val;

	if (len <= 0)
		return -EINVAL;
		
	val = simple_strtoul(buf, NULL, 10);
	
	printk("resets: delay_ms %d\n", val);

	rdev->delay_ms = val;

	return len;
}

static ssize_t delay_ms_attr_show(struct reset_device_t *rdev, char *buf)
{
	return snprintf(buf, 32, "%d\n", rdev->delay_ms);
}

#define to_object(k) container_of(k, struct reset_device_t, kobj)
#define to_attr(a) container_of(a, struct reset_attribute_t, attr)

static ssize_t resets_attr_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct reset_attribute_t *rattr = to_attr(attr);
	struct reset_device_t *rdev = to_object(kobj);
	ssize_t ret;

	ret = rattr->show ? rattr->show(rdev, buf) : 0;
	
	return ret;
}



static ssize_t resets_attr_store(struct kobject * kobj, struct attribute * attr, const char * buf, size_t len)
{
	struct reset_attribute_t *rattr = to_attr(attr);
	struct reset_device_t *rdev = to_object(kobj);
	ssize_t ret;

	ret = rattr->store ? rattr->store(rdev, buf, len) : 0;
	
	return ret;
}

#define RESET_ATTR(_name, _mode, _show, _store) \
	static struct reset_attribute_t reset_attr_##_name = __ATTR(_name, _mode, _show, _store)


RESET_ATTR(reset, 0644, NULL, reset_attr_store);
RESET_ATTR(delay_ms, 0644, delay_ms_attr_show, delay_ms_attr_store);

static const struct attribute * const resets_sysfs_attrs[] = {
	&reset_attr_reset.attr,
	&reset_attr_delay_ms.attr,
    NULL
};

static const struct sysfs_ops resets_sysfs_ops = {
	.show   = resets_attr_show,
	.store = resets_attr_store
};


static struct kobj_type resets_ktype = {
	.sysfs_ops = &resets_sysfs_ops
};

static int resets_ctrl_device_add(struct platform_device *pdev) {
	struct reset_device_t *rdev;
	int ret;

	rdev = devm_kzalloc(&pdev->dev, sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return -ENOMEM;

	rdev->dev = &pdev->dev;

	snprintf(rdev->name, sizeof(rdev->name), "%s", dev_name(&pdev->dev));

	rdev->reset = devm_reset_control_get_shared(&pdev->dev, rdev->name);
	if (IS_ERR(rdev->reset))
		return -EBUSY;

	INIT_LIST_HEAD(&rdev->list);

	list_add_tail(&rdev->list, &rsts_devices.rdevices_list);

	kobject_init(&rdev->kobj, &resets_ktype);
    if (kobject_add(&rdev->kobj, firmware_kobj, rdev->name)) {
		ret = -ENXIO;
        goto fail_add;
    }

    ret = sysfs_create_files(&rdev->kobj, resets_sysfs_attrs);
    if (ret) {
        goto fail_add;
    }

	return 0;

fail_add:
	kobject_put(&rdev->kobj);
	return ret;
}

static int resets_ctrl_device_remove(struct reset_device_t *rdev) {

	list_del(&rdev->list);

	sysfs_remove_files(&rdev->kobj, resets_sysfs_attrs);

	kobject_put(&rdev->kobj);

	return 0;
}


static int resets_remove_devices(void) {
	struct reset_device_t *rdev, *save;

	mutex_lock(&rsts_devices.list_mutex);
    list_for_each_entry_safe(rdev, save, &rsts_devices.rdevices_list, list) {
		if (rdev) {
        	resets_ctrl_device_remove(rdev);
			rsts_devices.count--;
		}
	}

	mutex_unlock(&rsts_devices.list_mutex);
    
	return 0;
}

static int resets_populate_devices(void) {
    struct platform_device *pdev;
    struct device_node *node;
	int ret;

	mutex_lock(&rsts_devices.list_mutex);
    for_each_compatible_node(node, NULL, "gpio-reset") {
		pdev = of_find_device_by_node(node);
		if (pdev) {
            ret = resets_ctrl_device_add(pdev);
			if (ret) {
				of_node_put(node);
				goto ret_err;
			}
			of_node_put(node);
		}
		rsts_devices.count++;
	}
	
	mutex_unlock(&rsts_devices.list_mutex);
    
	return 0;
ret_err:
	mutex_unlock(&rsts_devices.list_mutex);
	resets_remove_devices();
	return ret;
}

static int __init reset_control_class_init(void)
{
	rsts_devices.resets_sysfs_class = class_create(THIS_MODULE, "resets");
	if (IS_ERR(rsts_devices.resets_sysfs_class)) {
		rsts_devices.resets_sysfs_class = NULL;
	}

	mutex_init(&rsts_devices.list_mutex);

	INIT_LIST_HEAD(&rsts_devices.rdevices_list);

    return resets_populate_devices();
}
subsys_initcall(reset_control_class_init);

static void __exit reset_control_class_exit(void)
{
	resets_remove_devices();
	if (rsts_devices.resets_sysfs_class) {
		class_destroy(rsts_devices.resets_sysfs_class);
		rsts_devices.resets_sysfs_class = NULL;
	}
}
module_exit(reset_control_class_exit);

MODULE_DESCRIPTION("sysfs resets");
MODULE_AUTHOR("Anis CHALI <anis.chali@exfo.com>");
MODULE_LICENSE("GPL");

