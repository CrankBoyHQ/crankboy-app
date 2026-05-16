#include "pgb_common.h"

// To edit the structs in this file, please make a wholesale
// copy of this file instead of editing it directly.
// Bump PGB_VERSION and replace savestate_upgrade_to_*

// Compatability version (for save state upgrading).
#define PGB_VERSION 4

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
    uint32_t counter;
    uint32_t inc;
};

struct PGB_VERSIONED(chan_freq_sweep)
{
    uint16_t freq;
    uint8_t rate;
    uint8_t shift;
    uint32_t counter;
    uint32_t inc;
};

struct PGB_VERSIONED(chan)
{
    unsigned enabled : 1;
    unsigned powered : 1;
    unsigned on_left : 1;
    unsigned on_right : 1;
    unsigned muted : 1;
    uint8_t lfsr_wide : 1;
    unsigned sweep_up : 1;
    unsigned len_enabled : 1;

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
        } wave;
    };
};

struct PGB_VERSIONED(audio_data)
{
    int vol_l : 4;
    int vol_r : 4;
    uint8_t* audio_mem;
    struct PGB_VERSIONED(chan) chans[4];

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
        uint8_t lcd_blank : 1;
        uint8_t lcd_master_enable : 1;
    };

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

    uint8_t cgb_ff7x[3];
    uint16_t cgb_hdma_src;
    uint16_t cgb_hdma_dst;
    uint16_t cgb_hdma_len : 7;
    bool cgb_hdma_active : 1;

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
        uint8_t sram_updated : 1;
        uint8_t sram_dirty : 1;
        uint8_t crank_docked : 1;
        uint8_t joypad_interrupts : 1;
        uint8_t enable_xram : 1;
        uint8_t ignore_cgb_check : 1;
        uint8_t stat_line : 1;
        uint8_t has_read_accelerometer_this_frame : 1;
        uint8_t* oam_ghost_buffer;
        uint8_t blend_rect_x_min;
        uint8_t blend_rect_y_min;
        uint8_t blend_rect_x_max;
        uint8_t blend_rect_y_max;

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
        // (in actual usage, this points to a CB_GameSceneContext*)
        void* priv;
    } direct;

    uint32_t gb_cart_ram_size;

    struct PGB_VERSIONED(gb_breakpoint) * breakpoints;

    size_t gb_rom_size;

    // extended ram feature offered by crankboy
    uint8_t* xram;

    // always 32 zero bytes. Useful hack to implement CGB LCDC priority
    // bit, but can be used for other things
    // (so long as nothing writes anything non-zero here.)
    uint32_t zero32[5];

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
           MAX_BREAKPOINTS * sizeof(struct PGB_VERSIONED(gb_breakpoint));

    // skipped: lcd; rom
}

FORCE_INLINE void PGB_VERSIONED(gb_state_save)(struct PGB_VERSIONED(gb_s) * gb, char* out)
{
    // gb
    memcpy(out, gb, sizeof(*gb));
    out += sizeof(*gb);

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

    void* preserved_fields[] = {
        &gb->gb_rom,
        &gb->wram,
        &gb->vram,
        &gb->gb_cart_ram,
        &gb->breakpoints,
        &gb->direct.oam_ghost_buffer,
        &gb->lcd,
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

    // clear caches and other presentation-layer data
    memset(gb->lcd, 0, LCD_BUFFER_BYTES);

    // intentionally skipped: lcd; rom

    return NULL;
}

char* PGB_VERSIONED(gb_savestate_upgrade_to)(char** out, const char* in);

#ifdef PGB_SAVESTATE_UPGRADE_IMPL

#include "pgb_v3.h"

// in: points to a StateHeader which is followed by the rest of the save state.
// out: points to a StateHeader*, which will point to either:
//   (a) in, or
//   (b) a freshly-malloc'd state header (which must be caller-free'd)

// returns NULL if successful, caller-free'd string otherwise.
// if NULL is returned, state version number MUST be up-to-date.
// if NULL is returned, `out` MUST be non-null.
// if non-NULL is returned, caller needn't free anything.

char* savestate_upgrade_to_v4(char** out, size_t* out_size, char* in, size_t in_size)
{
    const StateHeader* const in_header = (const void*)in;
    if (in_header->version > PGB_VERSION)
    {
        return aprintf("Save state version too high: v%u", (unsigned)in_header->version);
    }
    if (in_header->version == PGB_VERSION)
    {
        *out = in;
        return NULL;
    }

    char* const org_in = in;
    size_t const org_in_size = in_size;
    if (in_header->version < 3)
    {
        const char* result = savestate_upgrade_to_v3(&in, &in_size, org_in, org_in_size);
        if (result)
            return aprintf("%s", result);
    }
    char* const v3_in = in;

#define DEFINE(type, name, src)    \
    type* const name = (void*)src; \
    src += sizeof(type);

    DEFINE(const StateHeader, v3_header, in);
    DEFINE(const struct gb_s_v3, v3_gb, in);

    char* v4_org = mallocz(sizeof(StateHeader) + sizeof(struct gb_s_v4));
    if (!v4_org)
    {
        if (v3_in != org_in)
            cb_free(v3_in);
        return aprintf("Out of memory");
    }
    char* v4 = v4_org;

    DEFINE(StateHeader, v4_header, v4);
    DEFINE(struct gb_s_v4, v4_gb, v4);

    memcpy(v4_header, v3_header, sizeof(StateHeader));
    v4_header->version = PGB_VERSION;
    v4_header->gb_s_size = sizeof(struct PGB_VERSIONED(gb_s));

    // Pointer fields are restored from the live gb_s by preserved_fields in
    // gb_state_load, so only data fields are migrated here.

    // gb_hle was inserted mid-bitfield, shifting subsequent bits.
    set_field(v4_gb, v3_gb, gb_halt);
    set_field(v4_gb, v3_gb, gb_stop);
    v4_gb->gb_hle = 0;
    set_field(v4_gb, v3_gb, gb_ime);
    set_field(v4_gb, v3_gb, gb_ime_countdown);
    set_field(v4_gb, v3_gb, is_cgb_mode);
    set_field(v4_gb, v3_gb, gb_frame);
    set_field(v4_gb, v3_gb, lcd_mode);
    set_field(v4_gb, v3_gb, lcd_blank);
    set_field(v4_gb, v3_gb, lcd_master_enable);

    // cgb_fast_mode_active was appended at the end of the CGB bitfield group;
    // v3's padding bit is 0 so the memcpy yields the correct default.
    set_fields(v4_gb, v3_gb, zero_bank_base, printer_last_cmd);
    set_fields(v4_gb, v3_gb, num_rom_banks_mask, counter);
    set_fields(v4_gb, v3_gb, hram, audio);

    *out_size = gb_get_state_size_v4(v4_gb);
    char* v4_realloc = cb_realloc(v4_org, *out_size);
    if (!v4_realloc)
    {
        if (v3_in != org_in)
            cb_free(v3_in);
        cb_free(v4_org);
        return aprintf("Out of memory");
    }
    v4_org = v4_realloc;

    memcpy(
        v4_org + sizeof(StateHeader) + sizeof(struct gb_s_v4),
        v3_in + sizeof(StateHeader) + sizeof(struct gb_s_v3),
        ROM_HEADER_SIZE + WRAM_SIZE_CGB + VRAM_SIZE_CGB + XRAM_SIZE + v3_gb->gb_cart_ram_size +
            MAX_BREAKPOINTS * sizeof(struct PGB_VERSIONED(gb_breakpoint))
    );

    *out = v4_org;
    if (v3_in != org_in)
        cb_free(v3_in);
    return NULL;

#undef DEFINE
}

#endif

#pragma pop_macro("PGB_VERSION")
