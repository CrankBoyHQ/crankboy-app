#include "pgb_common.h"

// Compatability version (for save state upgrading)
#define PGB_VERSION 1

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

    uint8_t tima_overflow_delay : 1;
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

    // shortcut to swappable bank (addr - 0x4000 offset built in)
    uint8_t* selected_bank_addr;

    struct
    {
        uint8_t gb_halt : 1;
        uint8_t gb_ime : 1;
        uint8_t gb_bios_enable : 1; // (deprecated)

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
        struct PGB_VERSIONED(cpu_registers_s) cpu_reg;
        uint8_t cpu_reg_raw[12];
        uint16_t cpu_reg_raw16[6];
    };
    struct PGB_VERSIONED(gb_registers_s) gb_reg;
    struct PGB_VERSIONED(count_s) counter;

    /* TODO: Allow implementation to allocate WRAM, VRAM and Frame Buffer. */
    uint8_t* wram;  // wram[WRAM_SIZE];
    uint8_t* vram;  // vram[VRAM_SIZE];
    uint8_t hram[HRAM_SIZE];
    uint8_t oam[OAM_SIZE];
    uint8_t* lcd;

    struct
    {
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

    struct PGB_VERSIONED(gb_breakpoint) * breakpoints;

    size_t gb_rom_size;
    uint8_t* gb_boot_rom; // deprecated

    // NOTE: this MUST be the last member of gb_s.
    // sometimes we perform memory operations on the whole gb struct except for
    // audio.
    audio_data audio;
};

FORCE_INLINE uint32_t PGB_VERSIONED(gb_get_state_size)(const struct PGB_VERSIONED(gb_s) * gb)
{
    return sizeof(struct StateHeader) + sizeof(*gb) + ROM_HEADER_SIZE  // for safe-keeping
           + WRAM_SIZE + VRAM_SIZE + XRAM_SIZE + gb->gb_cart_ram_size +
           MAX_BREAKPOINTS * sizeof(struct PGB_VERSIONED(gb_breakpoint));

    // skipped: lcd; rom
}

char* PGB_VERSIONED(gb_savestate_upgrade_to)(char** out, const char* in);

#ifdef PGB_SAVESTATE_UPGRADE_IMPL

char* savestate_upgrade_to_v1(char** out, size_t* out_size, char* in, size_t in_size)
{
    const StateHeader* const in_header = (const void*)in;
    if (in_header->version > PGB_VERSION)
    {
        return aprintf("Save state version too high: v%u", (unsigned)in_header->version);
    }

    // Note: v1 and v0 are actually identical.
    *out = in;
    return NULL;
}

#endif

#pragma pop_macro("PGB_VERSION")