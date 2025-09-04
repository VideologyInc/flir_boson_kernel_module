# SPDX-License-Identifier: GPL-2.0

# Out-of-tree module build support
flir-boson-objs := flir-boson-core.o flir-boson-fslp.o Client_Dispatcher.o I2C_Connector.o
obj-m += flir-boson.o

EXTRA_CFLAGS += -DDEBUG

# In-tree kernel build support
obj-$(CONFIG_VIDEO_FLIR_BOSON) += flir-boson.o
flir-boson-y := flir-boson-core.o flir-boson-fslp.o Client_Dispatcher.o I2C_Connector.o

# Build configuration
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_SRC ?= /lib/modules/$(KERNEL_VERSION)/build

default:
	make -C $(KERNEL_SRC) M=$(CURDIR) modules

modules_install:
	make -C $(KERNEL_SRC) M=$(CURDIR) KERNELRELEASE=$(KERNEL_VERSION) modules_install

clean:
	make -C $(KERNEL_SRC) M=$(CURDIR) clean

all: default

.PHONY: default hardware hw modules_install clean all
