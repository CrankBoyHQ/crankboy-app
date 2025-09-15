#!/usr/bin/env python3

import sys

# Address constants for Game Boy ROM header
CGB_FLAG_ADDR = 0x0143
CART_TYPE_ADDR = 0x0147
HEADER_CHECKSUM_ADDR = 0x014D
GLOBAL_CHECKSUM_ADDR_HI = 0x014E
GLOBAL_CHECKSUM_ADDR_LO = 0x014F

# Cartridge types that include a battery. The value is mapped to its non-battery equivalent.
# Source: Pan Docs - https://gbdev.io/pandocs/The_Cartridge_Header.html#0147---cartridge-type
BATTERY_CARTRIDGE_TYPES = {
    0x03: 0x02,  # MBC1+RAM+BATTERY -> MBC1+RAM
    0x06: 0x05,  # MBC2+BATTERY -> MBC2
    0x09: 0x08,  # MBC3+TIMER+BATTERY -> MBC3+TIMER
    0x0D: 0x0C,  # MMM01+RAM+BATTERY -> MMM01+RAM
    0x0F: 0x0E,  # MBC3+TIMER+RAM+BATTERY -> MBC3+TIMER+RAM (Pocket Cam)
    0x10: 0x0E,  # MBC3+TIMER+RAM+BATTERY -> MBC3+TIMER+RAM
    0x13: 0x12,  # MBC3+RAM+BATTERY -> MBC3+RAM
    0x1B: 0x1A,  # MBC5+RAM+BATTERY -> MBC5+RAM
    0x1E: 0x1D,  # MBC5+RUMBLE+RAM+BATTERY -> MBC5+RUMBLE+RAM
    0x22: 0x21,  # MBC7+SENSOR+RUMBLE+RAM+BATTERY -> MBC7+...
    0xFF: 0xFE,  # HuC1+RAM+BATTERY -> HuC1+RAM
}

def patch_cgb_flag(rom_data):
    """Turns off the CGB_ONLY flag (bit 6) at address 0x0143."""
    original_flag = rom_data[CGB_FLAG_ADDR]
    cgb_only_mask = 0x40  # 0b01000000

    if not (original_flag & cgb_only_mask):
        print("[*] CGB_ONLY flag is not set. No patching needed.")
        return False

    new_flag = original_flag & ~cgb_only_mask
    rom_data[CGB_FLAG_ADDR] = new_flag
    print(f"[*] Patched CGB flag from {original_flag:#04x} to {new_flag:#04x}")
    return True

def remove_battery(rom_data):
    """Removes battery capability by changing the cartridge type at 0x0147."""
    original_type = rom_data[CART_TYPE_ADDR]

    if original_type not in BATTERY_CARTRIDGE_TYPES:
        print(f"[*] Cartridge type is {original_type:#04x}. This type does not have a battery or is not supported. No patching needed.")
        return False

    new_type = BATTERY_CARTRIDGE_TYPES[original_type]
    rom_data[CART_TYPE_ADDR] = new_type
    print(f"[*] Patched cartridge type from {original_type:#04x} to {new_type:#04x} (Removed battery).")
    return True

def update_checksums(rom_data):
    """Recalculates and updates both the header and global checksums."""
    # --- Recalculate Header Checksum ---
    old_header_checksum = rom_data[HEADER_CHECKSUM_ADDR]
    header_checksum = 0
    for i in range(0x0134, 0x014D):
        header_checksum = header_checksum - rom_data[i] - 1
    rom_data[HEADER_CHECKSUM_ADDR] = header_checksum & 0xFF
    print(f"[*] Header checksum updated from {old_header_checksum:#04x} to {rom_data[HEADER_CHECKSUM_ADDR]:#04x}")

    # --- Recalculate Global Checksum ---
    old_global_checksum_val = (rom_data[GLOBAL_CHECKSUM_ADDR_HI] << 8) | rom_data[GLOBAL_CHECKSUM_ADDR_LO]

    # Temporarily zero out checksum bytes for calculation
    rom_data_for_checksum = bytearray(rom_data)
    rom_data_for_checksum[GLOBAL_CHECKSUM_ADDR_HI] = 0
    rom_data_for_checksum[GLOBAL_CHECKSUM_ADDR_LO] = 0

    global_checksum = sum(rom_data_for_checksum) & 0xFFFF

    rom_data[GLOBAL_CHECKSUM_ADDR_HI] = (global_checksum >> 8) & 0xFF
    rom_data[GLOBAL_CHECKSUM_ADDR_LO] = global_checksum & 0xFF
    print(f"[*] Global checksum updated from {old_global_checksum_val:#06x} to {global_checksum:#06x}")

def patch_rom(rom_path, patch_type):
    """
    Main function to patch a Game Boy ROM. It applies a selected patch
    and then updates the necessary checksums.
    """
    try:
        with open(rom_path, 'rb+') as f:
            rom_data = bytearray(f.read())
            patch_applied = False

            if patch_type == 'cgb':
                print("--- Applying CGB Flag Patch ---")
                patch_applied = patch_cgb_flag(rom_data)
            elif patch_type == 'battery':
                print("--- Applying Battery Removal Patch ---")
                patch_applied = remove_battery(rom_data)
            else:
                print(f"Error: Unknown patch type '{patch_type}'.")
                return

            if patch_applied:
                print("\n--- Updating Checksums ---")
                update_checksums(rom_data)

                # --- Write all changes back to the file ---
                f.seek(0)
                f.write(rom_data)
                print("\n[+] ROM patched successfully!")
            else:
                print("\n[!] No changes were made to the ROM.")

    except FileNotFoundError:
        print(f"Error: The file '{rom_path}' was not found.")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python patch_rom.py <path_to_rom.gbc> <patch_type>")
        print("Available patch types: 'cgb', 'battery'")
        sys.exit(1)

    rom_file = sys.argv[1]
    patch_action = sys.argv[2].lower()

    patch_rom(rom_file, patch_action)
