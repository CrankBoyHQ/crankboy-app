#!/usr/bin/env python3
"""
Convert a CrankBoy .sav into a raw .srm for use in RetroArch/other emulators.

CrankBoy .sav layout (see src/scenes/game_scene.c write_cart_ram_file):
  [0:sram_len]  raw SRAM
  +5            cart_rtc[5]
  (MBC9 only)   huc3.mem[128] + sub_seconds[1]
  +4            timestamp (unsigned int)
  +4            flags
  +4            patches_hash
  +8            SRAM_MAGIC_NUMBER (0x5900424B4E415243, little-endian b'CRANKB\\x00Y')

This script strips the trailing metadata so only the raw SRAM remains.
"""

import sys
import pathlib
import struct

MAGIC = b'CRANKB\x00Y'
TRAILER_SIZE = 5 + 4 + 4 + 4 + 8          # rtc + timestamp + flags + patches_hash + magic
HUc3_EXTRA = 128 + 1                       # huc3.mem + sub_seconds


def strip_sav(sav_path, huc3):
    sav_path = pathlib.Path(sav_path)

    if not sav_path.exists():
        print(f"Error: file not found: {sav_path}", file=sys.stderr)
        return False

    with open(sav_path, 'rb') as f:
        data = f.read()

    trailer = TRAILER_SIZE + (HUc3_EXTRA if huc3 else 0)

    if len(data) < trailer:
        print(f"Error: {sav_path} too small ({len(data)} bytes) to contain a trailer", file=sys.stderr)
        return False

    if data[-8:] != MAGIC:
        print(f"Warning: {sav_path} has no CrankBoy trailer magic, skipping", file=sys.stderr)
        return False

    raw = data[:-trailer]

    name = sav_path.stem
    if len(name) >= 2 and name[-2] == '.' and name[-1].isalpha():
        name = name[:-2]
    out_path = sav_path.with_name(name + '.srm')

    with open(out_path, 'wb') as f:
        f.write(raw)

    print(f"written: {out_path}  ({len(data):,} -> {len(raw):,} bytes)")
    return True


def main():
    args = sys.argv[1:]
    huc3 = False
    if '--huc3' in args:
        huc3 = True
        args.remove('--huc3')

    if not args:
        print(f"Usage: {sys.argv[0]} [--huc3] <file.sav> [file.sav ...]", file=sys.stderr)
        sys.exit(1)

    ok = True
    for arg in args:
        ok = strip_sav(arg, huc3) and ok

    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
