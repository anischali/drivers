# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the USB input drivers
#

ccflags-$(CONFIG_USB_HID_COMPOSITE_DEBUG)			+= -DDEBUG -Og
obj-$(CONFIG_USB_HID_COMPOSITE)						+= hid-composite.o
obj-$(CONFIG_USB_HID_COMPOSITE_BATTERY)				+= hid-battery.o
obj-$(CONFIG_USB_HID_COMPOSITE_RTC)					+= rtc-hid-time.o
obj-$(CONFIG_USB_HID_COMPOSITE_RGB_LEDS)			+= hid-led.o