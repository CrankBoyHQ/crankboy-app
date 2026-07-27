#include "pgb_common.h"

/* DEVELOPMENT VERSION RANGE: (v2.2.1, TBD] */

// To edit the structs in this file, please make a wholesale
// copy of this file instead of editing it directly.
// Bump PGB_VERSION and replace savestate_upgrade_to_*

// Compatability version (for save state upgrading).
#define PGB_VERSION 6

struct PGB_VERSIONED(gb_breakpoint)
{
    // -1 to disable
    uint32_t rom_addr : 24;

    // what byte was replaced?
    char opcode;
};

struct PGB_VERSIONED(cpu_registers_s)
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
            // Note: stored order of AF is swapped compared to convention
            uint8_t a;
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

struct PGB_VERSIONED(count_s)
{
    uint_fast16_t lcd_count;     /* LCD Timing */
    uint_fast16_t div_count;     /* Divider Register Counter */
    uint_fast16_t tima_count;    /* Timer Counter */
    uint_fast16_t serial_count;  /* Serial Counter */
    uint_fast32_t lcd_off_count; /* Cycles LCD has been disabled */
    uint_fast32_t apu_count;     /* Cycle counter for APU write timestamps.
                                  * Normalised by inst_cycles >>= doubleSpeed
                                  * in done_instr_timing. Reset per frame. */
};

struct PGB_VERSIONED(gb_registers_s)
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
    uint8_t tac_input_bit;

    uint8_t tima_overflow_delay : 1;
};

struct PGB_VERSIONED(chan_len_ctr)
{
    uint8_t load;
    uint32_t counter;
    uint32_t inc;
};

struct PGB_VERSIONED(chan_vol_env)
{
    uint8_t step : 3;
    unsigned up : 1;
    unsigned locked : 1;       // envelope locked after volume hits 0 or MAX
    unsigned should_lock : 1;  // computed when clock rises, applied when clock falls
    unsigned clock : 1;        // high between divider->0 and volume change
    uint32_t counter;
    uint32_t inc;
};

struct PGB_VERSIONED(chan_freq_sweep)
{
    uint16_t freq;
    uint8_t rate;
    uint8_t shift;
    unsigned did_subtract : 1;
    unsigned enabled : 1;
    uint32_t counter;
    uint32_t inc;
    uint8_t divider;
};

struct PGB_VERSIONED(chan)
{
    unsigned enabled : 1;
    unsigned powered : 1;
    unsigned on_left : 1;
    unsigned on_right : 1;
    unsigned muted : 1;
    unsigned lfsr_narrow : 1;
    unsigned sweep_up : 1;
    unsigned len_enabled : 1;
    unsigned sample_surpressed : 1;
    unsigned env_pending : 1;

    uint8_t volume : 4;
    uint8_t volume_init : 4;
    uint16_t freq;
    uint32_t freq_counter;
    uint32_t freq_inc;

    int_fast16_t val;

    struct PGB_VERSIONED(chan_len_ctr) len;
    struct PGB_VERSIONED(chan_vol_env) env;
    struct PGB_VERSIONED(chan_freq_sweep) sweep;

    union
    {
        struct
        {
            uint8_t duty;
            uint8_t duty_counter;
        } square;
        struct
        {
            uint16_t lfsr_reg;
            uint8_t lfsr_div;
        } noise;
        struct
        {
            int8_t sample;
            bool just_read;  // transient: CH3 just read sample, gates DMG wave RAM CPU access
            bool pulsed;     // set on trigger, cleared on DAC disable; gates bugged read path
        } wave;
    };

    int32_t envelope_smooth;

    /* Accurate-mode frame sequencer dividers. Tick at 64 Hz (envelope) / 128 Hz
     * (sweep). Loaded from env.step / sweep.rate on trigger, decremented at
     * the corresponding DIV-APU step. When zero, tick and reload. */
    uint8_t env_divider;
};

/* Max per-frame APU register write events (cycle-accurate replay).
 * Covers CH3 wave-RAM streaming to ~7.6 kHz (16 byte-writes per period). */
#define PGB_APU_EVENT_CAP 2048

struct PGB_VERSIONED(audio_data)
{
    int vol_l : 4;
    int vol_r : 4;
    uint8_t* audio_mem;
    struct PGB_VERSIONED(chan) chans[4];

    /* DIV-APU frame sequencer step (0-7). Clocked at 512 Hz by DIV bit 4
     * falling edges. Gates envelope (step 7), sweep (2,6), length (0,2,4,6). */
    uint8_t div_apu_step;

    /* Set when APU powers on while DIV bit 4/5 is high. The first falling
     * edge tick is skipped (hardware glitch). */
    bool skip_next_apu_tick : 1;

    /* Per-frame register write events for cycle-accurate replay.
     * Sized for CH1/2 volume PCM (16 kHz voice = ~267/frame) and
     * CH3 wave-RAM streaming (4-8 kHz = ~1067-2133/frame). */
    uint16_t apu_event_count;
    struct
    {
        uint32_t apu_count;
        uint16_t addr;
        uint8_t val;
    } apu_events[PGB_APU_EVENT_CAP];

    /* Pre-frame channel snapshot for event replay. */
    struct PGB_VERSIONED(chan) pre_frame_chans[4];
    uint8_t pre_frame_div_apu_step;
    bool pre_frame_skip_apu_tick;
    bool pre_frame_valid;

#if TARGET_PLAYDATE
    int32_t capacitor_l;
    int32_t capacitor_r;
#else
    float capacitor_l;
    float capacitor_r;
#endif
};

/**
 * Emulator context.
 *
 * Only values within the `direct` struct may be modified directly by the
 * front-end implementation. Other variables must not be modified.
 */
struct PGB_VERSIONED(gb_s)
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
    void (*gb_error)(struct PGB_VERSIONED(gb_s) *, const enum gb_error_e, const uint16_t val);

    /* Transmit one byte and return the received byte. */
    void (*gb_serial_tx)(struct PGB_VERSIONED(gb_s) *, const uint8_t tx);
    enum gb_serial_rx_ret_e (*gb_serial_rx)(struct PGB_VERSIONED(gb_s) *, uint8_t* rx);

    struct
    {
        uint8_t gb_halt : 1;
        uint8_t gb_stop : 1;
        uint8_t gb_hle : 1;  // cpu suspended during high-level emulation of a routine
        uint8_t gb_ime : 1;
        uint8_t gb_ime_countdown : 2;
        uint8_t is_cgb_mode : 1;

        /* gb_frame is set when the equivalent time of a frame has
         * passed. It is likely that a new frame has been drawn,
         * but it is also possible that the LCD was off. */

        uint8_t gb_frame : 1;

#define LCD_HBLANK 0
#define LCD_VBLANK 1
#define LCD_SEARCH_OAM 2
#define LCD_TRANSFER 3
        uint8_t lcd_mode : 2;
        uint8_t lcd_blank : 1; /* UNUSED */
        uint8_t lcd_master_enable : 1;
        uint8_t gb_halt_bug : 2;
    };

    uint16_t gb_halt_bug_pc;

    uint32_t zero_bank_base;  // base for 0000–3FFF; 0 for all non-MBC1M

    /* Cartridge information:
     * Memory Bank Controller (MBC) type. */
    uint8_t mbc;
    /* Whether the MBC has internal RAM. */
    uint8_t cart_ram : 1;
    uint8_t cart_battery : 1;

    // state flags for cart ram
    uint8_t enable_cart_ram : 1;
    uint8_t cart_mode_select : 1;  // 1 if ram mode
    uint8_t overclock : 2;

    uint8_t is_mbc1m : 1;

    // 1-7, cgb only
    bool cgb_fast_mode_armed : 1;

    uint8_t cgb_wram_bank : 3;
    uint8_t cgb_ff75 : 3;
    bool cgb_fast_mode : 1;  // source-of-truth
    uint8_t cgb_ff6c : 1;

    uint8_t cgb_vram_bank : 1;
    bool cgb_fast_mode_active : 1;  // temp. as above, but settings-affected
    bool cgb_speed_permitted : 1;
    bool hle_enabled : 1;
    uint8_t hle_ioaddr;
    uint16_t cgb_speed_switch_halt_period;

    uint8_t cgb_ff7x[3];
    uint16_t cgb_hdma_src;
    uint16_t cgb_hdma_dst;
    uint16_t cgb_hdma_len : 7;
    bool cgb_hdma_active : 1;

    bool dma_active : 1;
    uint16_t dma_src;
    uint8_t dma_dest;

    uint8_t* cgb_bg_palette;
    uint8_t* cgb_obj_palette;
    uint8_t cgb_bg_palette_gray[8];
    uint8_t cgb_obj_palette_gray[8];
    uint8_t cgb_obj_palette_gray_alt[8];
    uint8_t cgb_bg_palette_index;
    uint8_t cgb_obj_palette_index;

    uint8_t printer_stub_state;
    uint16_t printer_data_len;
    uint8_t printer_last_cmd;

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
        struct PGB_VERSIONED(cpu_registers_s) cpu_reg;
        uint8_t cpu_reg_raw[12];
        uint16_t cpu_reg_raw16[6];
    };
    struct PGB_VERSIONED(gb_registers_s) gb_reg;
    struct PGB_VERSIONED(count_s) counter;

    /* Pre-computed base pointers. Includes subtraction of region start address.*/
    union
    {
        // Addressable as ram_base[addr >> 12][addr]
        uint8_t* ram_base[16];
        struct
        {
            /*0-7*/ uint8_t* rom_bank_base[2][4];
            /*8-9*/ uint8_t* _unused_vram[2];       // unused (see note on vram)
            /*A-B*/ uint8_t* _unused_cart_base[2];  // unused (need to set sram dirty bit) -- TODO
            /*C-D*/ uint8_t* wram_base[2];
            /*E  */ uint8_t* echo_ram_base;  // section E only
            /*F  */ uint8_t* _unused_io;
        };
    };
    uint8_t* vram_base;  // see note about vram
    uint8_t* selected_cart_bank_addr;

    /* TODO: Allow implementation to allocate WRAM, VRAM and Frame Buffer. */
    uint8_t* wram;  // wram[WRAM_SIZE_CGB];
    uint8_t* vram;  // vram[VRAM_SIZE_CGB]; /* NOTE: tile data (0-0x1800) is stored in reverse bit
                    // order. */
    uint8_t hram[HRAM_SIZE];  // note: includes registers as well as hram for some reason
    uint8_t oam[OAM_SIZE];
    uint8_t* lcd;
    uint8_t* lcd_alt;

    struct
    {
        /* Palettes */
        uint8_t bg_palette[4];
        uint8_t sp_palette[8];

        uint8_t window_clear;
        uint8_t WY;

        uint8_t* bg_map_base;
        uint8_t* window_map_base;

        uint16_t current_mode3_cycles;
        uint16_t current_mode0_cycles;

        uint8_t oam_latch[OAM_SIZE];
        uint8_t latched_scx;
        uint8_t latched_scy;
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
        uint8_t frame_skip : 1;
        uint8_t sound : 1;
        uint8_t sram_updated : 1;
        uint8_t sram_dirty : 1;
        uint8_t crank_docked : 1;
        uint8_t enable_xram : 1;
        uint8_t ignore_cgb_check : 1;
        uint8_t stat_line : 1;
        uint8_t wy_latched : 1;
        uint8_t first_scanline_besu_skip : 1;
        uint8_t has_read_accelerometer_this_frame : 1;
        uint8_t cgb_dual_output : 1;

        int joypad_interrupt_delay;

        // if set, causes crank register to behave as delta-menu-selection instead
        uint8_t ext_crank_menu_indexing : 1;

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
        // (in actual usage, this points to a CB_GameSceneContext*)
        void* priv;
    } direct;

    uint32_t gb_cart_ram_size;

    struct PGB_VERSIONED(gb_breakpoint) * breakpoints;

    size_t gb_rom_size;

    // extended ram feature offered by crankboy
    uint8_t* xram;

    // NOTE: this MUST be the last member of gb_s.
    // sometimes we perform memory operations on the whole gb struct except for
    // audio.
    struct PGB_VERSIONED(audio_data) audio;
};

// Note: used on unswizzled gb struct, so must not follow any pointers
FORCE_INLINE uint32_t PGB_VERSIONED(gb_get_state_size)(const struct PGB_VERSIONED(gb_s) * gb)
{
    return sizeof(struct StateHeader) + sizeof(*gb) + ROM_HEADER_SIZE  // for safe-keeping
           + WRAM_SIZE_CGB + VRAM_SIZE_CGB + XRAM_SIZE + gb->gb_cart_ram_size +
           MAX_BREAKPOINTS * sizeof(struct PGB_VERSIONED(gb_breakpoint)) + 128;

    // skipped: lcd; rom
}

FORCE_INLINE void PGB_VERSIONED(gb_state_save)(struct PGB_VERSIONED(gb_s) * gb, char* out)
{
    // gb
    memcpy(out, gb, sizeof(*gb));
    out += sizeof(*gb);

    // CGB palette data (heap allocated, not in struct)
    memcpy(out, gb->cgb_bg_palette, 64);
    out += 64;
    memcpy(out, gb->cgb_obj_palette, 64);
    out += 64;

    // rom header (so we know the associated rom for this state)
    memcpy(out, gb->gb_rom + ROM_HEADER_START, ROM_HEADER_SIZE);
    out += ROM_HEADER_SIZE;

    // wram
    memcpy(out, gb->wram, WRAM_SIZE_CGB);
    out += WRAM_SIZE_CGB;

    // vram
    memcpy(out, gb->vram, VRAM_SIZE_CGB);
    out += VRAM_SIZE_CGB;

    // xram
    memcpy(out, gb->xram, XRAM_SIZE);
    out += XRAM_SIZE;

    // cart ram
    if (gb->gb_cart_ram_size > 0)
    {
        memcpy(out, gb->gb_cart_ram, gb->gb_cart_ram_size);
        out += gb->gb_cart_ram_size;
    }

    // breakpoints
    memcpy(out, gb->breakpoints, MAX_BREAKPOINTS * sizeof(struct PGB_VERSIONED(gb_breakpoint)));
    out += MAX_BREAKPOINTS * sizeof(struct PGB_VERSIONED(gb_breakpoint));

    // intentionally skipped: lcd; rom

    // TODO: audio
}

FORCE_INLINE const char* PGB_VERSIONED(gb_state_load)(
    struct PGB_VERSIONED(gb_s) * gb, const char* in, size_t size
)
{
    const StateHeader* header = (void*)in;
    in += sizeof(*header);
    if (header->version != PGB_VERSION)
    {
        return "State comes from an incompatible version of CrankBoy.";
    }

    struct PGB_VERSIONED(gb_s)* in_gb = (void*)in;
    in += sizeof(*gb);

    // CGB palette data appended after struct
    const char* in_palettes = in;
    in += 128;

    size_t state_size = PGB_VERSIONED(gb_get_state_size)(in_gb);

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

    memcpy(gb->cgb_bg_palette, in_palettes, 64);
    memcpy(gb->cgb_obj_palette, in_palettes + 64, 64);

    void* preserved_fields[] = {
        &gb->gb_rom,
        &gb->wram,
        &gb->vram,
        &gb->gb_cart_ram,
        &gb->breakpoints,
        &gb->lcd,
        &gb->lcd_alt,
        &gb->direct.priv,
        &gb->gb_error,
        &gb->gb_serial_tx,
        &gb->gb_serial_rx,
        &gb->vram_base,
        &gb->xram,
        &gb->display.bg_map_base,
        &gb->display.window_map_base,
        &gb->ram_base[0],
        &gb->ram_base[1],
        &gb->ram_base[2],
        &gb->ram_base[3],
        &gb->ram_base[4],
        &gb->ram_base[5],
        &gb->ram_base[6],
        &gb->ram_base[7],
        &gb->ram_base[8],
        &gb->ram_base[9],
        &gb->ram_base[10],
        &gb->ram_base[11],
        &gb->ram_base[12],
        &gb->ram_base[13],
        &gb->ram_base[14],
        &gb->ram_base[15],
        &gb->cgb_bg_palette,
        &gb->cgb_obj_palette,
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
    memcpy(gb->wram, in, WRAM_SIZE_CGB);
    in += WRAM_SIZE_CGB;

    // vram
    memcpy(gb->vram, in, VRAM_SIZE_CGB);
    in += VRAM_SIZE_CGB;

    // xram
    memcpy(gb->xram, in, XRAM_SIZE);
    in += XRAM_SIZE;

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
    in += MAX_BREAKPOINTS * sizeof(struct PGB_VERSIONED(gb_breakpoint));

    // IE reads go through the hram mirror; heal states with a stale one
    gb->hram[0xFF] = gb->gb_reg.IE;

    // clear caches and other presentation-layer data
    memset(gb->lcd, 0, LCD_BUFFER_BYTES);

    // intentionally skipped: lcd; rom

    return NULL;
}

char* PGB_VERSIONED(gb_savestate_upgrade_to)(char** out, const char* in);

#ifdef PGB_SAVESTATE_UPGRADE_IMPL

#include "pgb_v5.h"

// in: points to a StateHeader which is followed by the rest of the save state.
// out: points to a StateHeader*, which will point to either:
//   (a) in, or
//   (b) a freshly-malloc'd state header (which must be caller-free'd)

// returns NULL if successful, caller-free'd string otherwise.
// if NULL is returned, state version number MUST be up-to-date.
// if NULL is returned, `out` MUST be non-null.
// if non-NULL is returned, caller needn't free anything.

char* savestate_upgrade_to_v6(char** out, size_t* out_size, char* in, size_t in_size)
{
    const StateHeader* const in_header = (const void*)in;
    if (in_header->version > PGB_VERSION)
    {
        return aprintf("Save state version too high: v%u", (unsigned)in_header->version);
    }
    if (in_header->version == PGB_VERSION)
    {
        *out = in;
        *out_size = in_size;
        return NULL;
    }

    // upgrade to v5 if needed
    char* const org_in = in;
    size_t const org_in_size = in_size;
    if (in_header->version < 5)
    {
        const char* result = savestate_upgrade_to_v5(&in, &in_size, org_in, org_in_size);
        if (result)
            return aprintf("%s", result);
    }
    char* const v5_in = in;

    const StateHeader* const v5_header = (const void*)v5_in;
    const struct gb_s_v5* v5_gb = (const void*)(v5_in + sizeof(StateHeader));

    size_t v6_gb_size = sizeof(struct gb_s_v6);
    size_t extra_sz = in_size - sizeof(StateHeader) - sizeof(struct gb_s_v5);
    char* v6_org = mallocz(sizeof(StateHeader) + v6_gb_size + extra_sz);
    if (!v6_org)
    {
        if (v5_in != org_in)
            cb_free(v5_in);
        return aprintf("Out of memory");
    }

    StateHeader* v6_header = (StateHeader*)v6_org;
    struct gb_s_v6* v6_gb = (struct gb_s_v6*)(v6_org + sizeof(StateHeader));

    // v6 removes the unused zero32 field (was between xram and audio);
    // layout is identical except sweep struct grew (enabled + divider fields)
    set_fields(v6_gb, v5_gb, gb_rom, xram);

    v6_gb->audio.vol_l = v5_gb->audio.vol_l;
    v6_gb->audio.vol_r = v5_gb->audio.vol_r;
    v6_gb->audio.audio_mem = NULL;
    v6_gb->audio.div_apu_step = v5_gb->audio.div_apu_step;
    v6_gb->audio.skip_next_apu_tick = v5_gb->audio.skip_next_apu_tick;
    v6_gb->audio.capacitor_l = v5_gb->audio.capacitor_l;
    v6_gb->audio.capacitor_r = v5_gb->audio.capacitor_r;

    for (int ch = 0; ch < 4; ch++)
    {
        v6_gb->audio.chans[ch].enabled = v5_gb->audio.chans[ch].enabled;
        v6_gb->audio.chans[ch].powered = v5_gb->audio.chans[ch].powered;
        v6_gb->audio.chans[ch].on_left = v5_gb->audio.chans[ch].on_left;
        v6_gb->audio.chans[ch].on_right = v5_gb->audio.chans[ch].on_right;
        v6_gb->audio.chans[ch].muted = v5_gb->audio.chans[ch].muted;
        v6_gb->audio.chans[ch].lfsr_narrow = v5_gb->audio.chans[ch].lfsr_narrow;
        v6_gb->audio.chans[ch].sweep_up = v5_gb->audio.chans[ch].sweep_up;
        v6_gb->audio.chans[ch].len_enabled = v5_gb->audio.chans[ch].len_enabled;
        v6_gb->audio.chans[ch].sample_surpressed = v5_gb->audio.chans[ch].sample_surpressed;
        v6_gb->audio.chans[ch].env_pending = v5_gb->audio.chans[ch].env_pending;
        v6_gb->audio.chans[ch].volume = v5_gb->audio.chans[ch].volume;
        v6_gb->audio.chans[ch].volume_init = v5_gb->audio.chans[ch].volume_init;
        v6_gb->audio.chans[ch].freq = v5_gb->audio.chans[ch].freq;
        v6_gb->audio.chans[ch].freq_counter = v5_gb->audio.chans[ch].freq_counter;
        v6_gb->audio.chans[ch].freq_inc = v5_gb->audio.chans[ch].freq_inc;
        v6_gb->audio.chans[ch].val = v5_gb->audio.chans[ch].val;

        v6_gb->audio.chans[ch].len.load = v5_gb->audio.chans[ch].len.load;
        v6_gb->audio.chans[ch].len.counter = v5_gb->audio.chans[ch].len.counter;
        v6_gb->audio.chans[ch].len.inc = v5_gb->audio.chans[ch].len.inc;

        v6_gb->audio.chans[ch].env.step = v5_gb->audio.chans[ch].env.step;
        v6_gb->audio.chans[ch].env.up = v5_gb->audio.chans[ch].env.up;
        v6_gb->audio.chans[ch].env.locked = v5_gb->audio.chans[ch].env.locked;
        v6_gb->audio.chans[ch].env.should_lock = false;
        v6_gb->audio.chans[ch].env.clock = false;
        v6_gb->audio.chans[ch].env.counter = v5_gb->audio.chans[ch].env.counter;
        v6_gb->audio.chans[ch].env.inc = v5_gb->audio.chans[ch].env.inc;

        v6_gb->audio.chans[ch].sweep.freq = v5_gb->audio.chans[ch].sweep.freq;
        v6_gb->audio.chans[ch].sweep.rate = v5_gb->audio.chans[ch].sweep.rate;
        v6_gb->audio.chans[ch].sweep.shift = v5_gb->audio.chans[ch].sweep.shift;
        v6_gb->audio.chans[ch].sweep.did_subtract = v5_gb->audio.chans[ch].sweep.did_subtract;
        v6_gb->audio.chans[ch].sweep.counter = v5_gb->audio.chans[ch].sweep.counter;
        v6_gb->audio.chans[ch].sweep.inc = v5_gb->audio.chans[ch].sweep.inc;
        v6_gb->audio.chans[ch].sweep.enabled = false;
        v6_gb->audio.chans[ch].sweep.divider = 0;

        memcpy(
            &v6_gb->audio.chans[ch].noise, &v5_gb->audio.chans[ch].noise,
            sizeof(v5_gb->audio.chans[ch].noise)
        );
        if (ch == 2)
            v6_gb->audio.chans[ch].wave.pulsed = false;

        v6_gb->audio.chans[ch].envelope_smooth = v5_gb->audio.chans[ch].envelope_smooth;
        v6_gb->audio.chans[ch].env_divider = v5_gb->audio.chans[ch].env_divider;
    }

    v6_gb->audio.apu_event_count = 0;
    v6_gb->audio.pre_frame_valid = false;

    memcpy(v6_header, v5_header, sizeof(StateHeader));
    v6_header->version = PGB_VERSION;
    v6_header->gb_s_size = v6_gb_size;

    if (extra_sz)
        memcpy(
            v6_org + sizeof(StateHeader) + v6_gb_size,
            v5_in + sizeof(StateHeader) + sizeof(struct gb_s_v5), extra_sz
        );

    *out_size = sizeof(StateHeader) + v6_gb_size + extra_sz;
    *out = v6_org;
    if (v5_in != org_in)
        cb_free(v5_in);
    return NULL;
}

#endif

#pragma pop_macro("PGB_VERSION")
