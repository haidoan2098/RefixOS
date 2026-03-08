# RefixOS — Top-level Makefile
#
# Usage:
#   make                  → build for QEMU (default)
#   make PLATFORM=qemu    → build kernel only (QEMU)
#   make PLATFORM=bbb     → build SPL + kernel (BeagleBone Black)
#
# All output goes to build/

PLATFORM ?= qemu
PLATFORM_UPPER := $(shell echo $(PLATFORM) | tr a-z A-Z)

# Build directories
ROOT_BUILD_DIR := $(CURDIR)/build
PLATFORM_BUILD_DIR := $(ROOT_BUILD_DIR)/$(PLATFORM)
OBJ_BUILD_DIR := $(ROOT_BUILD_DIR)/obj/$(PLATFORM)
export ROOT_BUILD_DIR PLATFORM_BUILD_DIR OBJ_BUILD_DIR

# Remember last built platform
LAST_PLATFORM_FILE := $(ROOT_BUILD_DIR)/.last_platform
ifneq ($(wildcard $(LAST_PLATFORM_FILE)),)
  LAST_PLATFORM := $(shell cat $(LAST_PLATFORM_FILE))
else
  LAST_PLATFORM := $(PLATFORM)
endif

export PLATFORM
export PLATFORM_UPPER

# Toolchain
CROSS   ?= arm-none-eabi
CC      := $(CROSS)-gcc
AS      := $(CROSS)-as
LD      := $(CROSS)-ld
OBJCOPY := $(CROSS)-objcopy
GDB     := $(CROSS)-gdb

export CC AS LD OBJCOPY GDB

# Mandatory compiler flags (Hard constraint — do NOT remove)
CFLAGS := -nostdlib -ffreestanding -nostartfiles \
          -mcpu=cortex-a8 -marm \
          -DPLATFORM_$(PLATFORM_UPPER) \
          -Wall -Wextra -g

export CFLAGS

.DEFAULT_GOAL := all

.PHONY: all clean qemu

# Create build directories
$(ROOT_BUILD_DIR):
	mkdir -p $(ROOT_BUILD_DIR)

$(PLATFORM_BUILD_DIR):
	mkdir -p $(PLATFORM_BUILD_DIR)

$(OBJ_BUILD_DIR):
	mkdir -p $(OBJ_BUILD_DIR)

# Delegate to boot/Makefile (which dispatches to kernel/ and/or spl/)
all: $(ROOT_BUILD_DIR) $(PLATFORM_BUILD_DIR) $(OBJ_BUILD_DIR)
	@echo $(PLATFORM) > $(LAST_PLATFORM_FILE)
	$(MAKE) -C boot

clean:
	$(MAKE) -C boot clean
	rm -rf $(ROOT_BUILD_DIR)/$(LAST_PLATFORM) $(ROOT_BUILD_DIR)/obj/$(LAST_PLATFORM)

clean-all:
	$(MAKE) -C boot clean
	rm -rf $(ROOT_BUILD_DIR)/qemu $(ROOT_BUILD_DIR)/bbb $(ROOT_BUILD_DIR)/obj

status:
	@echo "Current PLATFORM: $(LAST_PLATFORM)"

# Launch QEMU (PLATFORM=qemu only)
qemu:
	@bash scripts/qemu/run.sh

