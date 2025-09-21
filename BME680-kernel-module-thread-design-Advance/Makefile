obj-m += bme680_core.o bme680_i2c.o bme680_spi.o bme680_ipc.o

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
CC := gcc
DTC := dtc
APP := bme680_app
APP_SRC := bme680_app.c thread_pool.c monitor.c pubsub.c logger.c bme680_config.c fork_handler.c
APP_HEADERS := bme680.h thread_pool.h monitor.h pubsub.h logger.h bme680_config.h fork_handler.h
DTS_FILES := bme680.dts
DTBO_FILES := $(DTS_FILES:.dts=.dtbo)
PLATFORM ?= raspberry
VERSION := 3.2.1
CFLAGS := -g -O2 -Wall -pthread -lrt -lseccomp -march=native -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
KCFLAGS := -DCONFIG_BME680_DEBUG=$(BME680_DEBUG) -DCONFIG_VMALLOC=y -DCONFIG_NETLINK=y -DCONFIG_HWMON=y -DCONFIG_TRACEPOINTS=y -DCONFIG_SPI=y -DCONFIG_LOCKDEP=y -DCONFIG_PROVE_LOCKING=y -DCONFIG_DEBUG_LOCK_ALLOC=y # Thêm lockdep
BME680_DEBUG ?= 0
MAKEFLAGS += -j$(shell nproc)

all: check-tools dt app module

module:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules EXTRA_CFLAGS="$(KCFLAGS)"

dt: $(DTBO_FILES)
$(DTBO_FILES): %.dtbo: %.dts
	$(DTC) -@ -I dts -O dtb -o $@ $<

app: $(APP_SRC) $(APP_HEADERS)
	$(CC) $(CFLAGS) -o $(APP) $(APP_SRC) -lm

install: module dt
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	mkdir -p /boot/overlays
	for dtbo in $(DTBO_FILES); do cp $$dtbo /boot/overlays/; done
	depmod -a
	echo "Add 'dtoverlay=bme680' to /boot/config.txt"

test: install
	if lsmod | grep -q bme680; then sudo rmmod bme680_core bme680_i2c bme680_spi bme680_ipc; fi
	sudo insmod $(PWD)/bme680_core.ko
	sudo insmod $(PWD)/bme680_i2c.ko
	sudo insmod $(PWD)/bme680_spi.ko
	sudo insmod $(PWD)/bme680_ipc.ko
	./$(APP) -i 1000

test_multithread: app
	./$(APP) -i 1000 -t 8 # Test with 8 threads
	./$(APP) -i 500 -t 16 # Test with 16 threads and faster interval
	./$(APP) -i 2000 -t 4 # Test with 4 threads and slower interval
	# Thêm test deadlock: valgrind --tool=helgrind ./$(APP) -t 8

plot:
	gnuplot -e "set terminal png; plot 'data.log' with lines" > plot.png

backup:
	mkdir -p backup
	TIMESTAMP=$$(date +%F-%H%M%S); \
	for dtbo in $(DTBO_FILES); do [ -f /boot/overlays/$$dtbo ] && cp /boot/overlays/$$dtbo backup/$$dtbo-$$TIMESTAMP; done; \
	for mod in bme680_core.ko bme680_i2c.ko bme680_spi.ko bme680_ipc.ko; do \
		[ -f /lib/modules/$(uname -r)/kernel/drivers/iio/chemical/$$mod ] && cp /lib/modules/$(uname -r)/kernel/drivers/iio/chemical/$$mod backup/$$mod-$$TIMESTAMP; \
	done

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f $(DTBO_FILES) $(APP) plot.png
	rm -rf *.o *.ko *.mod *.mod.c *.symvers *.order .*.cmd .tmp_versions

check-tools:
	which $(CC) >/dev/null || (echo "Error: gcc not found"; exit 1)
	which $(DTC) >/dev/null || (echo "Error: dtc not found"; exit 1)
	pkg-config --exists pthread seccomp || echo "Warning: pthread/seccomp not found"
	[ -d $(KERNEL_DIR) ] || (echo "Error: Kernel dir not found"; exit 1)

version:
	echo "$(VERSION)" > version.txt

ifeq ($(PLATFORM),raspberry)
    DTS_FILES := bme680.dts
endif

.PHONY: all module dt app install test test_multithread plot backup clean check-tools version