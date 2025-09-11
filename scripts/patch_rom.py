#!/usr/bin/env python3

import sys

def patch_rom_cgb_flag(rom_path):
    """
    Patches a Game Boy ROM by specifically turning off the CGB_ONLY flag (bit 6)
    at address 0x0143 using a bitwise operation, then recalculates and
    updates the header and global checksums.
    """
    CGB_FLAG_ADDR = 0x0143
    CGB_ONLY_MASK = 0x40  # Bit 6 is the CGB-only flag

    try:
        with open(rom_path, 'rb+') as f:
            rom_data = bytearray(f.read())

            # --- Read the original flag byte ---
            original_flag = rom_data[CGB_FLAG_ADDR]

            # --- Check if the CGB_ONLY flag is set ---
            if not (original_flag & CGB_ONLY_MASK):
                print(f"[*] Flag byte is {original_flag:#04x}. The CGB_ONLY flag is not set.")
                print("[!] No patching is necessary.")
                return

            # --- Patch the flag using a bitwise AND operation ---
            # This turns off bit 6 without affecting any other bits.
            new_flag = original_flag & ~CGB_ONLY_MASK
            rom_data[CGB_FLAG_ADDR] = new_flag

            print(f"[*] Patched CGB flag from {original_flag:#04x} to {new_flag:#04x}")

            # --- Recalculate Header Checksum ---
            header_checksum_addr = 0x014D
            old_header_checksum = rom_data[header_checksum_addr]

            header_checksum = 0
            for i in range(0x0134, 0x014D):
                header_checksum = header_checksum - rom_data[i] - 1

            rom_data[header_checksum_addr] = header_checksum & 0xFF

            print(f"[*] Header checksum updated from {old_header_checksum:#04x} to {rom_data[header_checksum_addr]:#04x}")

            # --- Recalculate Global Checksum ---
            global_checksum_addr_hi = 0x014E
            global_checksum_addr_lo = 0x014F

            # Temporarily zero out the checksum bytes for calculation
            old_global_checksum_val = (rom_data[global_checksum_addr_hi] << 8) | rom_data[global_checksum_addr_lo]
            rom_data[global_checksum_addr_hi] = 0
            rom_data[global_checksum_addr_lo] = 0

            global_checksum = sum(rom_data) & 0xFFFF

            # Write the new checksum back into the rom_data
            rom_data[global_checksum_addr_hi] = (global_checksum >> 8) & 0xFF
            rom_data[global_checksum_addr_lo] = global_checksum & 0xFF

            print(f"[*] Global checksum updated from {old_global_checksum_val:#06x} to {global_checksum:#06x}")

            # --- Write all changes back to the file ---
            f.seek(0)
            f.write(rom_data)
            print("\n[+] ROM patched successfully!")

    except FileNotFoundError:
        print(f"Error: The file '{rom_path}' was not found.")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python patch_rom.py <path_to_rom.gbc>")
        sys.exit(1)

    rom_file = sys.argv[1]
    patch_rom_cgb_flag(rom_file)
