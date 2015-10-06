obj-m := rgbled-fb.o ws2812b-spi-fb.o apa102-spi-fb.o
rgbled-fb-y := rgbled-fb-core.o rgbled-fb-of.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) $(XTRA) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) $(XTRA) M=$(PWD) modules modules_install
	sync

.DEFAULT:
	$(MAKE) -C $(KDIR) M=$(PWD) $@
