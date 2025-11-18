/**
 * MIT License
 *
 * Copyright (c) 2018-2022 Mahyar Koshkouei
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Please note that at least three parts of source code within this project was
 * taken from the SameBoy project at https://github.com/LIJI32/SameBoy/ which at
 * the time of this writing is released under the MIT License. Occurrences of
 * this code is marked as being taken from SameBoy with a comment.
 * SameBoy, and code marked as being taken from SameBoy,
 * is Copyright (c) 2015-2019 Lior Halphon.
 */

#ifndef PEANUT_GB_H
#define PEANUT_GB_H

#include "../src/app.h"
#include "../src/preferences.h"
#include "../src/utility.h"

#include <stddef.h> /* Required for offsetof */
#include <stdint.h> /* Required for int types */
#include <stdlib.h> /* Required for abort */
#include <string.h> /* Required for memset */
#include <time.h>   /* Required for tm struct */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;

// color game boy support
#ifndef PGB_CGB
#define PGB_CGB 0
#endif

/**
 * Sound support must be provided by an external library. When audio_read() and
 * audio_write() functions are provided, define ENABLE_SOUND to a non-zero value
 * before including peanut_gb.h in order for these functions to be used.
 */
#ifndef ENABLE_SOUND
#define ENABLE_SOUND 1
#endif

/* Enable LCD drawing. On by default. May be turned off for testing purposes. */
#ifndef ENABLE_LCD
#define ENABLE_LCD 1
#endif

/* Interrupt masks */
#define VBLANK_INTR 0x01
#define LCDC_INTR 0x02
#define TIMER_INTR 0x04
#define SERIAL_INTR 0x08
#define CONTROL_INTR 0x10
#define ANY_INTR 0x1F

/* Memory section sizes for DMG */
#define WRAM_SIZE 0x2000
#define WRAM_SIZE_CGB 0x8000
#define VRAM_SIZE 0x2000
#define VRAM_SIZE_CGB 0x4000
#define XRAM_SIZE (0x100 - 0xA0)
#define HRAM_SIZE 0x0100
#define OAM_SIZE 0x00A0

#define ROM_HEADER_START 0x134
#define ROM_HEADER_SIZE (0x150 - ROM_HEADER_START)

/* Memory addresses */
#define ROM_0_ADDR 0x0000
#define ROM_N_ADDR 0x4000
#define VRAM_ADDR 0x8000
#define CART_RAM_ADDR 0xA000
#define WRAM_0_ADDR 0xC000
#define WRAM_1_ADDR 0xD000
#define ECHO_ADDR 0xE000
#define OAM_ADDR 0xFE00
#define UNUSED_ADDR 0xFEA0
#define IO_ADDR 0xFF00
#define HRAM_ADDR 0xFF80
#define INTR_EN_ADDR 0xFFFF

/* Cart section sizes */
#define ROM_BANK_SIZE 0x4000
#define WRAM_BANK_SIZE 0x1000
#define CRAM_BANK_SIZE 0x2000
#define VRAM_BANK_SIZE 0x2000

/* DIV Register is incremented at rate of 16384Hz.
 * 4194304 / 16384 = 256 clock cycles for one increment. */
#define DIV_CYCLES 256

/* Serial clock locked to 8192Hz on DMG.
 * 4194304 / (8192 / 8) = 4096 clock cycles for sending 1 byte. */
#define SERIAL_CYCLES 4096

/* Calculating VSYNC. */
#ifndef DMG_CLOCK_FREQ
#define DMG_CLOCK_FREQ 4194304.0f
#endif

#ifndef SCREEN_REFRESH_CYCLES
#define SCREEN_REFRESH_CYCLES 70224.0f
#endif

#define VERTICAL_SYNC (DMG_CLOCK_FREQ / SCREEN_REFRESH_CYCLES)

/* SERIAL SC register masks. */
#define SERIAL_SC_TX_START 0x80
#define SERIAL_SC_CLOCK_SRC 0x01

/* STAT register masks */
#define STAT_LYC_INTR 0x40
#define STAT_MODE_2_INTR 0x20
#define STAT_MODE_1_INTR 0x10
#define STAT_MODE_0_INTR 0x08
#define STAT_LYC_COINC 0x04
#define STAT_MODE 0x03
#define STAT_USER_BITS 0xF8

/* LCDC control masks */
#define LCDC_ENABLE 0x80
#define LCDC_WINDOW_MAP 0x40
#define LCDC_WINDOW_ENABLE 0x20
#define LCDC_TILE_SELECT 0x10
#define LCDC_BG_MAP 0x08
#define LCDC_OBJ_SIZE 0x04
#define LCDC_OBJ_ENABLE 0x02
#define LCDC_BG_ENABLE 0x01
#define LCDC_CGB_MASTER_PRIORITY 0x01

/* CGB BG map tile attributes */
#define BG_MAP_ATTR_PRIORITY 0x80
#define BG_MAP_ATTR_Y_FLIP 0x40
#define BG_MAP_ATTR_X_FLIP 0x20
#define BG_MAP_ATTR_BANK 0x08
#define BG_MAP_ATTR_PALETTE 0x07

/* LCD characteristics */
#define LCD_VERT_LINES 154
#define LCD_LINE_CYCLES 456
#define LCD_FRAME_CYCLES (LCD_LINE_CYCLES * LCD_VERT_LINES)
#define LCD_WIDTH 160
#define LCD_PACKING 4 /* pixels per byte */
#define LCD_BITS_PER_PIXEL (8 / LCD_PACKING)
#define LCD_WIDTH_PACKED (LCD_WIDTH / LCD_PACKING)
#define LCD_HEIGHT 144
#define LCD_BUFFER_BYTES (LCD_HEIGHT * LCD_WIDTH_PACKED)

/* Simplified PPU timing model for performance */
#define PPU_MODE_2_OAM_CYCLES 80
#define PPU_MODE_3_VRAM_MIN_CYCLES 172
#define PPU_MODE_3_VRAM_MAX_CYCLES 289

/* VRAM Locations */
#define VRAM_TILES_1 (0x8000 - VRAM_ADDR)
#define VRAM_TILES_2 (0x8800 - VRAM_ADDR)
#define VRAM_BMAP_1 (0x9800 - VRAM_ADDR)
#define VRAM_BMAP_2 (0x9C00 - VRAM_ADDR)
#define VRAM_TILES_3 (0x8000 - VRAM_ADDR + VRAM_BANK_SIZE)
#define VRAM_TILES_4 (0x8800 - VRAM_ADDR + VRAM_BANK_SIZE)

/* Interrupt jump addresses */
#define VBLANK_INTR_ADDR 0x0040
#define LCDC_INTR_ADDR 0x0048
#define TIMER_INTR_ADDR 0x0050
#define SERIAL_INTR_ADDR 0x0058
#define CONTROL_INTR_ADDR 0x0060

/* SPRITE controls */
#define NUM_SPRITES 0x28
#define MAX_SPRITES_LINE 0x0A
#define OBJ_PRIORITY 0x80
#define OBJ_FLIP_Y 0x40
#define OBJ_FLIP_X 0x20
#define OBJ_PALETTE 0x10
#define OBJ_CGB_BANK 0x08
#define OBJ_CGB_PALETTE 0x07

#define ROM_HEADER_CHECKSUM_LOC 0x014D

#define CB_HW_BREAKPOINT_OPCODE 0xD3
#define MAX_BREAKPOINTS 0x80

#define PEANUT_GB_ARRAYSIZE(array) (sizeof(array) / sizeof(array[0]))

#define CB_SAVE_STATE_MAGIC "\xFA\x43\42sav\n\x1A"
#define CB_SAVE_STATE_VERSION PGB_VERSION

#define IO_PLAYDATE_EXTENSION_CTL 0x57
#define IO_PLAYDATE_EXTENSION_CRANK_LO 0x58
#define IO_PLAYDATE_EXTENSION_CRANK_HI 0x59
#define IO_PLAYDATE_EXTENSION_ACCX_LO 0x5A
#define IO_PLAYDATE_EXTENSION_ACCX_HI 0x5B
#define IO_PLAYDATE_EXTENSION_ACCY_LO 0x5C
#define IO_PLAYDATE_EXTENSION_ACCY_HI 0x5D
#define IO_PLAYDATE_EXTENSION_ACCZ_LO 0x5E
#define IO_PLAYDATE_EXTENSION_ACCZ_HI 0x5F

#if ENABLE_LCD
/* Bit mask for the shade of pixel to display */
#define LCD_COLOUR 0x03
/**
 * Bit mask for whether a pixel is OBJ0, OBJ1, or BG. Each may have a different
 * palette when playing a DMG game on CGB.
 */
#define LCD_PALETTE_OBJ 0x4
#define LCD_PALETTE_BG 0x8
/**
 * Bit mask for the two bits listed above.
 * LCD_PALETTE_ALL == 0b00 --> OBJ0
 * LCD_PALETTE_ALL == 0b01 --> OBJ1
 * LCD_PALETTE_ALL == 0b10 --> BG
 * LCD_PALETTE_ALL == 0b11 --> NOT POSSIBLE
 */
#define LCD_PALETTE_ALL 0x30
#endif

/**
 * Errors that may occur during emulation.
 */
enum gb_error_e
{
    GB_UNKNOWN_ERROR,
    GB_INVALID_OPCODE,
    GB_INVALID_READ,
    GB_INVALID_WRITE,

    GB_INVALID_MAX
};

/**
 * Errors that may occur during library initialisation.
 */
enum gb_init_error_e
{
    GB_INIT_NO_ERROR,
    GB_INIT_NO_ERROR_BUT_REQUIRES_CGB,
    GB_INIT_CARTRIDGE_UNSUPPORTED,
    GB_INIT_INVALID_CHECKSUM
};

/**
 * Return codes for serial receive function, mainly for clarity.
 */
enum gb_serial_rx_ret_e
{
    GB_SERIAL_RX_SUCCESS = 0,
    GB_SERIAL_RX_NO_CONNECTION = 1
};

// NOTE: header struct is shared between save state version,
// so we must keep the size consistent and not reorder fields.
// (_reserved can be shrunk.)
typedef struct StateHeader
{
    char magic[8];
    u32 version;

    // emulator architecture
    uint8_t big_endian : 1;
    uint8_t bits : 4;

    // indicates if a script is active
    uint8_t script : 1;

    // indicates if cgb mode is active
    uint8_t cgb : 1;

    // Custom field for CrankBoy timestamp.
    uint32_t timestamp;

    // Size of the gb_s struct (for verification.)
    uint32_t gb_s_size;

    // amount of data stored for the script
    uint32_t script_save_data_size;

    // for use in future versions
    char _reserved[12];
} StateHeader;

#ifdef PGB_IMPL
#define PGB_SAVESTATE_UPGRADE_IMPL
#endif

// ---------------------
// On struct version update, please change these two lines
#include "pgb/pgb_v3.h"
#define PGB_VERSION 3
// ---------------------

typedef struct PGB_VERSIONED(gb_s) gb_s;
typedef struct PGB_VERSIONED(gb_breakpoint) gb_breakpoint;
typedef struct PGB_VERSIONED(audio_data) audio_data;
typedef struct PGB_VERSIONED(chan_len_ctr) chan_len_ctr;
typedef struct PGB_VERSIONED(chan_vol_env) chan_vol_env;
typedef struct PGB_VERSIONED(chan_freq_sweep) chan_freq_sweep;
typedef struct PGB_VERSIONED(chan) chan;

void gb_step_cpu(gb_s* gb);
void gb_run_frame(gb_s* gb);

enum cgb_support_e gb_get_models_supported(uint8_t* gb_rom);
bool gb_get_rom_uses_battery(uint8_t* gb_rom);

#ifdef PGB_IMPL

#include "minigb_apu/minigb_apu.h"

// relocatable and tightly-packed interpreter code
#ifdef TARGET_SIMULATOR
#define __core_dmg
#define __core_dmg_section(x)
#define __core_cgb
#define __core_cgb_section(x)
#else
#ifdef ITCM_CORE
#define __core_dmg                                                        \
    __attribute__((optimize("Os"))) __attribute__((section(".itcm.dmg"))) \
    __attribute__((short_call))
#define __core_dmg_section(x)                                                \
    __attribute__((optimize("Os"))) __attribute__((section(".itcm.dmg." x))) \
    __attribute__((short_call))
#define __core_cgb                                                        \
    __attribute__((optimize("Os"))) __attribute__((section(".itcm.cgb"))) \
    __attribute__((short_call))
#define __core_cgb_section(x)                                                \
    __attribute__((optimize("Os"))) __attribute__((section(".itcm.cgb." x))) \
    __attribute__((short_call))
#else
#define __core_dmg __attribute__((optimize("Os"))) __attribute__((section(".text.itcm.dmg")))
#define __core_dmg_section(x) __core_dmg
#define __core_cgb __attribute__((optimize("Os"))) __attribute__((section(".text.itcm.cgb")))
#define __core_cgb_section(x) __core_cgb
#endif
#endif

__core_cgb static void __gb_check_lyc__cgb(gb_s* gb);
__core_cgb static void __gb_update_stat_irq__cgb(gb_s* gb);

__core_dmg static unsigned __gb_run_instruction_micro__dmg(gb_s* gb);
__core_cgb static unsigned __gb_run_instruction_micro__cgb(gb_s* gb);

__core_dmg static uint8_t __gb_execute_cb__dmg(gb_s* gb);
__core_cgb static uint8_t __gb_execute_cb__cgb(gb_s* gb);

__core_dmg_section("short") static void __gb_write16__dmg(gb_s* restrict gb, u16 addr, u16 v);
__core_cgb_section("short") static void __gb_write16__cgb(gb_s* restrict gb, u16 addr, u16 v);

__core_dmg_section("short") static uint16_t __gb_read16__dmg(gb_s* restrict gb, u16 addr);
__core_cgb_section("short") static uint16_t __gb_read16__cgb(gb_s* restrict gb, u16 addr);

__core_dmg_section("short") static uint16_t __gb_fetch16__dmg(gb_s* restrict gb);
__core_cgb_section("short") static uint16_t __gb_fetch16__cgb(gb_s* restrict gb);

__core_dmg_section("short") static void __gb_push16__dmg(gb_s* restrict gb, u16 v);
__core_cgb_section("short") static void __gb_push16__cgb(gb_s* restrict gb, u16 v);

static void __gb_write__cgb(gb_s* restrict gb, const uint16_t addr, uint8_t v);
static void __gb_write__dmg(gb_s* restrict gb, const uint16_t addr, uint8_t v);

static uint8_t __gb_read__cgb(gb_s* gb, const uint16_t addr);
static uint8_t __gb_read__dmg(gb_s* gb, const uint16_t addr);

void __gb_on_breakpoint(gb_s* gb, int breakpoint_number);
void __gb_dump_vram(gb_s* gb);

enum cgb_support_e gb_get_models_supported(uint8_t* gb_rom)
{
    uint8_t cgb_byte = gb_rom[0x143];
    if (cgb_byte == 0x80)
        return GB_SUPPORT_DMG_AND_CGB;
    if (cgb_byte == 0xC0)
        return GB_SUPPORT_CGB;

    return GB_SUPPORT_DMG;
}

__section__(".rare") bool gb_get_rom_uses_battery(uint8_t* gb_rom)
{
    const uint8_t cart_battery[] = {
        0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, /* 00-0F */
        1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, /* 10-1F */
        1, 0, 1                                         /* 20-2F */
    };

    return cart_battery[gb_rom[0x0147]];
}

/**
 * Returns the title of ROM.
 *
 * \param gb        Initialised context.
 * \param title_str Allocated string at least 16 characters.
 * \returns         Pointer to start of string, null terminated.
 */
const char* gb_get_rom_name(uint8_t* gb_rom, char* title_str)
{
    uint_fast16_t title_loc = 0x134;
    /* End of title may be 0x13E for newer games. */
    const uint_fast16_t title_end = 0x143;
    const char* title_start = title_str;

    for (; title_loc <= title_end; title_loc++)
    {
        const char title_char = gb_rom[title_loc];

        if (title_char >= ' ' && title_char <= '~')
        {
            *title_str = title_char;
            title_str++;
        }
        else
            break;
    }

    *title_str = '\0';
    return title_start;
}

/**
 * Directly calculates and applies the RTC state after a given
 * number of seconds have passed.
 *
 * This is a replacement for calling gb_tick_rtc() in a loop.
 * The logic is inspired by SameBoy's RTC implementation.
 *
 */
__section__(".text.cb") void gb_catch_up_rtc_direct(gb_s* gb, unsigned int seconds_to_add)
{
    if ((gb->rtc_bits.high & 0x40) || seconds_to_add == 0)
    {
        return;
    }

    uint16_t current_days = gb->rtc_bits.yday | ((gb->rtc_bits.high & 0x01) << 8);

    unsigned long long total_seconds = gb->rtc_bits.sec + gb->rtc_bits.min * 60ULL +
                                       gb->rtc_bits.hour * 3600ULL + current_days * 86400ULL;

    total_seconds += seconds_to_add;

    uint8_t new_sec = total_seconds % 60;
    total_seconds /= 60;
    uint8_t new_min = total_seconds % 60;
    total_seconds /= 60;
    uint8_t new_hour = total_seconds % 24;
    total_seconds /= 24;
    uint16_t new_days = total_seconds;

    uint8_t day_overflow = (new_days > 511);

    new_days %= 512;

    gb->rtc_bits.sec = new_sec;
    gb->rtc_bits.min = new_min;
    gb->rtc_bits.hour = new_hour;
    gb->rtc_bits.yday = (uint8_t)(new_days & 0xFF);

    uint8_t high_byte = gb->rtc_bits.high & 0x40;
    high_byte |= (new_days >> 8) & 0x01;
    if (day_overflow)
    {
        high_byte |= 0x80;
    }
    gb->rtc_bits.high = high_byte;
}

/**
 * Tick the internal RTC by one second.
 * This was taken from SameBoy, which is released under MIT Licence.
 *
 * NOTE: This function is currently unused in favor of the more performant
 * gb_catch_up_rtc_direct() function. It is kept for reference and potential
 * future use in a cycle-accurate timing model.
 */
__section__(".text.cb") void gb_tick_rtc(gb_s* gb)
{
    /* is timer running? */
    if ((gb->cart_rtc[4] & 0x40) == 0)
    {
        if (++gb->rtc_bits.sec == 60)
        {
            gb->rtc_bits.sec = 0;

            if (++gb->rtc_bits.min == 60)
            {
                gb->rtc_bits.min = 0;

                if (++gb->rtc_bits.hour == 24)
                {
                    gb->rtc_bits.hour = 0;

                    if (++gb->rtc_bits.yday == 0)
                    {
                        if (gb->rtc_bits.high & 1) /* Bit 8 of days*/
                        {
                            gb->rtc_bits.high |= 0x80; /* Overflow bit */
                        }

                        gb->rtc_bits.high ^= 1;
                    }
                }
            }
        }
    }
}

u8 reverse_bits_u8(u8 b);

/**
 * Set initial values in RTC.
 * Should be called after gb_init().
 */
__section__(".text.cb") void gb_set_rtc(gb_s* gb, const struct tm* const time)
{
    gb->cart_rtc[0] = time->tm_sec;
    gb->cart_rtc[1] = time->tm_min;
    gb->cart_rtc[2] = time->tm_hour;
    gb->cart_rtc[3] = time->tm_yday & 0xFF; /* Low 8 bits of day counter. */

    // Preserve control flags (bits 7-1) and only set the day counter's high bit (bit 0).
    uint8_t high_byte = gb->rtc_bits.high;
    high_byte &= ~0x01;                       /* Clear the old 9th day bit. */
    high_byte |= (time->tm_yday >> 8) & 0x01; /* Set the new 9th day bit. */

    gb->rtc_bits.high = high_byte;

    // Copy these initial values to the latched registers to ensure
    // the very first read by the game gets the correct time.
    memcpy(gb->latched_rtc, gb->cart_rtc, sizeof(gb->latched_rtc));
}

__section__(".text.cb") static void __gb_update_tac(gb_s* gb)
{
    static const uint8_t TAC_CYCLES[4] = {10, 4, 6, 8};

    // subtract 1 so it can be used as a mask for quick modulo.
    gb->gb_reg.tac_cycles_shift = TAC_CYCLES[gb->gb_reg.tac_rate];
    gb->gb_reg.tac_cycles = (1 << (int)TAC_CYCLES[gb->gb_reg.tac_rate]) - 1;
}

__section__(".text.cb") static void __gb_update_selected_bank_addr(gb_s* gb)
{
    // swappable cartridge ROM bank
    int32_t offset = ((int)(gb->selected_rom_bank & gb->num_rom_banks_mask) - 1) * ROM_BANK_SIZE;
    gb->selected_bank_addr = gb->gb_rom + offset;

    // swappable cgb wram bank
    int wram_bank = 1;
    if (gb->is_cgb_mode && gb->cgb_wram_bank >= 2)
    {
        wram_bank = gb->cgb_wram_bank;
    }
    gb->wram_base[1] = gb->wram - WRAM_1_ADDR + 0x1000 * wram_bank;

    // swappable cgb vram bank
    int vram_bank = 0;
    if (gb->is_cgb_mode)
        vram_bank = gb->cgb_vram_bank;
    gb->vram_base = gb->vram - VRAM_ADDR + VRAM_SIZE * vram_bank;
}

__section__(".text.cb") static void __gb_update_zero_bank_addr(gb_s* gb)
{
    gb->gb_zero_bank = gb->gb_rom + gb->zero_bank_base;
}

__section__(".text.cb") static void __gb_update_selected_cart_bank_addr(gb_s* gb)
{
    // NULL indicates special access, must do _full version
    gb->selected_cart_bank_addr = NULL;
    if (gb->enable_cart_ram && gb->num_ram_banks > 0)
    {
        if (gb->mbc == 3 && gb->cart_ram_bank >= 0x8)
        {
            gb->selected_cart_bank_addr = NULL;
        }
        else if ((gb->cart_mode_select || gb->mbc != 1) && gb->cart_ram_bank < gb->num_ram_banks)
        {
            gb->selected_cart_bank_addr = gb->gb_cart_ram + (gb->cart_ram_bank * CRAM_BANK_SIZE);
        }
        else
        {
            gb->selected_cart_bank_addr = gb->gb_cart_ram;
        }
    }

    if (gb->selected_cart_bank_addr)
    {
        // so that accesses don't need to subtract 0xA000
        gb->selected_cart_bank_addr -= 0xA000;
    }
}

__section__(".text.cb") static void __gb_init_memory_pointers(gb_s* gb)
{
    gb->wram_base[0] = gb->wram - WRAM_0_ADDR;
    gb->wram_base[1] = gb->wram - WRAM_1_ADDR + 0x1000;
    gb->echo_ram_base = gb->wram - ECHO_ADDR;
    gb->vram_base = gb->vram - VRAM_ADDR;
}

__section__(".text.cb") static void __gb_update_map_pointers(gb_s* gb)
{
    gb->display.bg_map_base =
        gb->vram + ((gb->gb_reg.LCDC & LCDC_BG_MAP) ? VRAM_BMAP_2 : VRAM_BMAP_1);

    gb->display.window_map_base =
        gb->vram + ((gb->gb_reg.LCDC & LCDC_WINDOW_MAP) ? VRAM_BMAP_2 : VRAM_BMAP_1);
}

/* Detect MBC1M (multi-cart) by scanning for a Nintendo logo
 * at 0x0104 in banks 0x10/0x20/0x30 when ROM size >= 512 KiB. */
__section__(".rare") static uint8_t __gb_detect_mbc1m(const gb_s* gb)
{
    if (gb->mbc != 1 || gb->gb_rom_size < 0x80000)
        return false;

    static const uint8_t logo[] = {0xCE, 0xED, 0x66, 0x66};
    static const int banks_to_check[] = {0x10, 0x20, 0x30};

    for (int i = 0; i < 3; i++)
    {
        size_t off = (size_t)banks_to_check[i] * ROM_BANK_SIZE + 0x0104;
        if (off + sizeof(logo) <= gb->gb_rom_size &&
            memcmp(&gb->gb_rom[off], logo, sizeof(logo)) == 0)
        {
            return 1;
        }
    }
    return 0;
}

__shell static void __gb_do_hdma(gb_s* gb)
{
    int hdma_remaning = ((int8_t)gb->cgb_hdma_len);

    uint16_t src = gb->cgb_hdma_src;
    uint16_t dst = VRAM_ADDR | (gb->cgb_hdma_dst % VRAM_SIZE);
    // OPTIMIZE: common sources
    for (int i = 0; i < 0x10; ++i)
    {
        // FIXME: bugs when this is an unusual address
        // NOTE: we use __cgb version because hdma is only possible on CGB.
        uint8_t v = __gb_read__cgb(gb, src);
        if (dst < 0x9800)
        {
            v = reverse_bits_u8(v);
        }
        gb->vram_base[dst] = v;
        src++;
        dst++;
    }

    gb->cgb_hdma_len = hdma_remaning - 1;
    gb->cgb_hdma_active = hdma_remaning > 0;

    // TODO: verify
    gb->cgb_hdma_src += 0x10;
    gb->cgb_hdma_dst += 0x10;
}

__section__(".rare.cb") static void __gb_rare_write(
    gb_s* gb, const uint16_t addr, const uint8_t val
)
{
    // unused memory area
    if (addr >= 0xFEA0 && addr < 0xFF00)
    {
        if (gb->direct.enable_xram)
        {
            gb->xram[addr - 0xFEA0] = val;
        }
        return;
    }

    if ((addr >> 8) == 0xFF)
    {
        switch (addr & 0xFF)
        {
        // On a DMG, these writes are ignored. This list is expanded to include
        // all CGB-only registers that the game is attempting to write to.
        case 0x4C:  // KEY0 (CGB Undocumented)
            return;
        case 0x4D:  // KEY1 (CGB Speed Switch)

        case 0x4F:  // VBK (CGB VRAM Bank)
            if (gb->is_cgb_mode)
            {
                gb->cgb_vram_bank = val;
                __gb_update_selected_bank_addr(gb);
            }
            return;
        case 0x52:  // HDMA src lo
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_src &= 0xFF00;
                gb->cgb_hdma_src |= val & 0xF0;
            }
            return;
        case 0x51:  // HDMA src hi
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_src &= 0x00FF;
                gb->cgb_hdma_src |= ((unsigned)val & 0xF0) << 8;
            }
            return;
        case 0x54:  // HDMA dst lo
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_dst &= 0xFF00;
                gb->cgb_hdma_dst |= val;
            }
            return;
        case 0x53:  // HDMA dst hi
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_dst &= 0x00FF;
                gb->cgb_hdma_dst |= ((unsigned)val) << 8;
            }
            return;
        case 0x55:  // HDMA5 (VRAM DMA)
            if (gb->is_cgb_mode)
            {
                int was_len = gb->cgb_hdma_len;
                gb->cgb_hdma_len = val & 0x7F;
                bool was_active = gb->cgb_hdma_active;
                gb->cgb_hdma_active = (val >> 7);

                if (!gb->cgb_hdma_active && was_active)
                {
                    playdate->system->logToConsole(
                        "active HDMA stopped, pc=%x, len was %d", gb->cpu_reg.pc, was_len
                    );
                }
                else
                {
                    if (gb->cgb_hdma_active)
                    {
                        playdate->system->logToConsole(
                            "HDMA (async) 0x%x -> 0x%x, len=%d, pc=%x", gb->cgb_hdma_src,
                            gb->cgb_hdma_dst, gb->cgb_hdma_len, gb->cpu_reg.pc
                        );
                    }
                    else
                    {
                        if (gb->cgb_hdma_len != -1)  // TODO: double-check this condition
                        {
                            playdate->system->logToConsole(
                                "HDMA 0x%x -> 0x%x, len=%d, pc=%x", gb->cgb_hdma_src,
                                gb->cgb_hdma_dst, gb->cgb_hdma_len, gb->cpu_reg.pc
                            );
                            gb->cgb_hdma_active = true;
                            while (gb->cgb_hdma_active)
                                __gb_do_hdma(gb);
                        }
                        else
                        {
                            // TODO: is this real behaviour?
                            playdate->system->logToConsole(
                                "HDMA (null) 0x%x -> 0x%x, pc=%x", gb->cgb_hdma_src,
                                gb->cgb_hdma_dst, gb->cpu_reg.pc
                            );
                        }
                    }
                }
            }
            return;
        case 0x56:  // RP (CGB Infrared Port)
            return;
        case 0x68:  // BCPS (CGB BG Palette Spec)
        case 0x69:  // BCPD (CGB BG Palette Data)
        case 0x6A:  // OCPS (CGB OBJ Palette Spec)
        case 0x6B:  // OCPD (CGB OBJ Palette Data)
            return;
        case 0x76:  // PCM12 (CGB Audio)
        case 0x77:  // PCM34 (CGB Audio)
            return;

        // Undocumented CGB registers
        case 0x6C:  // OPRI (CGB Object priority mode)
            if (gb->is_cgb_mode)
            {
                gb->cgb_ff6c = val;
            }
            return;
        case 0x72:
        case 0x73:
        case 0x74:
            if (gb->is_cgb_mode)
            {
                gb->cgb_ff7x[(addr & 0xFF) - 0x72] = val;
            }
            return;
        case 0x75:
            if (gb->is_cgb_mode)
            {
                gb->cgb_ff75 = val >> 4;
            }
            return;

        case 0x70:  // SVBK (CGB WRAM Bank)
            gb->cgb_wram_bank = val & 7;
            __gb_update_selected_bank_addr(gb);
            return;

        case 0x50:
            /* Turn off boot ROM (not supported) */
            return;

        case IO_PLAYDATE_EXTENSION_CTL:
            // bit 0: accelerometer
            playdate->system->logToConsole("Set accelerometer enabled: %d", val & 1);
            playdate->system->setPeripheralsEnabled((val & 1) ? kAccelerometer : kNone);

            // bit 1: gb->xram
            gb->direct.enable_xram = !!(val & 2);

            // bit 2: crank menu indexing mode
            gb->direct.ext_crank_menu_indexing = !!(val & 4);
            return;

        case IO_PLAYDATE_EXTENSION_CRANK_LO:
            // reset crank menu delta
            gb->direct.crank_menu_delta = 0;
            return;

        case IO_PLAYDATE_EXTENSION_CRANK_HI:
            // reset crank menu delta accumulation
            gb->direct.crank_menu_accumulation = 0x8000;
            return;

        /* Interrupt Enable Register */
        case 0xFF:
            gb->gb_reg.IE = val;
            gb->direct.joypad_interrupts = (val & CONTROL_INTR) != 0;
            return;
        }
    }

    (gb->gb_error)(gb, GB_INVALID_WRITE, addr);
}

__section__(".rare.cb") static uint8_t __gb_rare_read(gb_s* gb, const uint16_t addr)
{
    if (addr >= 0xFEA0 && addr < 0xFF00)
    {
        if (gb->direct.enable_xram)
        {
            return gb->xram[addr - 0xFEA0];
        }
        else
        {
            return 0x00;
        }
    }

    if ((addr >> 8) == 0xFF)
    {
        switch (addr & 0xFF)
        {
            // unimplemented CGB-only registers. On a DMG, reading these returns 0xFF.

        case 0x56:  // RP (CGB Infrared Port)
        case 0x68:  // BCPS (CGB BG Palette Spec)
        case 0x69:  // BCPD (CGB BG Palette Data)
        case 0x6A:  // OCPS (CGB OBJ Palette Spec)
        case 0x6B:  // OCPD (CGB OBJ Palette Data)
        case 0x76:  // PCM12 (CGB Audio)
        case 0x77:  // PCM34 (CGB Audio)
            return 0xFF;

            // CGB registers

        case 0x4C:        // KEY0 (CGB Undocumented)
            return 0xFF;  // TODO: (?) should differ if running as cgb in compatability mode

        case 0x4D:  // KEY1 (CGB Speed Switch)
            if (gb->is_cgb_mode)
            {
                return 0x7E | (gb->cgb_fast_mode << 7) | (gb->cgb_fast_mode_armed);
            }
            return 0xFF;

        case 0x4F:  // VBK (CGB VRAM Bank)
            if (gb->is_cgb_mode)
            {
                return 0xFE | gb->cgb_vram_bank;
            }
            return 0xFF;

        case 0x51:        // HDMA1
        case 0x52:        // HDMA2
        case 0x53:        // HDMA3
        case 0x54:        // HDMA4
            return 0xFF;  // (confirmed)

        case 0x55:  // HDMA5 (VRAM DMA)
            if (gb->is_cgb_mode)
            {
                return ((uint8_t)gb->cgb_hdma_len & 0x7F) | ((gb->cgb_hdma_active) ? 0 : 0x80);
            }
            return 0xFF;

        case 0x6C:
            if (gb->is_cgb_mode)
            {
                return 0xFE | gb->cgb_ff6c;
            }
            return 0xFF;
        case 0x70:  // SVBK (CGB WRAM Bank)
            if (gb->is_cgb_mode)
            {
                return (~7) | MAX(1, gb->cgb_wram_bank);
            }
            else
                return 0xFF;

        // Undocumented CGB registers
        case 0x72:
        case 0x73:
        case 0x74:
            if (gb->is_cgb_mode)
            {
                // TODO: the cgb can access '72 and '73,
                // even when in DMG mode.
                // (Do we need to emulate that?)
                return gb->cgb_ff7x[(addr & 0xFF) - 0x72];
            }
            return 0xFF;
        case 0x75:
            if (gb->is_cgb_mode)
                return (gb->cgb_ff75 << 4) | ~(7 << 4);
            return 0xFF;

        case IO_PLAYDATE_EXTENSION_CTL:
            // (| 0x1C is temporary, to prevent devs from assuming the reserved bits are 0.)
            return gb->direct.crank_docked | 0x1C;

        case 0x58 ... 0x5F:
            if (gb->direct.ext_crank_menu_indexing)
            {
                // crank register is handled specially in menu mode
                if (addr == 0xFF58)
                {
                    return gb->direct.crank_menu_delta;
                }
                else if (addr == 0xFF59)
                {
                    return 0x80 + (((int)gb->direct.crank_menu_accumulation - 0x8000) * 0x7F) /
                                      (CRANK_MENU_DELTA_BINANGLE);
                }
            }

            return gb->direct.peripherals[((addr & 0xFF) - 0x58) / 2] >> (8 * (addr % 2));
        /* Interrupt Enable Register */
        case 0xFF:
            return gb->gb_reg.IE;
        }
    }

    (gb->gb_error)(gb, GB_INVALID_READ, addr);
    return 0xFF;
}

/**
 * Internal function used to read bytes.
 */
__shell uint8_t __gb_read_full(gb_s* gb, const uint_fast16_t addr)
{
    switch (addr >> 12)
    {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
        return gb->gb_zero_bank[addr];

    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
        return gb->selected_bank_addr[addr];

    case 0x8:
    case 0x9:
        if (addr < 0x1800 + VRAM_ADDR)
            return reverse_bits_u8(gb->vram_base[addr]);
        return gb->vram_base[addr];

    case 0xA:
    case 0xB:
        if (gb->enable_cart_ram)
        {
            if (gb->mbc == 2)
            {
                // Mask address to 9 bits (0x1FF) to handle the 512-byte RAM and
                // its mirroring.
                uint16_t ram_addr = (addr - CART_RAM_ADDR) & 0x1FF;

                // Read the stored 4-bit value and OR with 0xF0 because the
                // upper 4 bits are undefined and read as 1s.
                return (gb->gb_cart_ram[ram_addr] & 0x0F) | 0xF0;
            }
            else if (gb->mbc == 7)
            {
                if (gb->mbc7.ram_enable_1 && gb->mbc7.ram_enable_2)
                {
                    uint8_t reg = (addr >> 4) & 0x0F;
                    switch (reg)
                    {
                    case 0x2:
                        return gb->mbc7.accel_x_latched & 0xFF;
                    case 0x3:
                        return gb->mbc7.accel_x_latched >> 8;
                    case 0x4:
                        return gb->mbc7.accel_y_latched & 0xFF;
                    case 0x5:
                        return gb->mbc7.accel_y_latched >> 8;
                    case 0x6:
                        return 0x00;
                    case 0x7:
                        return 0xFF;
                    case 0x8:
                        return (gb->mbc7.eeprom_pins & 0x83) | 0x7C;
                    }
                }
            }

            if (gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
            {
                return gb->latched_rtc[gb->cart_ram_bank - 0x08];
            }
            else if ((gb->cart_mode_select || gb->mbc != 1) &&
                     gb->cart_ram_bank < gb->num_ram_banks)
            {
                return gb->gb_cart_ram[addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE)];
            }
            else
                return gb->gb_cart_ram[addr - CART_RAM_ADDR];
        }

        return 0xFF;

    case 0xC:
        return gb->wram_base[0][addr];

    case 0xD:
        return gb->wram_base[1][addr];

    case 0xE:
        return gb->echo_ram_base[addr];

    case 0xF:
        if (addr < OAM_ADDR)
            return gb->echo_ram_base[addr];

        if (addr < UNUSED_ADDR)
            return gb->oam[addr - OAM_ADDR];

        /* Unusable memory area. Reading from this area returns 0.*/
        if (addr < IO_ADDR)
            goto rare_read;

        /* HRAM */
        if (HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
            return gb->hram[addr - IO_ADDR];

        /* APU registers. */
        if ((addr >= 0xFF10) && (addr <= 0xFF3F))
        {
            if (gb->direct.sound)
            {
                return audio_read(&gb->audio, addr);
            }
            else
            { /* clang-format off */
                static const uint8_t ortab[] = {
                    0x80, 0x3f, 0x00, 0xff, 0xbf,
                    0xff, 0x3f, 0x00, 0xff, 0xbf,
                    0x7f, 0xff, 0x9f, 0xff, 0xbf,
                    0xff, 0xff, 0x00, 0x00, 0xbf,
                    0x00, 0x00, 0x70,
                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                };
                /* clang-format on */
                return gb->hram[addr - IO_ADDR] | ortab[addr - IO_ADDR];
            }
        }

        /* IO and Interrupts. */
        switch (addr & 0xFF)
        {
        /* IO Registers */
        case 0x00:  // P1 / JOYP
        {
            uint8_t p1_val = gb->gb_reg.P1;
            uint8_t joypad_val = gb->direct.joypad;
            uint8_t result = 0xFF;  // Default to all high (no buttons pressed, all lines high)

            // If D-Pad selection line is low, AND the D-pad state.
            if ((p1_val & 0x10) == 0)
            {
                // (joypad_val >> 4) gets the upper nibble (D-pad)
                // | 0xF0 ensures we only affect the lower nibble.
                result &= (joypad_val >> 4) | 0xF0;
            }

            // If Action button selection line is low, AND the button state.
            if ((p1_val & 0x20) == 0)
            {
                // (joypad_val & 0x0F) gets the lower nibble (buttons)
                // | 0xF0 ensures we only affect the lower nibble.
                result &= (joypad_val & 0x0F) | 0xF0;
            }

            // Finally, combine the input result with the selection bits.
            // Bits 7 and 6 are always high.
            result = (result & 0x0F) | (p1_val & 0x30) | 0xC0;

            return result;
        }

        case 0x01:
            return gb->gb_reg.SB;

        case 0x02:
            return gb->gb_reg.SC;

        /* Timer Registers */
        case 0x04:
            return gb->gb_reg.DIV;

        case 0x05:
            return gb->gb_reg.TIMA;

        case 0x06:
            return gb->gb_reg.TMA;

        case 0x07:
            return gb->gb_reg.TAC;

        /* Interrupt Flag Register */
        case 0x0F:
            return gb->gb_reg.IF;

        /* LCD Registers */
        case 0x40:
            return gb->gb_reg.LCDC;

        case 0x41:
            return gb->gb_reg.STAT | 0x80;

        case 0x42:
            return gb->gb_reg.SCY;

        case 0x43:
            return gb->gb_reg.SCX;

        case 0x44:
            return gb->gb_reg.LY;

        case 0x45:
            return gb->gb_reg.LYC;

        /* DMA Register */
        case 0x46:
            return gb->gb_reg.DMA;

        /* DMG Palette Registers */
        case 0x47:
            return gb->gb_reg.BGP;

        case 0x48:
            return gb->gb_reg.OBP0;

        case 0x49:
            return gb->gb_reg.OBP1;

        /* Window Position Registers */
        case 0x4A:
            return gb->gb_reg.WY;

        case 0x4B:
            return gb->gb_reg.WX;
        }
    }

rare_read:
    return __gb_rare_read(gb, addr);
}

/**
 * Handles a clock tick for the MBC7 EEPROM.
 * This function is called on the rising edge of the EEPROM's CLK pin.
 */
__section__(".text.cb") static void __gb_mbc7_eeprom_clock(gb_s* gb)
{
    uint8_t di = (gb->mbc7.eeprom_pins & 0x02) >> 1;

    /* Data is clocked in/out while CS is high. */
    if ((gb->mbc7.eeprom_pins & 0x80) == 0)
        return;

    /* Default DO to high (ready) */
    gb->mbc7.eeprom_pins |= 0x01;

    switch (gb->mbc7.eeprom_state)
    {
    /* Idle state, waiting for a start bit. */
    case 0: /* IDLE */
        if (di)
        {
            gb->mbc7.eeprom_state = 1; /* COMMAND */
            gb->mbc7.eeprom_bits_shifted = 0;
            gb->mbc7.eeprom_shift_reg = 0;
        }
        break;

    /* Receiving command and address. */
    case 1: /* COMMAND */
        gb->mbc7.eeprom_shift_reg = (gb->mbc7.eeprom_shift_reg << 1) | di;
        gb->mbc7.eeprom_bits_shifted++;

        /* All commands are 9 bits after start bit */
        if (gb->mbc7.eeprom_bits_shifted == 9)
        {
            uint8_t opcode = (gb->mbc7.eeprom_shift_reg >> 7) & 0x03;
            gb->mbc7.eeprom_addr = gb->mbc7.eeprom_shift_reg & 0x7F;

            gb->mbc7.eeprom_state = 0; /* Default to IDLE */

            switch (opcode)
            {
            case 0b00:                                          /* Control opcodes */
                if ((gb->mbc7.eeprom_shift_reg >> 5) == 0b0011) /* EWEN */
                    gb->mbc7.eeprom_write_enabled = 1;
                else if ((gb->mbc7.eeprom_shift_reg >> 5) == 0b0000) /* EWDS */
                    gb->mbc7.eeprom_write_enabled = 0;
                else if ((gb->mbc7.eeprom_shift_reg >> 5) == 0b0010) /* ERAL */
                {
                    if (gb->mbc7.eeprom_write_enabled)
                        for (int i = 0; i < 128; i++)
                            ((uint16_t*)gb->gb_cart_ram)[i] = 0xFFFF;
                }
                else if ((gb->mbc7.eeprom_shift_reg >> 5) == 0b0001) /* WRAL */
                {
                    if (gb->mbc7.eeprom_write_enabled)
                    {
                        gb->mbc7.eeprom_state = 3;   /* WRITE */
                        gb->mbc7.eeprom_addr = 0xFF; /* WRAL flag */
                        gb->mbc7.eeprom_bits_shifted = 0;
                    }
                }
                break;

            case 0b01: /* WRITE */
                if (gb->mbc7.eeprom_write_enabled)
                {
                    gb->mbc7.eeprom_state = 3; /* WRITE */
                    gb->mbc7.eeprom_bits_shifted = 0;
                }
                break;

            case 0b10:                     /* READ */
                gb->mbc7.eeprom_state = 2; /* READ */
                gb->mbc7.eeprom_read_buffer = ((uint16_t*)gb->gb_cart_ram)[gb->mbc7.eeprom_addr];
                gb->mbc7.eeprom_bits_shifted = 0;
                gb->mbc7.eeprom_pins &= ~0x01; /* Dummy 0 bit */
                return;

            case 0b11: /* ERASE */
                if (gb->mbc7.eeprom_write_enabled)
                    ((uint16_t*)gb->gb_cart_ram)[gb->mbc7.eeprom_addr] = 0xFFFF;
                break;
            }
        }
        break;

    /* Shifting out data for a READ command. */
    case 2: /* READ */
        gb->mbc7.eeprom_bits_shifted++;
        gb->mbc7.eeprom_pins =
            (gb->mbc7.eeprom_pins & ~1) | ((gb->mbc7.eeprom_read_buffer >> 15) & 1);
        gb->mbc7.eeprom_read_buffer <<= 1;
        if (gb->mbc7.eeprom_bits_shifted >= 16)
            gb->mbc7.eeprom_state = 0; /* IDLE */
        break;

    /* Shifting in data for a WRITE or WRAL command. */
    case 3: /* WRITE */
        /* Not implemented: Writing to EEPROM is currently not supported. */
        break;
    }
}

/**
 * Internal function used to write bytes.
 */
__shell void __gb_write_full(gb_s* gb, const uint_fast16_t addr, const uint8_t val)
{
    switch (addr >> 12)
    {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
        if (gb->mbc == 2)
        {
            if (addr & 0x0100)  // Bit 8 of address is set: This controls ROM Bank.
            {
                gb->selected_rom_bank = val & 0x0F;
                if (gb->selected_rom_bank == 0)
                    gb->selected_rom_bank = 1;
            }
            else  // Bit 8 of address is clear: This controls RAM Enable.
            {
                if (gb->cart_ram)
                    gb->enable_cart_ram = ((val & 0x0F) == 0x0A);
            }
        }
        // Handle other MBCs (MBC1, 3, 5,) which have distinct register ranges.
        else if (addr < 0x2000)  // Address is 0000-1FFF (RAM Enable)
        {
            if (gb->mbc == 7)
                gb->mbc7.ram_enable_1 = ((val & 0x0F) == 0x0A);
            else if (gb->mbc > 0 && gb->cart_ram)
                gb->enable_cart_ram = ((val & 0x0F) == 0x0A);
        }
        else if (addr < 0x4000)  // Address is 2000-3FFF (ROM Bank Lower Bits)
        {
            if (gb->mbc == 1)
            {
                if (gb->is_mbc1m)
                {
                    uint8_t lo = (val & 0x0F);
                    if (lo == 0x00)
                        lo = 0x01;  // 00->01 quirk for low nibble
                    gb->selected_rom_bank = (gb->selected_rom_bank & 0x30) | lo;
                }
                else
                {
                    uint8_t lo = (val & 0x1F);
                    if (lo == 0x00)
                        lo = 0x01;  // 00->01 quirk for low 5 bits
                    gb->selected_rom_bank = (gb->selected_rom_bank & 0x60) | lo;
                }
            }
            else if (gb->mbc == 3)
            {
                gb->selected_rom_bank = val;
                if (!gb->selected_rom_bank)
                    gb->selected_rom_bank++;
            }
            else if (gb->mbc == 5)
            {
                if (addr < 0x3000)
                {
                    gb->selected_rom_bank = (gb->selected_rom_bank & 0x100) | val;
                }
                else
                {
                    gb->selected_rom_bank = ((val & 0x01) << 8) | (gb->selected_rom_bank & 0xFF);
                }
            }
            else if (gb->mbc == 7)
            {
                gb->selected_rom_bank = val & 0x7F;
                if (!gb->selected_rom_bank)
                    gb->selected_rom_bank = 1;
            }
        }

        if (gb->mbc > 0)
        {
            gb->selected_rom_bank &= gb->num_rom_banks_mask;
            __gb_update_selected_bank_addr(gb);
            __gb_update_selected_cart_bank_addr(gb);
        }
        return;
    case 0x4:
    case 0x5:
        if (gb->mbc == 1)
        {
            gb->cart_ram_bank = (val & 3);

            if likely (!gb->is_mbc1m)
            {
                // Standard MBC1: sets bits 5–6 of the ROM bank
                gb->selected_rom_bank = ((val & 3) << 5) | (gb->selected_rom_bank & 0x1F);
            }
            else
            {
                // MBC1M: sets bits 4–5 of the ROM bank (selects the 0x10/0x20/0x30 group)
                gb->selected_rom_bank = ((val & 3) << 4) | (gb->selected_rom_bank & 0x0F);
                gb->zero_bank_base = ((gb->cart_ram_bank & 0x03) << 4) * ROM_BANK_SIZE;
                __gb_update_zero_bank_addr(gb);
            }

            gb->selected_rom_bank &= gb->num_rom_banks_mask;
            __gb_update_selected_bank_addr(gb);
        }
        else if (gb->mbc == 3)
            gb->cart_ram_bank = val;
        else if (gb->mbc == 5)
            gb->cart_ram_bank = (val & 0x0F);
        else if (gb->mbc == 7)
            gb->mbc7.ram_enable_2 = (val == 0x40);

        __gb_update_selected_cart_bank_addr(gb);
        return;

    case 0x6:
    case 0x7:
        if (gb->mbc == 3)
        {
            if (gb->rtc_latch_s1 && val == 0x01)
            {
                memcpy(gb->latched_rtc, gb->cart_rtc, sizeof(gb->latched_rtc));
            }

            gb->rtc_latch_s1 = (val == 0x00);
        }
        else if (gb->mbc == 1 && !gb->is_mbc1m)
        {
            gb->cart_mode_select = (val & 1);
            __gb_update_selected_cart_bank_addr(gb);
        }
        return;

    case 0x8:
    case 0x9:
        if (addr < 0x1800 + VRAM_ADDR)
            gb->vram_base[addr] = reverse_bits_u8(val);
        else
            gb->vram_base[addr] = val;
        return;

    case 0xA:
    case 0xB:
        if (gb->enable_cart_ram)
        {
            if (gb->mbc == 2)
            {
                if (addr < 0xA200)
                {
                    uint16_t ram_addr = (addr - CART_RAM_ADDR) & 0x1FF;
                    uint8_t value_to_write = val & 0x0F;

                    if (gb->gb_cart_ram_size > 0)
                    {
                        const u8 prev = gb->gb_cart_ram[ram_addr];
                        gb->direct.sram_updated |= prev != value_to_write;
                        gb->gb_cart_ram[ram_addr] = value_to_write;
                    }
                }
            }
            else if (gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
            {
                size_t idx = gb->cart_ram_bank - 0x08;
                CB_ASSERT(idx < PEANUT_GB_ARRAYSIZE(gb->cart_rtc));
                gb->cart_rtc[idx] = val;
            }
            else if (gb->mbc == 7)
            {
                if (gb->mbc7.ram_enable_1 && gb->mbc7.ram_enable_2)
                {
                    uint8_t reg = (addr >> 4) & 0x0F;
                    uint8_t old_pins = gb->mbc7.eeprom_pins;
                    switch (reg)
                    {
                    case 0x0: /* Latch Accelerometer */
                        if (val == 0x55)
                            gb->mbc7.accel_latch_state = 1;
                        break;

                    case 0x1: /* Latch Accelerometer */
                        if (gb->mbc7.accel_latch_state == 1 && val == 0xAA)
                        {
                            /* Center value is 0x81D0. Gravity is ~0x70. */
                            gb->mbc7.accel_x_latched = 0x81D0 + (int16_t)gb->direct.accel_x;
                            gb->mbc7.accel_y_latched = 0x81D0 + (int16_t)gb->direct.accel_y;
                            gb->mbc7.accel_latch_state = 0;
                        }
                        break;

                    case 0x8: /* EEPROM Control */
                        gb->mbc7.eeprom_pins = val;
                        if (!(old_pins & 0x80) && (val & 0x80))
                        {
                            gb->mbc7.eeprom_state = 0;
                            gb->mbc7.eeprom_bits_shifted = 0;
                        }
                        if ((val & 0x80) && !(old_pins & 0x40) && (val & 0x40))
                        {
                            __gb_mbc7_eeprom_clock(gb);
                        }
                        break;
                    }
                }
            }
            else if ((gb->cart_mode_select || gb->mbc != 1) &&
                     gb->cart_ram_bank < gb->num_ram_banks)
            {
                size_t idx = addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE);
                CB_ASSERT(idx < gb->gb_cart_ram_size);
                const u8 prev = gb->gb_cart_ram[idx];
                gb->gb_cart_ram[idx] = val;
                gb->direct.sram_updated |= prev != val;
            }
            else if (gb->num_ram_banks)
            {
                size_t idx = addr - CART_RAM_ADDR;
                CB_ASSERT(idx < gb->gb_cart_ram_size);
                const u8 prev = gb->gb_cart_ram[idx];
                gb->gb_cart_ram[idx] = val;
                gb->direct.sram_updated |= prev != val;
            }
        }
        return;

    case 0xC:
        gb->wram_base[0][addr] = val;
        return;

    case 0xD:
        gb->wram_base[1][addr] = val;
        return;

    case 0xE:
        gb->echo_ram_base[addr] = val;
        return;

    case 0xF:
        if (addr < OAM_ADDR)
        {
            gb->echo_ram_base[addr] = val;
            return;
        }

        if (addr < UNUSED_ADDR)
        {
            gb->oam[addr - OAM_ADDR] = val;
            return;
        }

        /* Unusable memory area. */
        if (addr < IO_ADDR)
            goto rare_write;

        if (HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
        {
            gb->hram[addr - IO_ADDR] = val;
            return;
        }

        if ((addr >= 0xFF10) && (addr <= 0xFF3F))
        {
            if (gb->direct.sound)
            {
                audio_write(&gb->audio, addr, val);
            }
            else
            {
                gb->hram[addr - IO_ADDR] = val;
            }
            return;
        }

        /* IO and Interrupts. */
        switch (addr & 0xFF)
        {
        /* Joypad */
        case 0x00:  // P1 / JOYP
        {
            /* A write to P1 only affects the selection bits (4 and 5). */
            gb->gb_reg.P1 = (val & 0x30);
            return;
        }

        /* Serial */
        case 0x01:
            gb->gb_reg.SB = val;
            return;

        case 0x02:  // SC - Serial Control
        {
            bool internal_transfer_start =
                (val & SERIAL_SC_TX_START) && (val & SERIAL_SC_CLOCK_SRC);

            // Keep user-writable bits (0x7E) + the start/clock bits from the game
            gb->gb_reg.SC = (val & (SERIAL_SC_CLOCK_SRC | SERIAL_SC_TX_START)) | 0x7E;

            if (internal_transfer_start && gb->gb_serial_tx == NULL)
            {
                uint8_t sb = gb->gb_reg.SB;

                // A printer sequence starts with $88 OR $00 (for status)
                // OR we are already in a sequence.
                bool is_printer = (gb->printer_stub_state > 0) || (sb == 0x88) || (sb == 0x00);

                if (is_printer)
                {
                    // Default reply is ACK
                    gb->gb_reg.SB = 0x00;

                    switch (gb->printer_stub_state)
                    {
                    case 0:  // Idle, waiting for Magic 1 or $00
                        if (sb == 0x88)
                        {
                            gb->printer_stub_state = 1;  // -> Magic 2
                        }
                        else if (sb == 0x00)
                        {
                            // Game is just asking for status
                            gb->gb_reg.SB = 0x81;         // Reply "Printer OK"
                            gb->printer_stub_state = 10;  // -> Final $00
                        }
                        break;
                    case 1:  // Waiting for Magic 2
                        if (sb == 0x33)
                            gb->printer_stub_state = 2;  // -> Command
                        else
                            gb->printer_stub_state = 0;  // Bad sequence
                        break;
                    case 2:  // Waiting for Command
                        gb->printer_last_cmd = sb;
                        gb->printer_data_len = 0;
                        gb->printer_stub_state = 3;  // -> Compression
                        break;
                    case 3:                          // Waiting for Compression Flag
                        gb->printer_stub_state = 4;  // -> Length LSB
                        break;
                    case 4:  // Waiting for Length LSB
                        gb->printer_data_len = sb;
                        gb->printer_stub_state = 5;  // -> Length MSB
                        break;
                    case 5:  // Waiting for Length MSB
                        gb->printer_data_len |= (sb << 8);
                        if (gb->printer_data_len == 0)
                        {
                            gb->printer_stub_state = 7;  // No data, -> Checksum LSB
                        }
                        else
                        {
                            gb->printer_stub_state = 6;  // -> Data
                        }
                        break;
                    case 6:  // In Data Transfer
                        if (--gb->printer_data_len == 0)
                        {
                            gb->printer_stub_state = 7;  // -> Checksum LSB
                        }
                        break;
                    case 7:                          // Waiting for Checksum LSB
                        gb->printer_stub_state = 8;  // -> Checksum MSB
                        break;
                    case 8:                          // Waiting for Checksum MSB
                        gb->printer_stub_state = 9;  // -> Alive Indicator
                        break;
                    case 9:  // Waiting for Alive Indicator ($00)
                        if (sb == 0x00)
                        {
                            // This is the 9th byte, where we send status
                            gb->gb_reg.SB = 0x81;         // Reply "Printer OK"
                            gb->printer_stub_state = 10;  // -> Final $00
                        }
                        else
                        {
                            gb->printer_stub_state = 0;  // Bad sequence
                        }
                        break;
                    case 10:  // Waiting for Final $00
                    default:
                        gb->printer_stub_state = 0;
                        break;
                    }

                    // Instantly trigger the interrupt and clear the start flag
                    gb->gb_reg.IF |= SERIAL_INTR;
                    gb->gb_reg.SC &= ~SERIAL_SC_TX_START;
                }
                else
                {
                    // playdate->system->logToConsole("Serial anomaly: PC=%x, SB=%x",
                    // gb->cpu_reg.pc, gb->gb_reg.SB);

                    gb->counter.serial_count = SERIAL_CYCLES;
                    gb->printer_stub_state = 0;
                    gb->printer_data_len = 0;
                    gb->printer_last_cmd = 0;

                    // Apply instant interrupt hack only if it's the specific early-game polling
                    // loop (SB=0xFF at low PC).
                    if (gb->gb_reg.SB == 0xFF && gb->cpu_reg.pc < 0x300)
                    {
                        gb->gb_reg.IF |= SERIAL_INTR;
                    }
                }
            }
            else if (!(val & SERIAL_SC_TX_START))
            {
                // The game manually stopped a transfer.
                gb->printer_stub_state = 0;
                gb->printer_data_len = 0;
                gb->printer_last_cmd = 0;
            }
            return;
        }

        /* Timer Registers */
        case 0x04:
            gb->gb_reg.DIV = 0x00;
            return;

        case 0x05:
            gb->gb_reg.TIMA = val;
            return;

        case 0x06:
            gb->gb_reg.TMA = val;
            return;

        case 0x07:
            gb->gb_reg.TAC = val;
            __gb_update_tac(gb);
            return;

        /* Interrupt Flag Register */
        case 0x0F:
            gb->gb_reg.IF = (val | 0b11100000);
            return;

        /* LCD Registers */
        case 0x40:  // LCDC
        {
            uint8_t old_lcdc = gb->gb_reg.LCDC;
            bool was_enabled = (old_lcdc & LCDC_ENABLE);

            gb->gb_reg.LCDC = val;

            if (val != old_lcdc)
            {
                __gb_update_map_pointers(gb);
            }

            bool is_enabled = (gb->gb_reg.LCDC & LCDC_ENABLE);

            if (was_enabled && !is_enabled)
            {
                gb->counter.lcd_off_count += gb->counter.lcd_count;
                gb->gb_reg.LY = 0;
                gb->counter.lcd_count = 0;
                gb->lcd_mode = LCD_HBLANK;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | gb->lcd_mode;
                __gb_check_lyc__cgb(gb);
            }
            else if (!was_enabled && is_enabled)
            {
                gb->counter.lcd_count = 4;
                gb->gb_reg.LY = 0;
                gb->lcd_mode = LCD_SEARCH_OAM;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | gb->lcd_mode;
                __gb_check_lyc__cgb(gb);
            }
            return;
        }

        case 0x41:  // STAT Register
        {
            // --- Spurious STAT interrupt quirk (DMG-only) ---
            // On DMG, a write to STAT can trigger an interrupt if an observable
            // condition (a specific PPU mode or LY=LYC) is already true,
            // regardless of the STAT enable bits.
            // The check is for any mode except Mode 3 (Pixel Transfer).
            // Required for games like Road Rash and F-1 Racing.
            if (!gb->is_cgb_mode && (gb->gb_reg.LCDC & LCDC_ENABLE))
            {
                if (gb->lcd_mode != LCD_TRANSFER || (gb->gb_reg.STAT & STAT_LYC_COINC))
                {
                    gb->gb_reg.IF |= LCDC_INTR;
                }
            }

            gb->gb_reg.STAT = (val & STAT_USER_BITS) | (gb->gb_reg.STAT & ~STAT_USER_BITS);

            __gb_update_stat_irq__cgb(gb);
            return;
        }

        case 0x42:
            gb->gb_reg.SCY = val;
            return;

        case 0x43:
            gb->gb_reg.SCX = val;
            return;

        /* LY (0xFF44) is read-only. Writes are ignored on real hardware.
         * The boot ROM (not supported) attempts to write to this register. */
        case 0x44:
            return;

        /* LY (0xFF44) is read only. */
        case 0x45:  // LYC Register
            gb->gb_reg.LYC = val;
            // Perform an LY=LYC check immediately if the LCD is enabled.
            if (gb->gb_reg.LCDC & LCDC_ENABLE)
            {
                __gb_check_lyc__cgb(gb);
                __gb_update_stat_irq__cgb(gb);
            }
            return;

        /* DMA Register */
        case 0x46:
            gb->gb_reg.DMA = (val % 0xF1);

            for (uint8_t i = 0; i < OAM_SIZE; i++)
                gb->oam[i] = __gb_read_full(gb, (gb->gb_reg.DMA << 8) + i);

            return;

        /* DMG Palette Registers */
        case 0x47:
            gb->gb_reg.BGP = val;
            gb->display.bg_palette[0] = (gb->gb_reg.BGP & 0x03);
            gb->display.bg_palette[1] = (gb->gb_reg.BGP >> 2) & 0x03;
            gb->display.bg_palette[2] = (gb->gb_reg.BGP >> 4) & 0x03;
            gb->display.bg_palette[3] = (gb->gb_reg.BGP >> 6) & 0x03;
            return;

        case 0x48:
            gb->gb_reg.OBP0 = val;
            gb->display.sp_palette[0] = (gb->gb_reg.OBP0 & 0x03);
            gb->display.sp_palette[1] = (gb->gb_reg.OBP0 >> 2) & 0x03;
            gb->display.sp_palette[2] = (gb->gb_reg.OBP0 >> 4) & 0x03;
            gb->display.sp_palette[3] = (gb->gb_reg.OBP0 >> 6) & 0x03;
            return;

        case 0x49:
            gb->gb_reg.OBP1 = val;
            gb->display.sp_palette[4] = (gb->gb_reg.OBP1 & 0x03);
            gb->display.sp_palette[5] = (gb->gb_reg.OBP1 >> 2) & 0x03;
            gb->display.sp_palette[6] = (gb->gb_reg.OBP1 >> 4) & 0x03;
            gb->display.sp_palette[7] = (gb->gb_reg.OBP1 >> 6) & 0x03;
            return;

        /* Window Position Registers */
        case 0x4A:
            gb->gb_reg.WY = val;
            return;

        case 0x4B:
            gb->gb_reg.WX = val;
            return;
        }
    }

rare_write:
    __gb_rare_write(gb, addr, val);
}

#if ENABLE_LCD
struct sprite_data
{
    uint8_t sprite_number;
    uint8_t x;
};
#endif

__shell static u8 __gb_rare_instruction(gb_s* restrict gb, uint8_t opcode);

__shell static unsigned __gb_run_instruction(gb_s* gb, uint8_t opcode)
{
    static const uint8_t op_cycles[0x100] = {
        /* clang-format off */
        /*  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F   */
            4,  12, 8,  8,  4,  4,  8,  4,  20, 8,  8,  8,  4,  4,  8,  4,  /* 0x00 */
            4,  12, 8,  8,  4,  4,  8,  4,  12, 8,  8,  8,  4,  4,  8,  4,  /* 0x10 */
            8,  12, 8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4,  /* 0x20 */
            8,  12, 8,  8,  12, 12, 12, 4,  8,  8,  8,  8,  4,  4,  8,  4,  /* 0x30 */

            4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0x40 */
            4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0x50 */
            4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0x60 */
            8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0x70 */

            4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0x80 */
            4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0x90 */
            4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0xA0 */
            4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  /* 0xB0 */

            8,  12, 12, 16, 12, 16, 8,  16, 8,  16, 12, 8,  12, 24, 8,  16, /* 0xC0 */
            8,  12, 12, 0,  12, 16, 8,  16, 8,  16, 12, 0,  12, 0,  8,  16, /* 0xD0 */
            12, 12, 8,  0,  0,  16, 8,  16, 16, 4,  16, 0,  0,  0,  8,  16, /* 0xE0 */
            12, 12, 8,  4,  0,  16, 8,  16, 12, 8,  16, 4,  0,  0,  8,  16  /* 0xF0 */
        /* clang-format on */
    };
    uint8_t inst_cycles = op_cycles[opcode];

    /* Execute opcode */

    static const void* op_table[256] = {
        &&exit,  &&_0x01, &&_0x02, &&_0x03,    &&_0x04,    &&_0x05,    &&_0x06, &&_0x07,
        &&_0x08, &&_0x09, &&_0x0A, &&_0x0B,    &&_0x0C,    &&_0x0D,    &&_0x0E, &&_0x0F,
        &&_0x10, &&_0x11, &&_0x12, &&_0x13,    &&_0x14,    &&_0x15,    &&_0x16, &&_0x17,
        &&_0x18, &&_0x19, &&_0x1A, &&_0x1B,    &&_0x1C,    &&_0x1D,    &&_0x1E, &&_0x1F,
        &&_0x20, &&_0x21, &&_0x22, &&_0x23,    &&_0x24,    &&_0x25,    &&_0x26, &&_0x27,
        &&_0x28, &&_0x29, &&_0x2A, &&_0x2B,    &&_0x2C,    &&_0x2D,    &&_0x2E, &&_0x2F,
        &&_0x30, &&_0x31, &&_0x32, &&_0x33,    &&_0x34,    &&_0x35,    &&_0x36, &&_0x37,
        &&_0x38, &&_0x39, &&_0x3A, &&_0x3B,    &&_0x3C,    &&_0x3D,    &&_0x3E, &&_0x3F,
        &&_0x40, &&_0x41, &&_0x42, &&_0x43,    &&_0x44,    &&_0x45,    &&_0x46, &&_0x47,
        &&_0x48, &&_0x49, &&_0x4A, &&_0x4B,    &&_0x4C,    &&_0x4D,    &&_0x4E, &&_0x4F,
        &&_0x50, &&_0x51, &&_0x52, &&_0x53,    &&_0x54,    &&_0x55,    &&_0x56, &&_0x57,
        &&_0x58, &&_0x59, &&_0x5A, &&_0x5B,    &&_0x5C,    &&_0x5D,    &&_0x5E, &&_0x5F,
        &&_0x60, &&_0x61, &&_0x62, &&_0x63,    &&_0x64,    &&_0x65,    &&_0x66, &&_0x67,
        &&_0x68, &&_0x69, &&_0x6A, &&_0x6B,    &&_0x6C,    &&_0x6D,    &&_0x6E, &&_0x6F,
        &&_0x70, &&_0x71, &&_0x72, &&_0x73,    &&_0x74,    &&_0x75,    &&_0x76, &&_0x77,
        &&_0x78, &&_0x79, &&_0x7A, &&_0x7B,    &&_0x7C,    &&_0x7D,    &&_0x7E, &&_0x7F,
        &&_0x80, &&_0x81, &&_0x82, &&_0x83,    &&_0x84,    &&_0x85,    &&_0x86, &&_0x87,
        &&_0x88, &&_0x89, &&_0x8A, &&_0x8B,    &&_0x8C,    &&_0x8D,    &&_0x8E, &&_0x8F,
        &&_0x90, &&_0x91, &&_0x92, &&_0x93,    &&_0x94,    &&_0x95,    &&_0x96, &&_0x97,
        &&_0x98, &&_0x99, &&_0x9A, &&_0x9B,    &&_0x9C,    &&_0x9D,    &&_0x9E, &&_0x9F,
        &&_0xA0, &&_0xA1, &&_0xA2, &&_0xA3,    &&_0xA4,    &&_0xA5,    &&_0xA6, &&_0xA7,
        &&_0xA8, &&_0xA9, &&_0xAA, &&_0xAB,    &&_0xAC,    &&_0xAD,    &&_0xAE, &&_0xAF,
        &&_0xB0, &&_0xB1, &&_0xB2, &&_0xB3,    &&_0xB4,    &&_0xB5,    &&_0xB6, &&_0xB7,
        &&_0xB8, &&_0xB9, &&_0xBA, &&_0xBB,    &&_0xBC,    &&_0xBD,    &&_0xBE, &&_0xBF,
        &&_0xC0, &&_0xC1, &&_0xC2, &&_0xC3,    &&_0xC4,    &&_0xC5,    &&_0xC6, &&_0xC7,
        &&_0xC8, &&_0xC9, &&_0xCA, &&_0xCB,    &&_0xCC,    &&_0xCD,    &&_0xCE, &&_0xCF,
        &&_0xD0, &&_0xD1, &&_0xD2, &&_invalid, &&_0xD4,    &&_0xD5,    &&_0xD6, &&_0xD7,
        &&_0xD8, &&_0xD9, &&_0xDA, &&_invalid, &&_0xDC,    &&_invalid, &&_0xDE, &&_0xDF,
        &&_0xE0, &&_0xE1, &&_0xE2, &&_invalid, &&_invalid, &&_0xE5,    &&_0xE6, &&_0xE7,
        &&_0xE8, &&_0xE9, &&_0xEA, &&_invalid, &&_invalid, &&_invalid, &&_0xEE, &&_0xEF,
        &&_0xF0, &&_0xF1, &&_0xF2, &&_0xF3,    &&_invalid, &&_0xF5,    &&_0xF6, &&_0xF7,
        &&_0xF8, &&_0xF9, &&_0xFA, &&_0xFB,    &&_invalid, &&_invalid, &&_0xFE, &&_0xFF
    };

    goto* op_table[opcode];

_0x00:
{ /* NOP */
    goto exit;
}

_0x01:
{ /* LD BC, imm */
    gb->cpu_reg.c = __gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.b = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x02:
{ /* LD (BC), A */
    __gb_write_full(gb, gb->cpu_reg.bc, gb->cpu_reg.a);
    goto exit;
}

_0x03:
{ /* INC BC */
    gb->cpu_reg.bc++;
    goto exit;
}

_0x04:
{ /* INC B */
    gb->cpu_reg.b++;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.b == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.b & 0x0F) == 0x00);
    goto exit;
}

_0x05:
{ /* DEC B */
    gb->cpu_reg.b--;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.b == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.b & 0x0F) == 0x0F);
    goto exit;
}

_0x06:
{ /* LD B, imm */
    gb->cpu_reg.b = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x07:
{ /* RLCA */
    gb->cpu_reg.a = (gb->cpu_reg.a << 1) | (gb->cpu_reg.a >> 7);
    gb->cpu_reg.f_bits.z = 0;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = (gb->cpu_reg.a & 0x01);
    goto exit;
}

_0x08:
{ /* LD (imm), SP */
    uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
    temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
    __gb_write_full(gb, temp++, gb->cpu_reg.sp & 0xFF);
    __gb_write_full(gb, temp, gb->cpu_reg.sp >> 8);
    goto exit;
}

_0x09:
{ /* ADD HL, BC */
    uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.bc;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.bc) & 0x1000 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
    gb->cpu_reg.hl = (temp & 0x0000FFFF);
    goto exit;
}

_0x0A:
{ /* LD A, (BC) */
    gb->cpu_reg.a = __gb_read_full(gb, gb->cpu_reg.bc);
    goto exit;
}

_0x0B:
{ /* DEC BC */
    gb->cpu_reg.bc--;
    goto exit;
}

_0x0C:
{ /* INC C */
    gb->cpu_reg.c++;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.c == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.c & 0x0F) == 0x00);
    goto exit;
}

_0x0D:
{ /* DEC C */
    gb->cpu_reg.c--;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.c == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.c & 0x0F) == 0x0F);
    goto exit;
}

_0x0E:
{ /* LD C, imm */
    gb->cpu_reg.c = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x0F:
{ /* RRCA */
    gb->cpu_reg.f_bits.c = gb->cpu_reg.a & 0x01;
    gb->cpu_reg.a = (gb->cpu_reg.a >> 1) | (gb->cpu_reg.a << 7);
    gb->cpu_reg.f_bits.z = 0;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    goto exit;
}

_0x10:
{ /* STOP */
    /* We check for button-press glitch on DMG. */
    if (!gb->is_cgb_mode && (gb->direct.joypad != 0xFF) && ((gb->gb_reg.P1 & 0x30) != 0x30))
    {
        /* STOP becomes a 1-byte NOP. The PC was advanced by the main
           loop's fetch, but we do NOT advance it further. The byte
           at the original PC+1 (the STOP operand) will be executed next. */
        goto exit;
    }

    gb->cpu_reg.pc++;

    if (gb->gb_reg.IF & gb->gb_reg.IE &
        ANY_INTR /* FIXME: why can't we assume (IF & ~ANY_INTR) is 0?*/)
    {
        if (gb->gb_ime == 0)
        {
            /* HALT Bug: PC fails to increment correctly.
               Rewind PC to re-execute the operand as an instruction. */
            gb->cpu_reg.pc--;
        }
    }
    else
    {
        /* NORMAL OPERATION: Enter low-power STOP mode. */
        gb->gb_stop = 1;
        gb->gb_reg.DIV = 0;
    }
    goto exit;
}

_0x11:
{ /* LD DE, imm */
    gb->cpu_reg.e = __gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.d = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x12:
{ /* LD (DE), A */
    __gb_write_full(gb, gb->cpu_reg.de, gb->cpu_reg.a);
    goto exit;
}

_0x13:
{ /* INC DE */
    gb->cpu_reg.de++;
    goto exit;
}

_0x14:
{ /* INC D */
    gb->cpu_reg.d++;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.d == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.d & 0x0F) == 0x00);
    goto exit;
}

_0x15:
{ /* DEC D */
    gb->cpu_reg.d--;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.d == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.d & 0x0F) == 0x0F);
    goto exit;
}

_0x16:
{ /* LD D, imm */
    gb->cpu_reg.d = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x17:
{ /* RLA */
    uint8_t temp = gb->cpu_reg.a;
    gb->cpu_reg.a = (gb->cpu_reg.a << 1) | gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = 0;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = (temp >> 7) & 0x01;
    goto exit;
}

_0x18:
{ /* JR imm */
    int8_t temp = (int8_t)__gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.pc += temp;
    goto exit;
}

_0x19:
{ /* ADD HL, DE */
    uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.de;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.de) & 0x1000 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
    gb->cpu_reg.hl = (temp & 0x0000FFFF);
    goto exit;
}

_0x1A:
{ /* LD A, (DE) */
    gb->cpu_reg.a = __gb_read_full(gb, gb->cpu_reg.de);
    goto exit;
}

_0x1B:
{ /* DEC DE */
    gb->cpu_reg.de--;
    goto exit;
}

_0x1C:
{ /* INC E */
    gb->cpu_reg.e++;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.e == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.e & 0x0F) == 0x00);
    goto exit;
}

_0x1D:
{ /* DEC E */
    gb->cpu_reg.e--;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.e == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.e & 0x0F) == 0x0F);
    goto exit;
}

_0x1E:
{ /* LD E, imm */
    gb->cpu_reg.e = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x1F:
{ /* RRA */
    uint8_t temp = gb->cpu_reg.a;
    gb->cpu_reg.a = gb->cpu_reg.a >> 1 | (gb->cpu_reg.f_bits.c << 7);
    gb->cpu_reg.f_bits.z = 0;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = temp & 0x1;
    goto exit;
}

_0x20:
{ /* JP NZ, imm */
    if (!gb->cpu_reg.f_bits.z)
    {
        int8_t temp = (int8_t)__gb_read_full(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.pc += temp;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc++;

    goto exit;
}

_0x21:
{ /* LD HL, imm */
    gb->cpu_reg.l = __gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.h = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x22:
{ /* LDI (HL), A */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
    gb->cpu_reg.hl++;
    goto exit;
}

_0x23:
{ /* INC HL */
    gb->cpu_reg.hl++;
    goto exit;
}

_0x24:
{ /* INC H */
    gb->cpu_reg.h++;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.h == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.h & 0x0F) == 0x00);
    goto exit;
}

_0x25:
{ /* DEC H */
    gb->cpu_reg.h--;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.h == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.h & 0x0F) == 0x0F);
    goto exit;
}

_0x26:
{ /* LD H, imm */
    gb->cpu_reg.h = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x27:
{ /* DAA */
    uint16_t a = gb->cpu_reg.a;

    if (gb->cpu_reg.f_bits.n)
    {
        if (gb->cpu_reg.f_bits.h)
            a = (a - 0x06) & 0xFF;

        if (gb->cpu_reg.f_bits.c)
            a -= 0x60;
    }
    else
    {
        if (gb->cpu_reg.f_bits.h || (a & 0x0F) > 9)
            a += 0x06;

        if (gb->cpu_reg.f_bits.c || a > 0x9F)
            a += 0x60;
    }

    if ((a & 0x100) == 0x100)
        gb->cpu_reg.f_bits.c = 1;

    gb->cpu_reg.a = a;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0);
    gb->cpu_reg.f_bits.h = 0;

    goto exit;
}

_0x28:
{ /* JP Z, imm */
    if (gb->cpu_reg.f_bits.z)
    {
        int8_t temp = (int8_t)__gb_read_full(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.pc += temp;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc++;

    goto exit;
}

_0x29:
{ /* ADD HL, HL */
    uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.hl;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (temp & 0x1000) ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
    gb->cpu_reg.hl = (temp & 0x0000FFFF);
    goto exit;
}

_0x2A:
{ /* LD A, (HL+) */
    gb->cpu_reg.a = __gb_read_full(gb, gb->cpu_reg.hl++);
    goto exit;
}

_0x2B:
{ /* DEC HL */
    gb->cpu_reg.hl--;
    goto exit;
}

_0x2C:
{ /* INC L */
    gb->cpu_reg.l++;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.l == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.l & 0x0F) == 0x00);
    goto exit;
}

_0x2D:
{ /* DEC L */
    gb->cpu_reg.l--;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.l == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.l & 0x0F) == 0x0F);
    goto exit;
}

_0x2E:
{ /* LD L, imm */
    gb->cpu_reg.l = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x2F:
{ /* CPL */
    gb->cpu_reg.a = ~gb->cpu_reg.a;
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = 1;
    goto exit;
}

_0x30:
{ /* JP NC, imm */
    if (!gb->cpu_reg.f_bits.c)
    {
        int8_t temp = (int8_t)__gb_read_full(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.pc += temp;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc++;

    goto exit;
}

_0x31:
{ /* LD SP, imm */
    gb->cpu_reg.sp = __gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.sp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
    goto exit;
}

_0x32:
{ /* LD (HL), A */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
    gb->cpu_reg.hl--;
    goto exit;
}

_0x33:
{ /* INC SP */
    gb->cpu_reg.sp++;
    goto exit;
}

_0x34:
{ /* INC (HL) */
    uint8_t temp = __gb_read_full(gb, gb->cpu_reg.hl) + 1;
    gb->cpu_reg.f_bits.z = (temp == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((temp & 0x0F) == 0x00);
    __gb_write_full(gb, gb->cpu_reg.hl, temp);
    goto exit;
}

_0x35:
{ /* DEC (HL) */
    uint8_t temp = __gb_read_full(gb, gb->cpu_reg.hl) - 1;
    gb->cpu_reg.f_bits.z = (temp == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((temp & 0x0F) == 0x0F);
    __gb_write_full(gb, gb->cpu_reg.hl, temp);
    goto exit;
}

_0x36:
{ /* LD (HL), imm */
    __gb_write_full(gb, gb->cpu_reg.hl, __gb_read_full(gb, gb->cpu_reg.pc++));
    goto exit;
}

_0x37:
{ /* SCF */
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 1;
    goto exit;
}

_0x38:
{ /* JP C, imm */
    if (gb->cpu_reg.f_bits.c)
    {
        int8_t temp = (int8_t)__gb_read_full(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.pc += temp;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc++;

    goto exit;
}

_0x39:
{ /* ADD HL, SP */
    uint_fast32_t temp = gb->cpu_reg.hl + gb->cpu_reg.sp;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.hl & 0xFFF) + (gb->cpu_reg.sp & 0xFFF)) & 0x1000 ? 1 : 0;
    gb->cpu_reg.f_bits.c = temp & 0x10000 ? 1 : 0;
    gb->cpu_reg.hl = (uint16_t)temp;
    goto exit;
}

_0x3A:
{ /* LD A, (HL) */
    gb->cpu_reg.a = __gb_read_full(gb, gb->cpu_reg.hl--);
    goto exit;
}

_0x3B:
{ /* DEC SP */
    gb->cpu_reg.sp--;
    goto exit;
}

_0x3C:
{ /* INC A */
    gb->cpu_reg.a++;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0x0F) == 0x00);
    goto exit;
}

_0x3D:
{ /* DEC A */
    gb->cpu_reg.a--;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0x0F) == 0x0F);
    goto exit;
}

_0x3E:
{ /* LD A, imm */
    gb->cpu_reg.a = __gb_read_full(gb, gb->cpu_reg.pc++);
    goto exit;
}

_0x3F:
{ /* CCF */
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = ~gb->cpu_reg.f_bits.c;
    goto exit;
}

_0x40:
{ /* LD B, B */
    goto exit;
}

_0x41:
{ /* LD B, C */
    gb->cpu_reg.b = gb->cpu_reg.c;
    goto exit;
}

_0x42:
{ /* LD B, D */
    gb->cpu_reg.b = gb->cpu_reg.d;
    goto exit;
}

_0x43:
{ /* LD B, E */
    gb->cpu_reg.b = gb->cpu_reg.e;
    goto exit;
}

_0x44:
{ /* LD B, H */
    gb->cpu_reg.b = gb->cpu_reg.h;
    goto exit;
}

_0x45:
{ /* LD B, L */
    gb->cpu_reg.b = gb->cpu_reg.l;
    goto exit;
}

_0x46:
{ /* LD B, (HL) */
    gb->cpu_reg.b = __gb_read_full(gb, gb->cpu_reg.hl);
    goto exit;
}

_0x47:
{ /* LD B, A */
    gb->cpu_reg.b = gb->cpu_reg.a;
    goto exit;
}

_0x48:
{ /* LD C, B */
    gb->cpu_reg.c = gb->cpu_reg.b;
    goto exit;
}

_0x49:
{ /* LD C, C */
    goto exit;
}

_0x4A:
{ /* LD C, D */
    gb->cpu_reg.c = gb->cpu_reg.d;
    goto exit;
}

_0x4B:
{ /* LD C, E */
    gb->cpu_reg.c = gb->cpu_reg.e;
    goto exit;
}

_0x4C:
{ /* LD C, H */
    gb->cpu_reg.c = gb->cpu_reg.h;
    goto exit;
}

_0x4D:
{ /* LD C, L */
    gb->cpu_reg.c = gb->cpu_reg.l;
    goto exit;
}

_0x4E:
{ /* LD C, (HL) */
    gb->cpu_reg.c = __gb_read_full(gb, gb->cpu_reg.hl);
    goto exit;
}

_0x4F:
{ /* LD C, A */
    gb->cpu_reg.c = gb->cpu_reg.a;
    goto exit;
}

_0x50:
{ /* LD D, B */
    gb->cpu_reg.d = gb->cpu_reg.b;
    goto exit;
}

_0x51:
{ /* LD D, C */
    gb->cpu_reg.d = gb->cpu_reg.c;
    goto exit;
}

_0x52:
{ /* LD D, D */
    goto exit;
}

_0x53:
{ /* LD D, E */
    gb->cpu_reg.d = gb->cpu_reg.e;
    goto exit;
}

_0x54:
{ /* LD D, H */
    gb->cpu_reg.d = gb->cpu_reg.h;
    goto exit;
}

_0x55:
{ /* LD D, L */
    gb->cpu_reg.d = gb->cpu_reg.l;
    goto exit;
}

_0x56:
{ /* LD D, (HL) */
    gb->cpu_reg.d = __gb_read_full(gb, gb->cpu_reg.hl);
    goto exit;
}

_0x57:
{ /* LD D, A */
    gb->cpu_reg.d = gb->cpu_reg.a;
    goto exit;
}

_0x58:
{ /* LD E, B */
    gb->cpu_reg.e = gb->cpu_reg.b;
    goto exit;
}

_0x59:
{ /* LD E, C */
    gb->cpu_reg.e = gb->cpu_reg.c;
    goto exit;
}

_0x5A:
{ /* LD E, D */
    gb->cpu_reg.e = gb->cpu_reg.d;
    goto exit;
}

_0x5B:
{ /* LD E, E */
    goto exit;
}

_0x5C:
{ /* LD E, H */
    gb->cpu_reg.e = gb->cpu_reg.h;
    goto exit;
}

_0x5D:
{ /* LD E, L */
    gb->cpu_reg.e = gb->cpu_reg.l;
    goto exit;
}

_0x5E:
{ /* LD E, (HL) */
    gb->cpu_reg.e = __gb_read_full(gb, gb->cpu_reg.hl);
    goto exit;
}

_0x5F:
{ /* LD E, A */
    gb->cpu_reg.e = gb->cpu_reg.a;
    goto exit;
}

_0x60:
{ /* LD H, B */
    gb->cpu_reg.h = gb->cpu_reg.b;
    goto exit;
}

_0x61:
{ /* LD H, C */
    gb->cpu_reg.h = gb->cpu_reg.c;
    goto exit;
}

_0x62:
{ /* LD H, D */
    gb->cpu_reg.h = gb->cpu_reg.d;
    goto exit;
}

_0x63:
{ /* LD H, E */
    gb->cpu_reg.h = gb->cpu_reg.e;
    goto exit;
}

_0x64:
{ /* LD H, H */
    goto exit;
}

_0x65:
{ /* LD H, L */
    gb->cpu_reg.h = gb->cpu_reg.l;
    goto exit;
}

_0x66:
{ /* LD H, (HL) */
    gb->cpu_reg.h = __gb_read_full(gb, gb->cpu_reg.hl);
    goto exit;
}

_0x67:
{ /* LD H, A */
    gb->cpu_reg.h = gb->cpu_reg.a;
    goto exit;
}

_0x68:
{ /* LD L, B */
    gb->cpu_reg.l = gb->cpu_reg.b;
    goto exit;
}

_0x69:
{ /* LD L, C */
    gb->cpu_reg.l = gb->cpu_reg.c;
    goto exit;
}

_0x6A:
{ /* LD L, D */
    gb->cpu_reg.l = gb->cpu_reg.d;
    goto exit;
}

_0x6B:
{ /* LD L, E */
    gb->cpu_reg.l = gb->cpu_reg.e;
    goto exit;
}

_0x6C:
{ /* LD L, H */
    gb->cpu_reg.l = gb->cpu_reg.h;
    goto exit;
}

_0x6D:
{ /* LD L, L */
    goto exit;
}

_0x6E:
{ /* LD L, (HL) */
    gb->cpu_reg.l = __gb_read_full(gb, gb->cpu_reg.hl);
    goto exit;
}

_0x6F:
{ /* LD L, A */
    gb->cpu_reg.l = gb->cpu_reg.a;
    goto exit;
}

_0x70:
{ /* LD (HL), B */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.b);
    goto exit;
}

_0x71:
{ /* LD (HL), C */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.c);
    goto exit;
}

_0x72:
{ /* LD (HL), D */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.d);
    goto exit;
}

_0x73:
{ /* LD (HL), E */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.e);
    goto exit;
}

_0x74:
{ /* LD (HL), H */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.h);
    goto exit;
}

_0x75:
{ /* LD (HL), L */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.l);
    goto exit;
}

_0x76:
{ /* HALT */
    // The HALT bug is only present on the DMG.
    if (!gb->is_cgb_mode && gb->gb_ime == 0 && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
    {
        // HALT bug
        gb->cpu_reg.pc--;
    }
    else
    {
        gb->gb_halt = 1;
    }
    goto exit;
    goto exit;
}

_0x77:
{ /* LD (HL), A */
    __gb_write_full(gb, gb->cpu_reg.hl, gb->cpu_reg.a);
    goto exit;
}

_0x78:
{ /* LD A, B */
    gb->cpu_reg.a = gb->cpu_reg.b;
    goto exit;
}

_0x79:
{ /* LD A, C */
    gb->cpu_reg.a = gb->cpu_reg.c;
    goto exit;
}

_0x7A:
{ /* LD A, D */
    gb->cpu_reg.a = gb->cpu_reg.d;
    goto exit;
}

_0x7B:
{ /* LD A, E */
    gb->cpu_reg.a = gb->cpu_reg.e;
    goto exit;
}

_0x7C:
{ /* LD A, H */
    gb->cpu_reg.a = gb->cpu_reg.h;
    goto exit;
}

_0x7D:
{ /* LD A, L */
    gb->cpu_reg.a = gb->cpu_reg.l;
    goto exit;
}

_0x7E:
{ /* LD A, (HL) */
    gb->cpu_reg.a = __gb_read_full(gb, gb->cpu_reg.hl);
    goto exit;
}

_0x7F:
{ /* LD A, A */
    goto exit;
}

_0x80:
{ /* ADD A, B */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.b;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x81:
{ /* ADD A, C */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x82:
{ /* ADD A, D */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.d;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x83:
{ /* ADD A, E */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.e;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x84:
{ /* ADD A, H */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.h;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x85:
{ /* ADD A, L */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.l;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x86:
{ /* ADD A, (HL) */
    uint8_t hl = __gb_read_full(gb, gb->cpu_reg.hl);
    uint16_t temp = gb->cpu_reg.a + hl;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ hl ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x87:
{ /* ADD A, A */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.a;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = temp & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x88:
{ /* ADC A, B */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.b + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x89:
{ /* ADC A, C */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.c + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x8A:
{ /* ADC A, D */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.d + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x8B:
{ /* ADC A, E */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.e + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x8C:
{ /* ADC A, H */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.h + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x8D:
{ /* ADC A, L */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.l + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x8E:
{ /* ADC A, (HL) */
    uint8_t val = __gb_read_full(gb, gb->cpu_reg.hl);
    uint16_t temp = gb->cpu_reg.a + val + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x8F:
{ /* ADC A, A */
    uint16_t temp = gb->cpu_reg.a + gb->cpu_reg.a + gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    /* TODO: Optimisation here? */
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.a ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x90:
{ /* SUB B */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.b;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x91:
{ /* SUB C */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x92:
{ /* SUB D */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.d;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x93:
{ /* SUB E */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.e;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x94:
{ /* SUB H */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.h;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x95:
{ /* SUB L */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.l;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x96:
{ /* SUB (HL) */
    uint8_t val = __gb_read_full(gb, gb->cpu_reg.hl);
    uint16_t temp = gb->cpu_reg.a - val;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x97:
{ /* SUB A */
    gb->cpu_reg.a = 0;
    gb->cpu_reg.f_bits.z = 1;
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0x98:
{ /* SBC A, B */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.b - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x99:
{ /* SBC A, C */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.c - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x9A:
{ /* SBC A, D */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.d - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x9B:
{ /* SBC A, E */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.e - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x9C:
{ /* SBC A, H */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.h - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x9D:
{ /* SBC A, L */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.l - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x9E:
{ /* SBC A, (HL) */
    uint8_t val = __gb_read_full(gb, gb->cpu_reg.hl);
    uint16_t temp = gb->cpu_reg.a - val - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0x9F:
{ /* SBC A, A */
    gb->cpu_reg.a = gb->cpu_reg.f_bits.c ? 0xFF : 0x00;
    gb->cpu_reg.f_bits.z = !gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = gb->cpu_reg.f_bits.c;
    goto exit;
}

_0xA0:
{ /* AND B */
    gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.b;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA1:
{ /* AND C */
    gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.c;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA2:
{ /* AND D */
    gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.d;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA3:
{ /* AND E */
    gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.e;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA4:
{ /* AND H */
    gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.h;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA5:
{ /* AND L */
    gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.l;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA6:
{ /* AND B */
    gb->cpu_reg.a = gb->cpu_reg.a & __gb_read_full(gb, gb->cpu_reg.hl);
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA7:
{ /* AND A */
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA8:
{ /* XOR B */
    gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.b;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xA9:
{ /* XOR C */
    gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.c;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xAA:
{ /* XOR D */
    gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.d;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xAB:
{ /* XOR E */
    gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.e;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xAC:
{ /* XOR H */
    gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.h;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xAD:
{ /* XOR L */
    gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.l;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xAE:
{ /* XOR (HL) */
    gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read_full(gb, gb->cpu_reg.hl);
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xAF:
{ /* XOR A */
    gb->cpu_reg.a = 0x00;
    gb->cpu_reg.f_bits.z = 1;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB0:
{ /* OR B */
    gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.b;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB1:
{ /* OR C */
    gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.c;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB2:
{ /* OR D */
    gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.d;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB3:
{ /* OR E */
    gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.e;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB4:
{ /* OR H */
    gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.h;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB5:
{ /* OR L */
    gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.l;
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB6:
{ /* OR (HL) */
    gb->cpu_reg.a = gb->cpu_reg.a | __gb_read_full(gb, gb->cpu_reg.hl);
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB7:
{ /* OR A */
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xB8:
{ /* CP B */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.b;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    goto exit;
}

_0xB9:
{ /* CP C */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.c;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    goto exit;
}

_0xBA:
{ /* CP D */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.d;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    goto exit;
}

_0xBB:
{ /* CP E */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.e;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    goto exit;
}

_0xBC:
{ /* CP H */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.h;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    goto exit;
}

_0xBD:
{ /* CP L */
    uint16_t temp = gb->cpu_reg.a - gb->cpu_reg.l;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    goto exit;
}

/* TODO: Optimsation by combining similar opcode routines. */
_0xBE:
{ /* CP B */
    uint8_t val = __gb_read_full(gb, gb->cpu_reg.hl);
    uint16_t temp = gb->cpu_reg.a - val;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    goto exit;
}

_0xBF:
{ /* CP A */
    gb->cpu_reg.f_bits.z = 1;
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xC0:
{ /* RET NZ */
    if (!gb->cpu_reg.f_bits.z)
    {
        gb->cpu_reg.pc = __gb_read_full(gb, gb->cpu_reg.sp++);
        gb->cpu_reg.pc |= __gb_read_full(gb, gb->cpu_reg.sp++) << 8;
        inst_cycles += 12;
    }

    goto exit;
}

_0xC1:
{ /* POP BC */
    gb->cpu_reg.c = __gb_read_full(gb, gb->cpu_reg.sp++);
    gb->cpu_reg.b = __gb_read_full(gb, gb->cpu_reg.sp++);
    goto exit;
}

_0xC2:
{ /* JP NZ, imm */
    if (!gb->cpu_reg.f_bits.z)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
        temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        gb->cpu_reg.pc = temp;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xC3:
{ /* JP imm */
    uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
    temp |= __gb_read_full(gb, gb->cpu_reg.pc) << 8;
    gb->cpu_reg.pc = temp;
    goto exit;
}

_0xC4:
{ /* CALL NZ imm */
    if (!gb->cpu_reg.f_bits.z)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
        temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
        gb->cpu_reg.pc = temp;
        inst_cycles += 12;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xC5:
{ /* PUSH BC */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.b);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.c);
    goto exit;
}

_0xC6:
{ /* ADD A, imm */
    /* Taken from SameBoy, which is released under MIT Licence. */
    uint8_t value = __gb_read_full(gb, gb->cpu_reg.pc++);
    uint16_t calc = gb->cpu_reg.a + value;
    gb->cpu_reg.f_bits.z = ((uint8_t)calc == 0) ? 1 : 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0xF) + (value & 0xF) > 0x0F) ? 1 : 0;
    gb->cpu_reg.f_bits.c = calc > 0xFF ? 1 : 0;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.a = (uint8_t)calc;
    goto exit;
}

_0xC7:
{ /* RST 0x0000 */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0000;
    goto exit;
}

_0xC8:
{ /* RET Z */
    if (gb->cpu_reg.f_bits.z)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.sp++);
        temp |= __gb_read_full(gb, gb->cpu_reg.sp++) << 8;
        gb->cpu_reg.pc = temp;
        inst_cycles += 12;
    }

    goto exit;
}

_0xC9:
{ /* RET */
    uint16_t temp = __gb_read_full(gb, gb->cpu_reg.sp++);
    temp |= __gb_read_full(gb, gb->cpu_reg.sp++) << 8;
    gb->cpu_reg.pc = temp;
    goto exit;
}

_0xCA:
{ /* JP Z, imm */
    if (gb->cpu_reg.f_bits.z)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
        temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        gb->cpu_reg.pc = temp;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xCB:
{ /* CB INST */
    if (gb->is_cgb_mode)
    {
        inst_cycles = __gb_execute_cb__cgb(gb);
    }
    else
    {
        inst_cycles = __gb_execute_cb__dmg(gb);
    }
    goto exit;
}

_0xCC:
{ /* CALL Z, imm */
    if (gb->cpu_reg.f_bits.z)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
        temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
        gb->cpu_reg.pc = temp;
        inst_cycles += 12;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xCD:
{ /* CALL imm */
    uint16_t addr = __gb_read_full(gb, gb->cpu_reg.pc++);
    addr |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = addr;
    goto exit;
}

_0xCE:
{ /* ADC A, imm */
    uint8_t value, a, carry;
    value = __gb_read_full(gb, gb->cpu_reg.pc++);
    a = gb->cpu_reg.a;
    carry = gb->cpu_reg.f_bits.c;
    gb->cpu_reg.a = a + value + carry;

    gb->cpu_reg.f_bits.z = gb->cpu_reg.a == 0 ? 1 : 0;
    gb->cpu_reg.f_bits.h = ((a & 0xF) + (value & 0xF) + carry > 0x0F) ? 1 : 0;
    gb->cpu_reg.f_bits.c = (((uint16_t)a) + ((uint16_t)value) + carry > 0xFF) ? 1 : 0;
    gb->cpu_reg.f_bits.n = 0;
    goto exit;
}

_0xCF:
{ /* RST 0x0008 */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0008;
    goto exit;
}

_0xD0:
{ /* RET NC */
    if (!gb->cpu_reg.f_bits.c)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.sp++);
        temp |= __gb_read_full(gb, gb->cpu_reg.sp++) << 8;
        gb->cpu_reg.pc = temp;
        inst_cycles += 12;
    }

    goto exit;
}

_0xD1:
{ /* POP DE */
    gb->cpu_reg.e = __gb_read_full(gb, gb->cpu_reg.sp++);
    gb->cpu_reg.d = __gb_read_full(gb, gb->cpu_reg.sp++);
    goto exit;
}

_0xD2:
{ /* JP NC, imm */
    if (!gb->cpu_reg.f_bits.c)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
        temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        gb->cpu_reg.pc = temp;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xD4:
{ /* CALL NC, imm */
    if (!gb->cpu_reg.f_bits.c)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
        temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
        gb->cpu_reg.pc = temp;
        inst_cycles += 12;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xD5:
{ /* PUSH DE */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.d);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.e);
    goto exit;
}

_0xD6:
{ /* SUB imm */
    uint8_t val = __gb_read_full(gb, gb->cpu_reg.pc++);
    uint16_t temp = gb->cpu_reg.a - val;
    gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp & 0xFF);
    goto exit;
}

_0xD7:
{ /* RST 0x0010 */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0010;
    goto exit;
}

_0xD8:
{ /* RET C */
    if (gb->cpu_reg.f_bits.c)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.sp++);
        temp |= __gb_read_full(gb, gb->cpu_reg.sp++) << 8;
        gb->cpu_reg.pc = temp;
        inst_cycles += 12;
    }

    goto exit;
}

_0xD9:
{ /* RETI */
    uint16_t temp = __gb_read_full(gb, gb->cpu_reg.sp++);
    temp |= __gb_read_full(gb, gb->cpu_reg.sp++) << 8;
    gb->cpu_reg.pc = temp;
    gb->gb_ime = 1;
    gb->gb_ime_countdown = 0;
    goto exit;
}

_0xDA:
{ /* JP C, imm */
    if (gb->cpu_reg.f_bits.c)
    {
        uint16_t addr = __gb_read_full(gb, gb->cpu_reg.pc++);
        addr |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        gb->cpu_reg.pc = addr;
        inst_cycles += 4;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xDC:
{ /* CALL C, imm */
    if (gb->cpu_reg.f_bits.c)
    {
        uint16_t temp = __gb_read_full(gb, gb->cpu_reg.pc++);
        temp |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
        gb->cpu_reg.pc = temp;
        inst_cycles += 12;
    }
    else
        gb->cpu_reg.pc += 2;

    goto exit;
}

_0xDE:
{ /* SBC A, imm */
    uint8_t temp_8 = __gb_read_full(gb, gb->cpu_reg.pc++);
    uint16_t temp_16 = gb->cpu_reg.a - temp_8 - gb->cpu_reg.f_bits.c;
    gb->cpu_reg.f_bits.z = ((temp_16 & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ temp_8 ^ temp_16) & 0x10 ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp_16 & 0xFF00) ? 1 : 0;
    gb->cpu_reg.a = (temp_16 & 0xFF);
    goto exit;
}

_0xDF:
{ /* RST 0x0018 */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0018;
    goto exit;
}

_0xE0:
{ /* LD (0xFF00+imm), A */
    __gb_write_full(gb, 0xFF00 | __gb_read_full(gb, gb->cpu_reg.pc++), gb->cpu_reg.a);
    goto exit;
}

_0xE1:
{ /* POP HL */
    gb->cpu_reg.l = __gb_read_full(gb, gb->cpu_reg.sp++);
    gb->cpu_reg.h = __gb_read_full(gb, gb->cpu_reg.sp++);
    goto exit;
}

_0xE2:
{ /* LD (C), A */
    __gb_write_full(gb, 0xFF00 | gb->cpu_reg.c, gb->cpu_reg.a);
    goto exit;
}

_0xE5:
{ /* PUSH HL */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.h);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.l);
    goto exit;
}

_0xE6:
{ /* AND imm */
    /* TODO: Optimisation? */
    gb->cpu_reg.a = gb->cpu_reg.a & __gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 1;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xE7:
{ /* RST 0x0020 */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0020;
    goto exit;
}

_0xE8:
{ /* ADD SP, imm */
    int8_t offset = (int8_t)__gb_read_full(gb, gb->cpu_reg.pc++);
    uint16_t old_sp = gb->cpu_reg.sp;  // Store the original SP for flag calcs

    gb->cpu_reg.sp += offset;
    gb->cpu_reg.f_bits.z = 0;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((old_sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0;
    gb->cpu_reg.f_bits.c = ((old_sp & 0xFF) + (offset & 0xFF) > 0xFF);

    goto exit;
}

_0xE9:
{ /* JP (HL) */
    gb->cpu_reg.pc = gb->cpu_reg.hl;
    goto exit;
}

_0xEA:
{ /* LD (imm), A */
    uint16_t addr = __gb_read_full(gb, gb->cpu_reg.pc++);
    addr |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
    __gb_write_full(gb, addr, gb->cpu_reg.a);
    goto exit;
}

_0xEE:
{ /* XOR imm */
    gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xEF:
{ /* RST 0x0028 */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0028;
    goto exit;
}

_0xF0:
{ /* LD A, (0xFF00+imm) */
    gb->cpu_reg.a = __gb_read_full(gb, 0xFF00 | __gb_read_full(gb, gb->cpu_reg.pc++));
    goto exit;
}

_0xF1:
{ /* POP AF */
    uint8_t temp_8 = __gb_read_full(gb, gb->cpu_reg.sp++);
    gb->cpu_reg.f_bits.z = (temp_8 >> 7) & 1;
    gb->cpu_reg.f_bits.n = (temp_8 >> 6) & 1;
    gb->cpu_reg.f_bits.h = (temp_8 >> 5) & 1;
    gb->cpu_reg.f_bits.c = (temp_8 >> 4) & 1;
    gb->cpu_reg.a = __gb_read_full(gb, gb->cpu_reg.sp++);
    goto exit;
}

_0xF2:
{ /* LD A, (C) */
    gb->cpu_reg.a = __gb_read_full(gb, 0xFF00 | gb->cpu_reg.c);
    goto exit;
}

_0xF3:
{ /* DI */
    gb->gb_ime = 0;
    gb->gb_ime_countdown = 0;
    goto exit;
}

_0xF5:
{ /* PUSH AF */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.a);
    __gb_write_full(
        gb, --gb->cpu_reg.sp,
        gb->cpu_reg.f_bits.z << 7 | gb->cpu_reg.f_bits.n << 6 | gb->cpu_reg.f_bits.h << 5 |
            gb->cpu_reg.f_bits.c << 4
    );
    goto exit;
}

_0xF6:
{ /* OR imm */
    gb->cpu_reg.a = gb->cpu_reg.a | __gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = 0;
    gb->cpu_reg.f_bits.c = 0;
    goto exit;
}

_0xF7:
{ /* PUSH AF */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0030;
    goto exit;
}

_0xF8:
{ /* LD HL, SP+/-imm */
    /* Taken from SameBoy, which is released under MIT Licence. */
    int8_t offset = (int8_t)__gb_read_full(gb, gb->cpu_reg.pc++);
    gb->cpu_reg.hl = gb->cpu_reg.sp + offset;
    gb->cpu_reg.f_bits.z = 0;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0;
    gb->cpu_reg.f_bits.c = ((gb->cpu_reg.sp & 0xFF) + (offset & 0xFF) > 0xFF) ? 1 : 0;
    goto exit;
}

_0xF9:
{ /* LD SP, HL */
    gb->cpu_reg.sp = gb->cpu_reg.hl;
    goto exit;
}

_0xFA:
{ /* LD A, (imm) */
    uint16_t addr = __gb_read_full(gb, gb->cpu_reg.pc++);
    addr |= __gb_read_full(gb, gb->cpu_reg.pc++) << 8;
    gb->cpu_reg.a = __gb_read_full(gb, addr);
    goto exit;
}

_0xFB:
{ /* EI */
    gb->gb_ime_countdown = 2;
    goto exit;
}

_0xFE:
{ /* CP imm */
    uint8_t temp_8 = __gb_read_full(gb, gb->cpu_reg.pc++);
    uint16_t temp_16 = gb->cpu_reg.a - temp_8;
    gb->cpu_reg.f_bits.z = ((temp_16 & 0xFF) == 0x00);
    gb->cpu_reg.f_bits.n = 1;
    gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a ^ temp_8 ^ temp_16) & 0x10) ? 1 : 0;
    gb->cpu_reg.f_bits.c = (temp_16 & 0xFF00) ? 1 : 0;
    goto exit;
}

_0xFF:
{ /* RST 0x0038 */
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
    __gb_write_full(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
    gb->cpu_reg.pc = 0x0038;
    goto exit;
}

_invalid:
{
    (gb->gb_error)(gb, GB_INVALID_OPCODE, opcode);
    // Early exit
    gb->gb_frame = 1;
}

exit:
    return inst_cycles;
}

__shell static void __gb_interrupt(gb_s* gb)
{
    gb->gb_halt = 0;

    if (gb->gb_ime)
    {
        /* Disable interrupts */
        gb->gb_ime = 0;
        gb->gb_ime_countdown = 0;

        /* Push Program Counter */
        if (gb->is_cgb_mode)
            __gb_push16__cgb(gb, gb->cpu_reg.pc);
        else
            __gb_push16__dmg(gb, gb->cpu_reg.pc);

        /* Call interrupt handler if required. */
        if (gb->gb_reg.IF & gb->gb_reg.IE & VBLANK_INTR)
        {
            gb->cpu_reg.pc = VBLANK_INTR_ADDR;
            gb->gb_reg.IF ^= VBLANK_INTR;
        }
        else if (gb->gb_reg.IF & gb->gb_reg.IE & LCDC_INTR)
        {
            gb->cpu_reg.pc = LCDC_INTR_ADDR;
            gb->gb_reg.IF ^= LCDC_INTR;
        }
        else if (gb->gb_reg.IF & gb->gb_reg.IE & TIMER_INTR)
        {
            gb->cpu_reg.pc = TIMER_INTR_ADDR;
            gb->gb_reg.IF ^= TIMER_INTR;
        }
        else if (gb->gb_reg.IF & gb->gb_reg.IE & SERIAL_INTR)
        {
            gb->cpu_reg.pc = SERIAL_INTR_ADDR;
            gb->gb_reg.IF ^= SERIAL_INTR;
        }
        else if (gb->gb_reg.IF & gb->gb_reg.IE & CONTROL_INTR)
        {
            gb->cpu_reg.pc = CONTROL_INTR_ADDR;
            gb->gb_reg.IF ^= CONTROL_INTR;
        }
    }
}

__shell static uint16_t __gb_calc_halt_cycles(gb_s* gb)
{
    // In STOP mode, the CPU is paused until a button is pressed.
    if (gb->gb_halt && gb->direct.joypad != 0xFF)
    {
        gb->gb_stop = 0;
        return 16;
    }

#if 0
    // TODO: optimize serial
    if(gb->gb_reg.SC & SERIAL_SC_TX_START) return 16;
#endif

    uint32_t src[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

    if (gb->gb_reg.tac_enable)
    {
#if PGB_IS_CGB
        uint16_t tima_threshold = gb->gb_reg.tac_cycles >> gb->cgb_fast_mode;
#else
        uint16_t tima_threshold = gb->gb_reg.tac_cycles;
#endif

        if (tima_threshold == 0)
            tima_threshold = 1;

        uint16_t cycles_until_next_tick =
            tima_threshold - (gb->counter.tima_count % tima_threshold);
        if (cycles_until_next_tick == 0)
            cycles_until_next_tick = tima_threshold;
        uint16_t ticks_until_overflow = 0x100 - gb->gb_reg.TIMA;

        src[1] =
            ((uint32_t)(ticks_until_overflow - 1) * tima_threshold) + cycles_until_next_tick + 1;

        if (gb->gb_reg.tima_overflow_delay)
        {
            src[1] = 1;
        }
    }

    // PPU event calculation
    uint16_t ppu_cycles_remaining;
    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
    {
        ppu_cycles_remaining = LCD_FRAME_CYCLES - gb->counter.lcd_off_count;
    }
    else
    {
        switch (gb->lcd_mode)
        {
        case LCD_HBLANK:  // Mode 0
            ppu_cycles_remaining = gb->display.current_mode0_cycles - gb->counter.lcd_count;
            break;
        case LCD_VBLANK:  // Mode 1
            ppu_cycles_remaining = LCD_LINE_CYCLES - gb->counter.lcd_count;
            break;
        case LCD_SEARCH_OAM:  // Mode 2
            ppu_cycles_remaining = PPU_MODE_2_OAM_CYCLES - gb->counter.lcd_count;
            break;
        case LCD_TRANSFER:  // Mode 3
            ppu_cycles_remaining = gb->display.current_mode3_cycles - gb->counter.lcd_count;
            break;
        default:  // Should not happen
            ppu_cycles_remaining = 1;
            break;
        }
    }

    if ((int16_t)ppu_cycles_remaining <= 0)
    {
        ppu_cycles_remaining = 1;
    }
    src[2] = (uint32_t)ppu_cycles_remaining;

    // Find the minimum cycles until the next event
    uint32_t cycles = src[0];
    if (src[1] < cycles)
        cycles = src[1];
    if (src[2] < cycles)
        cycles = src[2];

    // ensure positive
    cycles = (cycles < 16) ? 16 : cycles;

    return (uint16_t)cycles;
}

const char* gb_get_rom_name(uint8_t* gb_rom, char* title_str);
void gb_reset(gb_s* gb, bool cgb_mode);

// Note: this function can be called on unswizzled structs;
// therefore, no pointers in the gb struct should be followed.
// Note: does not include size of script's save-state
__section__(".rare") uint32_t gb_get_state_size(gb_s* gb)
{
    return PGB_VERSIONED(gb_get_state_size)(gb);
}

__section__(".rare") void gb_state_save(gb_s* gb, char* out)
{
    // header
    struct StateHeader header;
    memset(&header, 0, sizeof(header));
    CB_ASSERT(strlen(CB_SAVE_STATE_MAGIC) == sizeof(header.magic));
    memcpy(header.magic, CB_SAVE_STATE_MAGIC, sizeof(header.magic));
    header.version = CB_SAVE_STATE_VERSION;
    header.gb_s_size = sizeof(gb_s);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    header.big_endian = 1;
#else
    header.big_endian = 0;
#endif
    header.bits = sizeof(void*);
    memcpy(out, &header, sizeof(header));

    PGB_VERSIONED(gb_state_save)(gb, out + sizeof(header));

    {
        StateHeader* header = (void*)out;
        CB_ASSERT(header->version == PGB_VERSION);
        CB_ASSERT(!strncmp(header->magic, CB_SAVE_STATE_MAGIC, sizeof(header->magic)));
    }
}

// returns NULL on success; error message otherwise
// if failure, no change is made to gb.
// Note: provided gb must already be initialized for the given ROM;
// in particular, it needs to have a gb_cart_ram field init'd with the correct
// size, and rom needs to be already loaded.
__section__(".rare") const char* gb_state_load(gb_s* gb, const char* const in, size_t size)
{
    // at least enough to read save header, rom header, and gb struct fields
    if (size < sizeof(struct StateHeader) + sizeof(gb_s) + ROM_HEADER_SIZE)
    {
        return "State size too small.";
    }

    struct StateHeader* header = (struct StateHeader*)in;

    if (strncmp(header->magic, CB_SAVE_STATE_MAGIC, sizeof(header->magic)))
    {
        return "Not a CrankBoy savestate.";
    }

    if (header->version > PGB_VERSION)
    {
        return "State comes from an incompatible future version of CrankBoy.";
    }

    if (header->bits != sizeof(void*))
    {
        return "State is for a different device (Playdate vs Simulator).";
    }

    if (header->version < PGB_VERSION)
    {
        char* upgraded_in;
        size_t upgraded_in_size;
        const char* result =
            PGB_VERSIONED(savestate_upgrade_to)(&upgraded_in, &upgraded_in_size, (char*)in, size);
        if (result)
            return result;
        if (upgraded_in != in)
        {
            result = gb_state_load(gb, upgraded_in, upgraded_in_size);
            cb_free(upgraded_in);
            return result;
        }
    }

    if (header->gb_s_size != sizeof(gb_s))
    {
        return "State is from an incompatible build (struct size mismatch).";
    }

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (!header->big_endian)
#else
    if (header->big_endian)
#endif
    {
        return "State endianness incorrect";
    }

    const char* result = PGB_VERSIONED(gb_state_load)(gb, in, size);
    if (result)
        return result;

    // re-compute precomputed fields
    __gb_update_selected_bank_addr(gb);
    __gb_update_selected_cart_bank_addr(gb);
    __gb_update_zero_bank_addr(gb);

    return NULL;
}

/**
 * Gets the size of the save file required for the ROM.
 */
uint_fast32_t gb_get_save_size(gb_s* gb)
{
    // Special case for MBC2, which has fixed internal RAM of 512.
    if (gb->mbc == 2)
        return 512;

    /* MBC7 has a 256-byte EEPROM. */
    if (gb->mbc == 7)
        return 512;

    const uint_fast16_t ram_size_location = 0x0149;
    const uint_fast32_t ram_sizes[] = {0x00, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    uint8_t ram_size = gb->gb_rom[ram_size_location];
    return ram_sizes[ram_size];
}

/**
 * Set the function used to handle serial transfer in the front-end. This is
 * optional.
 * gb_serial_transfer takes a byte to transmit and returns the received byte. If
 * no cable is connected to the console, return 0xFF.
 */
void gb_init_serial(
    gb_s* gb, void (*gb_serial_tx)(gb_s*, const uint8_t),
    enum gb_serial_rx_ret_e (*gb_serial_rx)(gb_s*, uint8_t*)
)
{
    gb->gb_serial_tx = gb_serial_tx;
    gb->gb_serial_rx = gb_serial_rx;
}

/**
 * Resets the context, and initialises startup values.
 */
__section__(".rare") void gb_reset(gb_s* gb, bool cgb_mode)
{
    gb->gb_halt = 0;
    gb->gb_ime = 1;

    /* Initialise MBC values. */
    gb->selected_rom_bank = 1;
    gb->cart_ram_bank = 0;
    gb->enable_cart_ram = 0;
    gb->cart_mode_select = 0;

    /* Initialize RTC latching values */
    gb->rtc_latch_s1 = 0;
    memset(gb->latched_rtc, 0, sizeof(gb->latched_rtc));

    /* Initialise MBC7 values. */
    if (gb->mbc == 7)
    {
        gb->mbc7.ram_enable_1 = 0;
        gb->mbc7.ram_enable_2 = 0;
        gb->mbc7.accel_latch_state = 0;
        gb->mbc7.accel_x_latched = 0x8000;
        gb->mbc7.accel_y_latched = 0x8000;
        gb->mbc7.eeprom_state = 0;
        gb->mbc7.eeprom_write_enabled = 0;
        gb->mbc7.eeprom_pins = 0x01; /* DO is high by default */
    }

    __gb_update_selected_bank_addr(gb);
    __gb_update_selected_cart_bank_addr(gb);
    __gb_update_zero_bank_addr(gb);

    if (cgb_mode)
    {
        /*****************************************************************/
        /* --- POST-BOOT ROM STATE (CGB Skip-BIOS) --- */
        /*****************************************************************/
        gb->cpu_reg.af = 0x8011;
        gb->cpu_reg.bc = 0x0000;
        gb->cpu_reg.de = 0xFF56;
        gb->cpu_reg.hl = 0x000D;
        gb->cpu_reg.sp = 0xFFFE;
        gb->cpu_reg.pc = 0x0100;

        /* Set registers to state after CGB boot ROM */
        gb->gb_reg.P1 = 0xCF;
        gb->gb_reg.SB = 0x00;
        gb->gb_reg.SC = 0x7E;
        gb->gb_reg.DIV = 0x26;
        gb->gb_reg.TIMA = 0x00;
        gb->gb_reg.TMA = 0x00;
        gb->gb_reg.TAC = 0xF8;
        gb->gb_reg.IF = 0xE1;
        gb->gb_reg.LCDC = 0x91;
        gb->gb_reg.STAT = 0x85;
        gb->gb_reg.SCY = 0x00;
        gb->gb_reg.SCX = 0x00;
        gb->gb_reg.LY = 144;
        gb->gb_reg.LYC = 0x00;
        gb->gb_reg.DMA = 0x00;
        __gb_write_full(gb, 0xFF47, 0xFC);
        __gb_write_full(gb, 0xFF48, 0xFF);
        __gb_write_full(gb, 0xFF49, 0xFF);
        gb->gb_reg.WY = 0x00;
        gb->gb_reg.WX = 0x00;
        gb->gb_reg.IE = 0x00;

        /* Sound registers */
        __gb_write_full(gb, 0xFF10, 0x80);
        __gb_write_full(gb, 0xFF11, 0xBF);
        __gb_write_full(gb, 0xFF12, 0xF3);
        __gb_write_full(gb, 0xFF13, 0xFF);
        __gb_write_full(gb, 0xFF14, 0xBF);
        __gb_write_full(gb, 0xFF16, 0x3F);
        __gb_write_full(gb, 0xFF17, 0x00);
        __gb_write_full(gb, 0xFF18, 0xFF);
        __gb_write_full(gb, 0xFF19, 0xBF);
        __gb_write_full(gb, 0xFF1A, 0x7F);
        __gb_write_full(gb, 0xFF1B, 0xFF);
        __gb_write_full(gb, 0xFF1C, 0x9F);
        __gb_write_full(gb, 0xFF1D, 0xFF);
        __gb_write_full(gb, 0xFF1E, 0xBF);
        __gb_write_full(gb, 0xFF20, 0xFF);
        __gb_write_full(gb, 0xFF21, 0x00);
        __gb_write_full(gb, 0xFF22, 0x00);
        __gb_write_full(gb, 0xFF23, 0xBF);
        __gb_write_full(gb, 0xFF24, 0x77);
        __gb_write_full(gb, 0xFF25, 0xF3);
        __gb_write_full(gb, 0xFF26, 0xF1);

        /* Wave RAM */
        __gb_write_full(gb, 0xFF30, 0xAC);
        __gb_write_full(gb, 0xFF31, 0xDD);
        __gb_write_full(gb, 0xFF32, 0xDA);
        __gb_write_full(gb, 0xFF33, 0x48);
        __gb_write_full(gb, 0xFF34, 0x36);
        __gb_write_full(gb, 0xFF35, 0x02);
        __gb_write_full(gb, 0xFF36, 0xCF);
        __gb_write_full(gb, 0xFF37, 0x16);
        __gb_write_full(gb, 0xFF38, 0x2C);
        __gb_write_full(gb, 0xFF39, 0x04);
        __gb_write_full(gb, 0xFF3A, 0xE5);
        __gb_write_full(gb, 0xFF3B, 0x2C);
        __gb_write_full(gb, 0xFF3C, 0xAC);
        __gb_write_full(gb, 0xFF3D, 0xDD);
        __gb_write_full(gb, 0xFF3E, 0xDA);
        __gb_write_full(gb, 0xFF3F, 0x48);

        /* CGB internal timer is 0x267C */
        gb->counter.div_count = 0x7C;
        gb->lcd_mode = LCD_VBLANK;
    }
    else
    {
        /*****************************************************************/
        /* --- POST-BOOT ROM STATE (DMG Skip-BIOS) --- */
        /*****************************************************************/

        /* Initialize CPU registers as though the boot ROM has just finished. */
        gb->cpu_reg.af = 0xB001;
        gb->cpu_reg.bc = 0x0013;
        gb->cpu_reg.de = 0x00D8;
        gb->cpu_reg.hl = 0x014D;
        gb->cpu_reg.sp = 0xFFFE;
        gb->cpu_reg.pc = 0x0100;

        /* Set registers to state after DMG boot ROM */
        gb->gb_reg.P1 = 0xCF;
        gb->gb_reg.SB = 0x00;
        gb->gb_reg.SC = 0x7E;
        gb->gb_reg.DIV = 0xAB;
        gb->gb_reg.TIMA = 0x00;
        gb->gb_reg.TMA = 0x00;
        gb->gb_reg.TAC = 0xF8;
        gb->gb_reg.IF = 0xE1;

        /* Sound registers */
        __gb_write_full(gb, 0xFF10, 0x80);
        __gb_write_full(gb, 0xFF11, 0xBF);
        __gb_write_full(gb, 0xFF12, 0xF3);
        __gb_write_full(gb, 0xFF13, 0xFF);
        __gb_write_full(gb, 0xFF14, 0xBF);
        __gb_write_full(gb, 0xFF16, 0x3F);
        __gb_write_full(gb, 0xFF17, 0x00);
        __gb_write_full(gb, 0xFF18, 0xFF);
        __gb_write_full(gb, 0xFF19, 0xBF);
        __gb_write_full(gb, 0xFF1A, 0x7F);
        __gb_write_full(gb, 0xFF1B, 0xFF);
        __gb_write_full(gb, 0xFF1C, 0x9F);
        __gb_write_full(gb, 0xFF1D, 0xFF);
        __gb_write_full(gb, 0xFF1E, 0xBF);
        __gb_write_full(gb, 0xFF20, 0xFF);
        __gb_write_full(gb, 0xFF21, 0x00);
        __gb_write_full(gb, 0xFF22, 0x00);
        __gb_write_full(gb, 0xFF23, 0xBF);
        __gb_write_full(gb, 0xFF24, 0x77);
        __gb_write_full(gb, 0xFF25, 0xF3);
        __gb_write_full(gb, 0xFF26, 0xF1);

        /* Wave RAM */
        __gb_write_full(gb, 0xFF30, 0xAC);
        __gb_write_full(gb, 0xFF31, 0xDD);
        __gb_write_full(gb, 0xFF32, 0xDA);
        __gb_write_full(gb, 0xFF33, 0x48);
        __gb_write_full(gb, 0xFF34, 0x36);
        __gb_write_full(gb, 0xFF35, 0x02);
        __gb_write_full(gb, 0xFF36, 0xCF);
        __gb_write_full(gb, 0xFF37, 0x16);
        __gb_write_full(gb, 0xFF38, 0x2C);
        __gb_write_full(gb, 0xFF39, 0x04);
        __gb_write_full(gb, 0xFF3A, 0xE5);
        __gb_write_full(gb, 0xFF3B, 0x2C);
        __gb_write_full(gb, 0xFF3C, 0xAC);
        __gb_write_full(gb, 0xFF3D, 0xDD);
        __gb_write_full(gb, 0xFF3E, 0xDA);
        __gb_write_full(gb, 0xFF3F, 0x48);

        gb->gb_reg.LCDC = 0x91;
        gb->gb_reg.STAT = 0x85;  // Mode 1 (VBlank)
        gb->gb_reg.SCY = 0x00;
        gb->gb_reg.SCX = 0x00;
        gb->gb_reg.LY = 144;
        gb->gb_reg.LYC = 0x00;
        gb->gb_reg.DMA = 0xFF;
        __gb_write_full(gb, 0xFF47, 0xFC);
        __gb_write_full(gb, 0xFF48, 0xFF);
        __gb_write_full(gb, 0xFF49, 0xFF);
        gb->gb_reg.WY = 0x00;
        gb->gb_reg.WX = 0x00;
        gb->gb_reg.IE = 0x00;

        /* DMG internal timer is 0xABCC */
        gb->counter.div_count = 0xCC;
        gb->lcd_mode = LCD_VBLANK;

        // F-1 Pole Position checks the value at 0xFF80 and enters an
        // infinite rst loop if it's zero. Real hardware boot ROM leaves
        // non-zero garbage here.
        gb->hram[0x80] = 0x95;
    }

    /* Common state for all modes */
    gb->counter.lcd_count = 0;
    gb->counter.tima_count = 0;
    gb->counter.serial_count = 0;
    gb->counter.lcd_off_count = 0;

    gb->printer_stub_state = 0;
    gb->printer_data_len = 0;
    gb->printer_last_cmd = 0;

    __gb_update_tac(gb);
    __gb_update_map_pointers(gb);

    gb->direct.joypad = 0xFF;
    gb->direct.stat_line = 0;

    gb->gb_reg.tima_overflow_delay = 0;

    gb->direct.crank_menu_accumulation = 0x8000;
    gb->direct.crank_menu_delta = 0;

    memset(gb->vram, 0x00, VRAM_SIZE_CGB);
    memset(gb->wram, 0x00, WRAM_SIZE_CGB);
}

/**
 * Initialise the emulator context. gb_reset() is also called to initialise
 * the CPU.
 */
__section__(".rare") enum gb_init_error_e gb_init(
    gb_s* gb, uint8_t* wram, uint8_t* vram, uint8_t* lcd, uint8_t* gb_rom, size_t rom_size,
    void (*gb_error)(gb_s*, const enum gb_error_e, const uint16_t), void* priv, bool cgb_mode
)
{
    const uint16_t mbc_location = 0x0147;
    const uint16_t bank_count_location = 0x0148;
    const uint16_t ram_size_location = 0x0149;
    /**
     * Table for cartridge type (MBC). -1 if invalid.
     * TODO: MMM01 is unsupported.
     * TODO: MBC6 is unsupported.
     * TODO: POCKET CAMERA is unsupported.
     * TODO: BANDAI TAMA5 is unsupported.
     * TODO: HuC3 is unsupported.
     * TODO: HuC1 is unsupported.
     **/
    /* clang-format off */
    const uint8_t cart_mbc[] =
    {
        0, 1, 1, 1, -1, 2, 2, -1, 0, 0, -1, 0, 0, 0, -1, 3,  /* 00-0F */
        3, 3, 3, 3, -1, -1, -1, -1, -1, 5, 5, 5, 5, 5, 5, 7, /* 10-1F */
        7, -1, 7                                             /* 20-2F */
    };
    const uint8_t cart_ram[] =
    {
        0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, /* 00-0F */
        1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, /* 10-1F */
        1, 0, 1                                         /* 20-2F */
    };
    const uint16_t num_rom_banks_mask[] =
    {
        2, 4, 8, 16, 32, 64, 128, 256, 512
    };
    const uint8_t num_ram_banks[] =
    {
        0, 1, 1, 4, 16, 8
    };
    /* clang-format on */

    static uint8_t xram[XRAM_SIZE];

    memset(xram, 0, XRAM_SIZE);

    gb->xram = xram;

    gb->wram = wram;
    gb->vram = vram;
    memset(gb->xram, 0, XRAM_SIZE);
    gb->lcd = lcd;
    gb->gb_rom = gb_rom;
    gb->gb_rom_size = rom_size;
    gb->gb_error = gb_error;
    gb->direct.priv = priv;

    __gb_init_memory_pointers(gb);

    static gb_breakpoint breakpoints[MAX_BREAKPOINTS];
    memset(breakpoints, 0xFF, sizeof(breakpoints));
    gb->breakpoints = breakpoints;

    /* Initialise serial transfer function to NULL. If the front-end does
     * not provide serial support, Peanut-GB will emulate no cable connected
     * automatically. */
    gb->gb_serial_tx = NULL;
    gb->gb_serial_rx = NULL;

    const uint16_t cgb_flag_location = 0x0143;
    const uint8_t cgb_flag = gb->gb_rom[cgb_flag_location];
    bool requires_cgb = true;

    if (!cgb_mode && !(GB_SUPPORT_DMG & gb_get_models_supported(gb_rom)))
    {
        requires_cgb = false;
    }

#if 0 /* ignore checksum */
    /* Check valid ROM using checksum value. */
    {
        uint8_t x = 0;

        for (uint16_t i = 0x0134; i <= 0x014C; i++)
            x = x - gb->gb_rom[i] - 1;

        if (x != gb->gb_rom[ROM_HEADER_CHECKSUM_LOC])
            return GB_INIT_INVALID_CHECKSUM;
    }
#endif

    /* Check if cartridge type is supported, and set MBC type. */
    {
        const uint8_t mbc_value = gb->gb_rom[mbc_location];

        if (mbc_value > sizeof(cart_mbc) - 1 || (gb->mbc = cart_mbc[mbc_value]) == 255u)
            return GB_INIT_CARTRIDGE_UNSUPPORTED;
    }

    gb->cart_ram = cart_ram[gb->gb_rom[mbc_location]];
    gb->cart_battery = gb_get_rom_uses_battery(gb->gb_rom);
    gb->num_rom_banks_mask = num_rom_banks_mask[gb->gb_rom[bank_count_location]] - 1;
    gb->num_ram_banks = num_ram_banks[gb->gb_rom[ram_size_location]];

    gb->is_cgb_mode = (gb->gb_rom[0x0143] & 0x80) && cgb_mode;
    gb->cgb_fast_mode = false;
    gb->cgb_fast_mode_armed = false;
    gb->cgb_wram_bank = 1;
    gb->cgb_ff6c = 0;
    gb->cgb_ff75 = 0;
    gb->cgb_vram_bank = 0;
    gb->cgb_ff7x[0] = 0;
    gb->cgb_ff7x[1] = 0;
    gb->cgb_ff7x[2] = 0;
    gb->cgb_hdma_active = false;

    gb->is_mbc1m = __gb_detect_mbc1m(gb);
    if (gb->is_mbc1m)
        gb->cart_mode_select = 0;
    // Initialize cached base (0 for non-MBC1M)
    gb->zero_bank_base = (gb->is_mbc1m ? ((gb->cart_ram_bank & 0x03) << 4) * ROM_BANK_SIZE : 0);

    gb->lcd_blank = 0;

    gb->direct.sound = ENABLE_SOUND;
    gb->direct.interlace_mask = 0xFF;
    gb->direct.enable_xram = 0;

    char title_str[17];
    gb_get_rom_name(gb->gb_rom, title_str);

    return requires_cgb ? GB_INIT_NO_ERROR_BUT_REQUIRES_CGB : GB_INIT_NO_ERROR;
}

// returns negative if failure
// returns breakpoint index otherwise
__section__(".rare") int set_hw_breakpoint(gb_s* gb, uint32_t rom_addr)
{
    size_t rom_size = 0x4000 * (gb->num_rom_banks_mask + 1);
    if (rom_addr > rom_size)
        return -2;

    for (size_t i = 0; i < MAX_BREAKPOINTS; ++i)
    {
        if (gb->breakpoints[i].rom_addr != 0xFFFFFF)
            continue;

        // found a breakpoint slot to use
        gb->breakpoints[i].rom_addr = rom_addr;
        gb->breakpoints[i].opcode = gb->gb_rom[rom_addr];
        gb->gb_rom[rom_addr] = CB_HW_BREAKPOINT_OPCODE;
        return i;
    }

    // couldn't find a breakpoint
    return -1;
}

// returns 0 if no breakpoint at current location
// returns cycles executed if breakpoint existed (runs breakpoint)
static __section__(".rare") int __gb_try_breakpoint(gb_s* gb)
{
    // only ROM-address breakpoints are supported
    size_t pc = gb->cpu_reg.pc - 1;
    if (pc >= 0x8000)
        return 0;

    // Use cached zero-bank base (0 for non-MBC1M)
    uint32_t base_or_bank =
        (pc < 0x4000) ? gb->zero_bank_base
                      : ((gb->selected_rom_bank & gb->num_rom_banks_mask) * ROM_BANK_SIZE);

    size_t rom_addr = base_or_bank + (pc % 0x4000);

    for (int i = 0; i < MAX_BREAKPOINTS; ++i)
    {
        int bp_addr = gb->breakpoints[i].rom_addr;
        int opcode = gb->breakpoints[i].opcode;
        if ((rom_addr & 0xFFFFFF) != bp_addr)
            continue;
        // breakpoint found!

        if unlikely (opcode == CB_HW_BREAKPOINT_OPCODE)
        {
            // this is pretty messed up, but let's handle it gracefully
            __gb_on_breakpoint(gb, i);
            return 4;
        }
        else
        {
            // restore to before running the breakpoint
            gb->gb_rom[rom_addr] = opcode;
            uint16_t prev_pc = --gb->cpu_reg.pc;
            uint16_t prev_bank = gb->selected_rom_bank;

            // handle breakpoint
            __gb_on_breakpoint(gb, i);

            int cycles = 0;

            // if bank,PC did not change, perform replaced instruction
            if (prev_pc == gb->cpu_reg.pc && prev_bank == gb->selected_rom_bank)
            {
                if (gb->is_cgb_mode)
                    cycles = __gb_run_instruction_micro__cgb(gb);
                else
                    cycles = __gb_run_instruction_micro__dmg(gb);
            }

            // restore breakpoint
            gb->breakpoints[i].opcode = gb->gb_rom[rom_addr];
            gb->gb_rom[rom_addr] = CB_HW_BREAKPOINT_OPCODE;
            return cycles <= 0 ? 4 : cycles;
        }
    }

    return 0;
}

#if ENABLE_LCD

void gb_init_lcd(gb_s* gb)
{
    gb->direct.frame_skip = 0;

    gb->display.window_clear = 0;
    gb->display.WY = 0;
    gb->lcd_master_enable = 1;

    return;
}

#else

void gb_init_lcd(gb_s* gb)
{
}

#endif

__section__(".rare") static u8 __gb_invalid_instruction(gb_s* restrict gb, uint8_t opcode)
{
    if (opcode == CB_HW_BREAKPOINT_OPCODE)
    {
        int rv = __gb_try_breakpoint(gb);
        if (rv > 0)
        {
            return rv;
        }
    }

    (gb->gb_error)(gb, GB_INVALID_OPCODE, opcode);
    gb->gb_frame = 1;
    return 0;
}

__shell static u8 __gb_rare_instruction(gb_s* restrict gb, uint8_t opcode)
{
    switch (opcode)
    {
    case 0x08:  // ld (a16), SP
        if (gb->is_cgb_mode)
        {
            __gb_write16__cgb(gb, __gb_fetch16__cgb(gb), gb->cpu_reg.sp);
        }
        else
        {
            __gb_write16__dmg(gb, __gb_fetch16__dmg(gb), gb->cpu_reg.sp);
        }
        return 5 * 4;
    case 0x10:  // stop
        gb->gb_ime = 0;
        gb->gb_halt = 1;
        playdate->system->logToConsole("'stop' instr");
        return 1 * 4;
    case 0x27:  // daa
    {
        uint16_t a = gb->cpu_reg.a;

        if (gb->cpu_reg.f_bits.n)
        {
            if (gb->cpu_reg.f_bits.h)
                a = (a - 0x06) & 0xFF;

            if (gb->cpu_reg.f_bits.c)
                a -= 0x60;
        }
        else
        {
            if (gb->cpu_reg.f_bits.h || (a & 0x0F) > 9)
                a += 0x06;

            if (gb->cpu_reg.f_bits.c || a > 0x9F)
                a += 0x60;
        }

        if ((a & 0x100) == 0x100)
            gb->cpu_reg.f_bits.c = 1;

        gb->cpu_reg.a = a;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0);
        gb->cpu_reg.f_bits.h = 0;
    }
        return 1 * 4;
    case 0x76:
        // The HALT bug is only present on the DMG.
        if (!gb->is_cgb_mode && gb->gb_ime == 0 && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
        {
            gb->cpu_reg.pc--;  // HALT bug
        }
        else
        {
            gb->gb_halt = 1;
        }
        return 1 * 4;
    case 0xE8:
    case 0xF8:
    {
        int8_t offset;
        if (gb->is_cgb_mode)
        {
            offset = (int8_t)__gb_read__cgb(gb, gb->cpu_reg.pc++);
        }
        else
        {
            offset = (int8_t)__gb_read__dmg(gb, gb->cpu_reg.pc++);
        }

        if (opcode == 0xF8)
        {
            uint16_t sp = gb->cpu_reg.sp;
            gb->cpu_reg.hl = sp + offset;

            gb->cpu_reg.f_bits.z = 0;
            gb->cpu_reg.f_bits.n = 0;
            gb->cpu_reg.f_bits.h = ((sp & 0xF) + (offset & 0xF) > 0xF);
            gb->cpu_reg.f_bits.c = ((sp & 0xFF) + (offset & 0xFF) > 0xFF);
            return 3 * 4;
        }
        else
        {
            uint16_t old_sp = gb->cpu_reg.sp;
            gb->cpu_reg.sp += offset;

            gb->cpu_reg.f_bits.z = 0;
            gb->cpu_reg.f_bits.n = 0;
            gb->cpu_reg.f_bits.h = ((old_sp & 0xF) + (offset & 0xF) > 0xF);
            gb->cpu_reg.f_bits.c = ((old_sp & 0xFF) + (offset & 0xFF) > 0xFF);
            return 4 * 4;
        }
    }
    case 0xE9:
        gb->cpu_reg.pc = gb->cpu_reg.hl;
        return 4;
    case 0xF3:
        gb->gb_ime = 0;
        gb->gb_ime_countdown = 0;
        return 1 * 4;
    case 0xF9:
        gb->cpu_reg.sp = gb->cpu_reg.hl;
        return 2 * 4;
    case 0xFB:
        gb->gb_ime_countdown = 2;
        return 1 * 4;
    default:
        return __gb_invalid_instruction(gb, opcode);
    }
}

// allows us to reuse the same code for different systems.
// this functions essentially like C++ templates.
#define $__(x, y) x##__##y
#define $_(x, y) $__(x, y)
#define $(x) $_(x, PGB_TEMPLATE)

// ------------ DMG ------------

#define PGB_TEMPLATE dmg
#define PGB_IS_DMG 1
#define PGB_IS_CGB 0

#define __core __core_dmg
#define __core_section(x) __core_dmg_section(x)

#include "peanut_gb_core.h"

#undef __core
#undef __core_section
#undef PGB_IS_DMG
#undef PGB_IS_CGB

// ------------ CGB ------------

#define PGB_TEMPLATE cgb
#define PGB_IS_DMG 0
#define PGB_IS_CGB 1
#define __core __core_cgb
#define __core_section(x) __core_cgb_section(x)

#include "peanut_gb_core.h"

#undef __core
#undef __core_section
#undef PGB_IS_DMG
#undef PGB_IS_CGB

// -----------------------------

void gb_step_cpu(gb_s* gb)
{
    if (gb->is_cgb_mode)
        __gb_step_cpu__cgb(gb);
    else
        __gb_step_cpu__dmg(gb);
}

void gb_run_frame(gb_s* gb)
{
    if (gb->is_cgb_mode)
        gb_run_frame__cgb(gb);
    else
        gb_run_frame__dmg(gb);
}

#endif  // PGB_IMPL
#endif  // PEANUT_GB_H
