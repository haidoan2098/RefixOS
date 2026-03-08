#!/usr/bin/env python3
"""
scripts/tools/gp_header.py
Generate GP (General Purpose) Image header for AM335x ROM Code

The AM335x ROM Code requires MLO to have an 8-byte GP header prepended
to the raw binary. Without this header, ROM will not load the SPL.

GP Header Format (TRM §26.1.10, Table 26-37):
  Offset 0x00: Image size    (4 bytes, LE) = size of executable code (NOT including header)
  Offset 0x04: Load address  (4 bytes, LE) = SRAM destination = entry point

ROM behavior:
  1. Reads header (8 bytes)
  2. Copies `size` bytes from file offset 0x08 to `load_addr` in SRAM
  3. Jumps to `load_addr` — so _start MUST be the very first instruction

Usage:
  python3 scripts/tools/gp_header.py <input.bin> <output.MLO>

Example:
  python3 scripts/tools/gp_header.py build/spl.bin build/MLO
"""

import struct
import sys
import os

# AM335x OCMC RAM public area — ROM loads SPL here (TRM §26.1.4.2)
LOAD_ADDR = 0x402F0400

# BootROM SRAM limit for GP image (TRM §26.1.9.2)
MAX_CODE_SIZE = 109 * 1024  # 111616 bytes


def make_gp_header(input_bin: str, output_mlo: str) -> int:
    if not os.path.exists(input_bin):
        print(f"Error: '{input_bin}' not found")
        return 1

    with open(input_bin, 'rb') as f:
        code = f.read()

    code_size = len(code)

    if code_size > MAX_CODE_SIZE:
        print(f"ERROR: {input_bin} is {code_size} bytes — exceeds 109KB SRAM limit!")
        return 1

    # 8-byte GP header: [size LE 4B] [load_addr LE 4B]
    header = struct.pack('<I', code_size) + struct.pack('<I', LOAD_ADDR)

    with open(output_mlo, 'wb') as f:
        f.write(header)
        f.write(code)

    total = len(header) + code_size
    pct   = code_size * 100 // MAX_CODE_SIZE

    print(f"GP Header generated: {output_mlo}")
    print(f"  Code size  : {code_size} bytes")
    print(f"  MLO total  : {total} bytes  (header 8 + code {code_size})")
    print(f"  Load addr  : 0x{LOAD_ADDR:08X}")
    print(f"  SRAM usage : {code_size}/{MAX_CODE_SIZE} bytes ({pct}%)")
    return 0


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: gp_header.py <input.bin> <output.MLO>")
        print("Example: python3 scripts/tools/gp_header.py build/spl.bin build/MLO")
        return 1
    return make_gp_header(sys.argv[1], sys.argv[2])


if __name__ == '__main__':
    sys.exit(main())
