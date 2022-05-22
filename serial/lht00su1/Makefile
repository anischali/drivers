obj-m:=fx2lafw.o
KDIR?="/usr/src/linux-headers-$(shell uname -r)"

SRC:=$(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(SRC) modules

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
