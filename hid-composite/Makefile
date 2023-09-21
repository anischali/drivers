obj-m += hid-composite.o hid-led.o hid-input.o

KDIR?="/usr/src/linux-headers-$(shell uname -r)"

SRC:=$(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(SRC) modules

clean:
	rm -f .*.o.d *.o.d *.o *~ core .depend .*.cmd *.ko *.mod.c *.mod
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers