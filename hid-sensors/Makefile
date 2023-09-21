# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Hid sensor common modules.
#

obj-m += hid-sensor-iio-common.o
obj-m += hid-sensor-trigger.o
hid-sensor-iio-common-y := hid-sensor-attributes.o


KDIR?="/usr/src/linux-headers-$(shell uname -r)"

SRC:=$(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(SRC) modules

clean:
	rm -f .*.o.d *.o.d *.o *~ core .depend .*.cmd *.ko *.mod.c *.mod
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers