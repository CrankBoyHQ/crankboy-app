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

extern uint8_t cgb_blend_stage;
extern uint8_t cgb_gray_lum_min;
extern uint8_t cgb_gray_lum_max;
extern int8_t cgb_gray_bias;

/* When set (Auto or Contrast gray mode), the render hooks count per-frame
 * pixel usage so the frontend can build a luminance histogram. */
extern bool cgb_hist_active;

/* When set (Contrast gray mode), __cgb_scan_luminance_range is skipped: the
 * frontend supplies the gray range directly. */
extern bool cgb_contrast_active;

/* Contrast gray mode: direct gray thresholds (T1 white .. T3 black) from the
 * frame luminance histogram; the stage-2 blend pass offsets them by delta. */
extern uint16_t cgb_thresh[3];
extern uint16_t cgb_thresh_delta;

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

/* CGB speed switch halt period.
 * Per Pandocs 2050 M-cycles = 8200 T-cycles. */
#define CGB_SPEED_SWITCH_HALT_T_CYCLES 8200

/* Serial clock locked to 8192Hz on DMG.
 * 4194304 / (8192 / 8) = 4096 clock cycles for sending 1 byte. */
#define SERIAL_CYCLES 4096

/* Timer input bits for TAC clock select 00,01,10,11 (falling-edge source). */
static const uint8_t TIMER_INPUT_BITS[4] = {9, 3, 5, 7};

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

/* Batch loop can overshoot budget by one CALL (24 T): BATCH_OVERSHOOT is that
 * (CPU T). */
#define BATCH_OVERSHOOT 23

/* How far (PPU T) a batch may run past the first PPU mode boundary. Bounds
 * STAT/LYC ISR latency and the STAT/LY peek window independently of total
 * batch length (VBlank LYC chains need cross + dispatch + ISR < 456). */
#define BATCH_CROSS_MAX 64
/* Total-length backstop (PPU T). */
#define BATCH_BUDGET_MAX 512

/* STAT mode-bit reads report the boundary this many CPU T late, approximating
 * one STAT poll-loop iteration (WH2Jet flicker). Scoped to STAT only - LY/lock
 * reads stay exact (RoboCop 2 hangs on a late LY). Added to remaining, not
 * folded into lag - folding flips late to early. */
#define STAT_READ_LAG_T 14

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
// Struct version bump: see header comment in pgb/pgb_v6.h
#include "pgb/pgb_v6.h"
#include "pgb/pgb_version.h"
// ---------------------

typedef struct PGB_VERSIONED(gb_s) gb_s;
typedef struct PGB_VERSIONED(gb_breakpoint) gb_breakpoint;
typedef struct PGB_VERSIONED(audio_data) audio_data;
typedef struct PGB_VERSIONED(chan_len_ctr) chan_len_ctr;
typedef struct PGB_VERSIONED(chan_vol_env) chan_vol_env;
typedef struct PGB_VERSIONED(chan_freq_sweep) chan_freq_sweep;
typedef struct PGB_VERSIONED(chan) chan;

void gb_step_cpu(gb_s* gb);
void gb_recompute_cgb_gray_palettes(gb_s* gb);

/* Precompute the static HLE poll-site table; re-run after a script patch. */
void __gb_hle_scan_rom(gb_s* gb);

enum cgb_support_e gb_get_models_supported(uint8_t* gb_rom);
bool gb_get_rom_uses_battery(uint8_t* gb_rom);

#ifdef TARGET_SIMULATOR
// Debug: when nonzero, gb_run_frame logs every instruction for this many frames
// (decremented per frame). Triggered from the simulator by pressing 'T'.
extern volatile int g_trace_frames_remaining;
#endif

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
#define __core_dmg __attribute__((optimize("Os"))) __attribute__((section(".text.itcm.dmg.")))
#define __core_dmg_section(x) __core_dmg
#define __core_cgb __attribute__((optimize("Os"))) __attribute__((section(".text.itcm.cgb.")))
#define __core_cgb_section(x) __core_cgb
#endif
#endif

// Draw cluster: dedicated sections so it can be relocated into the main
// DTCM pool as a separate block on (see tcm_relocate), keeping
// the core pocket small. Intra-cluster calls only (short_call).
#ifdef TARGET_SIMULATOR
#define __draw_dmg
#define __draw_cgb
#else
#define __draw_dmg                                                             \
    __attribute__((optimize("Os"))) __attribute__((section(".dtcm.draw.dmg"))) \
    __attribute__((short_call))
#define __draw_cgb                                                             \
    __attribute__((optimize("Os"))) __attribute__((section(".dtcm.draw.cgb"))) \
    __attribute__((short_call))
#endif

// Rare cluster (HALT/STOP/misc + HDMA) and HLE cluster: like the draw
// cluster, relocated as separate blocks into DTCM pockets (or main pool).
// Intra-cluster calls only (short_call); calls out to flash need long_call.
#ifdef TARGET_SIMULATOR
#define __rare_dmg
#define __rare_cgb
#define __hle_cgb
#else
#define __rare_dmg                                                        \
    __attribute__((optimize("Os"))) __attribute__((section(".rare.dmg"))) \
    __attribute__((short_call))
#define __rare_cgb                                                        \
    __attribute__((optimize("Os"))) __attribute__((section(".rare.cgb"))) \
    __attribute__((short_call))
#define __hle_cgb \
    __attribute__((optimize("Os"))) __attribute__((section(".hle.cgb"))) __attribute__((short_call))
#endif

// Cold (.rare) function callable from a relocated cluster: shell calling
// convention on device, plain noinline in the simulator (clang lacks
// long_call).
#ifdef TARGET_SIMULATOR
#define __rare_shell __section__(".rare") __attribute__((noinline))
#else
#define __rare_shell __section__(".rare") __attribute__((noinline, long_call))
#endif

// Offset of the relocated draw cluster (0 = run from flash). Set by
// tcm_relocate
extern intptr_t pgb_draw_reloc_offset;
// Same for the rare and HLE clusters.
extern intptr_t pgb_rare_reloc_offset;
extern intptr_t pgb_hle_reloc_offset;
extern intptr_t pgb_apu_write_reloc_offset;
extern intptr_t pgb_apu_sample_gen_reloc_offset;

#if ITCM_CORE
// 0 = run from flash; else DTCM relocation delta.
// core_itcm_offset: A block (hot: read/write helpers, CB, micro interpreter).
// core_itcm_offset_batch: batch block (batch-level: step_cpu, run_frame).
extern intptr_t core_itcm_offset;
extern intptr_t core_itcm_offset_batch;
#endif

// Call a draw-cluster function from core code through the relocation offset.
#define DRAW_CALL(fn, gb_) ((void (*)(gb_s*))((char*)(fn) + pgb_draw_reloc_offset))(gb_)

// Same for the rare cluster (void return) and a u8-returning rare op.
#define RARE_CALL(fn, gb_) ((void (*)(gb_s*))((char*)(fn) + pgb_rare_reloc_offset))(gb_)
#define RARE_CALL_U8(fn, gb_, op_) \
    ((u8 (*)(gb_s*, uint8_t))((char*)(fn) + pgb_rare_reloc_offset))(gb_, op_)

// HLE cluster gated read: u8 fn(gb, addr, v).
#define HLE_CALL_READ(fn, gb_, a_, v_) \
    ((u8 (*)(gb_s*, uint16_t, uint8_t))((char*)(fn) + pgb_hle_reloc_offset))(gb_, a_, v_)

// APU write cluster calls.
#define APU_CALL_PTR(fn) ((char*)(void*)(fn) + pgb_apu_write_reloc_offset)
#define APU_CALL_WR(fn, a_, ad_, v_, c_) \
    ((void (*)(audio_data*, uint16_t, uint8_t, uint32_t))APU_CALL_PTR(fn))(a_, ad_, v_, c_)

/* Cluster -> core calls: the core block is independently relocated, so a
 * plain (PC-relative) bl from a relocated cluster would jump into the void.
 * Call core functions through the core relocation offset instead (0 = flash
 * copy, also correct). Never mark the small core read/write helpers
 * long_call: that would slow every interpreter memory op. */
#if defined(ITCM_CORE) && !defined(TARGET_SIMULATOR)
#define CORE_CALL_PTR(fn) ((char*)(void*)(fn) + core_itcm_offset)
#else
#define CORE_CALL_PTR(fn) ((char*)(void*)(fn))
#endif
#define CORE_CALL_READ(fn, gb_, a_) ((u8 (*)(gb_s*, u16))CORE_CALL_PTR(fn))(gb_, a_)
#define CORE_CALL_READ32(fn, gb_, a_) ((uint32_t (*)(gb_s*, u16))CORE_CALL_PTR(fn))(gb_, a_)
#define CORE_CALL_FETCH16(fn, gb_) ((u16 (*)(gb_s*))CORE_CALL_PTR(fn))(gb_)
#define CORE_CALL_WRITE16(fn, gb_, a_, v_) \
    ((void (*)(gb_s*, u16, u16))CORE_CALL_PTR(fn))(gb_, a_, v_)

/* step_cpu (B block) -> micro interpreter (A block): the blocks relocate
 * independently, so call through the A offset (0 = flash copy, also correct;
 * a flash-resident step_cpu can safely enter the relocated micro). Same for
 * the other B->A calls (halt calc -> timer_distance, OAM DMA -> read). */
#if defined(ITCM_CORE) && !defined(TARGET_SIMULATOR)
#define MICRO_CALL(fn, gb_) ((unsigned (*)(gb_s*))((char*)(void*)(fn) + core_itcm_offset))(gb_)
#define CORE_CALL_U32(fn, gb_) ((uint32_t (*)(gb_s*))CORE_CALL_PTR(fn))(gb_)
#else
#define MICRO_CALL(fn, gb_) (fn)(gb_)
#define CORE_CALL_U32(fn, gb_) (fn)(gb_)
#endif

__draw_cgb static void __gb_check_lyc__cgb(gb_s* gb);
__draw_cgb static void __gb_update_stat_irq__cgb(gb_s* gb);
__draw_cgb static void __gb_update_lyc_and_stat_irq__cgb(gb_s* gb);

__core_dmg static unsigned __gb_run_instruction_micro__dmg(gb_s* gb);
__core_cgb static unsigned __gb_run_instruction_micro__cgb(gb_s* gb);

__core_dmg static uint8_t __gb_execute_cb__dmg(gb_s* gb);
__core_cgb static uint8_t __gb_execute_cb__cgb(gb_s* gb);

__core_dmg_section("short") static void __gb_write16__dmg(gb_s* restrict gb, u16 addr, u16 v);
__core_cgb_section("short") static void __gb_write16__cgb(gb_s* restrict gb, u16 addr, u16 v);

__core_dmg_section("short") static uint16_t __gb_read16__dmg(gb_s* restrict gb, u16 addr);
__core_cgb_section("short") static uint16_t __gb_read16__cgb(gb_s* restrict gb, u16 addr);

__core_dmg_section("short") static uint32_t __gb_read32__dmg(gb_s* restrict gb, u16 addr);
__core_cgb_section("short") static uint32_t __gb_read32__cgb(gb_s* restrict gb, u16 addr);

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

    const uint8_t mbc_value = gb_rom[0x0147];

    /* HuC3 (0xFE) and HuC1+RAM+BATTERY (0xFF) both have a battery. */
    if (mbc_value == 0xFE || mbc_value == 0xFF)
        return true;

    return cart_battery[mbc_value];
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

/* HuC3 MCU memory: 256 nybbles, packed 2 per byte, least significant
 * nybble at the lower address. */
__section__(".text.cb") static uint8_t __gb_huc3_get_nybble(const gb_s* gb, uint8_t addr)
{
    uint8_t b = gb->huc3.mem[addr >> 1];
    return (addr & 1) ? (b >> 4) : (b & 0x0F);
}

__section__(".text.cb") static void __gb_huc3_set_nybble(gb_s* gb, uint8_t addr, uint8_t val)
{
    uint8_t* b = &gb->huc3.mem[addr >> 1];

    if (addr & 1)
        *b = (*b & 0x0F) | ((val & 0x0F) << 4);
    else
        *b = (*b & 0xF0) | (val & 0x0F);
}

/* HuC3 time: nybbles $10-12 = minute of day (rolls at 1440),
 * $13-15 = day counter. */
__section__(".text.cb") static unsigned __gb_huc3_get_time(const gb_s* gb, uint8_t base)
{
    return __gb_huc3_get_nybble(gb, base) + __gb_huc3_get_nybble(gb, base + 1) * 10 +
           __gb_huc3_get_nybble(gb, base + 2) * 100;
}

__section__(".text.cb") static void __gb_huc3_set_time(gb_s* gb, uint8_t base, unsigned val)
{
    __gb_huc3_set_nybble(gb, base, val % 10);
    __gb_huc3_set_nybble(gb, base + 1, (val / 10) % 10);
    __gb_huc3_set_nybble(gb, base + 2, (val / 100) % 10);
}

/* Execute the HuC3 RTC mailbox command. Triggered by clearing semaphore
 * bit 0; synchronous, so the semaphore always reads "ready". */
__section__(".text.cb") static void __gb_huc3_rtc_exec(gb_s* gb)
{
    const uint8_t cmd = gb->huc3.cmd >> 4;
    const uint8_t arg = gb->huc3.cmd & 0x0F;

    switch (cmd)
    {
    case 0x1: /* Read value and increment access address */
        gb->huc3.response = __gb_huc3_get_nybble(gb, gb->huc3.addr);
        gb->huc3.addr++;
        break;

    case 0x2: /* Write, no increment (SameBoy; used by Pocket Family GB2) */
        __gb_huc3_set_nybble(gb, gb->huc3.addr, arg);
        break;

    case 0x3: /* Write value and increment access address */
        __gb_huc3_set_nybble(gb, gb->huc3.addr, arg);
        gb->huc3.addr++;
        break;

    case 0x4: /* Set access address least significant nybble */
        gb->huc3.addr = (gb->huc3.addr & 0xF0) | arg;
        break;

    case 0x5: /* Set access address most significant nybble */
        gb->huc3.addr = (arg << 4) | (gb->huc3.addr & 0x0F);
        break;

    case 0x6: /* Extended command */
        switch (arg)
        {
        case 0x0: /* Copy current time to $00-05 */
            for (uint8_t i = 0; i < 6; i++)
                __gb_huc3_set_nybble(gb, i, __gb_huc3_get_nybble(gb, 0x10 + i));
            break;

        case 0x1: /* Copy $00-05 to current time */
            for (uint8_t i = 0; i < 6; i++)
                __gb_huc3_set_nybble(gb, 0x10 + i, __gb_huc3_get_nybble(gb, i));
            break;

        case 0x2: /* Status request; games refuse to boot unless result is 1 */
            gb->huc3.response = 1;
            break;

        case 0xE: /* Tone generator (needs two executions); no speaker - stub */
        default:
            break;
        }
        break;

    default:
        break;
    }
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
    if (gb->mbc == 9)
    {
        /* HuC3: minute-of-day + days. sub_seconds keeps the fractional
         * remainder across calls so the minute counter advances. */
        if (seconds_to_add == 0)
            return;

        unsigned long long total =
            __gb_huc3_get_time(gb, 0x10) * 60ULL + gb->huc3.sub_seconds + seconds_to_add;
        unsigned days = __gb_huc3_get_time(gb, 0x13);

        __gb_huc3_set_time(gb, 0x10, (unsigned)((total / 60) % 1440));
        __gb_huc3_set_time(gb, 0x13, (days + (unsigned)(total / 86400)) % 1000);
        gb->huc3.sub_seconds = (uint8_t)(total % 60);
        return;
    }
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
    gb->gb_reg.tac_input_bit = TIMER_INPUT_BITS[gb->gb_reg.tac_rate];
}

__section__(".text.cb") static void __gb_timer_edge_tick(gb_s* gb)
{
    gb->gb_reg.TIMA++;
    if (gb->gb_reg.TIMA == 0x00)
    {
        gb->gb_reg.TIMA = gb->gb_reg.TMA;
        gb->gb_reg.tima_overflow_delay = 1;
        /* See the count path in __gb_step_cpu: IF.TIMER is set at overflow
         * detection, not one step later, so an IF write between the two
         * cannot drop the pending interrupt. */
        gb->gb_reg.IF |= TIMER_INTR;
    }
}

__section__(".text.cb") static void __gb_update_selected_bank_addr(gb_s* gb)
{
    // swappable cartridge ROM bank
    int effective_bank = gb->selected_rom_bank;
    // 00->01 quirk: MBC1 translates bank 0->1
    if (gb->mbc == 1 && (effective_bank & 0x1F) == 0)
        effective_bank++;
    int32_t offset = ((int)(effective_bank & gb->num_rom_banks_mask) - 1) * ROM_BANK_SIZE;

    for (int i = 0; i < 4; ++i)
    {
        gb->rom_bank_base[1][i] = gb->gb_rom + offset;
    }

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
    for (int i = 0; i < 4; ++i)
    {
        gb->rom_bank_base[0][i] =
            gb->gb_rom + (gb->zero_bank_base & ((gb->num_rom_banks_mask + 1) * ROM_BANK_SIZE - 1));
    }
}

__section__(".text.cb") static void __gb_update_mbc1_zero_bank(gb_s* gb)
{
    if (gb->mbc == 1 && gb->cart_mode_select)
    {
        if (gb->is_mbc1m)
            gb->zero_bank_base = ((gb->cart_ram_bank & 0x03) << 4) * ROM_BANK_SIZE;
        else
            gb->zero_bank_base = ((gb->cart_ram_bank & 0x03) << 5) * ROM_BANK_SIZE;
    }
    else
    {
        gb->zero_bank_base = 0;
    }
    __gb_update_zero_bank_addr(gb);
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
        else if (gb->mbc == 7)
        {
            gb->selected_cart_bank_addr = NULL;
        }
        else if (gb->mbc == 8 || gb->mbc == 9)
        {
            /* HuC1/HuC3: $A000-BFFF mapping depends on mode registers
             * (IR/RTC select), so always use the full access path. */
            gb->selected_cart_bank_addr = NULL;
        }
        else if (
            (gb->cart_mode_select || gb->mbc != 1) && !gb->is_mbc1m &&
            gb->cart_ram_bank < gb->num_ram_banks
        )
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
    gb->echo_ram_base = gb->wram_base[0];
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

__rare_cgb static void __gb_do_hdma(gb_s* gb)
{
    int hdma_remaning = (unsigned)gb->cgb_hdma_len;

    uint16_t src = gb->cgb_hdma_src;
    /* Bank offset 0..0x1FFF; wraps within bank (hardware behavior) and can
     * never index out of the VRAM buffer, even for corrupt state values. */
    unsigned base = gb->cgb_hdma_dst % VRAM_SIZE;

    // NOTE: we use __cgb version because hdma is only possible on CGB.
    for (int i = 0; i < 0x10; i += 4)
    {
        bool aligned = ((base + i) & 3) == 0;
        bool no_wrap = (base + i + 3) < VRAM_SIZE;
        bool same_region = ((base + i) < 0x1800) == ((base + i + 3) < 0x1800);
        bool src_ok = (src + i) < 0x8000 || (src + i + 3) >= 0xA000 ||
                      ((src + i) >= 0x9800) == ((src + i + 3) >= 0x9800);

        if (aligned && no_wrap && same_region && src_ok)
        {
            uint32_t v = CORE_CALL_READ32(__gb_read32__cgb, gb, src + i);
            if (base + i < 0x1800)
            {
                uint8_t b0 = reverse_bits_u8((uint8_t)(v));
                uint8_t b1 = reverse_bits_u8((uint8_t)(v >> 8));
                uint8_t b2 = reverse_bits_u8((uint8_t)(v >> 16));
                uint8_t b3 = reverse_bits_u8((uint8_t)(v >> 24));
                v = (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) |
                    ((uint32_t)b3 << 24);
            }
            *(uint32_t*)(&gb->vram_base[VRAM_ADDR + base + i]) = v;
        }
        else
        {
            for (int j = i; j < i + 4; ++j)
            {
                unsigned off = (base + j) % VRAM_SIZE;
                uint8_t v = CORE_CALL_READ(__gb_read__cgb, gb, src + j);
                gb->vram_base[VRAM_ADDR + off] = (off < 0x1800) ? reverse_bits_u8(v) : v;
            }
        }
    }

    gb->cgb_hdma_len = hdma_remaning - 1;
    gb->cgb_hdma_active = hdma_remaning > 0;

    gb->cgb_hdma_src += 0x10;
    gb->cgb_hdma_dst += 0x10;
}

#define __cgb_numer(x) ((x) < 0 ? 0 : (x) > 8 ? 8 : (x))

/* Merged-blend render mode: when set, the draw cluster renders BG via the
 * pre-blended remap LUTs (slots 16-47) and sprites via pgb_obj_blend_pal.
 * Set by the frontend around a frame render; always false between frames. */
static bool pgb_blend_merged;
static uint8_t pgb_obj_blend_pal[2][8];

/* Set by palette-data writes (BCPD/OCPD) and frontend events (init, state
 * load, bias change). Gates the per-frame gray/blend LUT rebuild. */
static bool pgb_cgb_lut_dirty;

/* Per-palette dirty masks for batched gray-LUT rebuilds. Bit i = palette i.
 * Set on BCPD/OCPD writes; flushed at the next mode 3 entry (writes can only
 * land outside mode 3, so this matches per-write update timing exactly). */
static uint8_t pgb_cgb_bg_pal_dirty, pgb_cgb_obj_pal_dirty;

/* Per-palette color luminance (RGB -> sum), used by the usage-histogram
 * (Auto/Contrast gray mode) render hooks in the core. */
static uint8_t cgb_bg_lum_sum[8][4];
static uint8_t cgb_obj_lum_sum[8][4];

/* Per-palette 4-pixel pattern -> packed luminance bins (6 bits each), used by
 * the frontend to expand per-frame usage counts into a histogram. Built when
 * a palette's gray mapping is rebuilt. */
static uint32_t cgb_bg_patbin[8][256];

/* Perceptual (gamma) luminance correction: linear luma 0-93 -> perceptual 0-93.
 * Applied at the luma source so gray mapping, histogram and range scan all
 * operate in perceptual space. Row i = gamma (0.6 + 0.1*i for i<4,
 * else 1.0 + 0.2*(i-4)); default 10 = 2.2x. */
static const uint8_t cgb_gamma_luts[13][94] = {
    {
        /* gamma 0.6 */
        0,  0,  0,  0,  0,  1,  1,  1,  2,  2,  2,  3,  3,  4,  4,  4,  5,  5,  6,
        7,  7,  8,  8,  9,  10, 10, 11, 12, 13, 13, 14, 15, 16, 17, 17, 18, 19, 20,
        21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 38, 39, 40,
        41, 42, 44, 45, 46, 47, 49, 50, 51, 53, 54, 55, 57, 58, 59, 61, 62, 64, 65,
        66, 68, 69, 71, 72, 74, 75, 77, 78, 80, 82, 83, 85, 86, 88, 90, 91, 93,
    },
    {
        /* gamma 0.7 */
        0,  0,  0,  1,  1,  1,  2,  2,  3,  3,  4,  4,  5,  6,  6,  7,  8,  8,  9,
        10, 10, 11, 12, 13, 13, 14, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25,
        26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 41, 42, 43, 44, 45,
        46, 47, 49, 50, 51, 52, 53, 55, 56, 57, 58, 59, 61, 62, 63, 65, 66, 67, 68,
        70, 71, 72, 74, 75, 76, 78, 79, 80, 82, 83, 85, 86, 87, 89, 90, 92, 93,
    },
    {
        /* gamma 0.8 */
        0,  0,  1,  1,  2,  2,  3,  4,  4,  5,  6,  6,  7,  8,  9,  10, 10, 11, 12,
        13, 14, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 25, 26, 27, 28, 29,
        30, 31, 32, 33, 34, 35, 36, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
        50, 52, 53, 54, 55, 56, 57, 58, 59, 61, 62, 63, 64, 65, 66, 68, 69, 70, 71,
        72, 73, 75, 76, 77, 78, 79, 81, 82, 83, 84, 86, 87, 88, 89, 91, 92, 93,
    },
    {
        /* gamma 0.9 */
        0,  1,  1,  2,  3,  4,  4,  5,  6,  7,  8,  9,  10, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 25, 26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 40, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, 62, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
        74, 75, 76, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 89, 90, 91, 92, 93,
    },
    {
        /* gamma 1.0 */
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
        19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
        38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
        57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
        76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93,
    },
    {
        /* gamma 1.2 */
        0,  2,  4,  5,  7,  8,  9,  11, 12, 13, 15, 16, 17, 18, 19, 20, 21, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
        44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 55, 56, 57, 58, 59, 60, 61,
        62, 63, 64, 65, 65, 66, 67, 68, 69, 70, 71, 72, 73, 73, 74, 75, 76, 77, 78,
        79, 79, 80, 81, 82, 83, 84, 85, 85, 86, 87, 88, 89, 90, 90, 91, 92, 93,
    },
    {
        /* gamma 1.4 */
        0,  4,  6,  8,  10, 12, 13, 15, 16, 18, 19, 20, 22, 23, 24, 25, 26, 28, 29,
        30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
        49, 50, 51, 52, 53, 54, 54, 55, 56, 57, 58, 59, 60, 61, 61, 62, 63, 64, 65,
        66, 66, 67, 68, 69, 70, 70, 71, 72, 73, 74, 74, 75, 76, 77, 77, 78, 79, 80,
        81, 81, 82, 83, 84, 84, 85, 86, 86, 87, 88, 89, 89, 90, 91, 92, 92, 93,
    },
    {
        /* gamma 1.6 */
        0,  5,  8,  11, 13, 15, 17, 18, 20, 22, 23, 24, 26, 27, 28, 30, 31, 32, 33,
        34, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50, 51, 52,
        53, 54, 55, 56, 57, 57, 58, 59, 60, 61, 62, 62, 63, 64, 65, 65, 66, 67, 68,
        68, 69, 70, 71, 71, 72, 73, 74, 74, 75, 76, 76, 77, 78, 79, 79, 80, 81, 81,
        82, 83, 83, 84, 85, 85, 86, 87, 87, 88, 89, 89, 90, 90, 91, 92, 92, 93,
    },
    {
        /* gamma 1.8 */
        0,  7,  11, 14, 16, 18, 20, 22, 24, 25, 27, 28, 30, 31, 32, 34, 35, 36, 37,
        38, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 51, 52, 53, 54, 55, 56,
        57, 57, 58, 59, 60, 61, 61, 62, 63, 64, 64, 65, 66, 67, 67, 68, 69, 69, 70,
        71, 72, 72, 73, 74, 74, 75, 76, 76, 77, 78, 78, 79, 79, 80, 81, 81, 82, 83,
        83, 84, 84, 85, 86, 86, 87, 87, 88, 88, 89, 90, 90, 91, 91, 92, 92, 93,
    },
    {
        /* gamma 2.0 */
        0,  10, 14, 17, 19, 22, 24, 26, 27, 29, 30, 32, 33, 35, 36, 37, 39, 40, 41,
        42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 55, 56, 57, 58, 59,
        59, 60, 61, 62, 62, 63, 64, 65, 65, 66, 67, 68, 68, 69, 70, 70, 71, 72, 72,
        73, 73, 74, 75, 75, 76, 77, 77, 78, 78, 79, 80, 80, 81, 81, 82, 82, 83, 84,
        84, 85, 85, 86, 86, 87, 87, 88, 88, 89, 89, 90, 90, 91, 91, 92, 92, 93,
    },
    {
        /* gamma 2.2 */
        0,  12, 16, 20, 22, 25, 27, 29, 30, 32, 34, 35, 37, 38, 39, 41, 42, 43, 44,
        45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 56, 57, 58, 59, 60, 60, 61,
        62, 63, 63, 64, 65, 65, 66, 67, 68, 68, 69, 70, 70, 71, 71, 72, 73, 73, 74,
        74, 75, 76, 76, 77, 77, 78, 78, 79, 80, 80, 81, 81, 82, 82, 83, 83, 84, 84,
        85, 85, 86, 86, 87, 87, 88, 88, 89, 89, 90, 90, 91, 91, 92, 92, 93, 93,
    },
    {
        /* gamma 2.4 */
        0,  14, 19, 22, 25, 28, 30, 32, 33, 35, 37, 38, 40, 41, 42, 43, 45, 46, 47,
        48, 49, 50, 51, 52, 53, 54, 55, 56, 56, 57, 58, 59, 60, 60, 61, 62, 63, 63,
        64, 65, 65, 66, 67, 67, 68, 69, 69, 70, 71, 71, 72, 72, 73, 74, 74, 75, 75,
        76, 76, 77, 77, 78, 79, 79, 80, 80, 81, 81, 82, 82, 83, 83, 84, 84, 85, 85,
        85, 86, 86, 87, 87, 88, 88, 89, 89, 90, 90, 90, 91, 91, 92, 92, 93, 93,
    },
    {
        /* gamma 2.6 */
        0,  16, 21, 25, 28, 30, 32, 34, 36, 38, 39, 41, 42, 44, 45, 46, 47, 48, 49,
        50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 59, 60, 61, 62, 62, 63, 64, 65, 65,
        66, 67, 67, 68, 69, 69, 70, 70, 71, 72, 72, 73, 73, 74, 74, 75, 75, 76, 77,
        77, 78, 78, 79, 79, 80, 80, 81, 81, 82, 82, 82, 83, 83, 84, 84, 85, 85, 86,
        86, 86, 87, 87, 88, 88, 89, 89, 89, 90, 90, 91, 91, 91, 92, 92, 93, 93,
    },
};

static inline int __cgb_gamma_index(void)
{
    int g = preferences_cgb_gamma;
    if (g < 0)
        g = 0;
    if (g > 12)
        g = 12;
    return g;
}

__section__(".rare") static uint8_t __cgb_gray_from_sum(uint16_t sum)
{
    if (cgb_contrast_active)
    {
        // Contrast mode: direct thresholds from the frame histogram.
        const uint16_t d = (cgb_blend_stage == 2) ? cgb_thresh_delta : 0;
        if (sum >= cgb_thresh[0] + d)
            return 0;
        if (sum >= cgb_thresh[1] + d)
            return 1;
        if (sum >= cgb_thresh[2] + d)
            return 2;
        return 3;
    }

    uint16_t range = (uint16_t)cgb_gray_lum_max - (uint16_t)cgb_gray_lum_min;
    if (range == 0)
        range = 1;
    uint16_t base = (uint16_t)cgb_gray_lum_min;
    int8_t b = cgb_gray_bias;

    // Thresholds in eighths of the range: bias shifts by range/8 per step.
    switch (cgb_blend_stage)
    {
    case 1:
        if (sum >= base + ((range * __cgb_numer(6 - b)) >> 3))
            return 0;
        if (sum >= base + ((range * __cgb_numer(4 - b)) >> 3))
            return 1;
        if (sum >= base + ((range * __cgb_numer(2 - b)) >> 3))
            return 2;
        return 3;
    case 2:
        if (sum >= base + ((range * __cgb_numer(7 - b)) >> 3))
            return 0;
        if (sum >= base + ((range * __cgb_numer(5 - b)) >> 3))
            return 1;
        if (sum >= base + ((range * __cgb_numer(3 - b)) >> 3))
            return 2;
        return 3;
    default:
        if (sum >= base + ((range * __cgb_numer(6 - b)) >> 3))
            return 0;
        if (sum >= base + ((range * __cgb_numer(4 - b)) >> 3))
            return 1;
        if (sum >= base + ((range * __cgb_numer(2 - b)) >> 3))
            return 2;
        return 3;
    }
}

__section__(".rare") static void __cgb_build_remap_lut(uint8_t* lut, uint8_t pal)
{
    for (int i = 0; i < 256; i++)
    {
        uint8_t lo = i & 0x0F;
        uint8_t hi = i >> 4;
        uint8_t p0 = (lo & 1) | ((hi & 1) << 1);
        uint8_t p1 = ((lo >> 1) & 1) | (((hi >> 1) & 1) << 1);
        uint8_t p2 = ((lo >> 2) & 1) | (((hi >> 2) & 1) << 1);
        uint8_t p3 = ((lo >> 3) & 1) | (((hi >> 3) & 1) << 1);
        uint8_t c0 = (pal >> (2 * p3)) & 3;
        uint8_t c1 = (pal >> (2 * p2)) & 3;
        uint8_t c2 = (pal >> (2 * p1)) & 3;
        uint8_t c3 = (pal >> (2 * p0)) & 3;
        lut[i] = (c0 << 6) | (c1 << 4) | (c2 << 2) | c3;
    }
}

__section__(".rare") static void __cgb_update_bg_gray_palette(
    gb_s* gb, uint8_t pal_idx, int lut_offset
)
{
    const bool hist = (preferences_cgb_bias_auto != 0);
    uint8_t pal = 0;
    for (int c = 0; c < 4; c++)
    {
        uint8_t lo = gb->cgb_bg_palette[pal_idx * 8 + c * 2];
        uint8_t hi = gb->cgb_bg_palette[pal_idx * 8 + c * 2 + 1];
        uint8_t r = lo & 0x1F;
        uint8_t g = ((lo >> 5) & 7) | ((hi & 3) << 3);
        uint8_t b = (hi >> 2) & 0x1F;
        uint16_t sum = (uint16_t)((231 * (uint16_t)r + 450 * (uint16_t)g + 87 * (uint16_t)b) >> 8);
        sum = cgb_gamma_luts[__cgb_gamma_index()][sum];
        uint8_t gray = __cgb_gray_from_sum(sum);
        if (hist)
            cgb_bg_lum_sum[pal_idx][c] = (uint8_t)sum;
        pal |= (uint8_t)(gray << (2 * c));
    }
    gb->cgb_bg_palette_gray[pal_idx] = pal;
    __cgb_build_remap_lut(gb->cgb_bg_palette + 64 + (pal_idx + lut_offset) * 256, pal);

    if (hist)
    {
        // Pack per-pattern luminance bins (6 bits each) for the usage
        // expansion: color_i = (lo_i) | (hi_i << 1), raw = lo | (hi << 4).
        for (int raw = 0; raw < 256; raw++)
        {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++)
            {
                uint8_t color = ((raw >> i) & 1) | ((((raw >> (4 + i)) & 1)) << 1);
                v |= (uint32_t)(cgb_bg_lum_sum[pal_idx][color] >> 2) << (6 * i);
            }
            cgb_bg_patbin[pal_idx][raw] = v;
        }
    }
}

__section__(".rare") static void __cgb_update_obj_gray_palette(
    gb_s* gb, uint8_t pal_idx, uint8_t* target
)
{
    const bool hist = (preferences_cgb_bias_auto != 0);
    uint8_t pal = 0;
    for (int c = 0; c < 4; c++)
    {
        uint8_t lo = gb->cgb_obj_palette[pal_idx * 8 + c * 2];
        uint8_t hi = gb->cgb_obj_palette[pal_idx * 8 + c * 2 + 1];
        uint8_t r = lo & 0x1F;
        uint8_t g = ((lo >> 5) & 7) | ((hi & 3) << 3);
        uint8_t b = (hi >> 2) & 0x1F;
        uint16_t sum = (uint16_t)((231 * (uint16_t)r + 450 * (uint16_t)g + 87 * (uint16_t)b) >> 8);
        sum = cgb_gamma_luts[__cgb_gamma_index()][sum];
        uint8_t gray = __cgb_gray_from_sum(sum);
        if (hist)
            cgb_obj_lum_sum[pal_idx][c] = (uint8_t)sum;
        pal |= (uint8_t)(gray << (2 * c));
    }
    target[pal_idx] = pal;
}

/* Rebuild gray palettes flagged by BCPD/OCPD writes since the last flush.
 * Called once per scanline at most (mode 3 entry). */
__shell static void __cgb_flush_pal_dirty(gb_s* gb)
{
    uint8_t bg = pgb_cgb_bg_pal_dirty, obj = pgb_cgb_obj_pal_dirty;
    pgb_cgb_bg_pal_dirty = 0;
    pgb_cgb_obj_pal_dirty = 0;
    while (bg)
    {
        int i = __builtin_ctz(bg);
        bg &= bg - 1;
        __cgb_update_bg_gray_palette(gb, i, 0);
    }
    while (obj)
    {
        int i = __builtin_ctz(obj);
        obj &= obj - 1;
        __cgb_update_obj_gray_palette(gb, i, gb->cgb_obj_palette_gray);
    }
}

// Build merged blend remap LUTs (BG slots 16-47 = 4 variants of line parity x subx parity)
// and OBJ blend pals from the stage-1 (bright) / stage-2 (dark) gray maps.
// Per-byte math is exactly blend_frames' per-field SWAR with the matching
// dither bias, so merged output is bit-identical to render+blend.
__section__(".rare") static void __cgb_build_blend_luts(gb_s* gb)
{
    for (int pal = 0; pal < 8; pal++)
    {
        const uint8_t* lut_b = gb->cgb_bg_palette + 64 + pal * 256;
        const uint8_t* lut_d = gb->cgb_bg_palette + 64 + (8 + pal) * 256;
        for (int variant = 0; variant < 4; variant++)
        {
            const int line_par = (variant >> 1) & 1;
            const int subx_par = variant & 1;
            uint8_t* out = gb->cgb_bg_palette + 64 + (16 + variant * 8 + pal) * 256;
            uint32_t bias_e = (line_par ^ subx_par) ? 0x11 : 0;
            uint32_t bias_o = (line_par ^ subx_par) ? 0 : 0x11;
            for (int i = 0; i < 256; i++)
            {
                uint32_t a = lut_b[i], b = lut_d[i];
                uint32_t e = (((a & 0x33) + (b & 0x33) + bias_e) >> 1) & 0x33;
                uint32_t o = ((((a >> 2) & 0x33) + ((b >> 2) & 0x33) + bias_o) >> 1) & 0x33;
                out[i] = e | (o << 2);
            }
        }
    }

    for (int pal = 0; pal < 8; pal++)
    {
        uint8_t g = gb->cgb_obj_palette_gray[pal];
        uint8_t d = gb->cgb_obj_palette_gray_alt[pal];
        for (int par = 0; par < 2; par++)
        {
            uint8_t m = 0;
            for (int c = 0; c < 4; c++)
            {
                uint8_t v = (((g >> (c * 2)) & 3) + ((d >> (c * 2)) & 3) + par) >> 1;
                m |= v << (c * 2);
            }
            pgb_obj_blend_pal[par][pal] = m;
        }
    }
}

__section__(".rare") static void __cgb_scan_luminance_range(gb_s* gb)
{
    if (cgb_contrast_active)
        return;

    uint8_t min_lum = 255;
    uint8_t max_lum = 0;

    for (int i = 0; i < 8; i++)
    {
        for (int c = 0; c < 4; c++)
        {
            uint8_t lo = gb->cgb_bg_palette[i * 8 + c * 2];
            uint8_t hi = gb->cgb_bg_palette[i * 8 + c * 2 + 1];
            uint8_t r = lo & 0x1F;
            uint8_t g = ((lo >> 5) & 7) | ((hi & 3) << 3);
            uint8_t b = (hi >> 2) & 0x1F;
            uint16_t sum =
                (uint16_t)((231 * (uint16_t)r + 450 * (uint16_t)g + 87 * (uint16_t)b) >> 8);
            sum = cgb_gamma_luts[__cgb_gamma_index()][sum];
            if (sum < min_lum)
                min_lum = (uint8_t)sum;
            if (sum > max_lum)
                max_lum = (uint8_t)sum;
        }
    }

    for (int i = 0; i < 8; i++)
    {
        for (int c = 0; c < 4; c++)
        {
            uint8_t lo = gb->cgb_obj_palette[i * 8 + c * 2];
            uint8_t hi = gb->cgb_obj_palette[i * 8 + c * 2 + 1];
            uint8_t r = lo & 0x1F;
            uint8_t g = ((lo >> 5) & 7) | ((hi & 3) << 3);
            uint8_t b = (hi >> 2) & 0x1F;
            uint16_t sum =
                (uint16_t)((231 * (uint16_t)r + 450 * (uint16_t)g + 87 * (uint16_t)b) >> 8);
            sum = cgb_gamma_luts[__cgb_gamma_index()][sum];
            if (sum < min_lum)
                min_lum = (uint8_t)sum;
            if (sum > max_lum)
                max_lum = (uint8_t)sum;
        }
    }

    cgb_gray_lum_min = min_lum;
    cgb_gray_lum_max = max_lum;
}

__section__(".rare") void gb_recompute_cgb_gray_palettes(gb_s* gb)
{
    __cgb_scan_luminance_range(gb);
    int lut_offset = (cgb_blend_stage == 2) ? 8 : 0;
    uint8_t* obj_target =
        (cgb_blend_stage == 2) ? gb->cgb_obj_palette_gray_alt : gb->cgb_obj_palette_gray;
    for (int i = 0; i < 8; i++)
    {
        __cgb_update_bg_gray_palette(gb, i, lut_offset);
        __cgb_update_obj_gray_palette(gb, i, obj_target);
    }
}

static inline __attribute__((always_inline)) uint16_t
__gb_ppu_cycles_remaining(gb_s* gb, int32_t slack);
static inline __attribute__((always_inline)) uint8_t __gb_ppu_next_mode(gb_s* gb);
static uint8_t __gb_ppu_mode_for_lock(gb_s* gb) __attribute__((always_inline));

__shell static void __gb_rare_write(gb_s* gb, const uint16_t addr, const uint8_t val)
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
            if (gb->is_cgb_mode)
            {
                gb->cgb_fast_mode_armed = val & 1;
            }
            return;

        case 0x4F:  // VBK (CGB VRAM Bank)
            if (gb->is_cgb_mode)
            {
                gb->cgb_vram_bank = val;
                __gb_update_selected_bank_addr(gb);
            }
            return;
        case 0x51:  // HDMA src hi
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_src &= 0x00FF;
                gb->cgb_hdma_src |= ((unsigned)val) << 8;
            }
            return;
        case 0x52:  // HDMA src lo
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_src &= 0xFF00;
                gb->cgb_hdma_src |= val & 0xF0;
            }
            return;
        case 0x53:  // HDMA dst hi
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_dst &= 0x00FF;
                gb->cgb_hdma_dst |= ((unsigned)val & 0x1F) << 8;
            }
            return;
        case 0x54:  // HDMA dst lo
            if (gb->is_cgb_mode)
            {
                gb->cgb_hdma_dst &= 0xFF00;
                gb->cgb_hdma_dst |= (val & 0xF0);
            }
            return;
        case 0x55:  // HDMA5 (VRAM DMA)
            if (gb->is_cgb_mode)
            {
                int was_len = (unsigned)gb->cgb_hdma_len;
                gb->cgb_hdma_len = val & 0x7F;
                bool was_active = gb->cgb_hdma_active;
                gb->cgb_hdma_active = (val >> 7);

                if (!gb->cgb_hdma_active && was_active)
                {
#if 0
                    playdate->system->logToConsole(
                        "active HDMA stopped, pc=%x, len was %d", gb->cpu_reg.pc, was_len
                    );
#endif
                }
                else
                {
                    if (gb->cgb_hdma_active)
                    {
#if 0
                        playdate->system->logToConsole(
                            "HDMA (async) 0x%x -> 0x%x, len=%d, pc=%x", gb->cgb_hdma_src,
                            gb->cgb_hdma_dst, gb->cgb_hdma_len, gb->cpu_reg.pc
                        );
#endif
                    }
                    else
                    {
#if 0
                        playdate->system->logToConsole(
                            "HDMA 0x%x -> 0x%x, len=%d, pc=%x", gb->cgb_hdma_src,
                            gb->cgb_hdma_dst, gb->cgb_hdma_len, gb->cpu_reg.pc
                        );
#endif
                        gb->cgb_hdma_active = true;
                        while (gb->cgb_hdma_active)
                            __gb_do_hdma(gb);

                        /* GDMA CPU stall: (len+1)x8 M-cycles, charged in PPU-domain
                         * T-cycles (x32) -- same duration in single and double speed. */
                        gb->cgb_gdma_halt_period = (uint16_t)((val & 0x7F) + 1) * 32;
                        gb->gb_halt = 1;
                    }
                }
            }
            return;
        case 0x56:  // RP (CGB Infrared Port)
            return;
        case 0x68:  // BCPS (CGB BG Palette Spec)
            if (gb->is_cgb_mode)
            {
                gb->cgb_bg_palette_index = val;
            }
            return;
        case 0x69:  // BCPD (CGB BG Palette Data)
            if (gb->is_cgb_mode)
            {
                if (__gb_ppu_mode_for_lock(gb) != LCD_TRANSFER)
                {
                    uint8_t idx = gb->cgb_bg_palette_index & 0x3F;
                    gb->cgb_bg_palette[idx] = val;
                    pgb_cgb_bg_pal_dirty |= 1 << (idx >> 3);
                    pgb_cgb_lut_dirty = true;
                    if (gb->cgb_bg_palette_index & 0x80)
                        gb->cgb_bg_palette_index =
                            (gb->cgb_bg_palette_index & 0x80) | ((idx + 1) & 0x3F);
                }
            }
            return;
        case 0x6A:  // OCPS (CGB OBJ Palette Spec)
            if (gb->is_cgb_mode)
            {
                gb->cgb_obj_palette_index = val;
            }
            return;
        case 0x6B:  // OCPD (CGB OBJ Palette Data)
            if (gb->is_cgb_mode)
            {
                if (__gb_ppu_mode_for_lock(gb) != LCD_TRANSFER)
                {
                    uint8_t idx = gb->cgb_obj_palette_index & 0x3F;
                    gb->cgb_obj_palette[idx] = val;
                    pgb_cgb_obj_pal_dirty |= 1 << (idx >> 3);
                    pgb_cgb_lut_dirty = true;
                    if (gb->cgb_obj_palette_index & 0x80)
                        gb->cgb_obj_palette_index =
                            (gb->cgb_obj_palette_index & 0x80) | ((idx + 1) & 0x3F);
                }
            }
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
            gb->hram[0xFF] = gb->gb_reg.IE;
            gb->direct.intr_pending = 1;  // conservative: re-checked at batch start
            return;
        }
    }

    (gb->gb_error)(gb, GB_INVALID_WRITE, addr);
}

/* HLE poll-loop verdict cache: decode each loop's load/cmp/jr shape
 * once per load-pc; per-read cost is a slot probe. ROM pcs only
 * (bank-keyed, immutable); WRAM polls re-analyze per read (rare). */
enum
{
    HLE_CMP_AND_A = 0,  // and a / or a:  z = (v == 0), c = 0
    HLE_CMP_CP_D8,      // cp d8:         z = (v == d8), c = (v < d8)
    HLE_CMP_SUB_D8,     // sub d8:        same flags as cp d8
    HLE_CMP_AND_D8,     // and d8:        z = !(v & d8), c = 0
    HLE_CMP_BIT_A,      // bit n,a:       z = !(v & bit), c unknown
    HLE_CMP_BIT_HL,     // bit n,(hl):    z = !(v & bit), c unknown
    HLE_CMP_AND_CP,     // and d8 + cp/sub d8: m = v & arg; z = (m == arg2), c = (m < arg2)
    HLE_CMP_AND_XOR,    // and d8 + xor d8:    z = ((v & arg) == arg2), c = 0
    HLE_CMP_CP_R,       // cp a,r:        z = (v == r), c = (v < r); arg = reg (0-5)
    HLE_CMP_AND_R,      // and a,r:       z = !(v & r), c = 0; arg = reg (0-5)
    HLE_CMP_OR_R,       // or a,r:        z = !(v | r), c = 0; arg = reg (0-5)
    HLE_CMP_XOR_R,      // xor a,r:       z = !(v ^ r), c = 0; arg = reg (0-5)
    HLE_CMP_SUB_R,      // sub a,r:       z = (v == r), c = (v < r); arg = reg (0-5)
    HLE_CMP_AND_DEC_A,  // and d8 + dec a: z = ((v & arg) == 1), c = 0
    HLE_CMP_SUB_CP,     // sub d8 + cp d8: z = ((v-arg) == arg2), c = ((v-arg) < arg2)
    HLE_CMP_CPL_AND,    // cpl + and d8:  z = ((v & arg) == arg), c = 0
    HLE_CMP_ADD_A,      // add a,a:       z = (v == 0 || v == 0x80), c = (v >= 0x80)
    HLE_CMP_SUB_R_CP    // sub a,r + cp d8: m = v - reg(arg); z = (m == arg2), c = (m < arg2)
};

enum
{
    HLE_JR_NZ = 0,
    HLE_JR_NC,
    HLE_JR_Z,
    HLE_JR_C
};

enum
{
    HLE_NO_WARP = 0,
    HLE_WARPED,
    HLE_MISS
};

/* __gb_hle_analyze result: whether the pc decoded to a poll-loop shape. */
enum
{
    HLE_ANALYZE_SKIP = 0,  // no recognized load+compare (spam): don't cache/log
    HLE_ANALYZE_NO,        // load+compare recognized, but jr is not a loop-back
    HLE_ANALYZE_WARP       // full poll-loop shape, rewind set
};

typedef struct
{
    uint16_t pc;    // post-load pc (key); 0 = empty
    uint16_t bank;  // bank key (__gb_hle_bank_key)
    uint8_t addr;   // ioaddr & 0xFF
    int8_t rewind;  // 0 = verdict NO; else -1..-5 (pc offset to loop head)
    uint8_t cmp_op;
    uint8_t cmp_arg;  // d8 operand, bit index, mask, or register selector
    uint8_t jr_pol;
    uint8_t cmp_arg2;  // target byte (HLE_CMP_AND_CP / HLE_CMP_AND_XOR)
} hle_slot_t;

/* Precomputed WARP verdicts, extended at runtime by __gb_hle_miss. */
#define PGB_HLE_TABLE_SIZE 64
#define PGB_HLE_TABLE_MASK (PGB_HLE_TABLE_SIZE - 1)

static hle_slot_t pgb_hle_table[PGB_HLE_TABLE_SIZE];

/* Simulator-only diagnostics for cache validation: always on in sim
 * builds (host cost is noise), nothing emitted on device. Periodic
 * counter dump plus per-pc fail sites via logToConsole. */
#ifdef TARGET_SIMULATOR
enum
{
    HLE_STAT_PROBE = 0,  // probe calls (hle_enabled only)
    HLE_STAT_WARP,       // hits that applied a warp
    HLE_STAT_HIT_NO,     // hits on verdict NO
    HLE_STAT_HIT_MET,    // warpable hit, wait condition already met
    HLE_STAT_MISS,       // probe misses
    HLE_STAT_ANALYZE,    // actual decodes (miss handler entries)
    HLE_STAT_FAIL,       // decodes yielding verdict NO
    HLE_STAT_SKIP,       // decodes yielding verdict SKIP (unrecognized shape)
    HLE_STAT_EVICT,      // store over an occupied different-pc slot
    HLE_STAT_COUNT
};
static uint32_t pgb_hle_stats[HLE_STAT_COUNT];
#define PGB_HLE_STAT_INC(i) (++pgb_hle_stats[HLE_STAT_##i])
__shell static void pgb_hle_stats_dump(void);

#define PGB_HLE_FAIL_LOG_LIMIT 32
static int pgb_hle_fail_logged;
#define PGB_HLE_SKIP_LOG_LIMIT 64
static int pgb_hle_skip_logged;
#else
#define PGB_HLE_STAT_INC(i) ((void)0)
#endif

/* Live register read for cp/and/or/xor/sub a,r compare forms. The register is
 * loop-invariant but re-read each wait (mirrors cp a,r) so the verdict stays
 * correct even if the loop reloads it between warp applications. */
static inline __attribute__((always_inline)) uint8_t __gb_hle_get_reg(gb_s* gb, const uint8_t sel)
{
    switch (sel)
    {
    case 0:
        return gb->cpu_reg.b;
    case 1:
        return gb->cpu_reg.c;
    case 2:
        return gb->cpu_reg.d;
    case 3:
        return gb->cpu_reg.e;
    case 4:
        return gb->cpu_reg.h;
    case 5:
        return gb->cpu_reg.l;
    default:
        return gb->cpu_reg.a;
    }
}

/* Evaluate the cached compare+branch: true while the loop keeps waiting. */
static inline __attribute__((always_inline)) bool __gb_hle_waiting(
    gb_s* gb, const hle_slot_t* s, const uint8_t v
)
{
    int z, c;
    switch (s->cmp_op)
    {
    case HLE_CMP_AND_A:
        z = (v == 0);
        c = 0;
        break;
    case HLE_CMP_CP_D8:
    case HLE_CMP_SUB_D8:
        z = (v == s->cmp_arg);
        c = (v < s->cmp_arg);
        break;
    case HLE_CMP_AND_D8:
        z = !(v & s->cmp_arg);
        c = 0;
        break;
    case HLE_CMP_AND_CP:
    {
        uint8_t m = v & s->cmp_arg;
        z = (m == s->cmp_arg2);
        c = (m < s->cmp_arg2);
        break;
    }
    case HLE_CMP_AND_XOR:
        z = ((v & s->cmp_arg) == s->cmp_arg2);
        c = 0;
        break;
    case HLE_CMP_CP_R:
    case HLE_CMP_SUB_R:
    {
        // cp/sub a,r against the live register ((hl) operand excluded at decode)
        const uint8_t r = __gb_hle_get_reg(gb, s->cmp_arg);
        z = (v == r);
        c = (v < r);
        break;
    }
    case HLE_CMP_AND_R:
        z = !(v & __gb_hle_get_reg(gb, s->cmp_arg));
        c = 0;
        break;
    case HLE_CMP_OR_R:
        z = !(v | __gb_hle_get_reg(gb, s->cmp_arg));
        c = 0;
        break;
    case HLE_CMP_XOR_R:
        z = !(v ^ __gb_hle_get_reg(gb, s->cmp_arg));
        c = 0;
        break;
    case HLE_CMP_AND_DEC_A:
        z = ((v & s->cmp_arg) == 1);
        c = 0;
        break;
    case HLE_CMP_SUB_CP:
    {
        const uint8_t m = v - s->cmp_arg;
        z = (m == s->cmp_arg2);
        c = (m < s->cmp_arg2);
        break;
    }
    case HLE_CMP_CPL_AND:
        // (~v) & arg == 0  <=>  all mask bits set in v
        z = ((v & s->cmp_arg) == s->cmp_arg);
        c = 0;
        break;
    case HLE_CMP_ADD_A:
        // shift-left: z on 0x00/0x80, carry = old bit 7
        z = (((v << 1) & 0xFF) == 0);
        c = (v & 0x80) != 0;
        break;
    case HLE_CMP_SUB_R_CP:
    {
        const uint8_t m = v - __gb_hle_get_reg(gb, s->cmp_arg);
        z = (m == s->cmp_arg2);
        c = (m < s->cmp_arg2);
        break;
    }
    default:  // BIT: carry unaffected -> the branch tests the live (loop-invariant) carry
        z = !(v & (1 << (s->cmp_arg & 7)));
        c = gb->cpu_reg.f_bits.c;
        break;
    }
    switch (s->jr_pol)
    {
    case HLE_JR_NZ:
        return !z;
    case HLE_JR_Z:
        return z == 1;
    case HLE_JR_NC:
        return c != 1;  // all compare forms produce an exact c (0/1)
    default:            // HLE_JR_C
        return c != 0;
    }
}

static inline __attribute__((always_inline)) void __gb_hle_apply_warp(gb_s* gb, const hle_slot_t* s)
{
    gb->gb_hle = true;
    gb->cpu_reg.pc += s->rewind;
    gb->hle_ioaddr = s->addr;
}

/* Bank identity of the code at pc. 0000-3FFF maps to the zero bank (fixed,
 * except MBC1 mode-select), 4000-7FFF to the selected bank. Keying bank-0
 * code by selected_rom_bank would invalidate its cache slot on every bank
 * switch. */
static inline __attribute__((always_inline)) uint16_t
__gb_hle_bank_key(const gb_s* gb, const uint16_t pc)
{
    return (pc < 0x4000) ? (uint16_t)(gb->zero_bank_base >> 14)
                         : (uint16_t)(gb->selected_rom_bank & gb->num_rom_banks_mask);
}

/* Hash (bank, pc, addr) so poll sites spread across slots. */
static inline __attribute__((always_inline)) uint32_t
__gb_hle_table_index(const uint16_t bank, const uint16_t pc, const uint8_t addr)
{
    uint32_t x = (uint32_t)(pc >> 1) ^ ((uint32_t)bank << 8) ^ ((uint32_t)addr << 16);
    x *= 0x9E3779B9u;
    x ^= x >> 16;
    return x & PGB_HLE_TABLE_MASK;
}

/* Return the matching precomputed slot, or NULL. */
static inline __attribute__((always_inline)) hle_slot_t* __gb_hle_static_probe(
    gb_s* gb, const uint16_t ioaddr
)
{
    const uint16_t pc = gb->cpu_reg.pc;
    const uint16_t bank = __gb_hle_bank_key(gb, pc);
    hle_slot_t* s = &pgb_hle_table[__gb_hle_table_index(bank, pc, (uint8_t)ioaddr)];
    if (s->pc == pc && s->bank == bank && s->addr == (uint8_t)ioaddr)
        return s;
    return NULL;
}

/* Shared HLE-gated read used by the fast and reference read paths. */
__hle_cgb static uint8_t __gb_hle_read_shared(gb_s* gb, const uint16_t addr, const uint8_t v);

__hle_cgb static uint8_t __gb_hle_miss(gb_s* gb, const uint_fast16_t ioaddr, u8 ioval);

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
            if (gb->is_cgb_mode)
                return gb->cgb_bg_palette_index | 0x40;
            return 0xFF;
        case 0x69:  // BCPD (CGB BG Palette Data)
            if (gb->is_cgb_mode)
            {
                if (__gb_ppu_mode_for_lock(gb) != LCD_TRANSFER)
                    return gb->cgb_bg_palette[gb->cgb_bg_palette_index & 0x3F];
                return 0xFF;
            }
            return 0xFF;
        case 0x6A:  // OCPS (CGB OBJ Palette Spec)
            if (gb->is_cgb_mode)
                return gb->cgb_obj_palette_index | 0x40;
            return 0xFF;
        case 0x6B:  // OCPD (CGB OBJ Palette Data)
            if (gb->is_cgb_mode)
            {
                if (__gb_ppu_mode_for_lock(gb) != LCD_TRANSFER)
                    return gb->cgb_obj_palette[gb->cgb_obj_palette_index & 0x3F];
                return 0xFF;
            }
            return 0xFF;
        case 0x76:  // PCM12 (CGB Audio)
        case 0x77:  // PCM34 (CGB Audio)
            return 0xFF;

            // CGB registers

        case 0x4C:        // KEY0 (CGB Undocumented)
            return 0xFF;  // TODO: (?) differs in CGB-compat mode; low priority

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
                const uint8_t v =
                    ((uint8_t)gb->cgb_hdma_len & 0x7F) | ((gb->cgb_hdma_active) ? 0 : 0x80);
                return __gb_hle_read_shared(gb, addr, v);
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
                return (~7) | gb->cgb_wram_bank;
            }
            else
                return 0xFF;

        // Undocumented CGB registers
        case 0x72:
        case 0x73:
        case 0x74:
            if (gb->is_cgb_mode)
            {
                // TODO: the cgb can access '72 and '73 even when in DMG mode.
                // Low priority - obscure quirk, game impact unknown.
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

        case 0x5A ... 0x5F:
            if (!gb->direct.has_read_accelerometer_this_frame)
            {
                float a[3];
                playdate->system->getAccelerometer(a, a + 1, a + 2);

                for (int i = 0; i < 3; ++i)
                {
                    float f = a[i];
                    if (f >= 4)
                        f = 4;
                    if (f < -4)
                        f = -4;
                    int32_t v = 0x8000 + 0x2000 * f;
                    if (v < 0)
                        v = 0;
                    if (v >= 0xFFFF)
                        v = 0xFFFF;
                    gb->direct.peripherals[i + 1] = (uint16_t)v;
                }

                gb->direct.has_read_accelerometer_this_frame = true;
            }

            // fallthrough

        case 0x58:
        case 0x59:
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

// attempt to detect an optimizable routine
// (e.g. tight-loop polling an io register)
/* Decode the load/cmp/jr poll-loop shape around pc into a verdict slot.
 * Pure analysis: no gb side effects. rewind stays 0 (verdict NO) unless
 * the full shape matches. Returns HLE_ANALYZE_SKIP/NO/WARP. */
__hle_cgb static int __gb_hle_analyze(gb_s* gb, const uint_fast16_t ioaddr, hle_slot_t* s)
{
    s->pc = gb->cpu_reg.pc;
    s->bank = __gb_hle_bank_key(gb, s->pc);
    s->addr = ioaddr & 0xFF;
    s->rewind = 0;
    s->cmp_op = HLE_CMP_AND_A;
    s->cmp_arg = 0;
    s->cmp_arg2 = 0;
    s->jr_pol = HLE_JR_NZ;

    // pc of instruction following compare
    const u16 pc = gb->cpu_reg.pc;

    // shouldn't go over ROM -- don't want to trigger side effects on read
    // (pc < 5: ei/di lookback reaches pc-5 for the ld a,(nn) load form)
    if (pc >= 0x7FF8 || pc < 5)
    {
        // executing from wram is okay though
        if (pc < 0xC003 || pc >= 0xEFF0)
        {
            return HLE_ANALYZE_SKIP;
        }
    }

#define READ8(addr) (gb->ram_base[(addr) >> 12][addr])

    int offset = 0;
    if (READ8(pc - 2) == 0xF0 && READ8(pc - 1) == (ioaddr & 0xFF))
    {
        // ld A, (a8)
        offset = -2;
    }
    else if (READ8(pc - 3) == 0xFA && ((READ8(pc - 1) << 8) | READ8(pc - 2)) == ioaddr)
    {
        // ld A, (a16)
        offset = -3;
    }
    else if (READ8(pc - 1) == 0xF2 && gb->cpu_reg.c == (ioaddr & 0xFF))
    {
        // ld A, (C)
        offset = -1;
    }
    else if (READ8(pc - 1) == 0x7E && gb->cpu_reg.hl == ioaddr)
    {
        // ld A, (HL)
        offset = -1;
    }
    else if (READ8(pc - 1) == 0x2A && gb->cpu_reg.hl == ioaddr)
    {
        // ld A, (HL+)
        offset = -1;
    }
    else if (READ8(pc - 1) == 0x3A && gb->cpu_reg.hl == ioaddr)
    {
        // ld A, (HL-)
        offset = -1;
    }
    else
    {
        // fall through to BIT n,(HL) check
    }

    u16 addr_next;
    if (offset == 0)
    {
        // BIT n,(HL) - combined read+compare in one instruction
        if (READ8(pc - 2) == 0xCB && (READ8(pc - 1) & 0xC7) == 0x46)
        {
            offset = -2;
            s->cmp_op = HLE_CMP_BIT_HL;
            s->cmp_arg = (READ8(pc - 1) >> 3) & 7;
            addr_next = pc;
            goto analyze_jr;
        }
        goto analyze_skip;
    }

    {
        u8 op0 = READ8(pc);
        u8 d8 = READ8(pc + 1);
        addr_next = pc + 2;
        if (op0 == 0xE6 &&
            (READ8(pc + 2) == 0xFE || READ8(pc + 2) == 0xD6 || READ8(pc + 2) == 0xEE))
        {
            // and d8 + cp/sub/xor d8: masked-mode waits,
            // "ldh a,(STAT); and 3; cp 1; jr nz"
            s->cmp_op = (READ8(pc + 2) == 0xEE) ? HLE_CMP_AND_XOR : HLE_CMP_AND_CP;
            s->cmp_arg = d8;              // mask
            s->cmp_arg2 = READ8(pc + 3);  // target
            addr_next = pc + 4;
        }
        else if (op0 >= 0xB8 && op0 <= 0xBD)
        {
            // cp a,r: register-value waits, "ldh a,(LY); cp b; jr c".
            // (0xBE = cp a,(hl) excluded: extra memory read per iteration;
            //  0xBF = cp a,a excluded: fixed flags, verdict can't track v)
            s->cmp_op = HLE_CMP_CP_R;
            s->cmp_arg = op0 - 0xB8;
            addr_next = pc + 1;
        }
        else if (op0 >= 0xA0 && op0 <= 0xA5)
        {
            // and a,r: "ldh a,(IF); and b; jr nz" (r = b..l; (hl) excluded)
            s->cmp_op = HLE_CMP_AND_R;
            s->cmp_arg = op0 - 0xA0;
            addr_next = pc + 1;
        }
        else if (op0 >= 0xB0 && op0 <= 0xB5)
        {
            // or a,r
            s->cmp_op = HLE_CMP_OR_R;
            s->cmp_arg = op0 - 0xB0;
            addr_next = pc + 1;
        }
        else if (op0 >= 0xA8 && op0 <= 0xAD)
        {
            // xor a,r
            s->cmp_op = HLE_CMP_XOR_R;
            s->cmp_arg = op0 - 0xA8;
            addr_next = pc + 1;
        }
        else if (op0 >= 0x90 && op0 <= 0x95 && READ8(pc + 1) == 0xFE)
        {
            // sub a,r + cp d8: m = v - r; z = (m == d8), c = (m < d8).
            // Must precede the plain sub a,r arm (same opcode range).
            s->cmp_op = HLE_CMP_SUB_R_CP;
            s->cmp_arg = op0 - 0x90;
            s->cmp_arg2 = READ8(pc + 2);
            addr_next = pc + 3;
        }
        else if (op0 >= 0x90 && op0 <= 0x95)
        {
            // sub a,r
            s->cmp_op = HLE_CMP_SUB_R;
            s->cmp_arg = op0 - 0x90;
            addr_next = pc + 1;
        }
        else if (op0 == 0x3D && READ8(pc + 1) == 0xFE)
        {
            // dec a + cp d8: cp clobbers dec's flags -> same shape as
            // sub d8 + cp d8 with an implicit arg of 1
            s->cmp_op = HLE_CMP_SUB_CP;
            s->cmp_arg = 1;
            s->cmp_arg2 = READ8(pc + 2);
            addr_next = pc + 3;
        }
        else if (op0 == 0x2F && READ8(pc + 1) == 0xE6)
        {
            // cpl + and d8: "wait until all mask bits set" without a cp
            s->cmp_op = HLE_CMP_CPL_AND;
            s->cmp_arg = READ8(pc + 2);
            addr_next = pc + 3;
        }
        else if (op0 == 0x87)
        {
            // add a,a: shift-left idiom; with jr c/nc a bit-7 test
            s->cmp_op = HLE_CMP_ADD_A;
            addr_next = pc + 1;
        }
        else if (op0 == 0xA7 || op0 == 0xB7)
        {
            // AND A / OR A: Z = (A == 0), C = 0. Single-byte instruction;
            // the classic "ldh a,(x); and a; jr z" flag-wait idiom.
            s->cmp_op = HLE_CMP_AND_A;
            s->cmp_arg = 0;
            addr_next = pc + 1;
        }
        else if (op0 == 0xFE)
        {
            // cp d8
            s->cmp_op = HLE_CMP_CP_D8;
            s->cmp_arg = d8;
        }
        else if (op0 == 0xD6 && READ8(pc + 2) == 0xFE)
        {
            // sub d8 + cp d8: z = ((v-d8) == d8'), c = ((v-d8) < d8')
            s->cmp_op = HLE_CMP_SUB_CP;
            s->cmp_arg = d8;
            s->cmp_arg2 = READ8(pc + 3);
            addr_next = pc + 4;
        }
        else if (op0 == 0xD6)
        {
            // sub d8
            s->cmp_op = HLE_CMP_SUB_D8;
            s->cmp_arg = d8;
        }
        else if (op0 == 0xE6 && READ8(pc + 2) == 0xB7)
        {
            // and d8 + or a: or a re-tests A -> flags identical to and d8
            s->cmp_op = HLE_CMP_AND_D8;
            s->cmp_arg = d8;
            addr_next = pc + 3;
        }
        else if (op0 == 0xE6 && READ8(pc + 2) == 0x3D)
        {
            // and d8 + dec a: z = ((v & d8) == 1), c = 0
            s->cmp_op = HLE_CMP_AND_DEC_A;
            s->cmp_arg = d8;
            addr_next = pc + 3;
        }
        else if (op0 == 0xE6)
        {
            // and d8
            s->cmp_op = HLE_CMP_AND_D8;
            s->cmp_arg = d8;
        }
        else if (op0 == 0xCB)
        {
            u8 bitop = READ8(pc + 1);
            // BIT n,A: opcodes 0x47+8n, mask 01bbb111
            if ((bitop & 0xC7) != 0x47)
                goto analyze_skip;
            s->cmp_op = HLE_CMP_BIT_A;
            s->cmp_arg = (bitop >> 3) & 7;
        }
        else
        {
            goto analyze_skip;
        }
    }

analyze_jr:;
    // branch destination should be the read-io opcode (jr cc or jp cc)
    {
        const u8 br_op = READ8(addr_next);
        uint8_t pol;
        bool is_jp;
        switch (br_op)
        {
        case 0x20:
            pol = HLE_JR_NZ;
            is_jp = false;
            break;
        case 0x30:
            pol = HLE_JR_NC;
            is_jp = false;
            break;
        case 0x28:
            pol = HLE_JR_Z;
            is_jp = false;
            break;
        case 0x38:
            pol = HLE_JR_C;
            is_jp = false;
            break;
        case 0xC2:
            pol = HLE_JR_NZ;
            is_jp = true;
            break;
        case 0xD2:
            pol = HLE_JR_NC;
            is_jp = true;
            break;
        case 0xCA:
            pol = HLE_JR_Z;
            is_jp = true;
            break;
        case 0xDA:
            pol = HLE_JR_C;
            is_jp = true;
            break;
        default:
            goto analyze_no;
        }

        // Loop head is normally the load instruction itself. Some routines
        // wrap the poll in an "ei; di" pair so interrupts stay serviced each
        // iteration ("ei; di; ldh a,(STAT); and 2; jr nz"): also accept the
        // branch landing 2 bytes before the load. ROM-only -- the lookback
        // is safe there and WRAM polls never use the prefix.
        const u16 landing = is_jp ? (READ8(addr_next + 1) | (READ8(addr_next + 2) << 8))
                                  : (u16)(addr_next + 2 + (int8_t)READ8(addr_next + 1));

        int16_t head = (int16_t)(pc + offset);
        if (landing != (u16)head)
        {
            if (pc < 0x8000 && landing == (u16)(head - 2) && READ8(head - 2) == 0xFB &&
                READ8(head - 1) == 0xF3)
                head -= 2;
            else
                goto analyze_no;
        }

        s->jr_pol = pol;
        s->rewind = (int8_t)(head - pc);
    }
#undef READ8
    return HLE_ANALYZE_WARP;

analyze_no:
#undef READ8
    return HLE_ANALYZE_NO;

analyze_skip:
#undef READ8
    return HLE_ANALYZE_SKIP;
}

/* Build pgb_hle_table: run __gb_hle_analyze over every bank's ldh/ld-abs poll
 * sites (position-independent forms only; (hl)/(c)/WRAM use the fallback). */
void __gb_hle_scan_rom(gb_s* gb)
{
    memset(pgb_hle_table, 0, sizeof(pgb_hle_table));

    int (*analyze)(gb_s*, uint_fast16_t, hle_slot_t*) =
        (int (*)(gb_s*, uint_fast16_t, hle_slot_t*))(
            (char*)(void*)__gb_hle_analyze + pgb_hle_reloc_offset
        );

    /* Save context; the scan repoints ram_base per bank. */
    uint8_t* saved_zero[4];
    uint8_t* saved_sel[4];
    for (int i = 0; i < 4; i++)
    {
        saved_zero[i] = gb->rom_bank_base[0][i];
        saved_sel[i] = gb->rom_bank_base[1][i];
    }
    const uint32_t saved_zero_bank = gb->zero_bank_base;
    const uint16_t saved_sel_bank = gb->selected_rom_bank;
    const uint16_t saved_pc = gb->cpu_reg.pc;
    const uint16_t saved_hl = gb->cpu_reg.hl;
    const uint8_t saved_c = gb->cpu_reg.c;

    int num_banks = (int)(gb->num_rom_banks_mask + 1);
    const int max_banks = (int)((gb->gb_rom_size + ROM_BANK_SIZE - 1) / ROM_BANK_SIZE);
    if (num_banks > max_banks)
        num_banks = max_banks;

    for (int b = 0; b < num_banks; b++)
    {
        /* Map 0000-7FFF onto physical bank b. */
        const uint8_t* base = gb->gb_rom + (uint32_t)b * ROM_BANK_SIZE;
        for (int i = 0; i < 4; i++)
        {
            gb->rom_bank_base[0][i] = (uint8_t*)base;
            gb->rom_bank_base[1][i] = (uint8_t*)(base - ROM_BANK_SIZE);
        }
        gb->zero_bank_base = (uint32_t)b * ROM_BANK_SIZE;
        gb->selected_rom_bank = (uint16_t)b;

        /* Keep post-load pc < 0x7FF8 and operand reads in-bank. */
        for (uint16_t pc = 0; pc < 0x7FF0; pc++)
        {
            const uint8_t op = gb->ram_base[pc >> 12][pc];
            uint16_t ioaddr;
            uint16_t post;
            if (op == 0xF0)
            {
                ioaddr = 0xFF00 | gb->ram_base[(pc + 1) >> 12][pc + 1];
                post = pc + 2;
            }
            else if (op == 0xFA)
            {
                ioaddr = (uint16_t)gb->ram_base[(pc + 1) >> 12][pc + 1] |
                         ((uint16_t)gb->ram_base[(pc + 2) >> 12][pc + 2] << 8);
                post = pc + 3;
            }
            else
            {
                continue;
            }

            /* STAT/LY/IF, HDMA5, or HRAM only. */
            if (ioaddr != 0xFF41 && ioaddr != 0xFF44 && ioaddr != 0xFF0F && ioaddr != 0xFF55 &&
                ioaddr < 0xFF80)
                continue;

            if (post < 5)
                continue;

            gb->cpu_reg.pc = post;
            gb->cpu_reg.hl = 0;
            gb->cpu_reg.c = 0;

            hle_slot_t slot;
            if (analyze(gb, ioaddr, &slot) != HLE_ANALYZE_WARP)
                continue;

            hle_slot_t* s = &pgb_hle_table[__gb_hle_table_index(slot.bank, slot.pc, slot.addr)];
            *s = slot;
        }
    }

    /* Restore context. */
    for (int i = 0; i < 4; i++)
    {
        gb->rom_bank_base[0][i] = saved_zero[i];
        gb->rom_bank_base[1][i] = saved_sel[i];
    }
    gb->zero_bank_base = saved_zero_bank;
    gb->selected_rom_bank = saved_sel_bank;
    gb->cpu_reg.pc = saved_pc;
    gb->cpu_reg.hl = saved_hl;
    gb->cpu_reg.c = saved_c;
}

__hle_cgb static uint8_t __gb_hle_miss(gb_s* gb, const uint_fast16_t ioaddr, const u8 ioval)
{
    PGB_HLE_STAT_INC(ANALYZE);

    hle_slot_t v;
    const int result = __gb_hle_analyze(gb, ioaddr, &v);

    /* Spam (no recognized load+compare shape): run normally, but never
     * cache or log -- one-off reads would otherwise evict warpable slots
     * and flood the fail log. */
    if (result == HLE_ANALYZE_SKIP)
    {
        PGB_HLE_STAT_INC(SKIP);
#ifdef TARGET_SIMULATOR
        /* Skip sites are the "missed pattern" ground truth: log the compare
         * opcode (post-load pc) for a capped sample so unsupported shapes can
         * be identified without a static ROM pass. */
        if (pgb_hle_skip_logged < PGB_HLE_SKIP_LOG_LIMIT)
        {
            const uint16_t spc = gb->cpu_reg.pc;
            // ram_base can be NULL outside ROM/WRAM (e.g. VRAM/cart RAM) --
            // unreachable in practice (pc is a code address) but guard anyway.
            uint8_t op = 0;
            uint8_t prev = 0;
            if (gb->ram_base[spc >> 12])
            {
                op = gb->ram_base[spc >> 12][spc];
                if (spc > 0 && gb->ram_base[(spc - 1) >> 12])
                    prev = gb->ram_base[(spc - 1) >> 12][spc - 1];
            }
            ++pgb_hle_skip_logged;
            playdate->system->logToConsole(
                "HLE Skip %x:@%04x (%04x) op=%02x prev=%02x", __gb_hle_bank_key(gb, spc), spc,
                ioaddr, op, prev
            );
            if (pgb_hle_skip_logged == PGB_HLE_SKIP_LOG_LIMIT)
                playdate->system->logToConsole(
                    "HLE Skip log limit (%d) reached, further sites suppressed",
                    PGB_HLE_SKIP_LOG_LIMIT
                );
        }
#endif
        return ioval;
    }

    /* Cache WARP verdicts so (hl)/(c)/bit loops stop re-analyzing; never cache NO. */
    if (v.pc < 0x8000 && v.rewind != 0)
    {
        hle_slot_t* s = &pgb_hle_table[__gb_hle_table_index(v.bank, v.pc, v.addr)];
#ifdef TARGET_SIMULATOR
        if (s->pc != 0 && s->pc != v.pc)
            ++pgb_hle_stats[HLE_STAT_EVICT];
#endif
        *s = v;
    }

    if (result == HLE_ANALYZE_NO)
    {
        PGB_HLE_STAT_INC(FAIL);
#ifdef TARGET_SIMULATOR
        /* Once per unique pc, but a game can have thousands of failing
         * sites -- cap the I/O burst. The fail counter keeps the
         * total (see periodic stats dump). Reset with the cache. */
        if (pgb_hle_fail_logged < PGB_HLE_FAIL_LOG_LIMIT)
        {
            ++pgb_hle_fail_logged;
            playdate->system->logToConsole(
                "HLE Fail %x:@%04x (%04x)", __gb_hle_bank_key(gb, v.pc), v.pc, ioaddr
            );
            if (pgb_hle_fail_logged == PGB_HLE_FAIL_LOG_LIMIT)
                playdate->system->logToConsole(
                    "HLE Fail log limit (%d) reached, further sites suppressed",
                    PGB_HLE_FAIL_LOG_LIMIT
                );
        }
#endif
        return ioval;
    }

    if (__gb_hle_waiting(gb, &v, ioval))
        __gb_hle_apply_warp(gb, &v);

    return ioval;
}

__hle_cgb static uint8_t __gb_hle_read_shared(gb_s* gb, const uint16_t addr, const uint8_t v)
{
    if (!gb->hle_enabled)
        return v;

    const uint16_t pc = gb->cpu_reg.pc;

    if (pc < 0x8000)
    {
        PGB_HLE_STAT_INC(PROBE);
#ifdef TARGET_SIMULATOR
        if ((pgb_hle_stats[HLE_STAT_PROBE] & 0xFFFFF) == 0)
            pgb_hle_stats_dump();
#endif

        hle_slot_t* s = __gb_hle_static_probe(gb, addr);
        if (s)
        {
            if (__gb_hle_waiting(gb, s, v))
            {
                __gb_hle_apply_warp(gb, s);
                PGB_HLE_STAT_INC(WARP);
            }
            else
            {
                PGB_HLE_STAT_INC(HIT_MET);
            }
            return v;
        }
        PGB_HLE_STAT_INC(MISS);

        /* HRAM: only ldh/ld-abs waits are HLE'd (now via the table); (hl)/(c)/bit
         * HRAM reads are routine access. */
        if (addr >= 0xFF80)
            return v;

        /* ldh/ld-abs not in the table: scan proved it isn't a loop. */
        if (pc >= 3)
        {
            if (gb->ram_base[(pc - 2) >> 12][pc - 2] == 0xF0 ||
                gb->ram_base[(pc - 3) >> 12][pc - 3] == 0xFA)
                return v;
        }

        /* Only (hl)/(c)/bit load forms can be poll loops; skip the analyzer otherwise. */
        if (pc >= 2)
        {
            const uint8_t b1 = gb->ram_base[(pc - 1) >> 12][pc - 1];
            if (b1 == 0xF2 || b1 == 0x7E || b1 == 0x2A || b1 == 0x3A)
                return __gb_hle_miss(gb, addr, v);
            if (pc >= 3 && gb->ram_base[(pc - 2) >> 12][pc - 2] == 0xCB && (b1 & 0xC7) == 0x46)
                return __gb_hle_miss(gb, addr, v);
        }
        return v;
    }

    /* WRAM-resident poll loops only; HRAM/VRAM/OAM-executed code is not HLE. */
    if (pc >= 0xC003 && pc < 0xEFF0)
        return __gb_hle_miss(gb, addr, v);

    return v;
}

#ifdef TARGET_SIMULATOR
__shell static void pgb_hle_stats_dump(void)
{
    playdate->system->logToConsole(
        "HLE stats: probes=%u warp=%u hit_no=%u hit_met=%u miss=%u analyze=%u fail=%u "
        "skip=%u evict=%u",
        (unsigned)pgb_hle_stats[HLE_STAT_PROBE], (unsigned)pgb_hle_stats[HLE_STAT_WARP],
        (unsigned)pgb_hle_stats[HLE_STAT_HIT_NO], (unsigned)pgb_hle_stats[HLE_STAT_HIT_MET],
        (unsigned)pgb_hle_stats[HLE_STAT_MISS], (unsigned)pgb_hle_stats[HLE_STAT_ANALYZE],
        (unsigned)pgb_hle_stats[HLE_STAT_FAIL], (unsigned)pgb_hle_stats[HLE_STAT_SKIP],
        (unsigned)pgb_hle_stats[HLE_STAT_EVICT]
    );
}
#endif

/* Cycles executed in the current CPU batch but not yet applied to the
 * PPU counters. Transient; set by the batch loop, zeroed when it ends. */
static uint16_t pgb_batch_elapsed;

/* CPU-T cycle of the current instruction's write bus end (same domain as
 * pgb_batch_elapsed). Write handlers set it; APU write timestamps use it. */
static uint16_t pgb_write_cycle;

/**
 * Cycles remaining until next PPU mode boundary.
 */
static inline __attribute__((always_inline)) uint16_t
__gb_ppu_cycles_remaining(gb_s* gb, int32_t slack)
{
    int32_t remaining;
    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
        remaining = LCD_FRAME_CYCLES - gb->counter.lcd_off_count;
    else
    {
        switch (gb->lcd_mode)
        {
        case LCD_SEARCH_OAM:
            remaining = PPU_MODE_2_OAM_CYCLES - gb->counter.lcd_count;
            break;
        case LCD_TRANSFER:
            remaining = gb->display.current_mode3_cycles - gb->counter.lcd_count;
            break;
        case LCD_HBLANK:
            remaining = gb->display.current_mode0_cycles - gb->counter.lcd_count;
            break;
        case LCD_VBLANK:
            remaining = LCD_LINE_CYCLES - gb->counter.lcd_count;
            break;
        default:
            remaining = 1;
            break;
        }
    }

    // Discount cycles already executed in the current batch (same shifts
    // inst_cycles will receive in the timing block).
    uint16_t lag = pgb_batch_elapsed;
    if (gb->lcd_mode == LCD_VBLANK)
        lag >>= gb->overclock;
    lag >>= gb->cgb_fast_mode_active;
    remaining -= lag;
    remaining += slack;
    return (remaining > 0) ? remaining : 0;
}

/**
 * Shared: next PPU mode after current. Pure, mirrors core PPU state machine.
 */
static inline __attribute__((always_inline)) uint8_t __gb_ppu_next_mode(gb_s* gb)
{
    switch (gb->lcd_mode)
    {
    case LCD_SEARCH_OAM:
        return LCD_TRANSFER;
    case LCD_TRANSFER:
        return LCD_HBLANK;
    case LCD_HBLANK:
        return (gb->gb_reg.LY + 1 == LCD_HEIGHT) ? LCD_VBLANK : LCD_SEARCH_OAM;
    case LCD_VBLANK:
        return (gb->gb_reg.LY == 0) ? LCD_SEARCH_OAM : LCD_VBLANK;
    default:
        return LCD_HBLANK;
    }
}

/**
 * Effective PPU mode for VRAM/OAM/palette access locks.
 */
__attribute__((always_inline)) static inline uint8_t __gb_ppu_mode_for_lock(gb_s* gb)
{
    /* LCD off: PPU stopped, VRAM/OAM/palettes fully accessible. Return the
     * parked mode (LCD_HBLANK = no lock); without this guard the projected
     * frame tick (remaining == 0) reports SEARCH_OAM and spuriously blocks
     * OAM for the tail of any batch that crosses lcd_off_count's wrap. */
    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
        return LCD_HBLANK;
    uint16_t remaining = __gb_ppu_cycles_remaining(gb, 0);
    if (remaining == 0)
        return __gb_ppu_next_mode(gb);  // exact engage/release
    return gb->lcd_mode;
}

/**
 * PPU read synchronization: project STAT/LY to the current batch position so
 * polling loops see the transition as the batch crosses the boundary, rather
 * than reading a stale (batch-start) value and never observing the change.
 */
static inline __attribute__((always_inline)) uint8_t __gb_read_stat_synced(gb_s* gb)
{
    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
        return gb->gb_reg.STAT | 0x80;

    uint16_t remaining = __gb_ppu_cycles_remaining(gb, STAT_READ_LAG_T);

    if (remaining == 0)
    {
        uint8_t new_stat = (gb->gb_reg.STAT & ~STAT_MODE) | __gb_ppu_next_mode(gb);
        return new_stat | 0x80;
    }

    return gb->gb_reg.STAT | 0x80;
}

static inline __attribute__((always_inline)) uint8_t __gb_read_ly_synced(gb_s* gb)
{
    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
        return gb->gb_reg.LY;

    switch (gb->lcd_mode)
    {
    case LCD_HBLANK:
    {
        /* LY increments at end of HBlank */
        uint16_t remaining = __gb_ppu_cycles_remaining(gb, 0);
        if (remaining == 0)
        {
            uint8_t next_ly = gb->gb_reg.LY + 1;
            return (next_ly >= 154) ? 0 : next_ly;
        }
        break;
    }
    case LCD_VBLANK:
    {
        /* Short Line 153: LY wraps to 0 ~4 cycles into the line */
        if (gb->gb_reg.LY == 153 && gb->counter.lcd_count >= 4)
            return 0;

        /* Post-wrap LY stays 0 across VBlank end (line-0 mode 2 keeps LY=0). */
        if (gb->gb_reg.LY == 0)
            return 0;

        /* During VBlank, LY increments at 456-cycle boundaries */
        uint16_t remaining = __gb_ppu_cycles_remaining(gb, 0);
        if (remaining == 0)
        {
            uint8_t next_ly = gb->gb_reg.LY + 1;
            if (next_ly >= 154)
                next_ly = 0;
            return next_ly;
        }
        break;
    }
    }

    return gb->gb_reg.LY;
}

/**
 * Timer read synchronization: project TIMA/IF.TIMER at the current batch
 * offset, so polling loops see the timer as of this read, not the last
 * batch boundary (batch no longer clamps to overflow when IE.TIMER is
 * clear). Non-mutating; the step tail stays authoritative. Same-batch
 * write-then-read skews <1 tick.
 */
static inline __attribute__((always_inline)) bool __gb_timer_peek(gb_s* gb, uint8_t* tima_out)
{
    bool ofl = gb->gb_reg.tima_overflow_delay;
    *tima_out = gb->gb_reg.TIMA;

    if (ofl || !gb->gb_reg.tac_enable)
        return ofl;

    /* Same shifts the step tail applies to inst_cycles (VBlank overclock,
     * then CGB fast mode). */
    uint16_t elapsed = pgb_batch_elapsed;
    if (gb->lcd_mode == LCD_VBLANK)
        elapsed >>= gb->overclock;
    elapsed >>= gb->cgb_fast_mode_active;
    if (elapsed == 0)
        return false;

    uint16_t threshold = gb->gb_reg.tac_cycles >> gb->cgb_fast_mode_active;
    if (threshold == 0)
        threshold = 1;

    uint32_t count = gb->counter.tima_count + elapsed;
    uint8_t tima = *tima_out;
    while (count >= threshold)
    {
        count -= threshold;
        if (++tima == 0x00)
        {
            tima = gb->gb_reg.TMA;
            ofl = true;
        }
    }
    *tima_out = tima;
    return ofl;
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

    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:

    case 0xC:
    case 0xD:
    case 0xE:
        return gb->ram_base[addr >> 12][addr];

    case 0x8:
    case 0x9:
        if (__gb_ppu_mode_for_lock(gb) == LCD_TRANSFER)
            return 0xFF;
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
                if (addr >= 0xB000)
                    return 0xFF;
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

                        // nonexistent Z axis?
                    case 0x6:
                        return 0x00;
                    case 0x7:
                        return 0xFF;

                    case 0x8:
                        return (gb->mbc7.eeprom_pins & 0xC3) | 0x3C;
                    }
                }
                return 0xFF;
            }
            else if (gb->mbc == 8)
            {
                if (gb->huc1.ir_mode)
                    /* IR receiver: bit 0 = saw light. No link partner. */
                    return 0xC0;

                if (gb->cart_ram_bank < gb->num_ram_banks)
                    return gb
                        ->gb_cart_ram[addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE)];

                return 0xFF;
            }
            else if (gb->mbc == 9)
            {
                switch (gb->huc3.ram_rtc_ir_select)
                {
                case 0x00: /* Cart RAM (read-only) */
                case 0x0A: /* Cart RAM (read/write) */
                    if (gb->cart_ram_bank < gb->num_ram_banks)
                        return gb->gb_cart_ram
                            [addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE)];
                    return 0xFF;

                case 0x0C: /* RTC response: result of last command. Games
                            * compare the full byte, so keep upper bits clear. */
                    return gb->huc3.response;

                case 0x0D: /* RTC semaphore: bit 0 high = ready (exec is synchronous) */
                    return 0x01;

                case 0x0E: /* IR: no link partner, no light seen */
                    return 0x00;

                default: /* Unmapped selects read open bus */
                    return 0xFF;
                }
            }

            if (gb->mbc == 3 && gb->cart_ram_bank >= 0x08 && gb->cart_ram_bank <= 0x0C)
            {
                return gb->latched_rtc[gb->cart_ram_bank - 0x08];
            }
            else if (
                (gb->cart_mode_select || gb->mbc != 1) && !gb->is_mbc1m &&
                gb->cart_ram_bank < gb->num_ram_banks
            )
            {
                return gb->gb_cart_ram[addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE)];
            }
            else
                return gb->gb_cart_ram[addr - CART_RAM_ADDR];
        }

        return 0xFF;

    case 0xF:
        if (addr < OAM_ADDR)
            return gb->echo_ram_base[addr];

        if (addr < UNUSED_ADDR)
        {
            uint8_t mode = __gb_ppu_mode_for_lock(gb);
            if (mode >= LCD_SEARCH_OAM && mode <= LCD_TRANSFER)
                return 0xFF;
            return gb->oam[addr - OAM_ADDR];
        }

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
            uint8_t result = 0xFF;

            if ((p1_val & 0x10) == 0)
                result &= (joypad_val >> 4) | 0xF0;
            if ((p1_val & 0x20) == 0)
                result &= (joypad_val & 0x0F) | 0xF0;

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
        {
            if (gb->gb_reg.tima_overflow_delay)
                return gb->gb_reg.TMA;
            uint8_t tima;
            __gb_timer_peek(gb, &tima);
            return tima;
        }

        case 0x06:
            return gb->gb_reg.TMA;

        case 0x07:
            return gb->gb_reg.TAC;

        /* Interrupt Flag Register */
        case 0x0F:
        {
            uint8_t tima_scratch;
            const uint8_t v = gb->gb_reg.IF | (__gb_timer_peek(gb, &tima_scratch) ? TIMER_INTR : 0);
            return __gb_hle_read_shared(gb, addr, v);
        }

        /* LCD Registers */
        case 0x40:
            return gb->gb_reg.LCDC;

        case 0x41:
        {
            const uint8_t v = __gb_read_stat_synced(gb);
            return __gb_hle_read_shared(gb, addr, v);
        }

        case 0x42:
            return gb->gb_reg.SCY;

        case 0x43:
            return gb->gb_reg.SCX;

        case 0x44:
        {
            const uint8_t v = __gb_read_ly_synced(gb);
            return __gb_hle_read_shared(gb, addr, v);
        }

        case 0x45:
            return gb->gb_reg.LYC;

        /* DMA Register */
        case 0x46:
        {
            const uint8_t v = gb->gb_reg.DMA;
            return __gb_hle_read_shared(gb, addr, v);
        }

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
    const bool di = !!(gb->mbc7.eeprom_pins & 0x02);

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

        /* Commands are 10 bits after the start bit: 2 opcode + 1 "x" +
         * 7 address (e.g. READ = "10xAAAAAAA"). */
        if (gb->mbc7.eeprom_bits_shifted == 10)
        {
            uint8_t opcode = (gb->mbc7.eeprom_shift_reg >> 8) & 0x03;
            gb->mbc7.eeprom_addr = gb->mbc7.eeprom_shift_reg & 0x7F;

            gb->mbc7.eeprom_state = 0; /* Default to IDLE */

            switch (opcode)
            {
            case 0b00: /* Control opcodes (sub-opcode in bits 7-6) */
                if (((gb->mbc7.eeprom_shift_reg >> 6) & 0x03) == 0b11) /* EWEN */
                    gb->mbc7.eeprom_write_enabled = 1;
                else if (((gb->mbc7.eeprom_shift_reg >> 6) & 0x03) == 0b00) /* EWDS */
                    gb->mbc7.eeprom_write_enabled = 0;
                else if (((gb->mbc7.eeprom_shift_reg >> 6) & 0x03) == 0b10) /* ERAL */
                {
                    if (gb->mbc7.eeprom_write_enabled)
                    {
                        for (int i = 0; i < gb->gb_cart_ram_size / 2; i++)
                            ((uint16_t*)gb->gb_cart_ram)[i] = 0xFFFF;
                        gb->direct.sram_updated = true;
                    }
                }
                else if (((gb->mbc7.eeprom_shift_reg >> 6) & 0x03) == 0b01) /* WRAL */
                {
                    gb->mbc7.eeprom_state = 3;   /* WRITE */
                    gb->mbc7.eeprom_addr = 0xFF; /* WRAL flag */
                    gb->mbc7.eeprom_bits_shifted = 0;
                }
                break;

            case 0b01:                     /* WRITE */
                gb->mbc7.eeprom_state = 3; /* WRITE */
                gb->mbc7.eeprom_bits_shifted = 0;
                break;

            case 0b10:                     /* READ */
                gb->mbc7.eeprom_state = 2; /* READ */
                gb->mbc7.eeprom_read_buffer = ((uint16_t*)gb->gb_cart_ram)[gb->mbc7.eeprom_addr];
                gb->mbc7.eeprom_bits_shifted = 0;
                return;

            case 0b11: /* ERASE */
                if (gb->mbc7.eeprom_write_enabled)
                {
                    ((uint16_t*)gb->gb_cart_ram)[gb->mbc7.eeprom_addr] = 0xFFFF;
                    gb->direct.sram_updated = true;
                }
                break;
            }
        }
        break;

    /* Shifting out data for a READ command. */
    case 2: /* READ */
        gb->mbc7.eeprom_pins =
            (gb->mbc7.eeprom_pins & ~1) | ((gb->mbc7.eeprom_read_buffer >> 15) & 1);
        gb->mbc7.eeprom_read_buffer <<= 1;
        if (++gb->mbc7.eeprom_bits_shifted >= 16)
        {
            gb->mbc7.eeprom_state = 0; /* IDLE */
        }
        break;

    /* Shifting in data for a WRITE or WRAL command. */
    case 3: /* WRITE */
        // shift in 16 bits:
        gb->mbc7.eeprom_shift_reg = (gb->mbc7.eeprom_shift_reg << 1) | di;
        if (++gb->mbc7.eeprom_bits_shifted >= 16)
        {
            if (gb->mbc7.eeprom_write_enabled)
            {
                uint16_t data = gb->mbc7.eeprom_shift_reg;
                if (gb->mbc7.eeprom_addr == 0xFF)
                {
                    // clear EEPROM
                    for (int i = 0; i < gb->gb_cart_ram_size / 2; i++)
                        ((uint16_t*)gb->gb_cart_ram)[i] = data;
                    gb->direct.sram_updated = 1;
                    // playdate->system->logToConsole("mbc7 wall %04x", data);
                }
                else
                {
                    // 16-bit write
                    u16* v = &((uint16_t*)gb->gb_cart_ram)[gb->mbc7.eeprom_addr & 0x7F];
                    if (*v != data)
                    {
                        gb->direct.sram_updated = 1;
                        *v = data;
                    }
                    // playdate->system->logToConsole("mbc7 write %04x <-
                    // %04x",gb->mbc7.eeprom_addr, data);
                }
            }

            gb->mbc7.eeprom_bits_shifted = 0;
            gb->mbc7.eeprom_state = 0;

            // indicate "done"
            // NOTE: in real hardware, this bit is clear during the time it takes
            // for a write to complete
            gb->mbc7.eeprom_pins |= 0x01;
        }
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
            else if (gb->mbc == 8)
                /* HuC1: no RAM enable; $0E maps IR register at $A000-BFFF,
                 * anything else maps cart RAM. */
                gb->huc1.ir_mode = ((val & 0x0F) == 0x0E);
            else if (gb->mbc == 9)
                /* HuC3: selects RAM / RTC registers / IR at $A000-BFFF. */
                gb->huc3.ram_rtc_ir_select = val & 0x0F;
            else if (gb->mbc > 0 && gb->cart_ram)
                gb->enable_cart_ram = ((val & 0x0F) == 0x0A);
        }
        else if (addr < 0x4000)  // Address is 2000-3FFF (ROM Bank Lower Bits)
        {
            if (gb->mbc == 1)
            {
                if (gb->is_mbc1m)
                {
                    uint8_t lo = (val & 0x1F);
                    if (lo == 0x00)
                        lo = 0x01;  // 00->01 quirk uses full 5-bit register
                    lo &= 0x0F;
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
                gb->selected_rom_bank = val & 0x7F;
                if (!gb->selected_rom_bank)
                    gb->selected_rom_bank = 1;
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
            }
            else if (gb->mbc == 8)
            {
                /* HuC1: 6-bit bank, no 00->01 quirk (differs from MBC1). */
                gb->selected_rom_bank = val & 0x3F;
            }
            else if (gb->mbc == 9)
            {
                /* HuC3: 7-bit bank; like MBC5, bank 0 may be mapped. */
                gb->selected_rom_bank = val & 0x7F;
            }
        }

        if (gb->mbc > 0)
        {
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
            }

            __gb_update_selected_bank_addr(gb);
            __gb_update_mbc1_zero_bank(gb);
        }
        else if (gb->mbc == 3)
            gb->cart_ram_bank = val;
        else if (gb->mbc == 5)
            gb->cart_ram_bank = (val & 0x0F);
        else if (gb->mbc == 7)
            gb->mbc7.ram_enable_2 = (val == 0x40);
        else if (gb->mbc == 8 || gb->mbc == 9)
            /* HuC1/HuC3: 2-bit RAM bank. */
            gb->cart_ram_bank = (val & 0x03);

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
            __gb_update_mbc1_zero_bank(gb);
        }
        return;

    case 0x8:
    case 0x9:
        if (__gb_ppu_mode_for_lock(gb) == LCD_TRANSFER)
            return;
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
            else if (gb->mbc == 3 && gb->cart_ram_bank >= 0x08 && gb->cart_ram_bank <= 0x0C)
            {
                gb->cart_rtc[gb->cart_ram_bank - 0x08] = val;
            }
            else if (gb->mbc == 7 && addr < 0xB000)
            {
                if (gb->mbc7.ram_enable_1 && gb->mbc7.ram_enable_2)
                {
                    uint8_t reg = (addr >> 4) & 0x0F;
                    uint8_t old_pins = gb->mbc7.eeprom_pins;
                    switch (reg)
                    {
                    case 0x0: /* Latch Accelerometer (arm) */
                        if (val == 0x55)
                        {
                            gb->mbc7.accel_latch_state = 1;
                            gb->mbc7.accel_x_latched = 0x8000;
                            gb->mbc7.accel_y_latched = 0x8000;
                        }
                        break;

                    case 0x1: /* Latch Accelerometer (trigger) */
                        if (gb->mbc7.accel_latch_state == 1 && val == 0xAA)
                        {
                            float a[2];
                            playdate->system->getAccelerometer(a, a + 1, NULL);
                            gb->mbc7.accel_x_latched = (uint16_t)(0x81D0 - 0x70 * a[0]);
                            gb->mbc7.accel_y_latched = (uint16_t)(0x81D0 - 0x70 * a[1]);
                            gb->mbc7.accel_latch_state = 0;
                        }
                        break;

                    case 0x8: /* EEPROM Control */
                        gb->mbc7.eeprom_pins &= 0x1;
                        gb->mbc7.eeprom_pins |= (val & ~0x1);
                        if (!(old_pins & 0x80) && (val & 0x80))
                        {
                            // reset state
                            gb->mbc7.eeprom_state = 0;
                            gb->mbc7.eeprom_bits_shifted = 0;
                        }
                        if ((val & 0xC0) == 0xC0 && !(old_pins & 0x40))
                        {
                            __gb_mbc7_eeprom_clock(gb);
                        }
                        break;
                    }
                }
            }
            else if (gb->mbc == 8)
            {
                if (gb->huc1.ir_mode)
                {
                    /* IR transmitter: bit 0 on/off. No link partner; stub. */
                }
                else if (gb->cart_ram_bank < gb->num_ram_banks)
                {
                    size_t idx = addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE);
                    CB_ASSERT(idx < gb->gb_cart_ram_size);
                    const u8 prev = gb->gb_cart_ram[idx];
                    gb->gb_cart_ram[idx] = val;
                    gb->direct.sram_updated |= prev != val;
                }
            }
            else if (gb->mbc == 9)
            {
                switch (gb->huc3.ram_rtc_ir_select)
                {
                case 0x0A: /* Cart RAM (read/write) */
                    if (gb->cart_ram_bank < gb->num_ram_banks)
                    {
                        size_t idx = addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE);
                        CB_ASSERT(idx < gb->gb_cart_ram_size);
                        const u8 prev = gb->gb_cart_ram[idx];
                        gb->gb_cart_ram[idx] = val;
                        gb->direct.sram_updated |= prev != val;
                    }
                    break;

                case 0x0B: /* RTC command/argument mailbox (D7 not connected) */
                    gb->huc3.cmd = val & 0x7F;
                    break;

                case 0x0D: /* RTC semaphore: clear bit 0 to execute command */
                    if (!(val & 0x01))
                        __gb_huc3_rtc_exec(gb);
                    break;

                case 0x0E: /* IR (stub, no link partner) */
                    break;

                default:
                    /* $00 (read-only RAM), $0C, and unmapped selects: ignore. */
                    break;
                }
            }
            else if (
                (gb->cart_mode_select || gb->mbc != 1) && !gb->is_mbc1m &&
                gb->cart_ram_bank < gb->num_ram_banks
            )
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
            uint8_t mode = __gb_ppu_mode_for_lock(gb);
            if (mode >= LCD_SEARCH_OAM && mode <= LCD_TRANSFER)
                return;
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
                uint32_t apu_ts = gb->counter.apu_count;
                uint32_t in_batch = pgb_write_cycle;
                if (gb->lcd_mode == LCD_VBLANK)
                    in_batch >>= gb->overclock;
                in_batch >>= gb->cgb_fast_mode_active;
                apu_ts += in_batch;
                APU_CALL_WR(audio_write, &gb->audio, addr, val, apu_ts);
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

                if (!gb->is_cgb_mode && gb->cpu_reg.pc < 0x300)
                {
                    // Early boot (e.g., Alleyway) expects instant completion when unplugged.
                    gb->gb_reg.SB = 0xFF;
                    gb->gb_reg.IF |= SERIAL_INTR;
                    gb->direct.intr_pending = 1;
                    gb->gb_reg.SC &= ~SERIAL_SC_TX_START;
                    gb->counter.serial_count = 0;
                    gb->printer_stub_state = 0;
                    gb->printer_data_len = 0;
                    gb->printer_last_cmd = 0;
                    return;
                }

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
                    gb->direct.intr_pending = 1;
                    gb->gb_reg.SC &= ~SERIAL_SC_TX_START;
                }
                else
                {
                    gb->counter.serial_count = SERIAL_CYCLES;
                    gb->printer_stub_state = 0;
                    gb->printer_data_len = 0;
                    gb->printer_last_cmd = 0;
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
        {
            uint16_t divider = ((uint16_t)gb->gb_reg.DIV << 8) | (gb->counter.div_count & 0xFF);
            bool old_input =
                gb->gb_reg.tac_enable && ((divider >> gb->gb_reg.tac_input_bit) & 0x01);
            if (preferences_sound_mode == 1)
            {
                uint8_t div_apu_mask = gb->cgb_fast_mode_active ? 0x20 : 0x10;
                if (gb->gb_reg.DIV & div_apu_mask)
                    audio_div_apu_tick(&gb->audio);
            }
            else if (preferences_sound_mode == 2)
            {
                uint8_t div_apu_mask = gb->cgb_fast_mode_active ? 0x20 : 0x10;
                __apu_div_step_write(gb->gb_reg.DIV, div_apu_mask);
            }
            gb->gb_reg.DIV = 0x00;
            gb->counter.div_count = 0;
            if (old_input)
            {
                __gb_timer_edge_tick(gb);
            }
            return;
        }

        case 0x05:
            gb->gb_reg.TIMA = val;
            return;

        case 0x06:
            gb->gb_reg.TMA = val;
            if (gb->gb_reg.tima_overflow_delay)
                gb->gb_reg.TIMA = val;
            return;

        case 0x07:
        {
            uint16_t divider = ((uint16_t)gb->gb_reg.DIV << 8) | (gb->counter.div_count & 0xFF);
            bool old_tac_enable = gb->gb_reg.tac_enable;
            bool old_input = old_tac_enable && ((divider >> gb->gb_reg.tac_input_bit) & 0x01);

            gb->gb_reg.TAC = val;
            __gb_update_tac(gb);

            bool new_tac_enable = gb->gb_reg.tac_enable;
            bool new_input = new_tac_enable && ((divider >> gb->gb_reg.tac_input_bit) & 0x01);

            if (new_tac_enable && old_input && !new_input)
            {
                __gb_timer_edge_tick(gb);
            }
            else if (!gb->is_cgb_mode && !new_tac_enable && old_input)
            {
                __gb_timer_edge_tick(gb);
            }
            return;
        }

        /* Interrupt Flag Register */
        case 0x0F:
            gb->gb_reg.IF = (val | 0b11100000);
            gb->direct.intr_pending = 1;  // conservative: re-checked at batch start
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

            // CGB: clearing the window enable bit resets the window Y
            // condition; WY must reach LY again for the window to reappear
            if (gb->is_cgb_mode && (old_lcdc & LCDC_WINDOW_ENABLE) &&
                !(gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE))
                gb->direct.wy_latched = 0;

            if (was_enabled && !is_enabled)
            {
                gb->counter.lcd_off_count = 0;
                gb->gb_reg.LY = 0;
                gb->counter.lcd_count = 0;
                gb->lcd_mode = LCD_HBLANK;
                gb->gb_reg.STAT &= ~(STAT_MODE | STAT_LYC_COINC);
                gb->direct.stat_line = 0;
                gb->display.window_clear = 0;
                gb->direct.wy_latched = 0;
                __gb_check_lyc__cgb(gb);
#if PGB_IS_CGB
                /* LCD off stops the PPU tick, which would stall an in-flight
                 * HBlank HDMA (hardware pauses it instead).*/
                while (gb->cgb_hdma_active)
                    __gb_do_hdma(gb);
#endif
            }
            else if (!was_enabled && is_enabled)
            {
                gb->counter.lcd_count = 4;
                gb->gb_reg.LY = 0;
                gb->lcd_mode = LCD_SEARCH_OAM;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | gb->lcd_mode;
                gb->direct.stat_line = 0;
                gb->direct.first_scanline_besu_skip = 1;
                __gb_update_lyc_and_stat_irq__cgb(gb);
            }
            return;
        }

        case 0x41:  // STAT Register
        {
            // --- Spurious STAT interrupt quirk (DMG-only) ---
            // Pan Docs: a STAT write behaves as if $FF were written for one
            // M-cycle, requesting an interrupt if any source condition is
            // true. Gated to VBlank: Road Rash depends on it polling STAT in
            // VBlank, while F-1 Pole Position's ISRs write STAT in modes
            // 0/2/3, where the glitch re-arms STAT endlessly and the nested
            // ISRs smash the stack. Rising-edge check only fires when the
            // STAT line was previously low.
            if (!gb->is_cgb_mode && (gb->gb_reg.LCDC & LCDC_ENABLE))
            {
                if (!gb->direct.stat_line && gb->lcd_mode == LCD_VBLANK)
                {
                    gb->gb_reg.IF |= LCDC_INTR;
                    gb->direct.intr_pending = 1;
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
                __gb_update_lyc_and_stat_irq__cgb(gb);
            return;

        /* DMA Register */
        case 0x46:
            /* val % 0xF1: removing this causes visible gfx glitches.
             * Undocumented but required for compatibility. */
            gb->gb_reg.DMA = (val % 0xF1);
            gb->dma_src = ((uint16_t)(val % 0xF1)) << 8;
            gb->dma_dest = 0;
            gb->dma_active = true;
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

#pragma pack(push, 1)
struct sprite_data
{
    uint8_t sprite_number;
    uint8_t x;
};
#pragma pack(pop)

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

        // 1. Advance PC over the operand (0x00). PC is now at (PC_0x10 + 2).
        // The instruction is fetched (PC+1) and the handler needs to advance it past the operand
        // (PC+2).
        gb->cpu_reg.pc++;

        // CGB speed switch
        if (gb->is_cgb_mode && gb->cgb_fast_mode_armed)
        {
            gb->cgb_fast_mode_armed = false;
            gb->gb_reg.DIV = 0;

            gb->cgb_fast_mode = !gb->cgb_fast_mode;
            gb->cgb_fast_mode_active = gb->cgb_fast_mode && (preferences_cgb_speed == 0);
            /* Keep the combined vblank cycle shift at most >>2 (see game_scene):
             * cap overclock at x2 the moment fast mode engages, not next frame. */
            if (gb->cgb_fast_mode_active)
                gb->overclock = MIN(gb->overclock, 1);
            gb->gb_halt = 1;
            gb->cgb_speed_switch_halt_period = CGB_SPEED_SWITCH_HALT_T_CYCLES;
            goto exit;
        }

        // 2. Check for DMG Button Glitch (STOP becomes a 1-byte NOP)
        if (!gb->is_cgb_mode && (gb->direct.joypad != 0xFF) && ((gb->gb_reg.P1 & 0x30) != 0x30))
        {
            /* STOP Glitch: STOP acts as a 1-byte NOP.
               PC is currently at (PC_0x10 + 2). We must rewind to (PC_0x10 + 1). */
            gb->cpu_reg.pc--;
            goto exit;
        }

        // 3. Check for Pending Interrupts
        gb->gb_reg.DIV = 0;

        if (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR)
        {
            if (gb->gb_ime == 0)
            {
                /* STOP/HALT Bug Triggered: CPU does not stop.
                   PC must be set to the operand address (PC_0x10 + 1) to repeat it. */

                // PC is currently at PC_0x10 + 2. Decrement to PC_0x10 + 1.
                gb->cpu_reg.pc--;
            }

            // If IME=1, the interrupt will wake the CPU before it stops.
            // The PC remains at PC_0x10 + 2, and the interrupt handler is called next.
        }
        else
        {
            /* 4. Normal STOP Operation: Enter low-power STOP mode. */
            gb->gb_stop = 1;
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
        gb->cpu_reg.f_bits.h =
            ((gb->cpu_reg.hl & 0xFFF) + (gb->cpu_reg.sp & 0xFFF)) & 0x1000 ? 1 : 0;
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
        if ((gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR) != 0)
        {
            if (gb->gb_ime)
            {
                /* Interrupt pending with IME=1: the halt latch never sets.
                 * The interrupt dispatch pushes HALT's own address, so the
                 * handler returns to HALT, which then halts normally. */
                gb->cpu_reg.pc--;
            }
            else if (gb->gb_ime_countdown > 0)
            {
                /* HALT bug (IME=0, pending interrupt) with active EI delay.
                 * Rewind PC to HALT address so the pending interrupt returns to
                 * HALT, which then re-executes and properly halts. */
                gb->cpu_reg.pc--;
                gb->gb_halt = 1;
            }
            else
            {
                /* HALT bug (IME=0, pending interrupt) without EI delay.
                 * HALT reads the operand byte (hardware bus cycle), then the
                 * same byte is read once as the next opcode: PC increment is
                 * inhibited for one fetch. */
                __gb_read_full(gb, gb->cpu_reg.pc);
                gb->gb_halt_bug = 1;
                gb->gb_halt_bug_pc = gb->cpu_reg.pc;
            }
        }
        else
        {
            gb->gb_halt = 1;
        }
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
    gb->gb_stop = 0;
    gb->cgb_speed_switch_halt_period = 0;

    if (gb->gb_ime)
    {
        /* Disable interrupts */
        gb->gb_ime = 0;
        gb->gb_ime_countdown = 0;

        /* Save IF before push - protects against SP overlapping IF register
         * (SP == 0xFF10 or 0xFF11) which corrupts IF via the push. */
        uint8_t pending_if = gb->gb_reg.IF;

        /* Push Program Counter */
        if (gb->is_cgb_mode)
            __gb_push16__cgb(gb, gb->cpu_reg.pc);
        else
            __gb_push16__dmg(gb, gb->cpu_reg.pc);

        /* IE push bug: if SP overlapped $FFFF during push, the IE register
         * was updated mid-dispatch. The priority chain must re-evaluate
         * with the possibly-changed IE. */
        if (gb->cpu_reg.sp == 0xFFFE || gb->cpu_reg.sp == 0xFFFF)
            pending_if &= gb->gb_reg.IE;

        /* Call interrupt handler if required. */
        if (pending_if & gb->gb_reg.IE & VBLANK_INTR)
        {
            gb->cpu_reg.pc = VBLANK_INTR_ADDR;
            pending_if ^= VBLANK_INTR;
        }
        else if (pending_if & gb->gb_reg.IE & LCDC_INTR)
        {
            gb->cpu_reg.pc = LCDC_INTR_ADDR;
            pending_if ^= LCDC_INTR;
        }
        else if (pending_if & gb->gb_reg.IE & TIMER_INTR)
        {
            gb->cpu_reg.pc = TIMER_INTR_ADDR;
            pending_if ^= TIMER_INTR;
        }
        else if (pending_if & gb->gb_reg.IE & SERIAL_INTR)
        {
            gb->cpu_reg.pc = SERIAL_INTR_ADDR;
            pending_if ^= SERIAL_INTR;
        }
        else if (pending_if & gb->gb_reg.IE & CONTROL_INTR)
        {
            gb->cpu_reg.pc = CONTROL_INTR_ADDR;
            pending_if ^= CONTROL_INTR;
        }

        gb->gb_reg.IF = pending_if | 0xE0;
    }
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
    __gb_update_map_pointers(gb);

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
        return 256;

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
    gb->cgb_gdma_halt_period = 0;  // stale period would gate interrupt dispatch
    gb->gb_halt_bug = 0;
    gb->gb_ime = 0;
    memset(pgb_hle_table, 0, sizeof(pgb_hle_table));
#ifdef TARGET_SIMULATOR
    pgb_hle_fail_logged = 0;
    pgb_hle_skip_logged = 0;
#endif

    /* Reset APU internal state + zero registers; values written below. */
    audio_reset(&gb->audio);

    /* Initialise MBC values. */
    gb->selected_rom_bank = 1;
    gb->cart_ram_bank = 0;
    gb->enable_cart_ram = 0;
    gb->cart_mode_select = 0;
    gb->zero_bank_base = 0;

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
        gb->enable_cart_ram = 1;
    }

    /* HuC carts have no RAM enable; keep $A000-BFFF always mapped. */
    if (gb->mbc == 8)
    {
        gb->huc1.ir_mode = 0;
        gb->enable_cart_ram = 1;
    }
    else if (gb->mbc == 9)
    {
        gb->huc3.ram_rtc_ir_select = 0;
        gb->enable_cart_ram = 1;
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
        gb->gb_reg.SC = 0x7F;
        gb->gb_reg.DIV = 0xAC;
        gb->counter.div_count = 0x28;
        gb->gb_reg.TIMA = 0x00;
        gb->gb_reg.TMA = 0x00;
        gb->gb_reg.TAC = 0xF8;
        gb->gb_reg.IF = 0xE1;
        gb->gb_reg.LCDC = 0x91;
        gb->gb_reg.STAT = 0x85;
        gb->gb_reg.SCY = 0x00;
        gb->gb_reg.SCX = 0x00;
        gb->gb_reg.LY = 146;
        gb->gb_reg.LYC = 0x00;
        gb->gb_reg.DMA = 0x00;
        gb->dma_active = false;
        gb->dma_dest = 0xA0;
        __gb_write_full(gb, 0xFF47, 0xFC);
        __gb_write_full(gb, 0xFF48, 0xFF);
        __gb_write_full(gb, 0xFF49, 0xFF);
        gb->gb_reg.WY = 0x00;
        gb->gb_reg.WX = 0x00;
        gb->gb_reg.IE = 0x00;

        /* Sound registers. NR52 first: audio_write drops NRx writes while
         * the APU is off (audio_reset zeroed NR52); power-on must precede
         * the register writes or they are lost (Road Rash stayed silent
         * until its sound driver did a full init mid-race).
         * NRx4 stay $3F (trigger clear), not Pan Docs' post-boot $BF: a
         * trigger at init causes a spurious "ba-ding". Reads are masked
         * with $BF in audio_read, so games see the Pan Docs value anyway. */
        __gb_write_full(gb, 0xFF26, 0xF1);
        __gb_write_full(gb, 0xFF10, 0x80);
        __gb_write_full(gb, 0xFF11, 0xBF);
        __gb_write_full(gb, 0xFF12, 0xF3);
        __gb_write_full(gb, 0xFF13, 0xFF);
        __gb_write_full(gb, 0xFF14, 0x3F);
        __gb_write_full(gb, 0xFF15, 0xFF);
        __gb_write_full(gb, 0xFF16, 0x3F);
        __gb_write_full(gb, 0xFF17, 0x00);
        __gb_write_full(gb, 0xFF18, 0xFF);
        __gb_write_full(gb, 0xFF19, 0x3F);
        __gb_write_full(gb, 0xFF1A, 0x7F);
        __gb_write_full(gb, 0xFF1B, 0xFF);
        __gb_write_full(gb, 0xFF1C, 0x9F);
        __gb_write_full(gb, 0xFF1D, 0xFF);
        __gb_write_full(gb, 0xFF1E, 0x3F);
        __gb_write_full(gb, 0xFF1F, 0xFF);
        __gb_write_full(gb, 0xFF20, 0xFF);
        __gb_write_full(gb, 0xFF21, 0x00);
        __gb_write_full(gb, 0xFF22, 0x00);
        __gb_write_full(gb, 0xFF23, 0x3F);
        __gb_write_full(gb, 0xFF24, 0x77);
        __gb_write_full(gb, 0xFF25, 0xF3);

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

        /* Initialize CGB palettes to post-boot-ROM state.
         * BG palettes: all initialized to white by boot ROM.
         * OBJ palettes: uninitialized, except OBJ0 color 0 lo = 0x00. */
        for (int i = 0; i < 64; i += 2)
        {
            gb->cgb_bg_palette[i] = 0xFF;
            gb->cgb_bg_palette[i + 1] = 0x7F;
            gb->cgb_obj_palette[i] = 0x00;
            gb->cgb_obj_palette[i + 1] = 0x00;
        }
        gb->cgb_obj_palette[0] = 0x00;
        gb->cgb_bg_palette_index = 0x40;
        gb->cgb_obj_palette_index = 0x40;
        for (int i = 0; i < 8; i++)
        {
            __cgb_update_bg_gray_palette(gb, i, 0);
            __cgb_update_obj_gray_palette(gb, i, gb->cgb_obj_palette_gray);
        }
        pgb_cgb_bg_pal_dirty = 0;
        pgb_cgb_obj_pal_dirty = 0;
        memset(gb->cgb_obj_palette_gray_alt, 0, sizeof(gb->cgb_obj_palette_gray_alt));

        /* CGB internal timer is 0xAC28 */
        gb->counter.lcd_count = 0;
        gb->lcd_mode = LCD_VBLANK;
    }
    else
    {
        /*****************************************************************/
        /* --- POST-BOOT ROM STATE (DMG Skip-BIOS) --- */
        /*****************************************************************/

        /* Initialize CPU registers as though the boot ROM has just finished. */
        uint8_t f = (gb->gb_rom[0x014D] == 0) ? 0x80 : 0xB0;
        gb->cpu_reg.af = (f << 8) | 0x01;
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
        gb->counter.div_count = 0xCC;
        gb->gb_reg.TIMA = 0x00;
        gb->gb_reg.TMA = 0x00;
        gb->gb_reg.TAC = 0xF8;
        gb->gb_reg.IF = 0xE1;

        /* Sound registers. NR52 first: audio_write drops NRx writes while
         * the APU is off (audio_reset zeroed NR52); power-on must precede
         * the register writes or they are lost (Road Rash stayed silent
         * until its sound driver did a full init mid-race).
         * NRx4 stay $3F (trigger clear), not Pan Docs' post-boot $BF: a
         * trigger at init causes a spurious "ba-ding". Reads are masked
         * with $BF in audio_read, so games see the Pan Docs value anyway. */
        __gb_write_full(gb, 0xFF26, 0xF1);
        __gb_write_full(gb, 0xFF10, 0x80);
        __gb_write_full(gb, 0xFF11, 0xBF);
        __gb_write_full(gb, 0xFF12, 0xF3);
        __gb_write_full(gb, 0xFF13, 0xFF);
        __gb_write_full(gb, 0xFF14, 0x3F);
        __gb_write_full(gb, 0xFF15, 0xFF);
        __gb_write_full(gb, 0xFF16, 0x3F);
        __gb_write_full(gb, 0xFF17, 0x00);
        __gb_write_full(gb, 0xFF18, 0xFF);
        __gb_write_full(gb, 0xFF19, 0x3F);
        __gb_write_full(gb, 0xFF1A, 0x7F);
        __gb_write_full(gb, 0xFF1B, 0xFF);
        __gb_write_full(gb, 0xFF1C, 0x9F);
        __gb_write_full(gb, 0xFF1D, 0xFF);
        __gb_write_full(gb, 0xFF1E, 0x3F);
        __gb_write_full(gb, 0xFF1F, 0xFF);
        __gb_write_full(gb, 0xFF20, 0xFF);
        __gb_write_full(gb, 0xFF21, 0x00);
        __gb_write_full(gb, 0xFF22, 0x00);
        __gb_write_full(gb, 0xFF23, 0x3F);
        __gb_write_full(gb, 0xFF24, 0x77);
        __gb_write_full(gb, 0xFF25, 0xF3);

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
        gb->gb_reg.STAT =
            0x85;  // Mode reads VBlank (quirk), actual PPU is HBlank - corrects within a few lines
        gb->gb_reg.SCY = 0x00;
        gb->gb_reg.SCX = 0x00;
        gb->gb_reg.LY = 0;
        gb->gb_reg.LYC = 0x00;
        gb->gb_reg.DMA = 0xFF;
        gb->dma_active = false;
        gb->dma_dest = 0xA0;
        __gb_write_full(gb, 0xFF47, 0xFC);
        __gb_write_full(gb, 0xFF48, 0xFF);
        __gb_write_full(gb, 0xFF49, 0xFF);
        gb->gb_reg.WY = 0x00;
        gb->gb_reg.WX = 0x00;
        gb->gb_reg.IE = 0x00;

        // DMG internal timer: DIV=0xAB, sub-tick=0xCC
        // (204 T-cycles per dmg-timing-spec post-boot)
        gb->display.current_mode3_cycles = 172;
        gb->display.current_mode0_cycles = 204;
        gb->counter.lcd_count = 144;
        gb->lcd_mode = LCD_HBLANK;
    }

    /* Common state for all modes */
    gb->counter.tima_count = 0;
    gb->counter.serial_count = 0;
    gb->counter.lcd_off_count = 0;

    /* Seed the replay snapshot now that the register values are final. */
    audio_reset_snapshot(&gb->audio);

    gb->printer_stub_state = 0;
    gb->printer_data_len = 0;
    gb->printer_last_cmd = 0;

    __gb_update_tac(gb);
    __gb_update_map_pointers(gb);

    gb->direct.joypad = 0xFF;
    gb->direct.stat_line = 0;
    gb->direct.joypad_interrupt_delay = 0;

    gb->gb_reg.tima_overflow_delay = 0;
    gb->hram[0xFF] = gb->gb_reg.IE;

    gb->direct.crank_menu_accumulation = 0x8000;
    gb->direct.crank_menu_delta = 0;
    gb->cgb_fast_mode_active = false;

    memset(gb->vram, 0x00, VRAM_SIZE_CGB);
    memset(gb->wram, 0x00, WRAM_SIZE_CGB);

    // Load Nintendo logo tiles into VRAM from ROM header (0x0104-0x0133).
    // The boot ROM normally handles this during the logo scroll animation.
    {
        const uint8_t* logo = &gb->gb_rom[0x0104];
        for (int i = 0; i < 48; i++)
        {
            uint8_t byte = logo[i];

            uint8_t hi = (byte >> 4) & 0x0F;
            hi = ((hi & 8) ? 0xC0 : 0) | ((hi & 4) ? 0x30 : 0) | ((hi & 2) ? 0x0C : 0) |
                 ((hi & 1) ? 0x03 : 0);

            uint8_t lo = byte & 0x0F;
            lo = ((lo & 8) ? 0xC0 : 0) | ((lo & 4) ? 0x30 : 0) | ((lo & 2) ? 0x0C : 0) |
                 ((lo & 1) ? 0x03 : 0);

            int offs = 0x10 + i * 8;
            gb->vram[offs] = reverse_bits_u8(hi);
            gb->vram[offs + 2] = reverse_bits_u8(hi);
            gb->vram[offs + 4] = reverse_bits_u8(lo);
            gb->vram[offs + 6] = reverse_bits_u8(lo);
        }

        // Trademark symbol tile at VRAM+0x190 (tile index 25)
        static const uint8_t trademark[] = {0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C};
        for (int row = 0; row < 8; row++)
        {
            gb->vram[0x190 + row * 2] = reverse_bits_u8(trademark[row]);
        }

        // Set up background tilemap (SCRN0 at VRAM+0x1800)
        // matches boot ROM logo scroll layout
        // Trademark at row 8, col 16
        gb->vram[0x1800 + 8 * 32 + 16] = 0x19;

        // Logo tiles: 12 tiles per row, 2 rows
        for (int row = 1; row >= 0; row--)
        {
            for (int col = 11; col >= 0; col--)
            {
                gb->vram[0x1800 + (8 + row) * 32 + 4 + col] = 1 + row * 12 + col;
            }
        }
    }
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
    memset(lcd, 0, LCD_BUFFER_BYTES);
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

        /* HuC3 (0xFE) and HuC1 (0xFF) live outside the table range;
         * both always have cart RAM. */
        if (mbc_value == 0xFE || mbc_value == 0xFF)
        {
            gb->mbc = (mbc_value == 0xFE) ? 9 : 8;
            gb->cart_ram = 1;
        }
        else
        {
            if (mbc_value > sizeof(cart_mbc) - 1 || (gb->mbc = cart_mbc[mbc_value]) == 255u)
                return GB_INIT_CARTRIDGE_UNSUPPORTED;

            gb->cart_ram = cart_ram[mbc_value];
        }
    }

    gb->cart_battery = gb_get_rom_uses_battery(gb->gb_rom);
    gb->num_rom_banks_mask = num_rom_banks_mask[gb->gb_rom[bank_count_location]] - 1;
    gb->num_ram_banks = num_ram_banks[gb->gb_rom[ram_size_location]];

    /* One-time HuC3 RTC init (battery-backed; not cleared in gb_reset).
     * Time starts at max: Robopon treats "RTC < saved SRAM timestamp" as
     * a dead battery, and fresh SRAM reads 0xFF-filled. */
    if (gb->mbc == 9)
    {
        gb->huc3.cmd = 0;
        gb->huc3.response = 0;
        gb->huc3.addr = 0;
        gb->huc3.sub_seconds = 0;
        memset(gb->huc3.mem, 0, sizeof(gb->huc3.mem));
        __gb_huc3_set_time(gb, 0x10, 0xFFF);
        __gb_huc3_set_time(gb, 0x13, 0xFFF);
    }

    gb->is_cgb_mode = (gb->gb_rom[0x0143] & 0x80) && cgb_mode;
    gb->cgb_fast_mode = false;
    gb->cgb_fast_mode_armed = false;
    gb->cgb_speed_switch_halt_period = 0;
    gb->cgb_gdma_halt_period = 0;
    memset(pgb_hle_table, 0, sizeof(pgb_hle_table));
#ifdef TARGET_SIMULATOR
    pgb_hle_fail_logged = 0;
    pgb_hle_skip_logged = 0;
#endif
    pgb_cgb_bg_pal_dirty = 0;
    pgb_cgb_obj_pal_dirty = 0;
    gb->cgb_wram_bank = 0;
    gb->cgb_ff6c = 0;
    gb->cgb_ff75 = 0;
    gb->cgb_vram_bank = 0;
    gb->cgb_ff7x[0] = 0;
    gb->cgb_ff7x[1] = 0;
    gb->cgb_ff7x[2] = 0;
    gb->cgb_hdma_active = false;

#define CGB_PALETTE_LUT_SIZE (48 * 256)
    gb->cgb_bg_palette = cb_malloc(64 + CGB_PALETTE_LUT_SIZE);
    gb->cgb_obj_palette = cb_malloc(64);
    memset(gb->cgb_bg_palette, 0, 64 + CGB_PALETTE_LUT_SIZE);
    memset(gb->cgb_obj_palette, 0, 64);

    gb->cgb_bg_palette_index = 0x40;
    gb->cgb_obj_palette_index = 0x40;

    gb->is_mbc1m = __gb_detect_mbc1m(gb);
    if (gb->is_mbc1m)
        gb->cart_mode_select = 0;

    gb->direct.sound = 1;
    gb->direct.enable_xram = 0;

    // gb_cart_ram_size is set later, in read_cart_ram_file (a required initialization step)

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

void gb_init_lcd(gb_s* gb)
{
    gb->direct.frame_skip = 0;

    gb->display.window_clear = 0;
    gb->display.WY = 0;
    gb->lcd_master_enable = 1;

    return;
}

__rare_shell static u8 __gb_invalid_instruction(gb_s* restrict gb, uint8_t opcode)
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

// allows us to reuse the same code for different systems.
// this functions essentially like C++ templates.
#define $__(x, y) x##__##y
#define $_(x, y) $__(x, y)
#define $(x) $_(x, PGB_TEMPLATE)

// Dirty-line tracking globals. Set by frontend before run_frame.
// Defined in game_scene.c
extern uint8_t* pgb_dirty_prev;
extern uint16_t* pgb_dirty_flags;
extern uint8_t pgb_dirty_skip;

// ------------ DMG ------------

#define PGB_TEMPLATE dmg
#define PGB_IS_DMG 1
#define PGB_IS_CGB 0

#define __core __core_dmg
#define __core_section(x) __core_dmg_section(x)
#define __draw __draw_dmg
#define __rare __rare_dmg

#include "peanut_gb_core.h"

#undef __core
#undef __core_section
#undef __draw
#undef __rare
#undef PGB_IS_DMG
#undef PGB_IS_CGB

// ------------ CGB ------------

#define PGB_TEMPLATE cgb
#define PGB_IS_DMG 0
#define PGB_IS_CGB 1
#define __core __core_cgb
#define __core_section(x) __core_cgb_section(x)
#define __draw __draw_cgb
#define __rare __rare_cgb

#include "peanut_gb_core.h"

#undef __core
#undef __core_section
#undef __draw
#undef __rare
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

typedef typeof(playdate->graphics->markUpdatedRows) markUpdateRows_t;

__shell void update_fb_dirty_lines(
    uint8_t* restrict framebuffer, uint8_t* restrict lcd,
    const uint16_t* restrict line_changed_flags, markUpdateRows_t markUpdatedRows, int scy,
    bool stable_scaling_enabled, uint8_t* restrict dither_lut0, uint8_t* restrict dither_lut1
)
{
    framebuffer += game_picture_x_offset / 8;
    int fb_y_playdate_current_bottom = CB_LCD_Y + CB_LCD_HEIGHT;
    const unsigned scaling = game_picture_scaling ? game_picture_scaling : 0x1000;

    int scale_index = preferences_dither_line;
    if (preferences_dither_stable)
        scale_index += 256 - scy;
    scale_index %= scaling;
    uint8_t* restrict dither_lut0_ptr = dither_lut0;
    uint8_t* restrict dither_lut1_ptr = dither_lut1;

    for (int y_gb = game_picture_y_bottom; y_gb-- > game_picture_y_top;)
    {
        int row_height_on_playdate = 2;
        if (++scale_index == scaling)
        {
            scale_index = 0;
            row_height_on_playdate = 1;

            uint8_t* restrict temp_ptr = dither_lut0_ptr;
            dither_lut0_ptr = dither_lut1_ptr;
            dither_lut1_ptr = temp_ptr;
        }

        int current_line_pd_top_y = fb_y_playdate_current_bottom - row_height_on_playdate;

        if (!((line_changed_flags[y_gb >> 4] >> (y_gb & 0xF)) & 1))
        {
            fb_y_playdate_current_bottom = current_line_pd_top_y;
            continue;
        }

        fb_y_playdate_current_bottom = current_line_pd_top_y;

        if (current_line_pd_top_y < 0)
        {
            break;
        }

        uint32_t* restrict gb_line_data32 = (uint32_t*)&lcd[y_gb * LCD_WIDTH_PACKED];
        uint32_t* restrict pd_fb_line_top_ptr32 =
            (uint32_t*)&framebuffer[current_line_pd_top_y * PLAYDATE_ROW_STRIDE];

        if (row_height_on_playdate == 2)
        {
            uint32_t* restrict pd_fb_line_bottom_ptr32 =
                (uint32_t*)((uint8_t*)pd_fb_line_top_ptr32 + PLAYDATE_ROW_STRIDE);

            for (int x = 0; x < LCD_WIDTH_PACKED / 8; ++x)
            {
                uint32_t org_pixelsA = gb_line_data32[x * 2];
                uint32_t org_pixelsB = gb_line_data32[x * 2 + 1];

                uint8_t p0 = org_pixelsA & 0xFF, p1 = (org_pixelsA >> 8) & 0xFF;
                uint8_t p2 = (org_pixelsA >> 16) & 0xFF, p3 = (org_pixelsA >> 24) & 0xFF;

                uint8_t p4 = org_pixelsB & 0xFF, p5 = (org_pixelsB >> 8) & 0xFF;
                uint8_t p6 = (org_pixelsB >> 16) & 0xFF, p7 = (org_pixelsB >> 24) & 0xFF;

                pd_fb_line_top_ptr32[x * 2] = dither_lut0_ptr[p0] | (dither_lut0_ptr[p1] << 8) |
                                              (dither_lut0_ptr[p2] << 16) |
                                              (dither_lut0_ptr[p3] << 24);
                pd_fb_line_bottom_ptr32[x * 2] = dither_lut1_ptr[p0] | (dither_lut1_ptr[p1] << 8) |
                                                 (dither_lut1_ptr[p2] << 16) |
                                                 (dither_lut1_ptr[p3] << 24);

                pd_fb_line_top_ptr32[x * 2 + 1] = dither_lut0_ptr[p4] | (dither_lut0_ptr[p5] << 8) |
                                                  (dither_lut0_ptr[p6] << 16) |
                                                  (dither_lut0_ptr[p7] << 24);
                pd_fb_line_bottom_ptr32[x * 2 + 1] =
                    dither_lut1_ptr[p4] | (dither_lut1_ptr[p5] << 8) | (dither_lut1_ptr[p6] << 16) |
                    (dither_lut1_ptr[p7] << 24);
            }
        }
        else
        {
            for (int x = 0; x < LCD_WIDTH_PACKED / 8; ++x)
            {
                uint32_t org_pixelsA = gb_line_data32[x * 2];
                uint32_t org_pixelsB = gb_line_data32[x * 2 + 1];

                uint8_t p0 = org_pixelsA & 0xFF, p1 = (org_pixelsA >> 8) & 0xFF;
                uint8_t p2 = (org_pixelsA >> 16) & 0xFF, p3 = (org_pixelsA >> 24) & 0xFF;

                uint8_t p4 = org_pixelsB & 0xFF, p5 = (org_pixelsB >> 8) & 0xFF;
                uint8_t p6 = (org_pixelsB >> 16) & 0xFF, p7 = (org_pixelsB >> 24) & 0xFF;

                pd_fb_line_top_ptr32[x * 2] = dither_lut0_ptr[p0] | (dither_lut0_ptr[p1] << 8) |
                                              (dither_lut0_ptr[p2] << 16) |
                                              (dither_lut0_ptr[p3] << 24);

                pd_fb_line_top_ptr32[x * 2 + 1] = dither_lut0_ptr[p4] | (dither_lut0_ptr[p5] << 8) |
                                                  (dither_lut0_ptr[p6] << 16) |
                                                  (dither_lut0_ptr[p7] << 24);
            }
        }

        markUpdatedRows(current_line_pd_top_y, current_line_pd_top_y + row_height_on_playdate - 1);
    }
}

#endif  // PGB_IMPL
#endif  // PEANUT_GB_H
