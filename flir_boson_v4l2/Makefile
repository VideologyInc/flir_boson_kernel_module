# SPDX-License-Identifier: GPL-2.0

# Out-of-tree module build support
flir-boson-objs := flir-boson-core.o flir-boson-fslp.o
obj-m += flir-boson.o

# In-tree kernel build support
obj-$(CONFIG_VIDEO_FLIR_BOSON) += flir-boson.o
flir-boson-y := flir-boson-core.o flir-boson-fslp.o

# Build configuration
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_SRC ?= /lib/modules/$(KERNEL_VERSION)/build

# Simulation mode control
ifdef SIM
    ccflags-y += -DFLIR_SIMULATION_MODE
    CFLAGS_flir-boson-core.o += -DFLIR_SIMULATION_MODE
    CFLAGS_flir-boson-fslp.o += -DFLIR_SIMULATION_MODE
endif

default:
	make -C $(KERNEL_SRC) M=$(CURDIR) modules

# Hardware mode (production)
hardware:
	@echo "Building FLIR Boson+ driver for HARDWARE mode..."
	make -C $(KERNEL_SRC) M=$(CURDIR) modules

# Simulation mode (testing)
simulation:
	@echo "Building FLIR Boson+ driver for SIMULATION mode..."
	make -C $(KERNEL_SRC) M=$(CURDIR) SIM=1 modules

# Aliases for convenience
sim: simulation
hw: hardware

modules_install:
	make -C $(KERNEL_SRC) M=$(CURDIR) KERNELRELEASE=$(KERNEL_VERSION) modules_install

clean:
	make -C $(KERNEL_SRC) M=$(CURDIR) clean

# Show current build mode
status:
	@if [ -f flir-boson.ko ]; then \
		if objdump -t flir-boson.ko | grep -q "flir_simulate"; then \
			echo "Current build: SIMULATION mode"; \
		else \
			echo "Current build: HARDWARE mode"; \
		fi; \
	else \
		echo "No module built yet. Use 'make simulation' or 'make hardware'"; \
	fi

help:
	@echo "FLIR Boson+ V4L2 Driver Build System"
	@echo "======================================"
	@echo ""
	@echo "Targets:"
	@echo "  hardware     - Build for real hardware (I2C communication)"
	@echo "  simulation   - Build for testing (printk-based I2C mock)"
	@echo "  sim          - Alias for simulation"
	@echo "  hw           - Alias for hardware"
	@echo "  clean        - Clean build artifacts"
	@echo "  status       - Show current build mode"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make simulation    # Build for hardware-less testing"
	@echo "  make hardware      # Build for production deployment"
	@echo "  make clean && make simulation  # Clean rebuild for testing"

all: default

.PHONY: default hardware simulation sim hw modules_install clean status help all