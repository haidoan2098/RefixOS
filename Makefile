# RingNova — Top-level Makefile
#
# Usage:
#   make                  → build for QEMU (default)
#   make PLATFORM=bbb     → build for BeagleBone Black
#   make qemu             → build + run on QEMU
#   make clean            → clean current platform
#   make clean-all        → clean all platforms
#
# All output goes to build/

PLATFORM ?= qemu
PLATFORM_UPPER := $(shell echo $(PLATFORM) | tr a-z A-Z)

# Toolchain
CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
AS      := $(CROSS)as
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

# Build directories
BUILD_DIR := build/$(PLATFORM)
OBJ_DIR   := build/obj/$(PLATFORM)

# Linker script
LDSCRIPT := kernel/linker/kernel_$(PLATFORM).ld

# Mandatory compiler flags
CFLAGS := -nostdlib -ffreestanding -nostartfiles \
          -mcpu=cortex-a8 -marm \
          -DPLATFORM_$(PLATFORM_UPPER) \
          -I kernel/include \
          -I kernel/drivers \
          -Wall -Wextra -g

# Source files
C_SRCS := kernel/main.c \
          kernel/drivers/uart/uart.c \
          kernel/drivers/intc/intc.c \
          kernel/drivers/timer/timer.c \
          kernel/arch/arm/exception/exception_handlers.c \
          kernel/arch/arm/mm/mmu.c \
          kernel/arch/arm/mm/pgtable.c \
          kernel/proc/process.c

S_SRCS := kernel/arch/arm/boot/start.S \
          kernel/arch/arm/exception/vectors.S \
          kernel/arch/arm/exception/exception_entry.S \
          kernel/arch/arm/mm/mmu_enable.S \
          kernel/arch/arm/proc/user_stub.S

# Object files
C_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SRCS))
S_OBJS := $(patsubst %.S,$(OBJ_DIR)/%.o,$(S_SRCS))
OBJS   := $(S_OBJS) $(C_OBJS)

# Output
TARGET_ELF := $(BUILD_DIR)/kernel.elf
TARGET_BIN := $(BUILD_DIR)/kernel.bin

.DEFAULT_GOAL := all

.PHONY: all clean clean-all qemu

all: $(TARGET_ELF) $(TARGET_BIN)
	@echo "[OK] $(TARGET_ELF)"

# Link
$(TARGET_ELF): $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -T $(LDSCRIPT) -o $@ $(OBJS) -lgcc

# Raw binary for flashing (BBB)
$(TARGET_BIN): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@

# Compile C
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble .S (through gcc for preprocessor support)
$(OBJ_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build/$(PLATFORM) build/obj/$(PLATFORM)

clean-all:
	rm -rf build/

# Build + launch QEMU
qemu: all
	@bash scripts/qemu/run.sh
