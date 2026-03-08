#!/bin/bash
# ===========================================================
# scripts/bbb/flash.sh — Flash RefixOS to SD card
#
# Usage: ./scripts/bbb/flash.sh /dev/sdX
#
# Writes:
#   - MLO (GP header + spl.bin) → FAT partition 1 as "MLO"
#   - kernel.bin                → raw sector 2048 (1MB offset)
#
# SD card must be pre-formatted:
#   Partition 1: FAT32, bootable, ~32MB
# ===========================================================

set -e

DEVICE="$1"

if [ -z "$DEVICE" ]; then
    echo "Usage: $0 <device>"
    echo "Example: $0 /dev/sdb"
    exit 1
fi

if ! sudo blockdev --getsz "$DEVICE" > /dev/null 2>&1; then
    echo "Error: '$DEVICE' is not a valid block device or requires root."
    exit 1
fi

# Safety: refuse /dev/sda (likely host OS disk)
if [ "$DEVICE" = "/dev/sda" ]; then
    echo "Warning: /dev/sda detected. Make sure this is NOT your system disk!"
    echo "         Current lsblk output for reference:"
    lsblk | grep -E "sda|nvme"
    echo ""
    read -p "Continue anyway? [y/N]: " FORCE_SDA
    if [ "$FORCE_SDA" != "y" ] && [ "$FORCE_SDA" != "Y" ]; then
        echo "Aborted."
        exit 1
    fi
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/bbb"
MLO_BIN="$BUILD_DIR/MLO"
KERNEL_BIN="$BUILD_DIR/kernel.bin"

echo "========================================"
echo "  RefixOS SD Flash — BeagleBone Black"
echo "========================================"
echo "  Device     : $DEVICE"
echo "  MLO        : $MLO_BIN"
echo "  Kernel     : $KERNEL_BIN"
echo "========================================"

if [ ! -f "$MLO_BIN" ]; then
    echo "Error: MLO not found after build."
    echo "       Build BBB artifacts first: make PLATFORM=bbb"
    exit 1
fi

if [ ! -f "$KERNEL_BIN" ]; then
    echo "Error: kernel.bin not found after build."
    echo "       Build BBB artifacts first: make PLATFORM=bbb"
    exit 1
fi

MLO_SIZE=$(wc -c < "$MLO_BIN")
echo "  MLO size   : $MLO_SIZE bytes (code = MLO - 8B header)"
echo ""

read -p "Proceed? This will overwrite data on $DEVICE [y/N]: " CONFIRM
if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
    echo "Aborted."
    exit 0
fi

BOOT_PART="${DEVICE}1"
if [ ! -b "$BOOT_PART" ]; then
    echo "Error: Partition $BOOT_PART not found."
    echo "       Make sure the SD card has a FAT32 partition 1."
    exit 1
fi

# Mount FAT partition
MOUNTPOINT=$(lsblk -no MOUNTPOINT "$BOOT_PART" 2>/dev/null | head -n 1)
MY_MOUNT=0

if [ -z "$MOUNTPOINT" ]; then
    MOUNTPOINT="/tmp/refixos_flash_$$"
    mkdir -p "$MOUNTPOINT"
    echo "Mounting $BOOT_PART → $MOUNTPOINT ..."
    sudo mount "$BOOT_PART" "$MOUNTPOINT"
    MY_MOUNT=1
else
    echo "Using existing mount: $MOUNTPOINT"
fi

# Write MLO to FAT (remove first to prevent FAT fragmentation)
echo "Removing old MLO ..."
sudo rm -f "$MOUNTPOINT/MLO"
sudo sync

echo "Writing MLO (GP header + SPL binary) ..."
sudo cp "$MLO_BIN" "$MOUNTPOINT/MLO"
sudo sync

[ -f "$MOUNTPOINT/MLO" ] && echo "MLO: OK" || echo "Error: MLO write failed!"

# Unmount before raw kernel write
if [ $MY_MOUNT -eq 1 ]; then
    echo "Unmounting ..."
    sudo umount "$MOUNTPOINT"
    rmdir "$MOUNTPOINT"
fi

# Write kernel raw at sector 2048 (1MB offset)
echo "Writing kernel.bin to $DEVICE at sector 2048 ..."
sudo dd if="$KERNEL_BIN" of="$DEVICE" bs=512 seek=2048 conv=notrunc status=progress
sudo sync

echo ""
echo "========================================"
echo "  Flash complete!"
echo "  Insert SD into BeagleBone Black"
echo "  Connect UART0 @ 115200 8N1 and power on"
echo "========================================"
