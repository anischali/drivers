obj-$(CONFIG_GPIO_RFKILL)							+= rfkill-gpio.o

KERNEL_SRC?="/usr/src/kernels/$(shell uname -r)"
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
