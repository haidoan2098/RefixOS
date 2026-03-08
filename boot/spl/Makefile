# boot/spl/Makefile — SPL build (BeagleBone Black only)
#
# Build pipeline:
#   start.S + main.c + uart.c + clock.c + ddr.c + mmc.c → spl.elf → spl.bin → MLO (GP header added)
#
# MLO is the file that goes onto the FAT partition of the SD card.
# AM335x ROM Code requires a GP header (8 bytes) prepended to the binary.
# See: scripts/tools/gp_header.py, TRM §26.1.10

# Inherit toolchain + flags from top-level Makefile
ROOT_BUILD_DIR ?= $(CURDIR)/../../build
PLATFORM_BUILD_DIR ?= $(ROOT_BUILD_DIR)/bbb
OBJ_BUILD_DIR ?= $(ROOT_BUILD_DIR)/obj/bbb
CROSS   ?= arm-none-eabi
CC      := $(CROSS)-gcc
AS      := $(CROSS)-as
LD      := $(CROSS)-ld
OBJCOPY := $(CROSS)-objcopy
CFLAGS  ?= -nostdlib -ffreestanding -nostartfiles -mcpu=cortex-a8 -marm -Wall -Wextra -g
SPL_ASM_SRCS := arch/arm/start.S
SPL_C_SRCS   := src/main.c src/uart.c src/clock.c src/ddr.c src/mmc.c

SPL_OBJS     := $(OBJ_BUILD_DIR)/spl_start.o \
                $(OBJ_BUILD_DIR)/spl_main.o  \
                $(OBJ_BUILD_DIR)/spl_uart.o  \
                $(OBJ_BUILD_DIR)/spl_clock.o \
                $(OBJ_BUILD_DIR)/spl_ddr.o   \
                $(OBJ_BUILD_DIR)/spl_mmc.o

LD_SCRIPT    := linker/spl.ld
ELF_OUT      := $(PLATFORM_BUILD_DIR)/spl.elf
BIN_OUT      := $(PLATFORM_BUILD_DIR)/spl.bin
MLO_OUT      := $(PLATFORM_BUILD_DIR)/MLO

# Path to GP header tool (relative to project root)
GP_HEADER    := ../../scripts/bbb/tools/gp_header.py

INCLUDE_DIRS := -Iinclude
SPL_CFLAGS   := $(CFLAGS) -DPLATFORM_BBB

.PHONY: all clean

all: $(MLO_OUT)

$(PLATFORM_BUILD_DIR):
	mkdir -p $(PLATFORM_BUILD_DIR)

$(OBJ_BUILD_DIR):
	mkdir -p $(OBJ_BUILD_DIR)

# Assemble SPL entry point
$(OBJ_BUILD_DIR)/spl_start.o: arch/arm/start.S | $(OBJ_BUILD_DIR)
	$(CC) $(SPL_CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Compile SPL main
$(OBJ_BUILD_DIR)/spl_main.o: src/main.c | $(OBJ_BUILD_DIR)
	$(CC) $(SPL_CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Compile SPL UART driver
$(OBJ_BUILD_DIR)/spl_uart.o: src/uart.c | $(OBJ_BUILD_DIR)
	$(CC) $(SPL_CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Compile SPL clock driver
$(OBJ_BUILD_DIR)/spl_clock.o: src/clock.c | $(OBJ_BUILD_DIR)
	$(CC) $(SPL_CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Compile SPL DDR driver
$(OBJ_BUILD_DIR)/spl_ddr.o: src/ddr.c | $(OBJ_BUILD_DIR)
	$(CC) $(SPL_CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Compile SPL MMC driver
$(OBJ_BUILD_DIR)/spl_mmc.o: src/mmc.c | $(OBJ_BUILD_DIR)
	$(CC) $(SPL_CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Link SPL
$(ELF_OUT): $(SPL_OBJS) | $(PLATFORM_BUILD_DIR)
	$(LD) -T $(LD_SCRIPT) $^ -o $@

# Strip to raw binary
$(BIN_OUT): $(ELF_OUT)
	$(OBJCOPY) -O binary $< $@

# Generate MLO: prepend 8-byte GP header required by AM335x ROM Code
# TRM §26.1.10, Table 26-37: [size LE 4B][load_addr LE 4B][binary code]
$(MLO_OUT): $(BIN_OUT)
	python3 $(GP_HEADER) $< $@

clean:
	rm -f $(OBJ_BUILD_DIR)/spl_start.o $(OBJ_BUILD_DIR)/spl_main.o \
	      $(OBJ_BUILD_DIR)/spl_uart.o  $(OBJ_BUILD_DIR)/spl_clock.o \
	      $(OBJ_BUILD_DIR)/spl_ddr.o   $(OBJ_BUILD_DIR)/spl_mmc.o   \
	      $(ELF_OUT) $(BIN_OUT) $(MLO_OUT)
