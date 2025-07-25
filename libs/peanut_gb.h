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

#include "../libs/minigb_apu/minigb_apu.h"
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

/**
 * Sound support must be provided by an external library. When audio_read() and
 * audio_write() functions are provided, define ENABLE_SOUND to a non-zero value
 * before including peanut_gb.h in order for these functions to be used.
 */
#ifndef ENABLE_SOUND
#define ENABLE_SOUND 1
#endif

#ifndef ENABLE_BGCACHE_DEFERRED
#define ENABLE_BGCACHE_DEFERRED 1
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
#define VRAM_SIZE 0x2000
#define HRAM_SIZE 0x0100
#define OAM_SIZE 0x00A0

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

/* LCD characteristics */
#define LCD_VERT_LINES 154
#define LCD_LINE_CYCLES 456
#define LCD_FRAME_CYCLES (LCD_LINE_CYCLES * LCD_VERT_LINES)
#define LCD_WIDTH 160
#define LCD_PACKING 4 /* pixels per byte */
#define LCD_BITS_PER_PIXEL (8 / LCD_PACKING)
#define LCD_WIDTH_PACKED (LCD_WIDTH / LCD_PACKING)
#define LCD_HEIGHT 144
#define LCD_FRAME_CYCLES (LCD_LINE_CYCLES * LCD_VERT_LINES)

/* Simplified PPU timing model for performance */
#define PPU_MODE_2_OAM_CYCLES 80
#define PPU_MODE_3_VRAM_CYCLES 168
#define PPU_MODE_0_HBLANK_CYCLES 208 /* 456 - 80 - 168 */

// FIXME -- do we need *2? Was intended for front buffer / back buffer
#define LCD_SIZE (LCD_HEIGHT * LCD_WIDTH_PACKED * 2)

// 2 tile indexing modes
// 2 screens
// 256 lines
// 256 pixels
// 4 pixels per byte
#define BGCACHE_SIZE (2 * 2 * 256 * 256 / 4)
#define BGCACHE_STRIDE (256 / 4)

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

#define ROM_HEADER_CHECKSUM_LOC 0x014D

#define CB_HW_BREAKPOINT_OPCODE 0xD3
#define MAX_BREAKPOINTS 0x80

#define PEANUT_GB_ARRAYSIZE(array) (sizeof(array) / sizeof(array[0]))

#define CB_SAVE_STATE_MAGIC "\xFA\x43\42sav\n\x1A"
#define CB_SAVE_STATE_VERSION 0

#define IO_PLAYDATE_EXTENSION_CTL 0x57
#define IO_PLAYDATE_EXTENSION_CRANK_LO 0x58
#define IO_PLAYDATE_EXTENSION_CRANK_HI 0x59
#define IO_PLAYDATE_EXTENSION_ACCX_LO 0x5A
#define IO_PLAYDATE_EXTENSION_ACCX_HI 0x5B
#define IO_PLAYDATE_EXTENSION_ACCY_LO 0x5C
#define IO_PLAYDATE_EXTENSION_ACCY_HI 0x5D
#define IO_PLAYDATE_EXTENSION_ACCZ_LO 0x5E
#define IO_PLAYDATE_EXTENSION_ACCZ_HI 0x5F

typedef struct gb_breakpoint
{
    // -1 to disable
    uint32_t rom_addr : 24;

    // what byte was replaced?
    char opcode;
} gb_breakpoint;

struct cpu_registers_s
{
    union
    {
        struct
        {
            uint8_t c;
            uint8_t b;
        };
        uint16_t bc;
    };

    union
    {
        struct
        {
            uint8_t e;
            uint8_t d;
        };
        uint16_t de;
    };

    union
    {
        struct
        {
            uint8_t l;
            uint8_t h;
        };
        uint16_t hl;
    };

    /* Combine A and F registers. */
    union
    {
        struct
        {
            uint8_t a;
            /* Define specific bits of Flag register. */
            union
            {
                struct
                {
                    uint8_t unused : 4;
                    uint8_t c : 1; /* Carry flag. */
                    uint8_t h : 1; /* Half carry flag. */
                    uint8_t n : 1; /* Add/sub flag. */
                    uint8_t z : 1; /* Zero flag. */
                } f_bits;
                uint8_t f;
            };
        };
        uint16_t af;
    };

    uint16_t sp; /* Stack pointer */
    uint16_t pc; /* Program counter */
};

struct count_s
{
    uint_fast16_t lcd_count;     /* LCD Timing */
    uint_fast16_t div_count;     /* Divider Register Counter */
    uint_fast16_t tima_count;    /* Timer Counter */
    uint_fast16_t serial_count;  /* Serial Counter */
    uint_fast32_t lcd_off_count; /* Cycles LCD has been disabled */
};

struct gb_registers_s
{
    /* Registers sorted by memory address. */

    /* Joypad info (0xFF00) */
    uint8_t P1;

    /* Serial data (0xFF01 - 0xFF02) */
    uint8_t SB;
    uint8_t SC;

    /* Timer Registers (0xFF04 - 0xFF07) */
    uint8_t DIV;
    uint8_t TIMA;
    uint8_t TMA;
    union
    {
        struct
        {
            uint8_t tac_rate : 2;   /* Input clock select */
            uint8_t tac_enable : 1; /* Timer enable */
            uint8_t unused : 5;
        };
        uint8_t TAC;
    };

    /* Interrupt Flag (0xFF0F) */
    uint8_t IF;

    /* LCD Registers (0xFF40 - 0xFF4B) */
    uint8_t LCDC;
    uint8_t STAT;
    uint8_t SCY;
    uint8_t SCX;
    uint8_t LY;
    uint8_t LYC;
    uint8_t DMA;
    uint8_t BGP;
    uint8_t OBP0;
    uint8_t OBP1;
    uint8_t WY;
    uint8_t WX;

    /* Interrupt Enable (0xFFFF) */
    uint8_t IE;

    /* Internal emulator state for timer implementation. */
    uint16_t tac_cycles;
    uint8_t tac_cycles_shift;

    uint8_t tima_overflow_delay : 1;
};

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

/**
 * Emulator context.
 *
 * Only values within the `direct` struct may be modified directly by the
 * front-end implementation. Other variables must not be modified.
 */
struct gb_s
{
    uint8_t* gb_rom;
    uint8_t* gb_cart_ram;

    /**
     * Notify front-end of error.
     *
     * \param gb_s          emulator context
     * \param gb_error_e    error code
     * \param val           arbitrary value related to error
     */
    void (*gb_error)(struct gb_s*, const enum gb_error_e, const uint16_t val);

    /* Transmit one byte and return the received byte. */
    void (*gb_serial_tx)(struct gb_s*, const uint8_t tx);
    enum gb_serial_rx_ret_e (*gb_serial_rx)(struct gb_s*, uint8_t* rx);

    // shortcut to swappable bank (addr - 0x4000 offset built in)
    uint8_t* selected_bank_addr;

    struct
    {
        uint8_t gb_halt : 1;
        uint8_t gb_ime : 1;
        uint8_t gb_bios_enable : 1;

        /* gb_frame is set when the equivalent time of a frame has
         * passed. It is likely that a new frame has been drawn,
         * but it is also possible that the LCD was off. */

        uint8_t gb_frame : 1;

#define LCD_HBLANK 0
#define LCD_VBLANK 1
#define LCD_SEARCH_OAM 2
#define LCD_TRANSFER 3
        uint8_t lcd_mode : 2;
        uint8_t lcd_blank : 1;
        uint8_t lcd_master_enable : 1;
    };

    /* Cartridge information:
     * Memory Bank Controller (MBC) type. */
    uint8_t mbc;
    /* Whether the MBC has internal RAM. */
    uint8_t cart_ram : 1;
    uint8_t cart_battery : 1;

    // state flags for cart ram
    uint8_t enable_cart_ram : 1;
    uint8_t cart_mode_select : 1;  // 1 if ram mode
    uint8_t joypad_interrupt : 1;

    uint8_t overclock : 2;

    uint8_t* selected_cart_bank_addr;

    /* Number of ROM banks in cartridge. */
    uint16_t num_rom_banks_mask;
    /* Number of RAM banks in cartridge. */
    uint8_t num_ram_banks;

    uint16_t selected_rom_bank;
    /* WRAM and VRAM bank selection not available. */
    uint8_t cart_ram_bank;

    /* Tracks if 0x00 was the last value written to 6000-7FFF */
    uint8_t rtc_latch_s1;

    /* Stores a copy of the RTC registers when latched */
    uint8_t latched_rtc[5];

    union
    {
        struct
        {
            uint8_t sec;
            uint8_t min;
            uint8_t hour;
            uint8_t yday;
            uint8_t high;
        } rtc_bits;
        uint8_t cart_rtc[5];

        struct
        {
            /* RAM Enable Flags */
            uint8_t ram_enable_1;
            uint8_t ram_enable_2;

            /* Accelerometer State */
            uint8_t accel_latch_state;
            uint16_t accel_x_latched;
            uint16_t accel_y_latched;

            /* EEPROM State */
            uint8_t eeprom_pins;
            uint8_t eeprom_state;
            uint8_t eeprom_write_enabled;
            uint16_t eeprom_shift_reg;
            uint8_t eeprom_bits_shifted;
            uint8_t eeprom_addr;
            uint16_t eeprom_read_buffer;
        } mbc7;

        // Put other MBC-specific data in this union.
    };

    union
    {
        struct cpu_registers_s cpu_reg;
        uint8_t cpu_reg_raw[12];
        uint16_t cpu_reg_raw16[6];
    };
    struct gb_registers_s gb_reg;
    struct count_s counter;

    /* TODO: Allow implementation to allocate WRAM, VRAM and Frame Buffer. */
    uint8_t* wram;  // wram[WRAM_SIZE];
    uint8_t* vram;  // vram[VRAM_SIZE];
    uint8_t hram[HRAM_SIZE];
    uint8_t oam[OAM_SIZE];
    uint8_t* lcd;

    struct
    {
        /**
         * Draw line on screen.
         *
         * \param gb_s      emulator context
         * \param pixels    The 160 pixels to draw.
         *                  Bits 1-0 are the colour to draw.
         *                  Bits 5-4 are the palette, where:
         *                      OBJ0 = 0b00,
         *                      OBJ1 = 0b01,
         *                      BG = 0b10
         *                  Other bits are undefined.
         *                  Bits 5-4 are only required by front-ends
         *                  which want to use a different colour for
         *                  different object palettes. This is what
         *                  the Game Boy Color (CGB) does to DMG
         *                  games.
         * \param line      Line to draw pixels on. This is
         *                  guaranteed to be between 0-144 inclusive.
         */

        /* Palettes */
        uint8_t bg_palette[4];
        uint8_t sp_palette[8];

        uint8_t window_clear;
        uint8_t WY;
    } display;

    /**
     * Variables that may be modified directly by the front-end.
     * This method seems to be easier and possibly less overhead than
     * calling a function to modify these variables each time.
     *
     * None of this is thread-safe.
     */
    struct
    {
        /* Set to enable interlacing. Interlacing will start immediately
         * (at the next line drawing).
         */
        uint8_t frame_skip : 1;
        uint8_t sound : 1;
        uint8_t dynamic_rate_enabled : 1;
        uint8_t transparency_enabled : 1;
        uint8_t sram_updated : 1;
        uint8_t sram_dirty : 1;
        uint8_t crank_docked : 1;
        uint8_t joypad_interrupts : 1;
        uint8_t enable_xram : 1;

        int joypad_interrupt_delay;

        // if set, causes crank register to behave as delta-menu-selection instead
        uint8_t ext_crank_menu_indexing : 1;

        // where this is 0, skip the line
        uint8_t interlace_mask;

        union
        {
            struct
            {
                uint8_t a : 1;
                uint8_t b : 1;
                uint8_t select : 1;
                uint8_t start : 1;
                uint8_t right : 1;
                uint8_t left : 1;
                uint8_t up : 1;
                uint8_t down : 1;
            } joypad_bits;
            uint8_t joypad;
        };

#define CB_IDLE_FRAMES_BEFORE_SAVE 180
        union
        {
            uint16_t peripherals[4];
            struct
            {
                uint16_t crank;
                uint16_t accel_x;
                uint16_t accel_y;
                uint16_t accel_z;
            };
        };

        // for ext_crank_menu_indexing. Defaults to 0x8000.
        uint16_t crank_menu_accumulation;
        int8_t crank_menu_delta;

        /* Implementation defined data. Set to NULL if not required. */
        void* priv;
    } direct;

    uint32_t gb_cart_ram_size;

    gb_breakpoint* breakpoints;

#if ENABLE_BGCACHE
    uint8_t* bgcache;

#if ENABLE_BGCACHE_DEFERRED
    bool dirty_tile_data_master : 1;
    uint32_t dirty_tile_data[0x180 / 32];

    // invariant: bit n is 1 iff dirty_tiles[n] nonzero.
    uint64_t dirty_tile_rows;

    // any tiles in the tilemap that are dirty
    // (screen 2 at indices >= 32)
    uint32_t dirty_tiles[64];
#endif
#endif

    size_t gb_rom_size;
    uint8_t* gb_boot_rom;

    // NOTE: this MUST be the last member of gb_s.
    // sometimes we perform memory operations on the whole gb struct except for
    // audio.
    audio_data audio;
};

__core unsigned int __gb_step_cpu(struct gb_s* gb);

#ifdef CB_IMPL
__section__(".rare") bool gb_detect_interrupt(struct gb_s* gb, unsigned addr)
{
    const uint8_t* rom = gb->gb_rom;

    // RETI
    if (rom[addr] == 0xD9)
        return false;

    // RET
    if (rom[addr] == 0xC9)
        return false;

    // EI; RET
    if (rom[addr] == 0xFB && rom[addr + 1] == 0xC9)
        return false;

    // nops (technically not safe)
    // if (rom[addr] == 0 && rom[addr+1] == 0 && rom[addr + 2] == 0 && rom[addr+3] == 0 &&
    // rom[addr+4] == 0) return false;

    // RST 38 to RST 38
    if (rom[addr] == 0xFF && rom[0x38] == 0xFF)
        return false;

    return true;
}

__section__(".rare") void gb_detect_interrupts(struct gb_s* gb)
{
    gb->joypad_interrupt = gb_detect_interrupt(gb, 0x60);
    // gb->timer_interrupt = gb_detect_interrupt(gb, 0x50);
    // gb->serial_interrupt = gb_detect_interrupt(gb, 0x58);
}

__section__(".rare") void gb_init_boot_rom(struct gb_s* gb, uint8_t* boot_rom)
{
    gb->gb_boot_rom = boot_rom;
    memcpy(gb->gb_rom, boot_rom, 0x100);
    gb_detect_interrupts(gb);
}

/**
 * Directly calculates and applies the RTC state after a given
 * number of seconds have passed.
 *
 * This is a replacement for calling gb_tick_rtc() in a loop.
 * The logic is inspired by SameBoy's RTC implementation.
 *
 */
__section__(".text.cb") void gb_catch_up_rtc_direct(struct gb_s* gb, unsigned int seconds_to_add)
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
__section__(".text.cb") void gb_tick_rtc(struct gb_s* gb)
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

/**
 * Set initial values in RTC.
 * Should be called after gb_init().
 */
__section__(".text.cb") void gb_set_rtc(struct gb_s* gb, const struct tm* const time)
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

__section__(".text.cb") static void __gb_update_tac(struct gb_s* gb)
{
    static const uint8_t TAC_CYCLES[4] = {10, 4, 6, 8};

    // subtract 1 so it can be used as a mask for quick modulo.
    gb->gb_reg.tac_cycles_shift = TAC_CYCLES[gb->gb_reg.tac_rate];
    gb->gb_reg.tac_cycles = (1 << (int)TAC_CYCLES[gb->gb_reg.tac_rate]) - 1;
}

__section__(".text.cb") static void __gb_update_selected_bank_addr(struct gb_s* gb)
{
    int32_t offset = (gb->selected_rom_bank - 1) * ROM_BANK_SIZE;

    gb->selected_bank_addr = gb->gb_rom + offset;
}

__section__(".text.cb") static void __gb_update_selected_cart_bank_addr(struct gb_s* gb)
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

// https://stackoverflow.com/a/2602885
__core_section("short") u8 reverse_bits_u8(u8 b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

// 256 bytes of rom that could be covered up by the bios
static uint8_t gb_original_rom[0x100];

// extended ram feature offered by crankboy
static uint8_t xram[0x100 - 0xA0];

__section__(".rare.cb") static void __gb_rare_write(
    struct gb_s* gb, const uint16_t addr, const uint8_t val
)
{
    // unused memory area
    if (addr >= 0xFEA0 && addr < 0xFF00)
    {
        if (gb->direct.enable_xram)
        {
            xram[addr - 0xFEA0] = val;
        }
        return;
    }

    if ((addr >> 8) == 0xFF)
    {
        switch (addr & 0xFF)
        {
        // On a DMG, these writes are ignored.
        case 0x4D:  // KEY1 (CGB Speed Switch)
        case 0x4F:  // VBK (CGB VRAM Bank)
        case 0x56:  // RP (CGB Infrared Port)
        case 0x68:  // BCPS (CGB BG Palette Spec)
        case 0x69:  // BCPD (CGB BG Palette Data)
        case 0x70:  // WRAM bank (CGB)
            return;

        /* Turn off boot ROM */
        case 0x50:
            if (gb->gb_bios_enable)
            {
                gb->gb_bios_enable = 0;
                memcpy(gb->gb_rom, gb_original_rom, sizeof(gb_original_rom));
                gb_detect_interrupts(gb);
            }
            return;

        case IO_PLAYDATE_EXTENSION_CTL:
            // bit 0: accelerometer
            playdate->system->logToConsole("Set accelerometer enabled: %d", val & 1);
            playdate->system->setPeripheralsEnabled((val & 1) ? kAccelerometer : kNone);

            // bit 1: xram
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
            return;
        }
    }

    (gb->gb_error)(gb, GB_INVALID_WRITE, addr);
}

__section__(".rare.cb") static uint8_t __gb_rare_read(struct gb_s* gb, const uint16_t addr)
{
    if (addr >= 0xFEA0 && addr < 0xFF00)
    {
        if (gb->direct.enable_xram)
        {
            return xram[addr - 0xFEA0];
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
        case 0x4D:  // KEY1
        case 0x4F:  // VBK
        case 0x56:  // RP
        case 0x68:  // BCPS
        case 0x69:  // BCPD
        case 0x70:  // WRAM bank
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
__shell uint8_t __gb_read_full(struct gb_s* gb, const uint_fast16_t addr)
{
    switch (addr >> 12)
    {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
        // Check for MBC1 in Mode 1
        if (gb->mbc == 1 && gb->cart_mode_select)
        {
            // In this mode, the 0000-3FFF area is banked using the upper
            // two bits from the 4000-5FFF register.
            // The lower 5 bits of the bank number are treated as 0.
            uint32_t bank_number = (gb->selected_rom_bank & 0x60);
            uint32_t bank_offset = bank_number * ROM_BANK_SIZE;
            uint32_t rom_addr = bank_offset + addr;

            return gb->gb_rom[rom_addr & (gb->num_rom_banks_mask * ROM_BANK_SIZE + 0x3FFF)];
        }
        else
        {
            // Default behavior (Mode 0 or not MBC1)
            return gb->gb_rom[addr];
        }

    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
        return gb->selected_bank_addr[addr];

    case 0x8:
    case 0x9:
        if (addr < 0x1800 + VRAM_ADDR)
            return reverse_bits_u8(gb->vram[addr - VRAM_ADDR]);
        return gb->vram[addr - VRAM_ADDR];

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
        return gb->wram[addr - WRAM_0_ADDR];

    case 0xD:
        return gb->wram[addr - WRAM_0_ADDR];

    case 0xE:
        return gb->wram[addr - ECHO_ADDR];

    case 0xF:
        if (addr < OAM_ADDR)
            return gb->wram[addr - ECHO_ADDR];

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

#if ENABLE_BGCACHE
#if ENABLE_BGCACHE_DEFERRED

// process changes to the bgcache later on, during rendering
// (since we might update it multiple times before rendering)

__core_section("bgdefer") void __gb_update_bgcache_tile_deferred(
    struct gb_s* restrict gb, int addr_mode, const int tmidx, const uint8_t tile
)
{
    // TODO: use addr_mode field
    CB_ASSERT(tmidx < 0x800)
    int row = tmidx / 32;
    gb->dirty_tile_rows |= (uint64_t)1 << row;
    gb->dirty_tiles[row] |= 1 << (tmidx % 32);
}

__core_section("bgdefer") void __gb_update_bgcache_tile_data_deferred(
    struct gb_s* restrict gb, unsigned tile
)
{
    gb->dirty_tile_data_master = 1;
    gb->dirty_tile_data[tile / 32] |= 1 << (tile % 32);
}

#else
#define __gb_update_bgcache_tile_deferred __gb_update_bgcache_tile
#define __gb_update_bgcache_tile_data_deferred __gb_update_bgcache_tile_data
#endif

// tile data was changed, so we need to redraw this tile where it appears in the
// tilemap tmidx: index of tile in map to update. (0x400+ is the second map.)
__core_section("bgcache") void __gb_update_bgcache_tile(
    struct gb_s* restrict gb, int addr_mode, const int tmidx, const uint8_t tile
)
{
    int ty = tmidx / 0x20;
    int tx = tmidx % 0x20;
    int tile_data_addr = 0x1000 * (addr_mode && tile < 128) | ((int)tile) * 0x10;
    uint8_t* bgcache = gb->bgcache + addr_mode * (BGCACHE_SIZE / 2);
    uint8_t* vram = &gb->vram[tile_data_addr];
    for (int tline = 0; tline < 8; tline++)
    {
        int y = tline + ty * 8;
        unsigned t1 = vram[2 * tline];
        unsigned t2 = vram[2 * tline + 1];

        // bgcache format: each 32 bits is a pair of 16 bit low color, 16 bit hi
        // color
        size_t index = (tx / 2) * 4 + y * BGCACHE_STRIDE + (tx % 2);
        CB_ASSERT(index + 2 < BGCACHE_SIZE);
        uint8_t* t = &bgcache[index];

        t[0] = t1;
        t[2] = t2;
    }
}

__core_section("bgcache") void __gb_update_bgcache_tile_data(
    struct gb_s* restrict gb, const unsigned tile
)
{
    // tile data update -- scan tilemap for matching tiles
    for (int i = 0; i < 0x800; ++i)
    {
        unsigned _t = gb->vram[0x1800 + i];

        // handle both tile addressing modes
        if (_t == tile % 256 && tile < 0x80)
        {
            __gb_update_bgcache_tile_deferred(gb, 0, i, _t);
        }
        else if (_t == tile % 256 && tile >= 0x100)
        {
            __gb_update_bgcache_tile_deferred(gb, 1, i, _t);
        }
        else if (_t == tile % 256)
        {
            __gb_update_bgcache_tile_deferred(gb, 0, i, _t);
            __gb_update_bgcache_tile_deferred(gb, 1, i, _t);
        }
    }
}

#if ENABLE_BGCACHE_DEFERRED
__core_section("bgdefer") void __gb_process_deferred_tile_data_update(struct gb_s* restrict gb)
{
    for (int i = 0; i < PEANUT_GB_ARRAYSIZE(gb->dirty_tile_data); ++i)
    {
        uint32_t dirty = gb->dirty_tile_data[i];
        if likely (!dirty)
            continue;
        for (int j = 0; dirty; ++j)
        {
            if unlikely (dirty & 1)
            {
                const unsigned tile = (i * 32) | j;
                CB_ASSERT(tile < 0x1800);
                __gb_update_bgcache_tile_data(gb, tile);
            }
            dirty >>= 1;
        }
        gb->dirty_tile_data[i] = 0;
    }
    gb->dirty_tile_data_master = 0;
}

__core_section("bgdefer") void __gb_process_deferred_tile_update(struct gb_s* restrict gb)
{
    uint64_t d = gb->dirty_tile_rows;
    for (int row = 0; d; ++row, d >>= 1)
    {
        if likely (!(d & 1))
            continue;

        // some dirty tile exists on this row
        uint32_t dirty_tiles = gb->dirty_tiles[row];
        for (int x = 0; dirty_tiles; ++x, dirty_tiles >>= 1)
        {
            if unlikely (dirty_tiles & 1)
            {
                int tmidx = (row * 32) | x;
                int tile = gb->vram[0x1800 + tmidx];
                __gb_update_bgcache_tile(gb, 0, tmidx, tile);
                __gb_update_bgcache_tile(gb, 1, tmidx, tile);
            }
        }
        gb->dirty_tiles[row] = 0;
    }

    gb->dirty_tile_rows = 0;
}

__shell uint8_t __gb_read_full(struct gb_s* gb, const uint_fast16_t addr);
__shell void __gb_write_full(struct gb_s* gb, const uint_fast16_t addr, const uint8_t val);

#endif /* CB_IMPL */

void __gb_write_vram(struct gb_s* gb, uint_fast16_t addr, const uint8_t val)
{
    addr -= 0x8000;
    if (addr < 0x1800)
    {
        uint8_t reversed_val = reverse_bits_u8(val);
        if (gb->vram[addr] == reversed_val)
            return;
        gb->vram[addr] = reversed_val;

        unsigned tile = (addr / 16);
        __gb_update_bgcache_tile_data_deferred(gb, tile);
    }
    else
    {
        if (gb->vram[addr] == val)
            return;
        gb->vram[addr] = val;

        int tmidx = addr - 0x1800;
        __gb_update_bgcache_tile_deferred(gb, 0, tmidx, val);
        __gb_update_bgcache_tile_deferred(gb, 1, tmidx, val);
    }
}
#endif

/**
 * Handles a clock tick for the MBC7 EEPROM.
 * This function is called on the rising edge of the EEPROM's CLK pin.
 */
__section__(".text.cb") static void __gb_mbc7_eeprom_clock(struct gb_s* gb)
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
 * Internal function to check for LY=LYC coincidence and update STAT.
 */
__core static void __gb_check_lyc(struct gb_s* gb)
{
    if (gb->gb_reg.LY == gb->gb_reg.LYC)
    {
        gb->gb_reg.STAT |= STAT_LYC_COINC;

        if (gb->gb_reg.STAT & STAT_LYC_INTR)
        {
            gb->gb_reg.IF |= LCDC_INTR;
        }
    }
    else
    {
        gb->gb_reg.STAT &= ~STAT_LYC_COINC;
    }
}

/**
 * Internal function used to write bytes.
 */
__shell void __gb_write_full(struct gb_s* gb, const uint_fast16_t addr, const uint8_t val)
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
            {
                gb->mbc7.ram_enable_1 = ((val & 0x0F) == 0x0A);
            }
            else if (gb->mbc > 0 && gb->cart_ram)
                gb->enable_cart_ram = ((val & 0x0F) == 0x0A);
        }
        else if (addr < 0x4000)  // Address is 2000-3FFF (ROM Bank Lower Bits)
        {
            if (gb->mbc == 1)
            {
                gb->selected_rom_bank = (val & 0x1F) | (gb->selected_rom_bank & 0x60);
                if ((gb->selected_rom_bank & 0x1F) == 0x00)
                    gb->selected_rom_bank++;
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
            gb->selected_rom_bank = ((val & 3) << 5) | (gb->selected_rom_bank & 0x1F);
            gb->selected_rom_bank = gb->selected_rom_bank & gb->num_rom_banks_mask;
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
        else if (gb->mbc == 1)
        {
            gb->cart_mode_select = (val & 1);
            __gb_update_selected_cart_bank_addr(gb);
        }
        return;

    case 0x8:
    case 0x9:
#if ENABLE_BGCACHE
        __gb_write_vram(gb, addr, val);
#else
        if (addr < 0x1800 + VRAM_ADDR)
            gb->vram[addr - VRAM_ADDR] = reverse_bits_u8(val);
        else
            gb->vram[addr - VRAM_ADDR] = val;
#endif
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
        gb->wram[addr - WRAM_0_ADDR] = val;
        return;

    case 0xD:
        gb->wram[addr - WRAM_1_ADDR + WRAM_BANK_SIZE] = val;
        return;

    case 0xE:
        gb->wram[addr - ECHO_ADDR] = val;
        return;

    case 0xF:
        if (addr < OAM_ADDR)
        {
            gb->wram[addr - ECHO_ADDR] = val;
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

        case 0x02:
            gb->gb_reg.SC = val;
            return;

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
            bool is_enabled = (gb->gb_reg.LCDC & LCDC_ENABLE);

            if (was_enabled && !is_enabled)
            {
                gb->counter.lcd_off_count += gb->counter.lcd_count;
                gb->gb_reg.LY = 0;
                gb->counter.lcd_count = 0;
                gb->lcd_mode = LCD_HBLANK;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | gb->lcd_mode;
                __gb_check_lyc(gb);
            }
            else if (!was_enabled && is_enabled)
            {
                gb->counter.lcd_count = 4;
                gb->gb_reg.LY = 0;
                gb->lcd_mode = LCD_SEARCH_OAM;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | gb->lcd_mode;
                __gb_check_lyc(gb);
            }
            return;
        }

        case 0x41:  // STAT Register
        {
            gb->gb_reg.STAT = (val & STAT_USER_BITS) | (gb->gb_reg.STAT & ~STAT_USER_BITS);

            if (gb->gb_reg.LCDC & LCDC_ENABLE)
            {
                bool mode_interrupt =
                    (gb->gb_reg.STAT & STAT_MODE_0_INTR && gb->lcd_mode == LCD_HBLANK) ||
                    (gb->gb_reg.STAT & STAT_MODE_1_INTR && gb->lcd_mode == LCD_VBLANK) ||
                    (gb->gb_reg.STAT & STAT_MODE_2_INTR && gb->lcd_mode == LCD_SEARCH_OAM);

                bool lyc_interrupt =
                    (gb->gb_reg.STAT & STAT_LYC_INTR) && (gb->gb_reg.STAT & STAT_LYC_COINC);

                if (mode_interrupt || lyc_interrupt)
                {
                    gb->gb_reg.IF |= LCDC_INTR;
                }
            }
            return;
        }

        case 0x42:
            gb->gb_reg.SCY = val;
            return;

        case 0x43:
            gb->gb_reg.SCX = val;
            return;

        /* LY (0xFF44) is read-only. Writes are ignored on real hardware.
         * The boot ROM attempts to write to this register. */
        case 0x44:
            return;

        /* LY (0xFF44) is read only. */
        case 0x45:  // LYC Register
            gb->gb_reg.LYC = val;
            // Perform an LY=LYC check immediately if the LCD is enabled.
            if (gb->gb_reg.LCDC & LCDC_ENABLE)
            {
                __gb_check_lyc(gb);
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

__core_section("short") static uint8_t __gb_read(struct gb_s* gb, const uint16_t addr)
{
    if likely (addr < 0x4000)
    {
        return gb->gb_rom[addr];
    }
    if likely (addr < 0x8000)
    {
        return gb->selected_bank_addr[addr];
    }
    if likely (addr >= 0xC000 && addr < 0xE000)
    {
        return gb->wram[addr % WRAM_SIZE];
    }
    if likely (addr >= 0xFF80 && addr <= 0xFFFE)
    {
        return gb->hram[addr % 0x100];
    }
    if likely (addr >= 0xA000 && addr < 0xC000 && gb->selected_cart_bank_addr)
    {
        return gb->selected_cart_bank_addr[addr];
    }
    return __gb_read_full(gb, addr);
}

__core_section("short") static void __gb_write(
    struct gb_s* restrict gb, const uint16_t addr, uint8_t v
)
{
    if likely (addr >= 0xC000 && addr < 0xE000)
    {
        gb->wram[addr % WRAM_SIZE] = v;
        return;
    }
    if likely (addr >= 0xFF80 && addr <= 0xFFFE)
    {
        gb->hram[addr % 0x100] = v;
        return;
    }
    if likely (addr >= 0xA000 && addr < 0xC000 && gb->selected_cart_bank_addr)
    {
        u8* b = &gb->selected_cart_bank_addr[addr];
        u8 prev = *b;
        *b = v;
        gb->direct.sram_updated |= prev != v;
    }
    __gb_write_full(gb, addr, v);
}

__core_section("util") clalign
    void gb_fast_memcpy_64(void* restrict _dst, const void* restrict _src, size_t len)
{
    CB_ASSERT(len % 8 == 0);
    CB_ASSERT(len > 0);
    uint64_t* dst = _dst;
    const uint64_t* src = _src;
    do
    {
        dst[0] = src[0];
        len -= 8;
        dst++;
        src++;
    } while (len > 0);
}

__core_section("short") static uint16_t __gb_read16(struct gb_s* restrict gb, u16 addr)
{
    // TODO: optimize
    u16 v = __gb_read(gb, addr);
    v |= (u16)__gb_read(gb, addr + 1) << 8;
    return v;
}

__core_section("short") static void __gb_write16(struct gb_s* restrict gb, u16 addr, u16 v)
{
    // TODO: optimize
    __gb_write(gb, addr, v & 0xFF);
    __gb_write(gb, addr + 1, v >> 8);
}

__core_section("short") static uint8_t __gb_fetch8(struct gb_s* restrict gb)
{
    return __gb_read(gb, gb->cpu_reg.pc++);
}

__core_section("short") static uint16_t __gb_fetch16(struct gb_s* restrict gb)
{
    u16 v;
    u16 addr = gb->cpu_reg.pc;

    if likely (addr < 0x3FFF)
    {
        v = gb->gb_rom[addr];
        v |= gb->gb_rom[addr + 1] << 8;
    }
    else if likely (addr >= 0x4000 && addr < 0x7FFF)
    {
        v = gb->selected_bank_addr[addr];
        v |= gb->selected_bank_addr[addr + 1] << 8;
    }
    else
    {
        v = __gb_read16(gb, addr);
    }
    gb->cpu_reg.pc += 2;
    return v;
}

__core_section("short") static uint16_t __gb_pop16(struct gb_s* restrict gb)
{
    u16 v;
    if likely (gb->cpu_reg.sp >= HRAM_ADDR && gb->cpu_reg.sp < 0xFFFE)
    {
        v = gb->hram[gb->cpu_reg.sp - IO_ADDR];
        v |= gb->hram[gb->cpu_reg.sp - IO_ADDR + 1] << 8;
    }
    else
    {
        v = __gb_read16(gb, gb->cpu_reg.sp);
    }
    gb->cpu_reg.sp += 2;
    return v;
}

__core_section("short") static void __gb_push16(struct gb_s* restrict gb, u16 v)
{
    gb->cpu_reg.sp -= 2;

    if likely (gb->cpu_reg.sp >= HRAM_ADDR && gb->cpu_reg.sp < HRAM_ADDR + 0x7E)
    {
        gb->hram[gb->cpu_reg.sp - IO_ADDR] = v & 0xFF;
        gb->hram[gb->cpu_reg.sp - IO_ADDR + 1] = v >> 8;
        return;
    };

    __gb_write16(gb, gb->cpu_reg.sp, v);
}

__core static uint8_t __gb_execute_cb(struct gb_s* gb)
{
    uint8_t inst_cycles;
    uint8_t cbop = __gb_fetch8(gb);
    uint8_t r = (cbop & 0x7) ^ 1;
    uint8_t b = (cbop >> 3) & 0x7;
    uint8_t d = (cbop >> 3) & 0x1;
    uint8_t val;
    uint8_t writeback = 1;

    inst_cycles = 8;
    /* Add an additional 8 cycles to these sets of instructions. */
    switch (cbop & 0xC7)
    {
    case 0x06:
    case 0x86:
    case 0xC6:
        inst_cycles += 8;
        break;
    case 0x46:
        inst_cycles += 4;
        break;
    }

    if (r == 7)
    {
        val = __gb_read(gb, gb->cpu_reg.hl);
    }
    else
    {
        val = gb->cpu_reg_raw[r];
    }

    /* switch based on highest 2 bits */
    switch (cbop >> 6)
    {
    case 0x0:
        cbop = (cbop >> 4) & 0x3;

        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;

        switch (cbop)
        {
        case 0x0:  /* RdC R */
        case 0x1:  /* Rd R */
            if (d) /* RRC R / RR R */
            {
                uint8_t temp = val;
                val = (val >> 1);
                val |= cbop ? (gb->cpu_reg.f_bits.c << 7) : (temp << 7);
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.c = (temp & 0x01);
            }
            else /* RLC R / RL R */
            {
                uint8_t temp = val;
                val = (val << 1);
                val |= cbop ? gb->cpu_reg.f_bits.c : (temp >> 7);
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.c = (temp >> 7);
            }

            break;

        case 0x2:
            if (d) /* SRA R */
            {
                gb->cpu_reg.f_bits.c = val & 0x01;
                val = (val >> 1) | (val & 0x80);
                gb->cpu_reg.f_bits.z = (val == 0x00);
            }
            else /* SLA R */
            {
                gb->cpu_reg.f_bits.c = (val >> 7);
                val = val << 1;
                gb->cpu_reg.f_bits.z = (val == 0x00);
            }

            break;

        case 0x3:
            if (d) /* SRL R */
            {
                gb->cpu_reg.f_bits.c = val & 0x01;
                val = val >> 1;
                gb->cpu_reg.f_bits.z = (val == 0x00);
            }
            else /* SWAP R */
            {
                uint8_t temp = (val >> 4) & 0x0F;
                temp |= (val << 4) & 0xF0;
                val = temp;
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.c = 0;
            }

            break;
        }

        break;

    case 0x1: /* BIT B, R */
        gb->cpu_reg.f_bits.z = !((val >> b) & 0x1);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        writeback = 0;
        break;

    case 0x2: /* RES B, R */
        val &= (0xFE << b) | (0xFF >> (8 - b));
        break;

    case 0x3: /* SET B, R */
        val |= (0x1 << b);
        break;
    }

    if (writeback)
    {
        if (r == 7)
        {
            __gb_write(gb, gb->cpu_reg.hl, val);
        }
        else
        {
            gb->cpu_reg_raw[r] = val;
        }
    }
    return inst_cycles;
}

#if ENABLE_LCD
struct sprite_data
{
    uint8_t sprite_number;
    uint8_t x;
};

__section__(".text.cb") static int compare_sprites(
    const struct sprite_data* const sd1, const struct sprite_data* const sd2
)
{
    /* Smaller X-coordinate has higher priority. */
    int x_res = (int)sd1->x - (int)sd2->x;
    if (x_res != 0)
        return x_res;

    /* If X is the same, smaller OAM index has higher priority. */
    return (int)sd1->sprite_number - (int)sd2->sprite_number;
}

__core_section("draw") static void __gb_draw_pixel(uint8_t* line, u8 x, u8 v)
{
    u8* pix = line + x / LCD_PACKING;
    x = (x % LCD_PACKING) * (8 / LCD_PACKING);
    *pix &= ~(((1 << LCD_BITS_PER_PIXEL) - 1) << x);
    *pix |= (v & 3) << x;
}

__core_section("draw") static u8 __gb_get_pixel(uint8_t* line, u8 x)
{
    u8* pix = line + x / LCD_PACKING;
    x = (x % LCD_PACKING) * LCD_BITS_PER_PIXEL;
    return (*pix >> x) % (1 << LCD_BITS_PER_PIXEL);
}

// renders one scanline
__core_section("draw") void __gb_draw_line(struct gb_s* restrict gb)
{
    if (gb->direct.dynamic_rate_enabled)
    {
        if (((gb->direct.interlace_mask >> (gb->gb_reg.LY % 8)) & 1) == 0)
        {
            if ((gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE) && (gb->gb_reg.LY >= gb->display.WY))
            {
                gb->display.window_clear++;
            }
            return;
        }
    }

#if ENABLE_BGCACHE && ENABLE_BGCACHE_DEFERRED
    if unlikely (gb->dirty_tile_data_master)
        __gb_process_deferred_tile_data_update(gb);
    if unlikely (gb->dirty_tile_rows)
        __gb_process_deferred_tile_update(gb);
#endif

    __builtin_prefetch(&gb->gb_reg.LCDC, 0);
    __builtin_prefetch(&gb->gb_reg.WX, 0);
    __builtin_prefetch(&gb->gb_reg.BGP, 0);
    __builtin_prefetch(&gb->display.WY, 0);

    uint8_t* pixels = &gb->lcd[gb->gb_reg.LY * LCD_WIDTH_PACKED];
    uint32_t line_priority[((LCD_WIDTH + 31) / 32)];
    const uint32_t line_priority_len = PEANUT_GB_ARRAYSIZE(line_priority);

    __builtin_prefetch(pixels, 1);

    for (int i = 0; i < line_priority_len; ++i)
        line_priority[i] = 0;

    uint32_t priority_bits = 0;

    int wx = LCD_WIDTH;
    if (gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE && gb->gb_reg.LY >= gb->display.WY &&
        gb->gb_reg.WX < LCD_WIDTH + 7)
    {
        // TODO: behaviour of wx if WX = 0-6 or WX = 166; apparently there are
        // hardware bugs?
        if (gb->gb_reg.WX >= 7)
        {
            wx = gb->gb_reg.WX - 7;
        }
        else
        {
            wx = 0;
        }
        if (wx >= LCD_WIDTH)
            wx = LCD_WIDTH;
    }

    // clear row
    for (int i = 0; i < LCD_WIDTH / 16; ++i)
        ((uint32_t*)pixels)[i] = 0;

// remaps 16-bit lo (t1) and hi (t2) colours to 2bbp 32-bit v
#define BG_REMAP(pal, t1, t2, v)                          \
    do                                                    \
    {                                                     \
        uint32_t t2_ = ((uint32_t)t2) << 1;               \
        for (int _q = 0; _q < 16; _q++)                   \
        {                                                 \
            int p = ((t1 >> _q) & 1) | ((t2_ >> _q) & 2); \
            int c = (pal >> (2 * p)) & 3;                 \
            v >>= 2;                                      \
            v |= c << 30;                                 \
        }                                                 \
    } while (0)

#if ENABLE_BGCACHE
    int addr_mode_2 = !(gb->gb_reg.LCDC & LCDC_TILE_SELECT);
#endif

    /* If background is enabled, draw it. */
    if ((gb->gb_reg.LCDC & LCDC_BG_ENABLE) && wx > 0)
    {
        /* Calculate current background line to draw. Constant because
         * this function draws only this one line each time it is
         * called. */
        const uint8_t bg_y = gb->gb_reg.LY + gb->gb_reg.SCY;

#if ENABLE_BGCACHE
        uint8_t bg_x = gb->gb_reg.SCX;
        int map2 = !!(gb->gb_reg.LCDC & LCDC_BG_MAP);
        uint32_t* bgcache =
            (uint32_t*)(gb->bgcache + (bg_y * BGCACHE_STRIDE) + addr_mode_2 * (BGCACHE_SIZE / 2) +
                        map2 * (BGCACHE_SIZE / 4));
        uint32_t hi = bgcache[(bg_x / 16) % 0x10];
        for (int i = 0; i < (wx + 15) / 16; ++i)
        {
            uint16_t* out = (uint16_t*)(void*)(pixels) + (i * 2);
            uint32_t lo = hi;
            hi = bgcache[(bg_x / 16 + i + 1) % 0x10];
            int xm = (bg_x % 16);
            uint16_t raw1 = ((lo & 0x0000FFFF) >> xm);
            uint16_t raw2 = ((lo & 0xFFFF0000) >> (16 + xm));
            raw1 |= ((hi & 0x0000FFFF) << (16 - xm));
            raw2 |= ((hi & 0xFFFF0000) >> xm);

            out[0] = raw1;
            out[1] = raw2;
        }
#else
        uint8_t bg_x = gb->gb_reg.SCX;
        int addr_mode_2 = !(gb->gb_reg.LCDC & LCDC_TILE_SELECT);
        int addr_mode_vram_tiledata_offset = addr_mode_2 ? 0x800 : 0;
        int map2 = !!(gb->gb_reg.LCDC & LCDC_BG_MAP);

        uint8_t* vram = gb->vram;

        // tiles on this line
        uint8_t* vram_line_tiles = (void*)&vram[(map2 ? 0x1C00 : 0x1800) | (32 * (bg_y / 8))];

        // points to line data for pixel offset
        uint16_t* vram_tile_data = (void*)&vram[2 * (bg_y % 8)];

        int subx = bg_x % 8;

        // prefetch each tile's data
        for (int x = 0; x <= (wx + 7) / 8; ++x)
        {
            uint8_t tile = vram_line_tiles[(bg_x / 8) % 32];
            __builtin_prefetch(
                &vram_line_tiles
                    [(tile < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile)],
                0
            );
        }

        uint8_t tile_hi = vram_line_tiles[(bg_x / 8) % 32];
        uint16_t vram_tile_data_hi = vram_tile_data
            [(tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile_hi)];

        for (int x = 0; x < (wx + 7) / 8; ++x)
        {
            uint8_t* out = pixels + (x % 2) + (x / 2) * 4;
            uint16_t vram_tile_data_lo = vram_tile_data_hi;
            uint16_t tile_hi = vram_line_tiles[(bg_x / 8 + x + 1) % 32];
            vram_tile_data_hi = vram_tile_data
                [(tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile_hi)];

            uint8_t raw1 = (vram_tile_data_lo & 0x00FF) >> subx;
            uint8_t raw2 = (uint16_t)vram_tile_data_lo >> (subx | 8);
            raw1 |= (vram_tile_data_hi & 0x00FF) << (8 - subx);
            raw2 |= ((vram_tile_data_hi & 0xFF00) >> subx) & 0xFF;

            out[0] = raw1;
            out[2] = raw2;
        }
#endif
    }

    /* draw window */
    if (wx < LCD_WIDTH)
    {
#if ENABLE_BGCACHE
        uint8_t wx_reg = gb->gb_reg.WX;

        // Determine the starting pixel on the screen and the starting pixel
        // to read from within the window's own data. This handles the
        // special hardware case where WX is between 0 and 6, which clips
        // the left side of the window.
        int screen_x_start = (wx_reg >= 7) ? (wx_reg - 7) : 0;
        int win_x_start = (wx_reg >= 7) ? 0 : (7 - wx_reg);

        uint8_t win_y = gb->display.window_clear;
        int map2 = !!(gb->gb_reg.LCDC & LCDC_WINDOW_MAP);
        uint32_t* win_cache_line =
            (uint32_t*)(gb->bgcache + (win_y * BGCACHE_STRIDE) + addr_mode_2 * (BGCACHE_SIZE / 2) +
                        map2 * (BGCACHE_SIZE / 4));

        uint16_t* line_pixels = (uint16_t*)(void*)pixels;

        int win_x = win_x_start;
        for (int screen_x = screen_x_start; screen_x < LCD_WIDTH; ++screen_x, ++win_x)
        {
            uint32_t src_chunk_data = win_cache_line[win_x / 16];
            int bit_in_chunk = win_x % 16;

            uint16_t src_low_bit = (src_chunk_data >> bit_in_chunk) & 1;
            uint16_t src_high_bit = (src_chunk_data >> (bit_in_chunk + 16)) & 1;

            if (!gb->direct.transparency_enabled && src_low_bit == 0 && src_high_bit == 0)
                continue;

            int dest_chunk_idx = screen_x / 16;
            int dest_bit_in_chunk = screen_x % 16;

            uint16_t bit_mask = (1 << dest_bit_in_chunk);

            uint16_t* dest_low_plane = &line_pixels[dest_chunk_idx * 2];
            uint16_t* dest_high_plane = &line_pixels[dest_chunk_idx * 2 + 1];

            *dest_low_plane = (*dest_low_plane & ~bit_mask) | (src_low_bit << dest_bit_in_chunk);
            *dest_high_plane = (*dest_high_plane & ~bit_mask) | (src_high_bit << dest_bit_in_chunk);
        }
#else
        uint8_t bg_x = 256 - wx;
        uint8_t bg_y = gb->display.window_clear;
        int addr_mode_2 = !(gb->gb_reg.LCDC & LCDC_TILE_SELECT);
        int map2 = !!(gb->gb_reg.LCDC & LCDC_WINDOW_MAP);
        int addr_mode_vram_tiledata_offset = addr_mode_2 ? 0x800 : 0;

        uint8_t* vram = gb->vram;

        // tiles on this line
        uint8_t* vram_line_tiles = (void*)&vram[(map2 ? 0x1C00 : 0x1800) | (32 * (bg_y / 8))];

        // points to line data for pixel offset
        uint16_t* vram_tile_data = (void*)&vram[2 * (bg_y % 8)];

        // prefetch each tile's data
        for (int x = wx / 8; x <= LCD_WIDTH / 8; ++x)
        {
            uint8_t tile = vram_line_tiles[(bg_x / 8) % 32];
            __builtin_prefetch(
                &vram_line_tiles
                    [(tile < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile)],
                0
            );
        }

        uint8_t tile_hi = vram_line_tiles[(bg_x / 8 + wx / 8) % 32];
        uint16_t vram_tile_data_hi = vram_tile_data
            [(tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile_hi)];

        int subx = bg_x % 8;

        // first part of window is obscured
        vram_tile_data_hi &= (0xFFFF) << subx;
        vram_tile_data_hi &= 0xFF | ((0xFF00) << subx);
        uint32_t bgmask = 0xFF >> subx;
        if (subx == 0)
            bgmask = 0;  // (why?)

        for (int x = wx / 8; x < LCD_WIDTH / 8; ++x)
        {
            uint8_t* out = pixels + (x % 2) + (x / 2) * 4;
            uint16_t vram_tile_data_lo = vram_tile_data_hi;
            uint16_t tile_hi = vram_line_tiles[(bg_x / 8 + x + 1) % 32];
            vram_tile_data_hi = vram_tile_data
                [(tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile_hi)];

            uint8_t raw1 = (vram_tile_data_lo & 0x00FF) >> subx;
            uint8_t raw2 = (uint16_t)vram_tile_data_lo >> (subx | 8);
            raw1 |= (vram_tile_data_hi & 0x00FF) << (8 - subx);
            raw2 |= ((vram_tile_data_hi & 0xFF00) >> subx) & 0xFF;

            uint32_t combined_mask = 0xFF00FF00 | (bgmask) | (bgmask << 16);
            uint32_t combined_planes = (uint32_t)(raw1) | ((uint32_t)raw2 << 16);

            *(uint32_t*)&out[0] &= combined_mask;
            *(uint32_t*)&out[0] |= combined_planes;

            // all further chunks should completely mask out the background
            bgmask = 0;
        }
#endif
        gb->display.window_clear++;
    }

    // remap background pixel by palette,
    // and set priority
    uint32_t pal = gb->gb_reg.BGP;
    for (int i = 0; i < LCD_WIDTH / 16; ++i)
    {
        uint16_t* p = (uint16_t*)(void*)pixels + (2 * i);
        uint16_t t0 = p[0];
        uint16_t t1 = p[1];

        // Initialize rm to 0. This is required because the BG_REMAP macro reads
        // from the variable before it is fully written to.
        uint32_t rm = 0;
#pragma GCC unroll 16
        BG_REMAP(pal, t0, t1, rm);
        *(uint32_t*)p = rm;

        ((uint16_t*)line_priority)[i] = (t1 | t0) ^ 0xFFFF;
    }

    // draw sprites
    if (gb->gb_reg.LCDC & LCDC_OBJ_ENABLE)
    {
        uint8_t number_of_sprites = 0;
        struct sprite_data sprites_to_render[MAX_SPRITES_LINE];

        /* Find up to 10 sprites on this line, sorted by priority.
         * Lower X-coordinate has higher priority. If X is the same,
         * lower OAM index has higher priority. */

        // Gather all visible sprites for this scanline (LY).
        const uint8_t sprite_height = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE) ? 16 : 8;
        const int16_t current_ly = gb->gb_reg.LY;

        for (uint8_t s = 0; s < NUM_SPRITES && number_of_sprites < MAX_SPRITES_LINE; s++)
        {
            const uint8_t* oam = &gb->oam[s * 4];
            const uint8_t oam_y = oam[0];
            const uint8_t oam_x = oam[1];

            if (oam_x > 0 && (current_ly + 16 >= oam_y) &&
                (current_ly + 16 < oam_y + sprite_height))
            {
                sprites_to_render[number_of_sprites].sprite_number = s;
                sprites_to_render[number_of_sprites].x = oam_x;
                number_of_sprites++;
            }
        }

        // Sort the small list of found sprites.
        if (number_of_sprites > 1)
        {
            for (int i = 1; i < number_of_sprites; i++)
            {
                struct sprite_data key = sprites_to_render[i];
                int j = i - 1;

                while (j >= 0 && compare_sprites(&sprites_to_render[j], &key) > 0)
                {
                    sprites_to_render[j + 1] = sprites_to_render[j];
                    j = j - 1;
                }
                sprites_to_render[j + 1] = key;
            }
        }

        const uint16_t OBP = gb->gb_reg.OBP0 | ((uint16_t)gb->gb_reg.OBP1 << 8);

        /* Render sprites from lowest priority to highest priority. */
        for (int8_t i = number_of_sprites - 1; i >= 0; i--)
        {
            uint8_t s_idx = sprites_to_render[i].sprite_number;
            uint8_t s_4 = s_idx * 4;

            uint8_t OY = gb->oam[s_4 + 0];
            uint8_t OX = gb->oam[s_4 + 1];
            uint8_t OT = gb->oam[s_4 + 2] & (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0xFE : 0xFF);
            uint8_t OF = gb->oam[s_4 + 3];

            uint8_t py = gb->gb_reg.LY - (OY - 16);

            if (OF & OBJ_FLIP_Y)
                py = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 15 : 7) - py;

            uint16_t t1_i = VRAM_TILES_1 + OT * 0x10 + 2 * py;
            uint8_t t1 = gb->vram[t1_i];
            uint8_t t2 = gb->vram[t1_i + 1];

            int dir, start, end;
            if (OF & OBJ_FLIP_X)
            {
                dir = 1;
                start = OX - 8;
                end = OX;
            }
            else
            {
                dir = -1;
                start = OX - 1;
                end = OX - 9;
            }

            uint8_t c_add = (OF & OBJ_PALETTE) ? 8 : 0;

            for (int disp_x = start; disp_x != end; disp_x += dir)
            {
                if unlikely (disp_x < 0 || disp_x >= LCD_WIDTH)
                    goto next_loop;

                uint8_t c = ((t1 & 0x80) >> 6) | ((t2 & 0x80) >> 5);
                if (c != 0)
                {
                    int P_segment_index = disp_x / 32;
                    int P_bit_in_segment = disp_x % 32;
                    uint8_t bg_is_transparent =
                        (line_priority[P_segment_index] >> P_bit_in_segment) & 1;

                    if (!((OF & OBJ_PRIORITY) && !bg_is_transparent))
                    {
                        __gb_draw_pixel(pixels, disp_x, (OBP >> (c | c_add)) & 3);
                    }
                }
            next_loop:
                t1 <<= 1;
                t2 <<= 1;
            }
        }
    }
}
#endif

__shell static unsigned __gb_run_instruction(struct gb_s* gb, uint8_t opcode)
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
    gb->gb_ime = 0;
    gb->gb_halt = 1;
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
    /* TODO: Emulate HALT bug? */
    gb->gb_halt = 1;
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
    inst_cycles = __gb_execute_cb(gb);
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
    gb->gb_ime = 1;
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

__core_section("short") static bool __gb_get_op_flag(struct gb_s* restrict gb, uint8_t op8)
{
    op8 %= 4;
    bool flag = (op8 <= 1) ? gb->cpu_reg.f_bits.z : gb->cpu_reg.f_bits.c;
    flag ^= (op8 % 2);
    return flag;
}

__core_section("short") static u16 __gb_add16(struct gb_s* restrict gb, u16 a, u16 b)
{
    unsigned temp = a + b;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((temp ^ a ^ b) >> 12) & 1;
    gb->cpu_reg.f_bits.c = temp >> 16;
    return temp;
}

__shell static u8 __gb_rare_instruction(struct gb_s* restrict gb, uint8_t opcode);

__core static unsigned __gb_run_instruction_micro(struct gb_s* gb)
{
#define FETCH8(gb) __gb_fetch8(gb)

#define FETCH16(gb) __gb_fetch16(gb)

    u8 opcode = FETCH8(gb);
    const u8 op8 = ((opcode & ~0xC0) / 8) ^ 1;
    float cycles = 1.0f;  // use fpu register, save space
    unsigned src;
    u8 srcidx;

    switch (opcode >> 6)
    {
    case 0:
    {
        int reg8 = 2 * (opcode / 16) | (op8 & 1);  // i.e. b, c, d, e, ...
        int reg16 = reg8 / 2;                      // i.e. bc, de, hl...
        if (reg16 == 3)
            reg16 = 4;  // hack for SP
        switch (opcode % 16)
        {
        case 0:
        case 8:
            if (opcode == 0)
                break;  // nop
            if (opcode < 0x18)
                return __gb_rare_instruction(gb, opcode);
            {
                // jr
                cycles = 2;
                bool flag = __gb_get_op_flag(gb, op8);
                if (opcode == 0x18)
                    flag = 1;
                if (flag)
                {
                    cycles = 3;
                    gb->cpu_reg.pc += (s8)FETCH8(gb);
                }
                else
                {
                    gb->cpu_reg.pc++;
                }
            }
            break;
        case 1:
            // LD r16, d16
            cycles = 3;
            gb->cpu_reg_raw16[reg16] = FETCH16(gb);
            break;
        case 2:
        case 10:
            // TODO
            cycles = 2;
            if (reg16 == 4)
                reg16 = 2;

            if (op8 % 2 == 1)
            {
                // ld (r16), a
                __gb_write(gb, gb->cpu_reg_raw16[reg16], gb->cpu_reg.a);
            }
            else
            {
                // ld a, (r16)
                gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg_raw16[reg16]);
            }

            goto inc_dec_hl;
            break;
        case 3:
        case 11:
        {
            // inc r16
            // dec r16
            s16 offset = (op8 % 2 == 1) ? 1 : -1;
            gb->cpu_reg_raw16[reg16] += offset;
            cycles = 2;
        }
        break;

        // inc/dec 8-bit
        case 4:
        case 5:
        case 12:
        case 13:
        {
            // FIXME -- optimize?
            // inc r8
            // dec r8
            s8 offset = (opcode % 8 == 4) ? 1 : -1;
            u8 src = (reg8 == 7) ? __gb_read(gb, gb->cpu_reg.hl) : gb->cpu_reg_raw[reg8];
            u8 tmp = src + offset;
            gb->cpu_reg.f_bits.z = tmp == 0;
            if (offset == 1)
            {
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = (tmp & 0xF) == 0;
            }
            else
            {
                gb->cpu_reg.f_bits.n = 1;
                gb->cpu_reg.f_bits.h = (tmp & 0xF) == 0xF;
            }
            if (reg8 == 7)
            {
                cycles = 3;
                __gb_write(gb, gb->cpu_reg.hl, tmp);
            }
            else
            {
                gb->cpu_reg_raw[reg8] = tmp;
            }
        }
        break;

        case 6:
        case 14:
            srcidx = 0;
            src = FETCH8(gb);
            cycles = 2;
            goto ld_x_x;
            break;

        case 7:
        case 15:
            // misc flag ops
            if (opcode < 0x20)
            {
                // rlca
                // rrca
                // rla
                // rra
                u32 v = gb->cpu_reg.a << 8;
                if (op8 & 2)
                {
                    // carry bit will rotate into a
                    u32 c = gb->cpu_reg.f_bits.c;
                    v |= (c << 7) | (c << 16);
                }
                else
                {
                    // opposite bit will rotate into a
                    v = v | (v << 8);
                    v = v | (v >> 8);
                }
                if (op8 & 1)
                {
                    v <<= 1;
                }
                else
                {
                    v >>= 1;
                }
                gb->cpu_reg.f = 0;
                gb->cpu_reg.f_bits.c = (v >> (7 + 9 * (op8 & 1))) & 1;
                gb->cpu_reg.a = (v >> 8) & 0xFF;
            }
            else if unlikely (opcode == 0x27)
                return __gb_rare_instruction(gb, opcode);
            else if (opcode == 0x2F)
            {
                gb->cpu_reg.a ^= 0xFF;
                gb->cpu_reg.f_bits.n = 1;
                gb->cpu_reg.f_bits.h = 1;
            }
            else if (op8 % 2 == 1)
            {
                gb->cpu_reg.f_bits.c = 1;
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;
            }
            else if (op8 % 2 == 0)
            {
                gb->cpu_reg.f_bits.c ^= 1;
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;
            }
            break;

        case 9:
            // add hl, r16
            cycles = 2;
            gb->cpu_reg.hl = __gb_add16(gb, gb->cpu_reg.hl, gb->cpu_reg_raw16[reg16]);
            break;

        default:
            __builtin_unreachable();
        }
    }
    break;
    case 1:
    case 2:
    {
        srcidx = (opcode % 8) ^ 1;
        if (srcidx == 7)
        {
            src = __gb_read(gb, gb->cpu_reg.hl);
            cycles = 2;
        }
        else
            src = gb->cpu_reg_raw[srcidx];

        switch (opcode >> 6)
        {
        case 1:
            // LD x, x
        ld_x_x:
        {
            u8 dstidx = op8;
            if (dstidx == 7)
            {
                if unlikely (srcidx == 7)
                {
                    gb->gb_halt = 1;
                    return 4;
                }
                else
                {
                    cycles++;
                    __gb_write(gb, gb->cpu_reg.hl, src);
                }
            }
            else
            {
                gb->cpu_reg_raw[dstidx] = src;
            }
        }
        break;
        case 2:
        arithmetic:
            switch (op8)
            {
            case 0:  // ADC
            case 1:  // ADD
            case 2:  // SBC
            case 3:  // SUB
            case 6:  // CP
            {
                // carry bit
                unsigned v = src;
                if (op8 % 2 == 0 && op8 != 6)
                {
                    v += gb->cpu_reg.f_bits.c;
                }

                // subtraction
                gb->cpu_reg.f_bits.n = 0;
                if (op8 & 2)
                {
                    v = -v;
                    gb->cpu_reg.f_bits.n = 1;
                }

                // adder
                const u16 temp = gb->cpu_reg.a + v;
                gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
                gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a ^ src ^ temp) >> 4) & 1;
                gb->cpu_reg.f_bits.c = temp >> 8;

                if (op8 != 6)
                {
                    gb->cpu_reg.a = temp & 0xFF;
                }
            }
            break;
            case 4:  // XOR
                gb->cpu_reg.a ^= src;
                gb->cpu_reg.f = 0;
                gb->cpu_reg.f_bits.z = gb->cpu_reg.a == 0;
                break;
            case 5:  // AND
                gb->cpu_reg.a &= src;
                gb->cpu_reg.f = 0;
                gb->cpu_reg.f_bits.h = 1;
                gb->cpu_reg.f_bits.z = gb->cpu_reg.a == 0;
                break;
            case 7:  // OR
                gb->cpu_reg.a |= src;
                gb->cpu_reg.f = 0;
                gb->cpu_reg.f_bits.z = gb->cpu_reg.a == 0;
                break;
            default:
                __builtin_unreachable();
            }
            break;
        }
    }
    break;
    case 3:
    {
        bool flag = __gb_get_op_flag(gb, op8);
        if (opcode % 8 == 3)
            flag = 1;
        switch ((opcode % 16) | ((opcode & 0x20) >> 1))
        {
        case 0x00:
        case 0x08:  // ret [flag]
            cycles = 2;
            if (flag)
            {
                goto ret;
            }
            break;
        case 0x01:
        case 0x11:  // pop
            cycles = 3;
            src = __gb_pop16(gb);
            if (op8 / 2 == 3)
            {
                gb->cpu_reg.a = src >> 8;
                gb->cpu_reg.f = src & 0xF0;
            }
            else
            {
                gb->cpu_reg_raw16[op8 / 2] = src;
            }
            break;
        case 0x02:
        case 0xA:  // jp [flag]
            cycles = 3;
            if (flag)
            {
                goto jp;
            }
            gb->cpu_reg.pc += 2;
            break;
        case 0x03:  // jp
            if unlikely (opcode == 0xD3)
            {
                return __gb_rare_instruction(gb, opcode);
            }
        jp:
            cycles = 4;
            gb->cpu_reg.pc = FETCH16(gb);
            break;
        case 0x04:
        case 0x0C:  // call [flag]
            cycles = 3;
            if (flag)
            {
                goto call;
            }
            gb->cpu_reg.pc += 2;
            break;
        case 0x05:
        case 0x15:  // push
            cycles = 4;
            src = gb->cpu_reg_raw16[op8 / 2];
            if (op8 / 2 == 3)
            {
                src = (gb->cpu_reg.a << 8) | (gb->cpu_reg.f & 0xF0);
            }
            __gb_push16(gb, src);
            break;
        case 0x06:
        case 0x0E:
        case 0x16:
        case 0x1E:  // arith d8
            cycles = 2;
            src = FETCH8(gb);
            goto arithmetic;
            break;
        case 0x07:
        case 0x0F:
        case 0x17:
        case 0x1F:  // rst
            cycles = 4;
            __gb_push16(gb, gb->cpu_reg.pc);
            gb->cpu_reg.pc = 8 * (op8 ^ 1);
            break;
        case 0x09:  // ret, reti
            if unlikely (opcode == 0xD9)
            {
                gb->gb_ime = 1;
            }
        ret:
            cycles += 3;
            gb->cpu_reg.pc = __gb_pop16(gb);
            break;
        case 0x0B:  // CB opcodes
            return __gb_execute_cb(gb);
            break;
        case 0x0D:  // call
            if unlikely (op8 & 2)
            {
                return __gb_rare_instruction(gb, opcode);
            }
        call:
            cycles = 6;
            {
                u16 tmp = FETCH16(gb);
                __gb_push16(gb, gb->cpu_reg.pc);
                gb->cpu_reg.pc = tmp;
            }
            break;
        case 0x10:  // ld (a8)
        {
            cycles = 3;
            // repurpose 'srcidx'
            srcidx = (FETCH8(gb));
        hram_op:;
            u16 addr = 0xFF00 | srcidx;
            if (opcode & 0x10)
            {
                u8 v = __gb_read(gb, addr);
                gb->cpu_reg.a = v;
            }
            else
            {
                __gb_write(gb, addr, gb->cpu_reg.a);
            }
        }
        break;
        case 0x12:  // ld (C)
        {
            cycles = 2;
            srcidx = gb->cpu_reg.c;
            goto hram_op;
        }
        break;
        case 0x13:
        case 0x1B:  // di/ei
        case 0x14:
        case 0x1C:
        case 0x1D:  // illegal
        case 0x18:  // SP+8
        case 0x19:  // pc/sp hl
            return __gb_rare_instruction(gb, opcode);
            break;
        case 0x1A:  // ld (a16)
        {
            cycles = 4;
            u16 v = FETCH16(gb);
            if (op8 & 2)
            {
                gb->cpu_reg.a = __gb_read(gb, v);
            }
            else
            {
                __gb_write(gb, v, gb->cpu_reg.a);
            }
        }
        break;
        default:
            __builtin_unreachable();
        }
    }
    break;
    default:
        __builtin_unreachable();
    }

    if (false)
    {
    inc_dec_hl:
        gb->cpu_reg.hl += (opcode >= 0x20);
        gb->cpu_reg.hl -= 2 * (opcode >= 0x30);
    }
    return cycles * 4;
}

__shell static void __gb_interrupt(struct gb_s* gb)
{
    gb->gb_halt = 0;

    if (gb->gb_ime)
    {
        /* Disable interrupts */
        gb->gb_ime = 0;

        /* Push Program Counter */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

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

__shell static uint16_t __gb_calc_halt_cycles(struct gb_s* gb)
{
    int src[] = {512, 512, 512};

#if 0
    // TODO: optimize serial
    if(gb->gb_reg.SC & SERIAL_SC_TX_START) return 16;
#endif

    if (gb->gb_reg.tac_enable)
    {
        src[1] = gb->gb_reg.tac_cycles + 1 - gb->counter.tima_count +
                 ((0x100 - gb->gb_reg.TIMA) << gb->gb_reg.tac_cycles_shift);
    }

    src[2] = LCD_LINE_CYCLES - gb->counter.lcd_count;
    if (gb->lcd_mode == LCD_HBLANK)
    {
        src[2] = PPU_MODE_0_HBLANK_CYCLES - gb->counter.lcd_count;
    }
    else if (gb->lcd_mode == LCD_SEARCH_OAM)
    {
        src[2] = PPU_MODE_2_OAM_CYCLES - gb->counter.lcd_count;
    }

    // return max{16, min(src...)}
    int cycles = src[0];
    if (src[1] < cycles)
        cycles = src[1];
    if (src[2] < cycles)
        cycles = src[2];

    // ensure positive
    cycles = (cycles < 16) ? 16 : cycles;

    return cycles;
}

/**
 * Internal function used to step the CPU.
 */
__core unsigned int __gb_step_cpu(struct gb_s* gb)
{
    unsigned inst_cycles = 16;

    /* Handle interrupts */
    if unlikely ((gb->gb_ime || gb->gb_halt) && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
    {
        __gb_interrupt(gb);
    }

    if unlikely (gb->gb_halt)
    {
        inst_cycles = __gb_calc_halt_cycles(gb);
        goto done_instr;
    }

#if CPU_VALIDATE == 0

    inst_cycles = __gb_run_instruction_micro(gb);
#else
    // run once as each, verify

    if (gb->cpu_reg.pc < 0x8000 && __gb_read_full(gb, gb->cpu_reg.pc) == CB_HW_BREAKPOINT_OPCODE)
    {
        // can't validate if breakpoint
        __gb_run_instruction_micro(gb);
    }
    else if (gb->gb_bios_enable)
    {
        // can't validate if bios
        __gb_run_instruction_micro(gb);
    }
    else
    {
        const u16 pc = gb->cpu_reg.pc;
        static u8 _wram[2][WRAM_SIZE];
        static u8 _vram[2][VRAM_SIZE];
        static u8 _cart_ram[2][0x20000];
        static struct gb_s _gb[2];

        memcpy(_wram[0], gb->wram, WRAM_SIZE);
        memcpy(_vram[0], gb->vram, VRAM_SIZE);
        if (gb->gb_cart_ram_size > 0)
            memcpy(_cart_ram[0], gb->gb_cart_ram, gb->gb_cart_ram_size);
        memcpy(&_gb[0], gb, sizeof(_gb));

        uint8_t opcode = (gb->gb_halt ? 0 : __gb_fetch8(gb));
        inst_cycles = __gb_run_instruction(gb, opcode);

        gb->cpu_reg.f_bits.unused = 0;

        memcpy(_wram[1], gb->wram, WRAM_SIZE);
        memcpy(_vram[1], gb->vram, VRAM_SIZE);
        memcpy(&_gb[1], gb, sizeof(struct gb_s));
        if (gb->gb_cart_ram_size > 0)
            memcpy(_cart_ram[1], gb->gb_cart_ram, gb->gb_cart_ram_size);

        memcpy(gb->wram, _wram[0], WRAM_SIZE);
        memcpy(gb->vram, _vram[0], VRAM_SIZE);
        memcpy(gb, &_gb[0], sizeof(struct gb_s));
        if (gb->gb_cart_ram_size > 0)
            memcpy(gb->gb_cart_ram, _cart_ram[0], gb->gb_cart_ram_size);

        uint8_t inst_cycles_m = __gb_run_instruction_micro(gb);

        gb->cpu_reg.f_bits.unused = 0;

        if (memcmp(gb->wram, _wram[1], WRAM_SIZE))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in wram on opcode %x", opcode);
        }
        if (memcmp(gb->vram, _vram[1], VRAM_SIZE))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in vram on opcode %x", opcode);
        }
        if (memcmp(gb->gb_cart_ram, _cart_ram[1], gb->gb_cart_ram_size))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in cart ram on opcode %x", opcode);
        }

        if (memcmp(&gb->cpu_reg, &_gb[1].cpu_reg, sizeof(struct cpu_registers_s)))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in CPU regs on opcode %x", opcode);
            if (gb->cpu_reg.af != _gb[1].cpu_reg.af)
            {
                playdate->system->error(
                    "AF, was %x, expected %x", gb->cpu_reg.af, _gb[1].cpu_reg.af
                );
            }
            if (gb->cpu_reg.bc != _gb[1].cpu_reg.bc)
            {
                playdate->system->error(
                    "BC, was %x, expected %x", gb->cpu_reg.bc, _gb[1].cpu_reg.bc
                );
            }
            if (gb->cpu_reg.de != _gb[1].cpu_reg.de)
            {
                playdate->system->error(
                    "DE, was %x, expected %x", gb->cpu_reg.de, _gb[1].cpu_reg.de
                );
            }
            if (gb->cpu_reg.hl != _gb[1].cpu_reg.hl)
            {
                playdate->system->error(
                    "HL, was %x, expected %x", gb->cpu_reg.hl, _gb[1].cpu_reg.hl
                );
            }
            if (gb->cpu_reg.sp != _gb[1].cpu_reg.sp)
            {
                playdate->system->error(
                    "SP, was %x, expected %x", gb->cpu_reg.sp, _gb[1].cpu_reg.sp
                );
            }
            if (gb->cpu_reg.pc != _gb[1].cpu_reg.pc)
            {
                playdate->system->error(
                    "PC, was %x, expected %x", gb->cpu_reg.pc, _gb[1].cpu_reg.pc
                );
            }
            goto printregs;
        }

        // assert audio data is final member of gb_s
        CB_ASSERT(sizeof(struct gb_s) - sizeof(audio_data) == offsetof(struct gb_s, audio));
        if (memcmp(gb, &_gb[1], offsetof(struct gb_s, audio)))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in gb struct on opcode %x, pc=%x", opcode, pc);
            goto printregs;
        }

        if (false)
        {
        printregs:
            playdate->system->logToConsole("AF %x -> %x", _gb[0].cpu_reg.af, gb->cpu_reg.af);
            playdate->system->logToConsole("BC %x -> %x", _gb[0].cpu_reg.bc, gb->cpu_reg.bc);
            playdate->system->logToConsole("DE %x -> %x", _gb[0].cpu_reg.de, gb->cpu_reg.de);
            playdate->system->logToConsole("HL %x -> %x", _gb[0].cpu_reg.hl, gb->cpu_reg.hl);
            playdate->system->logToConsole("SP %x -> %x", _gb[0].cpu_reg.sp, gb->cpu_reg.sp);
            playdate->system->logToConsole("PC %x -> %x", _gb[0].cpu_reg.pc, gb->cpu_reg.pc);
        }

        if (inst_cycles != inst_cycles_m)
        {
            gb->gb_frame = 1;
            playdate->system->error(
                "cycle difference on opcode %x (expected %d, was %d)", opcode, inst_cycles,
                inst_cycles_m
            );
        }
    }
#endif

    // cycles are halved/quartered during overclocked vblank
    if (gb->lcd_mode == LCD_VBLANK)
    {
        inst_cycles >>= gb->overclock;
    }

done_instr:
{
#if 0
        /* Check serial transmission. */
        if(gb->gb_reg.SC & SERIAL_SC_TX_START)
        {
            /* If new transfer, call TX function. */
            if(gb->counter.serial_count == 0 && gb->gb_serial_tx != NULL)
                (gb->gb_serial_tx)(gb, gb->gb_reg.SB);

            gb->counter.serial_count += inst_cycles;

            /* If it's time to receive byte, call RX function. */
            if(gb->counter.serial_count >= SERIAL_CYCLES)
            {
                /* If RX can be done, do it. */
                /* If RX failed, do not change SB if using external
                 * clock, or set to 0xFF if using internal clock. */
                uint8_t rx;

                if(gb->gb_serial_rx != NULL &&
                   (gb->gb_serial_rx(gb, &rx) ==
                    GB_SERIAL_RX_SUCCESS))
                {
                    gb->gb_reg.SB = rx;

                    /* Inform game of serial TX/RX completion. */
                    gb->gb_reg.SC &= 0x01;
                    gb->gb_reg.IF |= SERIAL_INTR;
                }
                else if(gb->gb_reg.SC & SERIAL_SC_CLOCK_SRC)
                {
                    /* If using internal clock, and console is not
                     * attached to any external peripheral, shifted
                     * bits are replaced with logic 1. */
                    gb->gb_reg.SB = 0xFF;

                    /* Inform game of serial TX/RX completion. */
                    gb->gb_reg.SC &= 0x01;
                    gb->gb_reg.IF |= SERIAL_INTR;
                }
                else
                {
                    /* If using external clock, and console is not
                     * attached to any external peripheral, bits are
                     * not shifted, so SB is not modified. */
                }

                gb->counter.serial_count = 0;
            }
        }
#endif

    /* Handle delayed TIMA overflow from the previous cycle. */
    if (gb->gb_reg.tima_overflow_delay)
    {
        gb->gb_reg.IF |= TIMER_INTR;
        gb->gb_reg.TIMA = gb->gb_reg.TMA;
        gb->gb_reg.tima_overflow_delay = 0;
    }

    /* TIMA register timing */
    if (gb->gb_reg.tac_enable)
    {
        gb->counter.tima_count += inst_cycles;
        while (gb->counter.tima_count >= gb->gb_reg.tac_cycles)
        {
            gb->counter.tima_count -= gb->gb_reg.tac_cycles;
            gb->gb_reg.TIMA++;

            if (gb->gb_reg.TIMA == 0x00)
            {
                gb->gb_reg.tima_overflow_delay = 1;
            }
        }
    }

    /* DIV register timing */
    // update DIV timer
    gb->counter.div_count += inst_cycles;
    gb->gb_reg.DIV += gb->counter.div_count / DIV_CYCLES;
    gb->counter.div_count %= DIV_CYCLES;

    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
    {
        gb->counter.lcd_off_count += inst_cycles;
        if (gb->counter.lcd_off_count >= LCD_FRAME_CYCLES)
        {
            gb->counter.lcd_off_count -= LCD_FRAME_CYCLES;
            gb->gb_frame = 1;
        }
    }
    else
    {
        /* LCD Timing */
        gb->counter.lcd_count += inst_cycles;

        switch (gb->lcd_mode)
        {
        // Mode 2: OAM Search (80 cycles)
        // The PPU is reading OAM (Sprite Attribute Table) to find sprites for the current line.
        case LCD_SEARCH_OAM:
            if (gb->counter.lcd_count >= PPU_MODE_2_OAM_CYCLES)
            {
                gb->counter.lcd_count -= PPU_MODE_2_OAM_CYCLES;
                gb->lcd_mode = LCD_TRANSFER;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_TRANSFER;
            }
            break;

        // Mode 3: Pixel Transfer (variable, avg. ~168-172 cycles)
        case LCD_TRANSFER:
            if (gb->counter.lcd_count >= PPU_MODE_3_VRAM_CYCLES)
            {
                gb->counter.lcd_count -= PPU_MODE_3_VRAM_CYCLES;

#if ENABLE_LCD
                if (gb->lcd_master_enable && !gb->lcd_blank && !gb->direct.frame_skip)
                    __gb_draw_line(gb);
#endif

                gb->lcd_mode = LCD_HBLANK;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_HBLANK;

                if (gb->gb_reg.STAT & STAT_MODE_0_INTR)
                    gb->gb_reg.IF |= LCDC_INTR;
            }
            break;

        // Mode 0: H-Blank (remaining cycles of the 456 total)
        // The PPU is idle until the end of the scanline.
        case LCD_HBLANK:
            if (gb->counter.lcd_count >= PPU_MODE_0_HBLANK_CYCLES)
            {
                gb->counter.lcd_count -= PPU_MODE_0_HBLANK_CYCLES;
                gb->gb_reg.LY++;

                __gb_check_lyc(gb);

                if (gb->gb_reg.LY == LCD_HEIGHT)
                {
                    gb->lcd_mode = LCD_VBLANK;
                    gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_VBLANK;
                    gb->gb_frame = 1;
                    gb->gb_reg.IF |= VBLANK_INTR;
                    gb->lcd_blank = 0;

                    if (gb->gb_reg.STAT & STAT_MODE_1_INTR)
                        gb->gb_reg.IF |= LCDC_INTR;
                }
                else
                {
                    gb->lcd_mode = LCD_SEARCH_OAM;
                    gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_SEARCH_OAM;

                    if (gb->gb_reg.STAT & STAT_MODE_2_INTR)
                        gb->gb_reg.IF |= LCDC_INTR;
                }
            }
            break;

        // Mode 1: V-Blank (10 lines, 4560 cycles total)
        // The PPU is idle, giving the CPU time to update VRAM.
        case LCD_VBLANK:
            if (gb->counter.lcd_count >= LCD_LINE_CYCLES)
            {
                gb->counter.lcd_count -= LCD_LINE_CYCLES;

                // LY continues to increment during V-Blank from 144 up to 153.
                if (gb->gb_reg.LY == (LCD_VERT_LINES - 1))
                {
                    gb->gb_reg.LY = 0;
                }
                else
                {
                    gb->gb_reg.LY++;
                }

                __gb_check_lyc(gb);

                if (gb->gb_reg.LY == 0)
                {
                    gb->lcd_mode = LCD_SEARCH_OAM;
                    gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_SEARCH_OAM;

                    if (gb->gb_reg.STAT & STAT_MODE_2_INTR)
                        gb->gb_reg.IF |= LCDC_INTR;

                    gb->display.window_clear = 0;
                    gb->display.WY = gb->gb_reg.WY;
                }
            }
            break;
        }
    }

    if (gb->direct.joypad_interrupts && gb->direct.joypad_interrupt_delay >= 0)
    {
        gb->direct.joypad_interrupt_delay -= inst_cycles;
        if (gb->direct.joypad_interrupt_delay < 0)
        {
            // Timer expired, fire the interrupt now.
            gb->gb_reg.IF |= CONTROL_INTR;
            // Reset the timer to its inactive state.
            gb->direct.joypad_interrupt_delay = -1;
        }
    }
}
    return inst_cycles;
}

__core void gb_run_frame(struct gb_s* gb)
{
    gb->gb_frame = 0;
    unsigned int total_cycles = 0;

    while (!gb->gb_frame && total_cycles < SCREEN_REFRESH_CYCLES)
    {
        total_cycles += __gb_step_cpu(gb);
#ifdef TRACE_LOG
        playdate->system->logToConsole(
            "%x:%04x %02x\n", gb->selected_rom_bank, gb->cpu_reg.pc, gb->cpu_reg.a
        );
        f
#endif
    }
}

#define ROM_HEADER_START 0x134
#define ROM_HEADER_SIZE (0x150 - ROM_HEADER_START)

void gb_reset(struct gb_s* gb);

struct StateHeader
{
    char magic[8];
    u32 version;

    // emulator architecture
    uint8_t big_endian : 1;
    uint8_t bits : 4;

    // indicates if a script is active
    uint8_t script : 1;

    // Custom field for CrankBoy timestamp.
    uint32_t timestamp;

    char reserved[20];
};

// Note: this version can be used on unswizzled structs,
// i.e. no pointers should be followed
__section__(".rare") uint32_t gb_get_state_size(struct gb_s* gb)
{
    return sizeof(struct StateHeader) + sizeof(struct gb_s) + ROM_HEADER_SIZE  // for safe-keeping
           + WRAM_SIZE + VRAM_SIZE + sizeof(xram) + gb->gb_cart_ram_size +
           MAX_BREAKPOINTS * sizeof(gb_breakpoint);

    // skipped: lcd; bgcache; rom
}

__section__(".rare") void gb_state_save(struct gb_s* gb, char* out)
{
    // header
    struct StateHeader header;
    memset(&header, 0, sizeof(header));
    CB_ASSERT(strlen(CB_SAVE_STATE_MAGIC) == sizeof(header.magic));
    memcpy(header.magic, CB_SAVE_STATE_MAGIC, sizeof(header.magic));
    header.version = CB_SAVE_STATE_VERSION;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    header.big_endian = 1;
#else
    header.big_endian = 0;
#endif
    header.bits = sizeof(void*);
    memcpy(out, &header, sizeof(header));
    out += sizeof(header);

    // gb
    memcpy(out, gb, sizeof(*gb));
    out += sizeof(*gb);

    // rom header (so we know the associated rom for this state)
    memcpy(out, gb->gb_rom + ROM_HEADER_START, ROM_HEADER_SIZE);
    out += ROM_HEADER_SIZE;

    // wram
    memcpy(out, gb->wram, WRAM_SIZE);
    out += WRAM_SIZE;

    // vram
    memcpy(out, gb->vram, VRAM_SIZE);
    out += VRAM_SIZE;

    // xram
    memcpy(out, xram, sizeof(xram));
    out += sizeof(xram);

    // cart ram
    if (gb->gb_cart_ram_size > 0)
    {
        memcpy(out, gb->gb_cart_ram, gb->gb_cart_ram_size);
        out += gb->gb_cart_ram_size;
    }

    // breakpoints
    memcpy(out, gb->breakpoints, MAX_BREAKPOINTS * sizeof(gb_breakpoint));
    out += MAX_BREAKPOINTS * sizeof(gb_breakpoint);

    // intentionally skipped: lcd; bgcache; rom

    // TODO: audio
}

// returns NULL on success; error message otherwise
// if failure, no change is made to gb.
// Note: provided gb must already be initialized for the given ROM;
// in particular, it needs to have a gb_cart_ram field init'd with the correct
// size, and rom needs to be already loaded.
__section__(".rare") const char* gb_state_load(struct gb_s* gb, const char* in, size_t size)
{
    // at least enough to read save header, rom header, and gb struct fields
    if (size < sizeof(struct StateHeader) + sizeof(struct gb_s) + ROM_HEADER_SIZE)
    {
        return "State size too small";
    }

    struct StateHeader* header = (struct StateHeader*)in;
    in += sizeof(struct StateHeader);

    if (strncmp(header->magic, CB_SAVE_STATE_MAGIC, sizeof(header->magic)))
    {
        return "Not a CrankBoy savestate";
    }

    if (header->version < CB_SAVE_STATE_VERSION)
    {
        return "State comes from an incompatible older version of CrankBoy";
    }

    if (header->version > CB_SAVE_STATE_VERSION)
    {
        return "State comes from an incompatible future version of CrankBoy";
    }

    if (header->bits != sizeof(void*))
    {
        return "State 64-bit/32-bit mismatch (note: Playdate/Simulator states "
               "cannot be shared)";
    }

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (!header->big_endian)
#else
    if (header->big_endian)
#endif
    {
        return "State endianness incorrect";
    }

    struct gb_s* in_gb = (struct gb_s*)(void*)in;
    in += sizeof(*gb);
    size_t state_size = gb_get_state_size(in_gb);

    if (size != state_size)
    {
        return "State size mismatch";
    }

    if (gb->gb_cart_ram_size != in_gb->gb_cart_ram_size)
    {
        return "Cartridge RAM size mismatch";
    }

    const uint8_t* in_rom_header = (const uint8_t*)in;
    const uint8_t* gb_rom_header = gb->gb_rom + ROM_HEADER_START;
    if (memcmp(in_rom_header, gb_rom_header, 15))
    {
        return "State appears to be for a different ROM";
    }
    in += ROM_HEADER_SIZE;

    // -- we're in the clear now --

    void* preserved_fields[] = {
        &gb->gb_rom,       &gb->wram,         &gb->vram,        &gb->gb_cart_ram,
        &gb->breakpoints,  &gb->lcd,          &gb->direct.priv, &gb->gb_error,
        &gb->gb_serial_tx, &gb->gb_serial_rx, &gb->gb_boot_rom,
#if ENABLE_BGCACHE
        &gb->bgcache,
#endif
    };

    void* preserved_data[sizeof(preserved_fields)];
    for (int i = 0; i < PEANUT_GB_ARRAYSIZE(preserved_fields); ++i)
    {
        memcpy(preserved_data + i, preserved_fields[i], sizeof(void*));
    }

    // gb struct
    memcpy(gb, in_gb, sizeof(*gb));

    for (int i = 0; i < PEANUT_GB_ARRAYSIZE(preserved_fields); ++i)
    {
        memcpy(preserved_fields[i], preserved_data + i, sizeof(void*));
    }

    // wram
    memcpy(gb->wram, in, WRAM_SIZE);
    in += WRAM_SIZE;

    // vram
    memcpy(gb->vram, in, VRAM_SIZE);
    in += VRAM_SIZE;

    // xram
    // memcpy(xram, in, sizeof(xram));
    in += sizeof(xram);

    // cartridge ram
    if (gb->gb_cart_ram_size > 0)
    {
        memcpy(gb->gb_cart_ram, in, gb->gb_cart_ram_size);
        in += gb->gb_cart_ram_size;
    }

    // breakpoints
    // NOTE: scripts should only set breakpoints on startup, so
    // we keep them as they are
    // memcpy(gb->breakpoints, in, MAX_BREAKPOINTS * sizeof(gb_breakpoint));
    in += MAX_BREAKPOINTS * sizeof(gb_breakpoint);

    // clear caches and other presentation-layer data
    memset(gb->lcd, 0, LCD_SIZE);
#if ENABLE_BGCACHE
    for (size_t i = 0; i < 0x800; ++i)
    {
        __gb_update_bgcache_tile_data_deferred(gb, i);
    }
#endif
    __gb_update_selected_bank_addr(gb);
    __gb_update_selected_cart_bank_addr(gb);

    // intentionally skipped: lcd; bgcache; rom

    // update boot rom overlay state
    if (gb->gb_bios_enable)
    {
        if (gb->gb_boot_rom)
        {
            memcpy(gb->gb_rom, gb->gb_boot_rom, 0x100);
        }
        else
        {
            // best we can do if boot rom is no longer available
            gb_reset(gb);
        }
    }
    else
    {
        memcpy(gb->gb_rom, gb_original_rom, 0x100);
    }

    gb_detect_interrupts(gb);

    return NULL;
}

/**
 * Gets the size of the save file required for the ROM.
 */
uint_fast32_t gb_get_save_size(struct gb_s* gb)
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
    struct gb_s* gb, void (*gb_serial_tx)(struct gb_s*, const uint8_t),
    enum gb_serial_rx_ret_e (*gb_serial_rx)(struct gb_s*, uint8_t*)
)
{
    gb->gb_serial_tx = gb_serial_tx;
    gb->gb_serial_rx = gb_serial_rx;
}

/**
 * Provides the emulator with the 256-byte boot ROM data.
 */
void gb_init_boot_rom(struct gb_s* gb, uint8_t* boot_rom);

uint8_t gb_colour_hash(struct gb_s* gb)
{
#define ROM_TITLE_START_ADDR 0x0134
#define ROM_TITLE_END_ADDR 0x0143

    uint8_t x = 0;

    for (uint16_t i = ROM_TITLE_START_ADDR; i <= ROM_TITLE_END_ADDR; i++)
        x += gb->gb_rom[i];

    return x;
}

/**
 * Resets the context, and initialises startup values.
 */
__section__(".rare") void gb_reset(struct gb_s* gb)
{
    gb->gb_halt = 0;
    gb->gb_ime = 1;
    gb->lcd_mode = LCD_HBLANK;

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

    if (gb->gb_boot_rom && preferences_bios)
    {
        /* With a boot ROM, we start from 0x0000 and enable the BIOS flag. */
        gb->gb_bios_enable = 1;

        /* Set registers to their power-on state. */
        gb->cpu_reg.af = 0x0000;
        gb->cpu_reg.bc = 0x0000;
        gb->cpu_reg.de = 0x0000;
        gb->cpu_reg.hl = 0x0000;
        gb->cpu_reg.sp = 0x0000;
        gb->cpu_reg.pc = 0x0000;
        gb->gb_reg.DIV = 0x00;
    }
    else if (preferences_experimental_gbc_mode)
    {
        playdate->system->logToConsole("Starting ROM with GBC registers");
        gb->gb_bios_enable = 1;
        gb->cpu_reg.af == 0x1180;
        gb->cpu_reg.bc = 0x0000;
        gb->cpu_reg.de = 0xFF56;
        gb->cpu_reg.hl = 0x000D;
        gb->cpu_reg.sp = 0xFFFE;
        gb->cpu_reg.pc = 0x0100;
        gb->gb_reg.DIV = 0xAC; // FIXME -- set this to the correct value
    }
    else
    {
        /* Without a boot ROM, we use the old "skip-BIOS" method. */
        gb->gb_bios_enable = 0;

        /* Initialise CPU registers as though the boot ROM has just finished. */
        gb->cpu_reg.af = 0x01B0;
        gb->cpu_reg.bc = 0x0013;
        gb->cpu_reg.de = 0x00D8;
        gb->cpu_reg.hl = 0x014D;
        gb->cpu_reg.sp = 0xFFFE;
        gb->cpu_reg.pc = 0x0100;
        gb->gb_reg.DIV = 0xAC;
    }

    gb->counter.lcd_count = 0;
    gb->counter.div_count = 0;
    gb->counter.tima_count = 0;
    gb->counter.serial_count = 0;
    gb->counter.lcd_off_count = 0;

    gb->gb_reg.TIMA = 0x00;
    gb->gb_reg.TMA = 0x00;
    gb->gb_reg.TAC = 0xF8;

    __gb_update_tac(gb);

    gb->gb_reg.IF = 0xE1;

    gb->gb_reg.LCDC = 0x91;
    gb->gb_reg.SCY = 0x00;
    gb->gb_reg.SCX = 0x00;
    gb->gb_reg.LYC = 0x00;

    /* Appease valgrind for invalid reads and unconditional jumps. */
    gb->gb_reg.SC = 0x7E;
    gb->gb_reg.STAT = 0;
    gb->gb_reg.LY = 0;

    __gb_write(gb, 0xFF47, 0xFC);  // BGP
    __gb_write(gb, 0xFF48, 0xFF);  // OBJP0
    __gb_write(gb, 0xFF49, 0x0F);  // OBJP1
    gb->gb_reg.WY = 0x00;
    gb->gb_reg.WX = 0x00;
    gb->gb_reg.IE = 0x00;

    gb->direct.joypad = 0xFF;
    gb->gb_reg.P1 = 0xCF;

    gb->gb_reg.tima_overflow_delay = 0;

    gb->direct.crank_menu_accumulation = 0x8000;
    gb->direct.crank_menu_delta = 0;

    memset(gb->vram, 0x00, VRAM_SIZE);
    memset(gb->wram, 0x00, WRAM_SIZE);
}

/**
 * Initialise the emulator context. gb_reset() is also called to initialise
 * the CPU.
 */
__section__(".rare") enum gb_init_error_e gb_init(
    struct gb_s* gb, uint8_t* wram, uint8_t* vram, uint8_t* lcd, uint8_t* gb_rom, size_t rom_size,
    void (*gb_error)(struct gb_s*, const enum gb_error_e, const uint16_t), void* priv
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
    const uint8_t cart_battery[] =
    {
        0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, /* 00-0F */
        1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, /* 10-1F */
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

    gb->wram = wram;
    gb->vram = vram;
#if ENABLE_BGCACHE
    static clalign uint8_t bgcache[BGCACHE_SIZE];
    memset(bgcache, 0, sizeof(bgcache));
    gb->bgcache = bgcache;
#endif
    memset(xram, 0, sizeof(xram));
    gb->lcd = lcd;
    gb->gb_rom = gb_rom;
    gb->gb_rom_size = rom_size;
    memcpy(gb_original_rom, gb_rom, sizeof(gb_original_rom));
    gb->gb_error = gb_error;
    gb->direct.priv = priv;
    static gb_breakpoint breakpoints[MAX_BREAKPOINTS];
    memset(breakpoints, 0xFF, sizeof(breakpoints));
    gb->breakpoints = breakpoints;

    /* Initialise serial transfer function to NULL. If the front-end does
     * not provide serial support, Peanut-GB will emulate no cable connected
     * automatically. */
    gb->gb_serial_tx = NULL;
    gb->gb_serial_rx = NULL;

    /* Check valid ROM using checksum value. */
    {
        uint8_t x = 0;

        for (uint16_t i = 0x0134; i <= 0x014C; i++)
            x = x - gb->gb_rom[i] - 1;

        if (x != gb->gb_rom[ROM_HEADER_CHECKSUM_LOC])
            return GB_INIT_INVALID_CHECKSUM;
    }

    /* Check if cartridge type is supported, and set MBC type. */
    {
        const uint8_t mbc_value = gb->gb_rom[mbc_location];

        if (mbc_value > sizeof(cart_mbc) - 1 || (gb->mbc = cart_mbc[mbc_value]) == 255u)
            return GB_INIT_CARTRIDGE_UNSUPPORTED;
    }

    gb->cart_ram = cart_ram[gb->gb_rom[mbc_location]];
    gb->cart_battery = cart_battery[gb->gb_rom[mbc_location]];
    gb->num_rom_banks_mask = num_rom_banks_mask[gb->gb_rom[bank_count_location]] - 1;
    gb->num_ram_banks = num_ram_banks[gb->gb_rom[ram_size_location]];

    gb->lcd_blank = 0;

    gb->direct.sound = ENABLE_SOUND;
    gb->direct.interlace_mask = 0xFF;
    gb->direct.enable_xram = 0;

    gb_detect_interrupts(gb);

    return GB_INIT_NO_ERROR;
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

        if (title_char >= ' ' && title_char <= '_')
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

void __gb_on_breakpoint(struct gb_s* gb, int breakpoint_number);

static unsigned __gb_run_instruction_micro(struct gb_s* gb);

// returns negative if failure
// returns breakpoint index otherwise
__section__(".rare") int set_hw_breakpoint(struct gb_s* gb, uint32_t rom_addr)
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
static __section__(".rare") int __gb_try_breakpoint(struct gb_s* gb)
{
    // only ROM-address breakpoints are supported
    size_t pc = gb->cpu_reg.pc - 1;
    if (pc >= 0x8000)
        return 0;
    size_t rom_addr =
        (pc < 0x4000)
            ? pc
            : (pc % 0x4000) | ((gb->selected_rom_bank & gb->num_rom_banks_mask) * ROM_BANK_SIZE);

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
                cycles = __gb_run_instruction_micro(gb);
            }

            // restore breakpoint
            gb->breakpoints[i].opcode = gb->gb_rom[rom_addr];
            gb->gb_rom[rom_addr] = CB_HW_BREAKPOINT_OPCODE;
            return cycles <= 0 ? 4 : cycles;
        }

        return 1;
    }

    return 0;
}

#if ENABLE_LCD

void gb_init_lcd(struct gb_s* gb)
{
    gb->direct.frame_skip = 0;

    gb->display.window_clear = 0;
    gb->display.WY = 0;
    gb->lcd_master_enable = 1;

    return;
}

#else

void gb_init_lcd(struct gb_s* gb)
{
}

#endif

__section__(".rare") static u8 __gb_invalid_instruction(struct gb_s* restrict gb, uint8_t opcode)
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
    return 1 * 4;  // ?
}

__shell static u8 __gb_rare_instruction(struct gb_s* restrict gb, uint8_t opcode)
{
    switch (opcode)
    {
    case 0x08:  // ld (a16), SP
        __gb_write16(gb, __gb_fetch16(gb), gb->cpu_reg.sp);
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
    case 0xE8:
    {
        int8_t offset = (int8_t)__gb_read(gb, gb->cpu_reg.pc++);
        uint16_t old_sp = gb->cpu_reg.sp;
        gb->cpu_reg.sp += offset;

        gb->cpu_reg.f_bits.z = 0;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((old_sp & 0xF) + (offset & 0xF) > 0xF);
        gb->cpu_reg.f_bits.c = ((old_sp & 0xFF) + (offset & 0xFF) > 0xFF);
    }
        return 4 * 4;
    case 0xE9:
        gb->cpu_reg.pc = gb->cpu_reg.hl;
        return 4;
    case 0xF3:
        gb->gb_ime = 0;
        return 1 * 4;
    case 0xF8:
    {
        int8_t offset = (int8_t)__gb_read(gb, gb->cpu_reg.pc++);
        uint16_t sp = gb->cpu_reg.sp;
        gb->cpu_reg.hl = sp + offset;

        gb->cpu_reg.f_bits.z = 0;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((sp & 0xF) + (offset & 0xF) > 0xF);
        gb->cpu_reg.f_bits.c = ((sp & 0xFF) + (offset & 0xFF) > 0xFF);
    }
        return 3 * 4;
    case 0xF9:
        gb->cpu_reg.sp = gb->cpu_reg.hl;
        return 2 * 4;
    case 0xFB:
        gb->gb_ime = 1;
        return 1 * 4;
    default:
        return __gb_invalid_instruction(gb, opcode);
    }
}

#endif
#endif  // PEANUT_GB_H
