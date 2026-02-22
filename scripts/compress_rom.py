#!/usr/bin/env python3
"""
Compress a Game Boy ROM into .gbz format.

Output format:
  [0:8]   Magic number: b'CRBKGBgz'
  [8]     Compression scheme version: 1
  [9]     1 if original extension was .gbc, else 0
  [10:14]    CRC32 of original ROM (big-endian)
  [14:18]    Decompressed ROM size in bytes (little-endian uint32)
  [18:46]    ROM header bytes 0x134–0x14F (title + cartridge info, no Nintendo logo)
  [46:0x150] 0xFF padding
  [0x150:]   gzip-compressed ROM data
  
Note: final 4 bytes of gzip data indicate size of decompressed rom.
"""

import sys
import os
import subprocess
import pathlib
import zlib
import struct

MAGIC = b'CB\x00\xFFGBgz'
VERSION = 1
HEADER_START = 0x134  # not including logo
HEADER_END = 0x14F    # inclusive
GZ_OFFSET = 0x150

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <rom.gb|rom.gbc>", file=sys.stderr)
        sys.exit(1)

    rom_path = pathlib.Path(sys.argv[1])
    if not rom_path.exists():
        print(f"Error: file not found: {rom_path}", file=sys.stderr)
        sys.exit(1)

    ext = rom_path.suffix.lower()
    if ext not in ('.gb', '.gbc'):
        print(f"Error: expected .gb or .gbc extension, got '{rom_path.suffix}'", file=sys.stderr)
        sys.exit(1)

    is_gbc = 1 if ext == '.gbc' else 0
    out_path = rom_path.with_suffix('.gbz')

    # Read ROM: compute CRC32 and extract header slice
    with open(rom_path, 'rb') as f:
        rom_data = f.read()

    crc = zlib.crc32(rom_data) & 0xFFFFFFFF
    header = rom_data[HEADER_START:HEADER_END + 1]

    if len(header) < HEADER_END - HEADER_START + 1:
        print(f"Error: ROM file too small to contain header (got {len(header)} bytes from 0x{HEADER_START:x})", file=sys.stderr)
        sys.exit(1)

    del rom_data  # free memory before compression

    # Compress ROM with gzip
    gz_path = rom_path.with_suffix(rom_path.suffix + '.gz')
    try:
        subprocess.run(['gzip', '-k', str(rom_path)], check=True)
        with open(gz_path, 'rb') as f:
            compressed = f.read()
    finally:
        if gz_path.exists():
            gz_path.unlink()

    # Write output
    original_size = rom_path.stat().st_size
    header_section = (MAGIC + bytes([VERSION, is_gbc])
                      + struct.pack('>I', crc)
                      + struct.pack('<I', original_size)
                      + header)
    assert len(header_section) == 46
    padding = b'\xFF' * (GZ_OFFSET - len(header_section))

    with open(out_path, 'wb') as f:
        f.write(header_section)
        f.write(padding)
        f.write(compressed)

    compressed_size = out_path.stat().st_size
    ratio = compressed_size / original_size * 100
    print(f"Written: {out_path}  ({original_size:,} → {compressed_size:,} bytes, {ratio:.1f}%)")

if __name__ == '__main__':
    main()
