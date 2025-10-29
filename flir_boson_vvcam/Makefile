# SPDX-License-Identifier: GPL-2.0

# Out-of-tree module build support
flir-boson-vvcam-objs := flir-boson-core.o flir-boson-fslp.o
obj-m += flir-boson-vvcam.o

EXTRA_CFLAGS += -DDEBUG

# In-tree kernel build support
obj-$(CONFIG_VIDEO_FLIR_BOSON_VVCAM) += flir-boson-vvcam.o
flir-boson-vvcam-y := flir-boson-core.o flir-boson-fslp.o

# Build configuration
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_SRC ?= /lib/modules/$(KERNEL_VERSION)/build

default:
	make -C $(KERNEL_SRC) M=$(CURDIR) modules

modules_install: default
	make -C $(KERNEL_SRC) M=$(CURDIR) KERNELRELEASE=$(KERNEL_VERSION) modules_install

clean:
	make -C $(KERNEL_SRC) M=$(CURDIR) clean

all: default

install: all
	-rmmod -f flir_boson_vvcam
	-rmmod -f imx8_media_dev
	insmod flir-boson-vvcam.ko
	modprobe imx8_media_dev

.PHONY: default hardware hw modules_install clean all install
