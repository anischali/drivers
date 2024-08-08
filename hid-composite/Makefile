#obj-m += hid-composite.o hid-led.o hid-input.o rtc-hid-time.o hid-battery.o

obj-$(CONFIG_SYSFS_RESETS)							+= gpio-sysfs-resets.o
obj-$(CONFIG_HID_COMPOSITE)						+= hid-composite.o
obj-$(CONFIG_HID_COMPOSITE_POWER_SUPPLY)		+= hid-composite.o hid-power-supply.o
obj-$(CONFIG_HID_COMPOSITE_RTC)					+= hid-composite.o hid-rtc.o
obj-$(CONFIG_HID_COMPOSITE_RGB_LEDS)			+= hid-composite.o hid-led.o
obj-$(CONFIG_HID_COMPOSITE_TYPEC)				+= hid-composite.o hid-typec.o
obj-$(CONFIG_HID_COMPOSITE_GPIO)				+= hid-composite.o hid-gpio.o
obj-$(CONFIG_HID_COMPOSITE_I2C)					+= hid-composite.o hid-i2c.o
obj-$(CONFIG_HID_COMPOSITE_FIRMWARE)			+= hid-composite.o hid-firmware.o
ccflags-$(CONFIG_HID_COMPOSITE_DEBUG)			+= -DDEBUG -Og
 
obj-$(CONFIG_HID_COMPOSITE_ALL)					+= hid-composite.o hid-gpio.o hid-typec.o hid-led.o hid-rtc.o hid-power-supply.o hid-i2c.o hid-firmware.o

KERNEL_SRC?="/usr/src/linux-headers-$(shell uname -r)"
DESTDIR ?= "$(INSTALL_MOD_PATH)"

SRC:=$(shell pwd)

modules all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install install: all
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install INSTALL_MOD_PATH=$(DESTDIR)

clean:
	rm -f .*.o.d *.o.d *.o *~ core .depend .*.cmd *.ko *.mod.c *.mod
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers