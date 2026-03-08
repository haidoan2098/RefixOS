# boot/kernel/Makefile — Kernel entry build
#
# Compiles: boot/kernel/arch/start.S (reset entry)
# Links with: kernel objects using platform-specific linker script
# Output: build/kernel.elf, build/kernel.bin

# Inherit toolchain + flags from top-level Makefile
ROOT_BUILD_DIR ?= $(CURDIR)/../../build
PLATFORM_BUILD_DIR ?= $(ROOT_BUILD_DIR)/$(PLATFORM)
OBJ_BUILD_DIR ?= $(ROOT_BUILD_DIR)/obj/$(PLATFORM)
CROSS   ?= arm-none-eabi
CC      := $(CROSS)-gcc
AS      := $(CROSS)-as
LD      := $(CROSS)-ld
OBJCOPY := $(CROSS)-objcopy
CFLAGS  ?= -nostdlib -ffreestanding -nostartfiles -mcpu=cortex-a8 -marm -Wall -Wextra -g
BOOT_ASM_SRCS := arch/start.S
KERNEL_C_SRCS := ../../kernel/main.c \
                 ../../bsp/drivers/uart/uart.c

BOOT_OBJS   := $(OBJ_BUILD_DIR)/kernel_start.o
KERNEL_OBJS := $(OBJ_BUILD_DIR)/kernel_main.o $(OBJ_BUILD_DIR)/kernel_uart.o

# Select linker script based on PLATFORM
ifeq ($(PLATFORM), bbb)
    LD_SCRIPT := linker/kernel_bbb.ld
else
    LD_SCRIPT := linker/kernel_qemu.ld
endif

ELF_OUT := $(PLATFORM_BUILD_DIR)/kernel.elf
BIN_OUT := $(PLATFORM_BUILD_DIR)/kernel.bin

INCLUDE_DIRS := -I../../bsp/include -I../../bsp/drivers/uart

.PHONY: all clean

all: $(ELF_OUT) $(BIN_OUT)

$(PLATFORM_BUILD_DIR):
	mkdir -p $(PLATFORM_BUILD_DIR)

$(OBJ_BUILD_DIR):
	mkdir -p $(OBJ_BUILD_DIR)

# Assemble reset entry
$(OBJ_BUILD_DIR)/kernel_start.o: arch/start.S | $(OBJ_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile kernel C entry
$(OBJ_BUILD_DIR)/kernel_main.o: ../../kernel/main.c | $(OBJ_BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Compile UART driver
$(OBJ_BUILD_DIR)/kernel_uart.o: ../../bsp/drivers/uart/uart.c | $(OBJ_BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Link
$(ELF_OUT): $(BOOT_OBJS) $(KERNEL_OBJS) | $(PLATFORM_BUILD_DIR)
	$(LD) -T $(LD_SCRIPT) $^ -o $@

# Raw binary (for U-Boot / SD card)
$(BIN_OUT): $(ELF_OUT)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJ_BUILD_DIR)/kernel_start.o $(OBJ_BUILD_DIR)/kernel_main.o \
	      $(OBJ_BUILD_DIR)/kernel_uart.o $(ELF_OUT) $(BIN_OUT)
