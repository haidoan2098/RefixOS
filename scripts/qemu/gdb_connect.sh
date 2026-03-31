#!/usr/bin/env bash
# scripts/qemu/gdb_connect.sh — Connect GDB to QEMU (must run debug.sh first)
#
# Usage: bash scripts/qemu/gdb_connect.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

KERNEL="${PROJECT_ROOT}/build/qemu/kernel.elf"
GDB_PORT="1234"

if [ ! -f "$KERNEL" ]; then
    echo "[ERROR] Kernel ELF not found: $KERNEL"
    echo "        Run 'make PLATFORM=qemu' first."
    exit 1
fi

echo "[INFO] Starting GDB..."
echo "       Kernel  : $KERNEL"
echo "       Port    : $GDB_PORT"
echo ""

GDB_CMDS=$(mktemp)
trap 'rm -f "$GDB_CMDS"' EXIT
cat > "$GDB_CMDS" <<EOF
target remote :$GDB_PORT
set architecture armv7-a
set endian little
break _start
break kmain
break uart_init
continue
EOF

echo "[INFO] GDB commands loaded:"
echo "       - target remote :$GDB_PORT"
echo "       - break _start"
echo "       - break kmain"
echo "       - break uart_init"
echo "       - continue"
echo ""
echo "[INFO] Useful debug commands:"
echo "       (gdb) stepi           # Step one instruction"
echo "       (gdb) nexti           # Next instruction"
echo "       (gdb) info registers  # Show all registers"
echo "       (gdb) x/10i \$pc      # Disassemble at PC"
echo "       (gdb) x/20x \$sp      # Show stack"
echo ""

arm-none-eabi-gdb -x "$GDB_CMDS" "$KERNEL"
