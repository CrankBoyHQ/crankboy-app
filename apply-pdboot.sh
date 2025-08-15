#!/bin/bash
set -x
set -e

# Find a suitable python command
if command -v python3.11 &>/dev/null; then
    PYTHON_CMD=python3.11
else
    PYTHON_CMD=python3
fi

echo "Using Python command: $PYTHON_CMD"

cd libs/pdboot
make -j 4
cd -

$PYTHON_CMD libs/pdboot/elfboiler/elfboiler.py Source/pdex.elf CrankBoy.pdx/appA.pdb 0x60000000
$PYTHON_CMD libs/pdboot/elfboiler/elfboiler.py Source/pdex.elf CrankBoy.pdx/appB.pdb 0x90000000
cp libs/pdboot/PDBoot.pdx/pdex.bin CrankBoy.pdx/pdex.bin

echo
echo "PDBoot applied. You can now launch CrankBoy.pdx"
