obj-m += pcm1808-master.o rpi-duplex-card.o

KDIR ?= /lib/modules/$(shell uname -r)/build
OVERLAY = pcm5102a-pcm1808-duplex

all: modules dtbo

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

dtbo: $(OVERLAY).dtbo

$(OVERLAY).dtbo: $(OVERLAY)-overlay.dts
	dtc -@ -I dts -O dtb -o $@ $<

install: all
	sudo $(MAKE) -C $(KDIR) M=$(PWD) modules_install
	sudo depmod -a
	sudo cp $(OVERLAY).dtbo /boot/firmware/overlays/

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(OVERLAY).dtbo
