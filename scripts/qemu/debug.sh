#!/usr/bin/env bash
# scripts/qemu/debug.sh — Start QEMU with GDB server
#
# Usage: bash scripts/qemu/debug.sh
# Then in another terminal: bash scripts/qemu/gdb_connect.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

KERNEL="${PROJECT_ROOT}/build/kernel.elf"
MACHINE="realview-pb-a8"
CPU="cortex-a8"
MEM="128M"

if [ ! -f "$KERNEL" ]; then
    echo "[ERROR] Kernel ELF not found: $KERNEL"
    echo "        Run 'make PLATFORM=qemu' first."
    exit 1
fi

echo "[INFO] Starting QEMU with GDB server..."
echo "       Machine : $MACHINE"
echo "       Kernel  : $KERNEL"
echo "       GDB port: 1234"
echo "       Exit    : Press Ctrl+A then 'x' to quit"
echo ""
echo "[INFO] In another terminal, run:"
echo "       bash scripts/qemu/gdb_connect.sh"
echo ""

exec qemu-system-arm \
    -M "$MACHINE" \
    -cpu "$CPU" \
    -m "$MEM" \
    -nographic \
    -serial mon:stdio \
    -kernel "$KERNEL" \
    -S -gdb tcp::1234
