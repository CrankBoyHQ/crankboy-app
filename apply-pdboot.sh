#!/bin/bash
set -x
set -e

cd libs/pdboot
make -j 4
cd -

python3.11 libs/pdboot/elfboiler/elfboiler.py Source/pdex.elf CrankBoy.pdx/appA.pdb 0x60000000
python3.11 libs/pdboot/elfboiler/elfboiler.py Source/pdex.elf CrankBoy.pdx/appB.pdb 0x90000000
cp libs/pdboot/PDBoot.pdx/pdex.bin CrankBoy.pdx/pdex.bin

echo
echo "PDBoot applied. You can now launch CrankBoy.pdx"