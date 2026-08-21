#!/usr/bin/env python3
"""
wrap_image.py - Wraps a boot image with a custom CHDR header.

Wire format (16 bytes header):
  [CHDR 4B][ZEROS 4B][CRC32 4B][SIZE 4B][payload]

  AAAA AAAA = CHDR  : 4 bytes, ASCII magic
  0000 0000 = ZEROS : 4 bytes, reserved
  BBBB BBBB = CRC32 : 4 bytes, big-endian CRC32 of payload
  CCCC CCCC = SIZE  : 4 bytes, big-endian payload size in bytes

Usage:
    python3 wrap_image.py boot_new.img boot_new.wrapped
    python3 wrap_image.py boot_new.img   # outputs boot_new.img.wrapped
"""

import sys
import os
import zlib
import struct

MAGIC       = b"CHDR"
RESERVED    = b"\x00\x00\x00\x00"
HEADER_SIZE = 16


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def wrap(input_path: str, output_path: str) -> None:
    with open(input_path, "rb") as f:
        payload = f.read()

    size     = len(payload)
    checksum = crc32(payload)

    header = (
        MAGIC                          # 4 bytes  "CHDR"
        + RESERVED                     # 4 bytes  0x00000000
        + struct.pack(">I", checksum)  # 4 bytes  CRC32 big-endian
        + struct.pack(">I", size)      # 4 bytes  SIZE  big-endian
    )

    assert len(header) == HEADER_SIZE

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(payload)

    print(f"Input:    {input_path}")
    print(f"Output:   {output_path}")
    print(f"Payload:  {size} bytes  (0x{size:08X})")
    print(f"CRC32:    0x{checksum:08X}")
    print(f"Header:   {header.hex(' ')}")
    print(f"Total:    {HEADER_SIZE + size} bytes")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input_image> [output_file]")
        sys.exit(1)

    input_path  = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else input_path + ".wrapped"

    if not os.path.isfile(input_path):
        print(f"Error: {input_path} not found")
        sys.exit(1)

    wrap(input_path, output_path)


if __name__ == "__main__":
    main()
