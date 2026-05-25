#!/bin/bash
# Add pdboot to a pdx
# Usage: scripts/apply-pdboot.sh <path/to/CrankBoy.pdx>
set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 path/to/CrankBoy.pdx" >&2
    exit 1
fi

PDX="$1"

if [ ! -d "$PDX" ]; then
    echo "Error: $PDX is not a directory" >&2
    exit 1
fi

if [ ! -f "$PDX/pdex.bin" ]; then
    echo "Error: $PDX/pdex.bin not found" >&2
    exit 1
fi

if [ -f "$PDX/crankboy.bin" ]; then
    echo "Error: $PDX/crankboy.bin already exists" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CRANKBOY_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PDBOOT_DIR="$CRANKBOY_ROOT/libs/pdboot"

if [ ! -f "$PDBOOT_DIR/Makefile" ]; then
    echo "Error: pdboot submodule not found at $PDBOOT_DIR" >&2
    exit 1
fi

echo "[apply-pdboot] Building PDBoot..."
make -C "$PDBOOT_DIR"

PDBOOT_PDEX="$PDBOOT_DIR/PDBoot.pdx/pdex.bin"
if [ ! -f "$PDBOOT_PDEX" ]; then
    echo "Error: PDBoot build did not produce $PDBOOT_PDEX" >&2
    exit 1
fi

echo "[apply-pdboot] Applying PDBoot to $PDX..."
mv "$PDX/pdex.bin" "$PDX/crankboy.bin"
cp "$PDBOOT_PDEX" "$PDX/pdex.bin"
printf 'crankboy.bin\n' > "$PDX/pdboot"

echo "[apply-pdboot] Done. $PDX now ships with PDBoot pdex.bin + crankboy.bin."
