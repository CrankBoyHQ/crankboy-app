/*
 * This file is templated between multiple systems (__dmg and __cgb).
 * This allows cgb behavior to be implemented with ~zero cost to dmg.
 *
 * These functions are known as "core" functions, and will be copied to ITCM
 * if ITCM acceleration is enabled. __core functions can only
 * safely call __core, __shell, or FORCE_INLINE functions.
 *
 * ITCM is a small, fast region of memory. Small functions that are
 * called very frequently -- many times per frame -- should be placed
 * in ITCM (i.e. __core). Functions which are not called often, but are
 * called from a __core function, should be desginated as __shell functions.
 *
 * Although it's not good practice, some of these functions are
 * called from outside of the core. If the __dmg and __cgb
 * implementations are the same, or the __cgb implementation is a
 * superset of the __dmg implementation, then the __cgb implementation
 * should be called. Otherwise, the caller should choose either the __dmg
 * or __cgb implementation based on gb->is_cgb_mode.
 */

#ifndef PGB_TEMPLATE
#error "PGB_TEMPLATE must be defined"
#endif

#include "../src/app.h"
#include "../src/preferences.h"
#include "../src/utility.h"

/**
 * Checks all STAT interrupt sources and requests an interrupt on a rising edge.
 */
__draw static void $(__gb_update_stat_irq)(gb_s* gb)
{
    /* No STAT interrupts can occur when the LCD is off. */
    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
    {
        gb->direct.stat_line = 0;
        return;
    }

    bool line_is_high =
        ((gb->gb_reg.STAT & STAT_MODE_0_INTR) && (gb->lcd_mode == LCD_HBLANK)) ||
        ((gb->gb_reg.STAT & STAT_MODE_1_INTR) && (gb->lcd_mode == LCD_VBLANK)) ||
        ((gb->gb_reg.STAT & STAT_MODE_2_INTR) && (gb->lcd_mode == LCD_SEARCH_OAM)) ||
        ((gb->gb_reg.STAT & STAT_LYC_INTR) && (gb->gb_reg.STAT & STAT_LYC_COINC));

    /* On a rising edge (from low to high), request the interrupt. */
    if (!gb->direct.stat_line && line_is_high)
    {
        gb->gb_reg.IF |= LCDC_INTR;
    }

    gb->direct.stat_line = line_is_high;
}

/**
 * Internal function to check for LY=LYC coincidence and update STAT.
 * Note: this is used outside of core.
 */
__draw static void $(__gb_check_lyc)(gb_s* gb)
{
    if (gb->gb_reg.LY == gb->gb_reg.LYC)
    {
        gb->gb_reg.STAT |= STAT_LYC_COINC;
    }
    else
    {
        gb->gb_reg.STAT &= ~STAT_LYC_COINC;
    }
}

__draw static void $(__gb_update_lyc_and_stat_irq)(gb_s* gb)
{
    if (gb->gb_reg.LY == gb->gb_reg.LYC)
        gb->gb_reg.STAT |= STAT_LYC_COINC;
    else
        gb->gb_reg.STAT &= ~STAT_LYC_COINC;

    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
    {
        gb->direct.stat_line = 0;
        return;
    }

    bool line_is_high =
        ((gb->gb_reg.STAT & STAT_MODE_0_INTR) && (gb->lcd_mode == LCD_HBLANK)) ||
        ((gb->gb_reg.STAT & STAT_MODE_1_INTR) && (gb->lcd_mode == LCD_VBLANK)) ||
        ((gb->gb_reg.STAT & STAT_MODE_2_INTR) && (gb->lcd_mode == LCD_SEARCH_OAM)) ||
        ((gb->gb_reg.STAT & STAT_LYC_INTR) && (gb->gb_reg.STAT & STAT_LYC_COINC));

    if (!gb->direct.stat_line && line_is_high)
        gb->gb_reg.IF |= LCDC_INTR;

    gb->direct.stat_line = line_is_high;
}

__core_section("short") static uint8_t $(__gb_read)(gb_s* gb, const uint16_t addr)
{
    uint8_t* ram_region_base = gb->ram_base[addr >> 12];
    if (ram_region_base)
    {
        return ram_region_base[addr];
    }
    if likely (addr >= 0xFF80)  // no need to check upper bound -- gb->hram[0xFF] should match IE
    {
        uint8_t val = gb->hram[addr % 0x100];
        if (gb->hle_enabled)
        {
            // HLE: games wait on HRAM flags set by ISRs ("ldh a,(x); and a;
            // jr z"). Pre-filter to ldh-imm reads from ROM before paying
            // for the full loop check.
            uint16_t pc = gb->cpu_reg.pc;
            if (pc >= 2 && pc < 0x8000 && gb->ram_base[pc >> 12][pc - 2] == 0xF0 &&
                gb->ram_base[pc >> 12][pc - 1] == (uint8_t)addr)
                return __gb_try_hle(gb, addr, val);
        }
        return val;
    }
    if likely (addr >= 0xA000 && addr < 0xC000 && gb->selected_cart_bank_addr)
    {
        return gb->selected_cart_bank_addr[addr];
    }

    // Hot IO reads: bypass read_full in polling loops. HLE-gated regs
    // (STAT/LY/IF) keep the full path when HLE is active (CGB only).
    if (addr >= 0xFF00 && addr < 0xFF80)
    {
        switch (addr)
        {
        case 0x41:
            if (!gb->hle_enabled)
                return __gb_read_stat_synced(gb);
            break;
        case 0x44:
            if (!gb->hle_enabled)
                return __gb_read_ly_synced(gb);
            break;
        case 0x0F:
            if (!gb->hle_enabled)
                return gb->gb_reg.IF;
            break;
        case 0x04:
            return gb->gb_reg.DIV;
        case 0x05:
            return gb->gb_reg.tima_overflow_delay ? gb->gb_reg.TMA : gb->gb_reg.TIMA;
        case 0x45:
            return gb->gb_reg.LYC;
        }
    }
    return __gb_read_full(gb, addr);
}

__core_section("short") static void $(__gb_write)(gb_s* restrict gb, const uint16_t addr, uint8_t v)
{
    if likely (addr >= 0xC000 && addr < 0xF000)
    {
        gb->ram_base[addr >> 12][addr] = v;
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
        return;
    }
    __gb_write_full(gb, addr, v);
}

__core_section("short") static uint16_t $(__gb_read16)(gb_s* restrict gb, u16 addr)
{
    if (addr % 0x1000 != 0xFFF)
    {
        // Fast path for ROM+WRAM+ECHO
        uint8_t* ram_region_base = gb->ram_base[addr >> 12];
        if (ram_region_base)
        {
            void* ptr = &ram_region_base[addr];
            return *(uint16_t*)ptr;
        }
        // Fast path for HRAM
        else if (addr >= HRAM_ADDR && addr < (INTR_EN_ADDR - 1))
        {
            void* ptr = &gb->hram[addr - IO_ADDR];
            return *(uint16_t*)ptr;
        }
    }

    // Fallback for all other cases (unaligned, I/O, etc.)
    u16 v = $(__gb_read)(gb, addr);
    v |= (u16)$(__gb_read)(gb, addr + 1) << 8;
    return v;
}

__core_section("short") static uint32_t $(__gb_read32)(gb_s* restrict gb, u16 addr)
{
    if ((addr & 0xFFF) <= 0xFFC)
    {
        uint8_t* ram_region_base = gb->ram_base[addr >> 12];
        if (ram_region_base)
        {
            void* ptr = &ram_region_base[addr];
            return *(uint32_t*)ptr;
        }
    }

    uint32_t v = $(__gb_read)(gb, addr);
    v |= (uint32_t)$(__gb_read)(gb, addr + 1) << 8;
    v |= (uint32_t)$(__gb_read)(gb, addr + 2) << 16;
    v |= (uint32_t)$(__gb_read)(gb, addr + 3) << 24;
    return v;
}

__core_section("short") static void $(__gb_write16)(gb_s* restrict gb, u16 addr, u16 v)
{
    // Fast path for WRAM
    if likely (
        addr >= WRAM_0_ADDR && addr < 0xE000 - 1
#if PGB_IS_CGB
        && addr != 0xCFFF
#endif
    )
    {
        void* ptr = &gb->ram_base[addr >> 12][addr];
        *(uint16_t*)ptr = v;
        return;
    }
    // Fast path for HRAM
    else if likely (addr >= HRAM_ADDR && addr < (INTR_EN_ADDR - 1))
    {
        void* ptr = &gb->hram[addr - IO_ADDR];
        *(uint16_t*)ptr = v;
        return;
    }

    // Fallback for other memory regions
    $(__gb_write)(gb, addr, v & 0xFF);
    $(__gb_write)(gb, addr + 1, v >> 8);
}

__core_section("short") static uint8_t $(__gb_fetch8)(gb_s* restrict gb)
{
    u16 addr = gb->cpu_reg.pc++;
    uint8_t* fetch_base = gb->ram_base[addr >> 12];
    if likely (fetch_base)
        return fetch_base[addr];
    return $(__gb_read)(gb, addr);
}

__core_section("short") static uint16_t $(__gb_fetch16)(gb_s* restrict gb)
{
    u16 addr = gb->cpu_reg.pc;

    uint8_t* rom_ptr;
    if likely (addr < 0x7FFF && addr != 0x3FFF)
    {
        rom_ptr = &gb->ram_base[addr >> 12][addr];
    }
    else
    {
        gb->cpu_reg.pc += 2;
        return $(__gb_read16)(gb, addr);
    }

    gb->cpu_reg.pc += 2;
    return *(uint16_t*)rom_ptr;
}

__core_section("short") static uint16_t $(__gb_pop16)(gb_s* restrict gb)
{
    u16 v;
    if likely (gb->cpu_reg.sp >= HRAM_ADDR && gb->cpu_reg.sp < 0xFFFE)
    {
        v = gb->hram[gb->cpu_reg.sp - IO_ADDR];
        v |= gb->hram[gb->cpu_reg.sp - IO_ADDR + 1] << 8;
    }
    else
    {
        v = $(__gb_read16)(gb, gb->cpu_reg.sp);
    }
    gb->cpu_reg.sp += 2;
    return v;
}

__core_section("short") static void $(__gb_push16)(gb_s* restrict gb, u16 v)
{
    if likely (gb->cpu_reg.sp >= HRAM_ADDR + 2)
    {
        gb->cpu_reg.sp--;
        gb->hram[gb->cpu_reg.sp - IO_ADDR] = v >> 8;

        gb->cpu_reg.sp--;
        gb->hram[gb->cpu_reg.sp - IO_ADDR] = v & 0xFF;
    }
    else
    {
        gb->cpu_reg.sp--;
        $(__gb_write)(gb, gb->cpu_reg.sp, v >> 8);

        gb->cpu_reg.sp--;
        $(__gb_write)(gb, gb->cpu_reg.sp, v & 0xFF);
    }
}

__core static uint8_t $(__gb_execute_cb)(gb_s* gb)
{
    uint8_t inst_cycles;
    uint8_t cbop = $(__gb_fetch8)(gb);
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
        val = $(__gb_read)(gb, gb->cpu_reg.hl);
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
            $(__gb_write)(gb, gb->cpu_reg.hl, val);
        }
        else
        {
            gb->cpu_reg_raw[r] = val;
        }
    }
    return inst_cycles;
}

static inline __attribute__((always_inline)) void $(__gb_draw_pixel)(uint8_t* line, u8 x, u8 v)
{
    u8* pix = line + x / LCD_PACKING;
    x = (x % LCD_PACKING) * (8 / LCD_PACKING);
    *pix &= ~(((1 << LCD_BITS_PER_PIXEL) - 1) << x);
    *pix |= (v & 3) << x;
}

static inline __attribute__((always_inline)) u8 $(__gb_get_pixel)(uint8_t* line, u8 x)
{
    u8* pix = line + x / LCD_PACKING;
    x = (x % LCD_PACKING) * LCD_BITS_PER_PIXEL;
    return (*pix >> x) % (1 << LCD_BITS_PER_PIXEL);
}

static inline int $(compare_sprites)(
    const struct sprite_data* const sd1, const struct sprite_data* const sd2
)
{
#if PGB_IS_CGB
    return (int)sd1->sprite_number - (int)sd2->sprite_number;
#else
    int x_res = (int)sd1->x - (int)sd2->x;
    if (x_res != 0)
        return x_res;

    return (int)sd1->sprite_number - (int)sd2->sprite_number;
#endif
}

__draw static void $(__gb_draw_line_sprites)(
    gb_s* restrict gb, const uint8_t* oam_src, const uint32_t* line_priority,
#if PGB_IS_CGB
    const uint32_t* line_cgb_priority, bool cgb_master_priority,
#endif
    uint8_t* pixels, uint8_t* pixels_alt
)
{
    uint8_t number_of_sprites = 0;
    struct sprite_data sprites_to_render[MAX_SPRITES_LINE];

    /* Find up to 10 sprites on this line, sorted by priority.
     * CGB: lower OAM index has higher priority.
     * DMG: lower X-coordinate has higher priority. If X is the same,
     * lower OAM index has higher priority. */

    // Gather all visible sprites for this scanline (LY).
    const uint8_t sprite_height = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE) ? 16 : 8;
    const int16_t current_ly = gb->gb_reg.LY;

    for (uint8_t s = 0; s < NUM_SPRITES && number_of_sprites < MAX_SPRITES_LINE; s++)
    {
        const uint8_t* oam = &oam_src[s * 4];
        const uint8_t oam_y = oam[0];
        const uint8_t oam_x = oam[1];

        if ((current_ly + 16 >= oam_y) && (current_ly + 16 < oam_y + sprite_height))
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
            while (j >= 0 && $(compare_sprites)(&sprites_to_render[j], &key) > 0)
            {
                sprites_to_render[j + 1] = sprites_to_render[j];
                j = j - 1;
            }
            sprites_to_render[j + 1] = key;
        }
    }

    const uint16_t OBP = gb->gb_reg.OBP0 | ((uint16_t)gb->gb_reg.OBP1 << 8);

    uint8_t column_decided[LCD_WIDTH];
    for (int x = 0; x < LCD_WIDTH; x++)
        column_decided[x] = 0;

    for (int8_t i = 0; i < number_of_sprites; i++)
    {
        uint8_t s_idx = sprites_to_render[i].sprite_number;
        uint8_t s_4 = s_idx * 4;

        uint8_t OY = oam_src[s_4 + 0];
        uint8_t OX = oam_src[s_4 + 1];

        if (OX == 0 || OX >= 168)
            continue;

        uint8_t OT = oam_src[s_4 + 2] & (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0xFE : 0xFF);
        uint8_t OF = oam_src[s_4 + 3];

        unsigned bank = 0;
#if PGB_IS_CGB
        if (OF & OBJ_CGB_BANK)
            bank = VRAM_SIZE;
#endif

        uint8_t py = gb->gb_reg.LY - (OY - 16);
        if (OF & OBJ_FLIP_Y)
            py = (sprite_height - 1) - py;

        uint16_t t1_i = bank + VRAM_TILES_1 + OT * 0x10 + 2 * py;
        uint8_t t1 = gb->vram[t1_i];
        uint8_t t2 = gb->vram[t1_i + 1];
        uint8_t t1_r = reverse_bits_u8(t1);
        uint8_t t2_r = reverse_bits_u8(t2);

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

#if PGB_IS_CGB
        uint8_t cgb_obj_pal = gb->cgb_obj_palette_gray[OF & OBJ_CGB_PALETTE];
#endif
        uint8_t c_add = (OF & OBJ_PALETTE) ? 4 : 0;

        for (int disp_x = start; disp_x != end; disp_x += dir)
        {
            if unlikely (disp_x < 0 || disp_x >= LCD_WIDTH)
                goto next_sprite_pixel;

            if (column_decided[disp_x])
                goto next_sprite_pixel;

            uint8_t c = (t2_r & 1) << 1 | (t1_r & 1);
            if (c != 0)
            {
                column_decided[disp_x] = 1;

                int P_segment_index = (unsigned)disp_x >> 5;
                int P_bit_in_segment = disp_x & 31;
#if PGB_IS_CGB
                uint8_t bg_cgb_priority = 0;
                if (cgb_master_priority)
                    bg_cgb_priority = ~(line_cgb_priority[P_segment_index] >> P_bit_in_segment) & 1;
                if (!(bg_cgb_priority ||
                      (cgb_master_priority && (OF & OBJ_PRIORITY) &&
                       !((line_priority[P_segment_index] >> P_bit_in_segment) & 1))))
#else
                uint8_t bg_is_transparent =
                    (line_priority[P_segment_index] >> P_bit_in_segment) & 1;
                if (!((OF & OBJ_PRIORITY) && !bg_is_transparent))
#endif
                {
#if PGB_IS_CGB
                    uint8_t color_value;
                    if (pgb_blend_merged)
                    {
                        // merged blend: pre-averaged pal, dither phase by (x ^ y)
                        color_value = (pgb_obj_blend_pal[(disp_x ^ gb->gb_reg.LY) & 1]
                                                        [OF & OBJ_CGB_PALETTE] >>
                                       (c * 2)) &
                                      3;
                    }
                    else
                    {
                        color_value = (cgb_obj_pal >> (c * 2)) & 3;
                    }
#else
                    uint8_t color_value = (OBP >> (c * 2 + c_add * 2)) & 3;
#endif
                    $(__gb_draw_pixel)(pixels, disp_x, color_value);
#if PGB_IS_CGB
                    if (pixels_alt)
                    {
                        uint8_t obj_pal_dark = gb->cgb_obj_palette_gray_alt[OF & OBJ_CGB_PALETTE];
                        uint8_t col_dark = (obj_pal_dark >> (c * 2)) & 3;
                        $(__gb_draw_pixel)(pixels_alt, disp_x, col_dark);
                    }
#endif
                }
            }
        next_sprite_pixel:
            t1_r >>= 1;
            t2_r >>= 1;
        }
    }
}

#if PGB_IS_CGB
static inline __attribute__((always_inline)) uint16_t
__cgb_remap_tile(uint8_t lo_plane, uint8_t hi_plane, const uint8_t* restrict lut)
{
    uint8_t idx_lo = (lo_plane & 0x0F) | ((hi_plane & 0x0F) << 4);
    uint8_t idx_hi = (lo_plane >> 4) | (hi_plane & 0xF0);
    return ((uint16_t)lut[idx_hi] << 8) | lut[idx_lo];
}

static inline __attribute__((always_inline)) void __cgb_merge_tiles(
    uint16_t tile_data_lo, uint16_t tile_data_hi, uint16_t pre_remapped_lo, bool has_pre_remapped,
    const uint8_t* restrict lut_lo, const uint8_t* restrict lut_hi, int subx,
    uint16_t* restrict out, uint8_t* restrict pri, uint16_t* restrict out_rm_hi,
    uint8_t* restrict out_pri_hi
)
{
    uint8_t lo_p = (uint8_t)tile_data_lo;
    uint8_t hi_p = (uint8_t)(tile_data_lo >> 8);
    uint8_t lo_hp = (uint8_t)tile_data_hi;
    uint8_t hi_hp = (uint8_t)(tile_data_hi >> 8);

    uint16_t rm_hi = __cgb_remap_tile(lo_hp, hi_hp, lut_hi);
    *out_rm_hi = rm_hi;
    *out_pri_hi = lo_hp | hi_hp;

    uint16_t rm_lo = has_pre_remapped ? pre_remapped_lo : __cgb_remap_tile(lo_p, hi_p, lut_lo);

    if (subx == 0)
    {
        *out = rm_lo;
        *pri = lo_p | hi_p;
        return;
    }

    *out = (rm_lo >> (subx * 2)) | (rm_hi << (16 - subx * 2));
    *pri = (uint8_t)((lo_p | hi_p) >> subx) | (uint8_t)((lo_hp | hi_hp) << (8 - subx));
}

static inline __attribute__((always_inline)) void __cgb_write_dark(
    uint16_t tile_hi, uint16_t rm_lo_dark, const uint8_t* restrict lut_hi_dark, int subx,
    uint8_t* restrict pixels_alt, int x, uint16_t* restrict rm_hi_dark_out
)
{
    uint8_t lo_h = (uint8_t)tile_hi;
    uint8_t hi_h = (uint8_t)(tile_hi >> 8);
    uint16_t rm_hi_dark = __cgb_remap_tile(lo_h, hi_h, lut_hi_dark);
    uint16_t* out = (uint16_t*)(pixels_alt + x * 2);
    *out = (subx == 0) ? rm_lo_dark : (rm_lo_dark >> (subx * 2)) | (rm_hi_dark << (16 - subx * 2));
    *rm_hi_dark_out = rm_hi_dark;
}

static inline __attribute__((always_inline)) uint16_t __cgb_fetch_tile(
    uint8_t* restrict tile_map, uint8_t* restrict attr_map, uint16_t* restrict reg_data,
    uint16_t* restrict flip_data, uint8_t map_idx, int tiledata_offset, uint8_t* out_palette
)
{
    uint8_t tile_idx = tile_map[map_idx];
    uint8_t attrs = attr_map[map_idx];
    unsigned bank = (attrs & BG_MAP_ATTR_BANK) ? VRAM_SIZE / sizeof(uint16_t) : 0;
    uint16_t* data = (attrs & BG_MAP_ATTR_Y_FLIP) ? flip_data : reg_data;
    uint16_t tile = data[bank | (tile_idx < 0x80 ? tiledata_offset : 0) | (8 * (unsigned)tile_idx)];
    *out_palette = attrs & (BG_MAP_ATTR_PALETTE | BG_MAP_ATTR_PRIORITY);
    return reverse_bits_in_each_byte_conditional_u16(tile, !!(attrs & BG_MAP_ATTR_X_FLIP));
}
#endif

#if PGB_IS_CGB
#define CGB_LUT(gb, pal_idx) ((gb)->cgb_bg_palette + 64 + ((pal_idx) & BG_MAP_ATTR_PALETTE) * 256)
#define CGB_LUT_DARK(gb, pal_idx) \
    ((gb)->cgb_bg_palette + 64 + 8 * 256 + ((pal_idx) & BG_MAP_ATTR_PALETTE) * 256)
// Merged pre-blended LUTs (slots 16-23 even lines, 24-31 odd lines)
#define CGB_LUT_BLEND(gb, pal_idx, par) \
    ((gb)->cgb_bg_palette + 64 + (16 + ((par) & 1) * 8 + ((pal_idx) & BG_MAP_ATTR_PALETTE)) * 256)

static inline __attribute__((always_inline)) void __cgb_draw_tile_strip(
    gb_s* restrict gb, uint8_t* restrict tile_map, uint8_t* restrict attr_map,
    uint16_t* restrict tile_data_y, uint16_t* restrict tile_data_y_flipped, int tiledata_offset,
    int map_x_offset, int start_x, int end_x, int subx, int merge_subx, uint8_t* restrict pixels,
    uint8_t* restrict pixels_alt, uint32_t* restrict line_priority,
    uint32_t* restrict line_cgb_priority, bool apply_bgmask
)
{
    uint8_t tile_palette_lo;
    uint16_t vram_tile_data_hi = __cgb_fetch_tile(
        tile_map, attr_map, tile_data_y, tile_data_y_flipped, (map_x_offset + start_x) % 32,
        tiledata_offset, &tile_palette_lo
    );

    if (apply_bgmask)
    {
        vram_tile_data_hi &= (0xFFFF) << subx;
        vram_tile_data_hi &= 0xFF | ((0xFF00) << subx);
    }

    const uint8_t* lut_lo = pgb_blend_merged ? CGB_LUT_BLEND(gb, tile_palette_lo, gb->gb_reg.LY)
                                             : CGB_LUT(gb, tile_palette_lo);
    uint8_t lo_p = (uint8_t)vram_tile_data_hi;
    uint8_t hi_p = (uint8_t)(vram_tile_data_hi >> 8);
    uint16_t rm_lo = __cgb_remap_tile(lo_p, hi_p, lut_lo);

    const uint8_t* lut_lo_dark = NULL;
    uint16_t rm_lo_dark = 0;
    if (pixels_alt)
    {
        lut_lo_dark = CGB_LUT_DARK(gb, tile_palette_lo);
        rm_lo_dark = __cgb_remap_tile(lo_p, hi_p, lut_lo_dark);
    }

    uint32_t bgmask = 0;
    if (apply_bgmask && subx != 0)
        bgmask = 0xFFu >> subx;

    for (int x = start_x; x < end_x; ++x)
    {
        uint16_t vram_tile_data_lo = vram_tile_data_hi;

        uint8_t tile_palette_hi_val;
        vram_tile_data_hi = __cgb_fetch_tile(
            tile_map, attr_map, tile_data_y, tile_data_y_flipped, (map_x_offset + x + 1) % 32,
            tiledata_offset, &tile_palette_hi_val
        );

        uint8_t pri_lo = tile_palette_lo & BG_MAP_ATTR_PRIORITY;
        uint8_t pri_hi = tile_palette_hi_val & BG_MAP_ATTR_PRIORITY;

        const uint8_t* lut_hi = pgb_blend_merged
                                    ? CGB_LUT_BLEND(gb, tile_palette_hi_val, gb->gb_reg.LY)
                                    : CGB_LUT(gb, tile_palette_hi_val);
        const uint8_t* lut_hi_dark = NULL;
        if (pixels_alt)
            lut_hi_dark = CGB_LUT_DARK(gb, tile_palette_hi_val);
        uint8_t pri;
        uint16_t rm_hi;
        uint8_t pri_hi_byte;
        uint16_t bg_pixels = 0;
        uint16_t bg_pixels_alt = 0;
        if (bgmask)
        {
            bg_pixels = *(uint16_t*)(pixels + x * 2);
            if (pixels_alt)
                bg_pixels_alt = *(uint16_t*)(pixels_alt + x * 2);
        }
        __cgb_merge_tiles(
            vram_tile_data_lo, vram_tile_data_hi, rm_lo, true, lut_lo, lut_hi, merge_subx,
            (uint16_t*)(pixels + x * 2), &pri, &rm_hi, &pri_hi_byte
        );

        uint16_t rm_hi_dark = 0;
        if (pixels_alt)
            __cgb_write_dark(
                vram_tile_data_hi, rm_lo_dark, lut_hi_dark, subx, pixels_alt, x, &rm_hi_dark
            );

        if (bgmask)
        {
            uint8_t n_bg = 8 - subx;
            uint8_t mask0_byte = (n_bg >= 4) ? 0xFFu : (uint8_t)((1u << (2 * n_bg)) - 1);
            uint8_t mask2_byte = (n_bg <= 4) ? 0x00u : (uint8_t)((1u << (2 * (n_bg - 4))) - 1);
            uint8_t* win_out = pixels + x * 2;
            uint8_t bg0 = (uint8_t)bg_pixels;
            uint8_t bg2 = (uint8_t)(bg_pixels >> 8);
            win_out[0] = (bg0 & mask0_byte) | (win_out[0] & ~mask0_byte);
            win_out[1] = (bg2 & mask2_byte) | (win_out[1] & ~mask2_byte);
            if (pixels_alt)
            {
                uint8_t d_bg0 = (uint8_t)bg_pixels_alt;
                uint8_t d_bg2 = (uint8_t)(bg_pixels_alt >> 8);
                uint8_t* d_win_out = pixels_alt + x * 2;
                d_win_out[0] = (d_bg0 & mask0_byte) | (d_win_out[0] & ~mask0_byte);
                d_win_out[1] = (d_bg2 & mask2_byte) | (d_win_out[1] & ~mask2_byte);
            }
            bgmask = 0;
        }

        uint8_t pri_mask = pri_lo ? (uint8_t)(0xFF >> merge_subx) : 0;
        if (pri_hi && merge_subx)
            pri_mask |= (uint8_t)(0xFF << (8 - merge_subx));
        line_priority[x / 4] &= ~(((uint32_t)pri) << ((x * 8) & 31));
        line_cgb_priority[x / 4] &= ~(((uint32_t)(pri & pri_mask)) << ((x * 8) & 31));

        rm_lo = rm_hi;
        lut_lo = lut_hi;
        tile_palette_lo = tile_palette_hi_val;
        if (pixels_alt)
        {
            rm_lo_dark = rm_hi_dark;
            lut_lo_dark = lut_hi_dark;
        }
    }
}
#endif

// renders one scanline
// __draw (+noinline): dedicated relocatable section, kept out of the core
// pocket; called from __gb_step_cpu via the offset-adjusted pointer.
__draw __attribute__((noinline)) void $(__gb_draw_line)(gb_s* restrict gb)
{
    __builtin_prefetch(&gb->gb_reg.LCDC, 0);
    __builtin_prefetch(&gb->gb_reg.WX, 0);
    __builtin_prefetch(&gb->gb_reg.BGP, 0);
    __builtin_prefetch(&gb->gb_reg.WY, 0);

    uint8_t* dest_pixels = &gb->lcd[gb->gb_reg.LY * LCD_WIDTH_PACKED];

    // render line to stack-buffer, then copy to dest
    uint32_t line_stage[LCD_WIDTH_PACKED / 4];
    uint8_t* pixels = (uint8_t*)line_stage;
    uint32_t line_priority[((LCD_WIDTH + 31) / 32)];
#if PGB_IS_CGB
    uint32_t line_cgb_priority[((LCD_WIDTH + 31) / 32)];
    uint32_t line_stage_alt[LCD_WIDTH_PACKED / 4];
    uint8_t* dest_pixels_alt =
        gb->direct.cgb_dual_output ? &gb->lcd_alt[gb->gb_reg.LY * LCD_WIDTH_PACKED] : NULL;
    uint8_t* pixels_alt = dest_pixels_alt ? (uint8_t*)line_stage_alt : NULL;
#else
    uint8_t* pixels_alt = NULL;
#endif

    const uint32_t line_priority_len = PEANUT_GB_ARRAYSIZE(line_priority);

    __builtin_prefetch(dest_pixels, 1);

    for (int i = 0; i < line_priority_len; ++i)
    {
#if PGB_IS_CGB
        line_priority[i] = ~0u;
        line_cgb_priority[i] = ~0u;
#else
        line_priority[i] = 0;
#endif
    }

    int wx = LCD_WIDTH;

    if ((gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE) &&
#if PGB_IS_DMG
        // non-CGB mode: window is also disabled if BG is disabled
        (gb->gb_reg.LCDC & LCDC_BG_ENABLE) &&
#endif
        (gb->direct.wy_latched || gb->gb_reg.LY >= gb->gb_reg.WY) &&
        (gb->gb_reg.WX < LCD_WIDTH + 7))
    {
        if (!gb->direct.wy_latched && gb->gb_reg.LY >= gb->gb_reg.WY)
            gb->direct.wy_latched = 1;
        if (gb->gb_reg.WX == 166)
        {
            // WX=166 is unreliable and can corrupt the next scanline.
            // We treat it as fully off-screen to prevent rendering artifacts.
            wx = LCD_WIDTH;
        }
        else if (gb->gb_reg.WX < 7)
        {
            // WX=0 causes the window to "stutter" based on SCX scroll.
            // Values 1-6 also seem to be unreliable
            wx = 0;
        }
        else
        {
            wx = gb->gb_reg.WX - 7;
        }
    }

    const int addr_mode_vram_tiledata_offset = (gb->gb_reg.LCDC & LCDC_TILE_SELECT) ? 0 : 0x800;

    // clear row
    for (int i = 0; i < LCD_WIDTH / 16; ++i)
        ((uint32_t*)pixels)[i] = 0;
#if PGB_IS_CGB
    if (pixels_alt)
        for (int i = 0; i < LCD_WIDTH / 16; ++i)
            ((uint32_t*)pixels_alt)[i] = 0;
#endif

// remaps 16-bit lo (t1) and hi (t2) colours to 2bbp 32-bit v
// Optimized version: processes 4 pixels at a time instead of 1
// Reduces loop iterations from 16 to 4 for better performance
#define BG_REMAP(pal, t1, t2, v)                                                       \
    do                                                                                 \
    {                                                                                  \
        uint32_t _t1 = (uint16_t)(t1);                                                 \
        uint32_t _t2 = (uint16_t)(t2);                                                 \
        uint32_t _v = 0;                                                               \
                                                                                       \
        /* Process 4 pixels at a time in reverse order to match original output */     \
        /* Original builds result from MSB to LSB, so we go from high nibble to low */ \
        for (int _q = 3; _q >= 0; _q--)                                                \
        {                                                                              \
            int _shift = _q * 4;                                                       \
            uint8_t _nib1 = (_t1 >> _shift) & 0x0F;                                    \
            uint8_t _nib2 = (_t2 >> _shift) & 0x0F;                                    \
                                                                                       \
            /* Extract 4 pixels from the nibbles */                                    \
            /* Pixel 0: bit 0 of nib1 and nib2 */                                      \
            /* Pixel 1: bit 1 of nib1 and nib2, etc. */                                \
            uint8_t _pix0 = ((_nib1 >> 0) & 1) | (((_nib2 >> 0) & 1) << 1);            \
            uint8_t _pix1 = ((_nib1 >> 1) & 1) | (((_nib2 >> 1) & 1) << 1);            \
            uint8_t _pix2 = ((_nib1 >> 2) & 1) | (((_nib2 >> 2) & 1) << 1);            \
            uint8_t _pix3 = ((_nib1 >> 3) & 1) | (((_nib2 >> 3) & 1) << 1);            \
                                                                                       \
            /* Lookup colors from palette */                                           \
            uint8_t _c0 = ((pal) >> (2 * _pix3)) & 3; /* Reverse order within byte */  \
            uint8_t _c1 = ((pal) >> (2 * _pix2)) & 3;                                  \
            uint8_t _c2 = ((pal) >> (2 * _pix1)) & 3;                                  \
            uint8_t _c3 = ((pal) >> (2 * _pix0)) & 3;                                  \
                                                                                       \
            _v <<= 8;                                                                  \
            _v |= (_c0 << 6) | (_c1 << 4) | (_c2 << 2) | _c3;                          \
        }                                                                              \
        (v) = _v;                                                                      \
    } while (0)

    /* If background is enabled, draw it. */
#if PGB_IS_CGB
    if (wx > 0)
#else
    if ((gb->gb_reg.LCDC & LCDC_BG_ENABLE) && wx > 0)
#endif
    {
        /* Calculate current background line to draw. Constant because
         * this function draws only this one line each time it is
         * called. */
        const uint8_t bg_y = gb->gb_reg.LY + gb->display.latched_scy;

        uint8_t bg_x = gb->display.latched_scx;

        uint8_t* vram = gb->vram;

        // tiles on this line
        uint8_t* vram_line_tiles = gb->display.bg_map_base + (32 * (bg_y / 8));

        // points to line data for pixel offset
        // OPTIMIZE: we could store vram tile data interleaved, e.g.
        // row 0 of all tiles, then row 1, etc...
        uint16_t* vram_tile_data = (void*)&vram[2 * (bg_y % 8)];

#if PGB_IS_CGB
        uint8_t* vram_line_tile_attrs = vram_line_tiles + VRAM_SIZE;

        // points to line data for flipped-y offset
        uint16_t* vram_tile_data_flipped_y = (void*)&vram[2 * (7 - (bg_y % 8))];
#endif

        int subx = bg_x % 8;

#if 0
        // prefetch each tile's data
        for (int x = 0; x <= (wx + 7) / 8; ++x)
        {
            uint8_t tile = vram_line_tiles[(bg_x / 8 + x) % 32];
            unsigned bank_offset = 0;
            uint16_t* tile_data = vram_tile_data;

#if PGB_IS_CGB
            uint8_t tile_attributes = vram_line_tile_attrs[(bg_x / 8 + x) % 32];
            if (tile_attributes & BG_MAP_ATTR_BANK)
            {
                bank_offset = VRAM_SIZE / sizeof(uint16_t);
            }
            if (tile_attributes & BG_MAP_ATTR_Y_FLIP)
            {
                tile_data = vram_tile_data_flipped_y;
            }
#endif

            __builtin_prefetch(
                &tile_data
                    [bank_offset | (tile < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile)],
                0
            );
        }
#endif

#if PGB_IS_CGB
        __cgb_draw_tile_strip(
            gb, vram_line_tiles, vram_line_tile_attrs, vram_tile_data, vram_tile_data_flipped_y,
            addr_mode_vram_tiledata_offset, bg_x / 8, 0, (wx + 7) / 8, subx, subx, pixels,
            pixels_alt, line_priority, line_cgb_priority, false
        );
#else
        unsigned bank_offset = 0;
        uint8_t tile_hi = vram_line_tiles[(bg_x / 8) % 32];
        uint16_t vram_tile_data_hi = vram_tile_data
            [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
             (8 * (unsigned)tile_hi)];

        for (int x = 0; x < (wx + 7) / 8; ++x)
        {
            uint8_t* out = pixels + (x % 2) + (x / 2) * 4;
            uint16_t vram_tile_data_lo = vram_tile_data_hi;
            uint16_t tile_hi = vram_line_tiles[(bg_x / 8 + x + 1) % 32];

            unsigned bank_offset = 0;
            vram_tile_data_hi = vram_tile_data
                [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
                 (8 * (unsigned)tile_hi)];

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
        uint8_t bg_x = 256 - wx;
        uint8_t bg_y = gb->display.window_clear;

        uint8_t* vram = gb->vram;

        // tiles on this line
        uint8_t* vram_line_tiles = gb->display.window_map_base + (32 * (bg_y / 8));
        int window_map_x_offset = (32 - (wx / 8)) % 32;

        // points to line data for pixel offset
        uint16_t* vram_tile_data = (void*)&vram[2 * (bg_y % 8)];

#if PGB_IS_CGB
        uint8_t* vram_line_tile_attrs = vram_line_tiles + VRAM_SIZE;

        // points to line data for flipped-y offset
        uint16_t* vram_tile_data_flipped_y = (void*)&vram[2 * ((7 - bg_y) % 8)];
#endif

#if 0
        // prefetch each tile's data
        for (int x = wx / 8; x <= LCD_WIDTH / 8; ++x)
        {
            uint8_t tile = vram_line_tiles[(window_map_x_offset + x) % 32];

            unsigned bank_offset = 0;
            uint16_t* tile_data = vram_tile_data;

#if PGB_IS_CGB
            uint8_t tile_attributes = vram_line_tile_attrs[(window_map_x_offset + x) % 32];
            if (tile_attributes & BG_MAP_ATTR_BANK)
            {
                bank_offset = VRAM_SIZE / sizeof(uint16_t);
            }
            if (tile_attributes & BG_MAP_ATTR_Y_FLIP)
            {
                tile_data = vram_tile_data_flipped_y;
            }
#endif

            __builtin_prefetch(
                &vram_tile_data
                    [bank_offset | (tile < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile)],
                0
            );
        }
#endif

#if PGB_IS_DMG
        unsigned bank_offset = 0;
        uint8_t tile_hi = vram_line_tiles[0];
        uint16_t vram_tile_data_hi = vram_tile_data
            [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
             (8 * (unsigned)tile_hi)];
#endif

        int subx = bg_x % 8;

#if PGB_IS_DMG
        // first part of window is obscured
        vram_tile_data_hi &= (0xFFFF) << subx;
        vram_tile_data_hi &= 0xFF | ((0xFF00) << subx);
        uint32_t bgmask = 0xFF >> subx;
        if (subx == 0)
            bgmask = 0;
#endif

#if PGB_IS_CGB
        __cgb_draw_tile_strip(
            gb, vram_line_tiles, vram_line_tile_attrs, vram_tile_data, vram_tile_data_flipped_y,
            addr_mode_vram_tiledata_offset, window_map_x_offset, wx / 8, LCD_WIDTH / 8, subx, 0,
            pixels, pixels_alt, line_priority, line_cgb_priority, true
        );
#else
        for (int x = wx / 8; x < LCD_WIDTH / 8; ++x)
        {
            uint8_t* out = pixels + (x % 2) + (x / 2) * 4;
            uint16_t vram_tile_data_lo = vram_tile_data_hi;
            uint16_t tile_hi = vram_line_tiles[(window_map_x_offset + x + 1) % 32];

            unsigned bank_offset = 0;
            vram_tile_data_hi = vram_tile_data
                [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
                 (8 * (unsigned)tile_hi)];

            uint8_t raw1 = vram_tile_data_lo & 0x00FF;
            uint8_t raw2 = (uint16_t)vram_tile_data_lo >> 8;

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

#if PGB_IS_DMG
    // remap background pixel by palette, and set priority
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
#endif

#if PGB_IS_CGB
    bool cgb_master_priority = !!(gb->gb_reg.LCDC & LCDC_CGB_MASTER_PRIORITY);
#endif

    // draw sprites
    if (gb->gb_reg.LCDC & LCDC_OBJ_ENABLE)
    {
        $(__gb_draw_line_sprites)(
            gb, gb->display.oam_latch, line_priority,
#if PGB_IS_CGB
            line_cgb_priority, cgb_master_priority,
#endif
            pixels, pixels_alt
        );
    }

    uint32_t* restrict line_out = (uint32_t*)(void*)dest_pixels;
#if PGB_IS_DMG
    if (pgb_dirty_prev && !pgb_dirty_skip)
    {
        uint32_t* restrict prev_out = (uint32_t*)&pgb_dirty_prev[gb->gb_reg.LY * LCD_WIDTH_PACKED];
        uint32_t changed = 0;
        for (int i = 0; i < LCD_WIDTH_PACKED / 4; ++i)
        {
            uint32_t s = line_stage[i];
            changed |= s ^ prev_out[i];
            line_out[i] = s;
            prev_out[i] = s;
        }
        if (changed && pgb_dirty_flags)
            pgb_dirty_flags[gb->gb_reg.LY >> 4] |= (1 << (gb->gb_reg.LY & 0xF));
    }
    else
#endif
    {
        for (int i = 0; i < LCD_WIDTH_PACKED / 4; ++i)
            line_out[i] = line_stage[i];
    }
#if PGB_IS_CGB
    if (dest_pixels_alt)
    {
        uint32_t* restrict line_out_alt = (uint32_t*)(void*)dest_pixels_alt;
        for (int i = 0; i < LCD_WIDTH_PACKED / 4; ++i)
            line_out_alt[i] = line_stage_alt[i];
    }
#endif
}

#if PGB_IS_CGB
#undef CGB_LUT
#undef CGB_LUT_DARK
#undef CGB_LUT_BLEND
#endif

// Per-scanline mode-3 setup: OAM latch, sprite penalties, window
// visibility, mode3/mode0 cycle lengths. Lives in the draw cluster
// (scanline cadence); called from the PPU step via DRAW_CALL.
__draw static void $(__gb_ppu_mode3_setup)(gb_s* gb)
{
    uint16_t mode3_cycles = PPU_MODE_3_VRAM_MIN_CYCLES;
    const bool besu_skip = gb->direct.first_scanline_besu_skip;
    gb->direct.first_scanline_besu_skip = 0;

    if (besu_skip)
    {
        // First scanline after LCD-on: BESU never sets on hardware,
        // so no OAM latch, no sprites, no sprite penalties.
        // SCX/SCY are not latched either - use live values.
        gb->display.latched_scx = gb->gb_reg.SCX;
        gb->display.latched_scy = gb->gb_reg.SCY;
        mode3_cycles += gb->display.latched_scx & 7;
    }
    else
    {
        for (int _i = 0; _i < OAM_SIZE >> 2; _i++)
            ((uint32_t*)gb->display.oam_latch)[_i] = ((uint32_t*)gb->oam)[_i];
        gb->display.latched_scx = gb->gb_reg.SCX;
        gb->display.latched_scy = gb->gb_reg.SCY;

        mode3_cycles += gb->display.latched_scx & 7;

        // PPU sprite timing: dynamic per-sprite penalty.
        // Stacked sprites at same X: first pays full alignment cost,
        // subsequent sprites cost only the 6-dot core (combinational re-fire).
        uint8_t sprites_found = 0;
        uint8_t last_penalty_x = 0xFF;
        const uint8_t sprite_height = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE) ? 16 : 8;
        static const uint8_t sprite_penalty_lut[8] = {11, 10, 9, 8, 7, 6, 6, 6};

        for (uint8_t s = 0; s < NUM_SPRITES && sprites_found < MAX_SPRITES_LINE; s++)
        {
            const uint8_t y = gb->display.oam_latch[s * 4];
            const uint8_t x = gb->display.oam_latch[s * 4 + 1];

            // Check if sprite Y intersects current line
            if (y <= gb->gb_reg.LY + 16 && gb->gb_reg.LY + 16 < y + sprite_height)
            {
                if (sprites_found > 0 && x == last_penalty_x)
                {
                    mode3_cycles += 6;
                }
                else
                {
                    const uint8_t alignment = ((gb->display.latched_scx & 7) + x) & 7;
                    mode3_cycles += sprite_penalty_lut[alignment];
                }
                last_penalty_x = x;
                sprites_found++;
            }
        }
    }

    bool win_visible = (gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE) && (gb->gb_reg.WX <= 166) &&
                       (gb->direct.wy_latched || gb->gb_reg.LY >= gb->gb_reg.WY);
#if PGB_IS_DMG
    win_visible &= (gb->gb_reg.LCDC & LCDC_BG_ENABLE);
#endif
    if (win_visible)
    {
        mode3_cycles += 6;
    }

    gb->display.current_mode3_cycles = MIN(mode3_cycles, PPU_MODE_3_VRAM_MAX_CYCLES);
    gb->display.current_mode0_cycles =
        LCD_LINE_CYCLES - PPU_MODE_2_OAM_CYCLES - gb->display.current_mode3_cycles;
    if (besu_skip)
        gb->display.current_mode0_cycles -= 2;
}

__core_section("short") static bool $(__gb_get_op_flag)(gb_s* restrict gb, uint8_t op8)
{
    op8 %= 4;
    bool flag = (op8 <= 1) ? gb->cpu_reg.f_bits.z : gb->cpu_reg.f_bits.c;
    flag ^= (op8 % 2);
    return flag;
}

__core_section("short") static u16 $(__gb_add16)(gb_s* restrict gb, u16 a, u16 b)
{
    unsigned temp = a + b;
    gb->cpu_reg.f_bits.n = 0;
    gb->cpu_reg.f_bits.h = ((temp ^ a ^ b) >> 12) & 1;
    gb->cpu_reg.f_bits.c = temp >> 16;
    return temp;
}

__core static unsigned $(__gb_run_instruction_micro)(gb_s* gb)
{
#define FETCH8(gb) $(__gb_fetch8)(gb)

#define FETCH16(gb) $(__gb_fetch16)(gb)

    u16 _pc = gb->cpu_reg.pc;
    u8 opcode;
    // Fast path: any region mapped in ram_base (ROM, WRAM, echo) + HRAM.
    // VRAM/IO/cart-RAM fetches fall through to preserve read side effects.
    uint8_t* fetch_base = gb->ram_base[_pc >> 12];
    if likely (fetch_base)
        opcode = fetch_base[_pc];
    else if (_pc >= 0xFF80)
        opcode = gb->hram[_pc & 0xFF];
    else
        opcode = $(__gb_read)(gb, _pc);
    gb->cpu_reg.pc++;
    unsigned cycles = 4;  // T-cycles (was float M-cycles)
    unsigned chain_cycles = 0;
    unsigned src;
    u8 srcidx;

    bool chained = false;
    int inst = 0;

    goto dispatch;

second_instruction:
    inst = 1;
    chain_cycles = cycles;
    cycles = 4;
    opcode = FETCH8(gb);
    chained = false;

dispatch:
{
    if unlikely (gb->gb_halt_bug)
    {
        if (gb->gb_halt_bug == 1)
            gb->cpu_reg.pc = gb->gb_halt_bug_pc;
        gb->gb_halt_bug--;
    }

    const u8 op8 = ((opcode & ~0xC0) / 8) ^ 1;

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
            {
                chained = true;
                break;  // nop
            }
            if (opcode == 0x10)  // STOP
                return chain_cycles + __gb_rare_instruction(gb, opcode);
            if (opcode < 0x18)
                return chain_cycles + __gb_rare_instruction(gb, opcode);
            {
                // jr
                cycles = 8;
                bool flag = $(__gb_get_op_flag)(gb, op8);
                if (opcode == 0x18)
                    flag = 1;
                if (flag)
                {
                    cycles = 12;
                    gb->cpu_reg.pc += (s8)FETCH8(gb);
                }
                else
                {
                    gb->cpu_reg.pc++;
                }
            }
            chained = true;
            break;
        case 1:
            // LD r16, d16
            cycles = 12;
            gb->cpu_reg_raw16[reg16] = FETCH16(gb);
            break;
        case 2:
        case 10:
            // TODO
            cycles = 8;
            if (reg16 == 4)
                reg16 = 2;

            if (op8 % 2 == 1)
            {
                // ld (r16), a
                $(__gb_write)(gb, gb->cpu_reg_raw16[reg16], gb->cpu_reg.a);
            }
            else
            {
                // ld a, (r16)
                gb->cpu_reg.a = $(__gb_read)(gb, gb->cpu_reg_raw16[reg16]);
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
            cycles = 8;
        }
            chained = true;
            break;

        // inc/dec 8-bit
        case 4:
        case 5:
        case 12:
        case 13:
        {
            const u8 is_dec = opcode & 1;
            const s8 offset = is_dec ? -1 : 1;

            u8 src = (reg8 == 7) ? $(__gb_read)(gb, gb->cpu_reg.hl) : gb->cpu_reg_raw[reg8];
            u8 tmp = src + offset;

            u8 f = gb->cpu_reg.f & 0x1F;
            f |= (tmp == 0) ? 0x80 : 0;
            f |= is_dec ? 0x40 : 0;
            f |= ((tmp & 0x0F) == (is_dec ? 0x0F : 0x00)) ? 0x20 : 0;
            gb->cpu_reg.f = f;

            if (reg8 == 7)
            {
                cycles = 12;
                $(__gb_write)(gb, gb->cpu_reg.hl, tmp);
            }
            else
            {
                gb->cpu_reg_raw[reg8] = tmp;
            }
        }
            chained = true;
            break;

        case 6:
        case 14:
            srcidx = 0;
            src = FETCH8(gb);
            cycles = 8;
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
                chained = true;
            }
            else if unlikely (opcode == 0x27)  // daa
            {
                u16 a = gb->cpu_reg.a;
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
                chained = true;
            }
            else if (opcode == 0x2F)
            {
                gb->cpu_reg.a ^= 0xFF;
                gb->cpu_reg.f_bits.n = 1;
                gb->cpu_reg.f_bits.h = 1;
                chained = true;
            }
            else if (op8 % 2 == 1)
            {
                gb->cpu_reg.f_bits.c = 1;
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;
                chained = true;
            }
            else if (op8 % 2 == 0)
            {
                gb->cpu_reg.f_bits.c ^= 1;
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;
                chained = true;
            }
            break;

        case 9:
            // add hl, r16
            cycles = 8;
            gb->cpu_reg.hl = $(__gb_add16)(gb, gb->cpu_reg.hl, gb->cpu_reg_raw16[reg16]);
            chained = true;
            break;

        default:
            __builtin_unreachable();
        }
    }
    break;
    case 1:
    case 2:
    {
        chained = true;
        srcidx = (opcode % 8) ^ 1;
        if (srcidx == 7)
        {
            src = $(__gb_read)(gb, gb->cpu_reg.hl);
            cycles = 8;
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
                    return chain_cycles + __gb_rare_instruction(gb, opcode);
                }
                else
                {
                    cycles += 4;
                    $(__gb_write)(gb, gb->cpu_reg.hl, src);
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
        bool flag = $(__gb_get_op_flag)(gb, op8);
        if (opcode % 8 == 3)
            flag = 1;
        switch ((opcode % 16) | ((opcode & 0x20) >> 1))
        {
        case 0x00:
        case 0x08:  // ret [flag]
            cycles = 8;
            if (flag)
            {
                goto ret;
            }
            break;
        case 0x01:
        case 0x11:  // pop
            cycles = 12;
            src = $(__gb_pop16)(gb);
            if (op8 / 2 == 3)
            {
                gb->cpu_reg.a = src >> 8;
                gb->cpu_reg.f = src & 0xF0;
            }
            else
            {
                gb->cpu_reg_raw16[op8 / 2] = src;
            }
            chained = true;
            break;
        case 0x02:
        case 0xA:  // jp [flag]
            cycles = 12;
            if (flag)
            {
                goto jp;
            }
            gb->cpu_reg.pc += 2;
            break;
        case 0x03:  // jp
            if unlikely (opcode == 0xD3)
            {
                return chain_cycles + __gb_rare_instruction(gb, opcode);
            }
        jp:
            chained = true;
            cycles = 16;
            gb->cpu_reg.pc = FETCH16(gb);
            break;
        case 0x04:
        case 0x0C:  // call [flag]
            cycles = 12;
            if (flag)
            {
                goto call;
            }
            gb->cpu_reg.pc += 2;
            break;
        case 0x05:
        case 0x15:  // push
            cycles = 16;
            src = gb->cpu_reg_raw16[op8 / 2];
            if (op8 / 2 == 3)
            {
                src = (gb->cpu_reg.a << 8) | (gb->cpu_reg.f & 0xF0);
            }
            $(__gb_push16)(gb, src);
            chained = true;
            break;
        case 0x06:
        case 0x0E:
        case 0x16:
        case 0x1E:  // arith d8
            cycles = 8;
            src = FETCH8(gb);
            goto arithmetic;
            break;
        case 0x07:
        case 0x0F:
        case 0x17:
        case 0x1F:  // rst
            chained = true;
            cycles = 16;
            $(__gb_push16)(gb, gb->cpu_reg.pc);
            gb->cpu_reg.pc = 8 * (op8 ^ 1);
            break;
        case 0x09:  // ret, reti
            if unlikely (opcode == 0xD9)
            {
                gb->gb_ime = 1;
                gb->gb_ime_countdown = 0;
                goto ret_common;
            }
        ret:
            chained = true;
        ret_common:
            cycles += 12;
            gb->cpu_reg.pc = $(__gb_pop16)(gb);
            break;
        case 0x0B:  // 0xCB prefix, 0xDB invalid
            if likely (opcode == 0xCB)
                return chain_cycles + $(__gb_execute_cb)(gb);
            return chain_cycles + __gb_rare_instruction(gb, opcode);
            break;
        case 0x0D:  // call
            if unlikely (op8 & 2)
            {
                return chain_cycles + __gb_rare_instruction(gb, opcode);
            }
        call:
            chained = true;
            cycles = 24;
            {
                u16 tmp = FETCH16(gb);
                $(__gb_push16)(gb, gb->cpu_reg.pc);
                gb->cpu_reg.pc = tmp;
            }
            break;
        case 0x10:  // ld (a8)
        {
            cycles = 12;
            // repurpose 'srcidx'
            srcidx = (FETCH8(gb));
        hram_op:;
            u16 addr = 0xFF00 | srcidx;
            if (opcode & 0x10)
            {
                u8 v = $(__gb_read)(gb, addr);
                gb->cpu_reg.a = v;
            }
            else
            {
                $(__gb_write)(gb, addr, gb->cpu_reg.a);
            }
        }
        break;
        case 0x12:  // ld (C)
        {
            cycles = 8;
            srcidx = gb->cpu_reg.c;
            chained = true;
            goto hram_op;
        }
        break;
        case 0x19:  // pc/sp hl (0xE9 JP HL, 0xF9 LD SP, HL)
            if (opcode == 0xF9)
            {
                gb->cpu_reg.sp = gb->cpu_reg.hl;
                cycles = 8;
            }
            else  // 0xE9
            {
                gb->cpu_reg.pc = gb->cpu_reg.hl;
                cycles = 4;
            }
            chained = true;
            break;
        case 0x13:  // DI (0xF3), 0xE3 invalid
            if unlikely (opcode != 0xF3)
                return chain_cycles + __gb_rare_instruction(gb, opcode);
            chained = true;
            cycles = 4;
            gb->gb_ime = 0;
            gb->gb_ime_countdown = 0;
            break;
        case 0x1B:  // EI (0xFB), 0xEB invalid
            if unlikely (opcode != 0xFB)
                return chain_cycles + __gb_rare_instruction(gb, opcode);
            chained = true;
            cycles = 4;
            gb->gb_ime_countdown = 2;
            break;
        case 0x14:
        case 0x1C:
        case 0x1D:  // illegal
        case 0x18:  // SP+8
            return chain_cycles + __gb_rare_instruction(gb, opcode);
            break;
        case 0x1A:  // ld (a16)
        {
            cycles = 16;
            u16 v = FETCH16(gb);
            if (op8 & 2)
            {
                gb->cpu_reg.a = $(__gb_read)(gb, v);
            }
            else
            {
                $(__gb_write)(gb, v, gb->cpu_reg.a);
            }
        }
        break;
        default:
            __builtin_unreachable();
        }
    }
    }
}

    if (false)
    {
    inc_dec_hl:
        gb->cpu_reg.hl += (opcode >= 0x20);
        gb->cpu_reg.hl -= 2 * (opcode >= 0x30);
#if CPU_VALIDATE == 0
        if (inst == 0)
        {
            if unlikely ((gb->gb_ime || gb->gb_halt) && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
                return cycles;
            goto second_instruction;
        }
#endif
    }

#if CPU_VALIDATE == 0
    if (inst == 0 && chained)
    {
        if unlikely ((gb->gb_ime || gb->gb_halt) && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
            return cycles;
        if (gb->gb_ime_countdown > 1)
            gb->gb_ime_countdown = 1;
        inst = 1;
        goto second_instruction;
    }
#endif

    return cycles + chain_cycles;
}

__core static uint16_t $(__gb_calc_halt_cycles)(gb_s* gb)
{
    // In STOP mode, the CPU is paused until a button is pressed.
    if (gb->gb_stop && gb->direct.joypad != 0xFF)
    {
        gb->gb_stop = 0;
        gb->gb_hle = false;  // paranoia
        return 16;
    }

    gb->gb_hle = false;

#if 0
    // TODO: optimize serial
    if(gb->gb_reg.SC & SERIAL_SC_TX_START) return 16;
#endif

    uint32_t src[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

    if (gb->gb_reg.tac_enable)
    {
#if PGB_IS_CGB
        uint16_t tima_threshold = gb->gb_reg.tac_cycles >> gb->cgb_fast_mode_active;
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
    uint16_t ppu_cycles_remaining = __gb_ppu_cycles_remaining(gb);

    if ((int16_t)ppu_cycles_remaining <= 0)
    {
        ppu_cycles_remaining = 1;
    }
    src[2] = (uint32_t)ppu_cycles_remaining;

    // Register-specific HLE: LY polling waits for LY change, not just mode change
    if (gb->hle_ioaddr == 0x44)
    {
        uint16_t ly_cycles;
        switch (gb->lcd_mode)
        {
        case LCD_HBLANK:  // already optimal, LY++ at end of HBlank
            ly_cycles = ppu_cycles_remaining;
            break;
        case LCD_TRANSFER:
            // skip remaining mode3 + all of mode0
            ly_cycles = LCD_LINE_CYCLES - PPU_MODE_2_OAM_CYCLES - gb->counter.lcd_count;
            break;
        default:
            // LCD_VBLANK, LCD_SEARCH_OAM, or LCD off: wait for scanline end
            ly_cycles = LCD_LINE_CYCLES - gb->counter.lcd_count;
            break;
        }
        src[2] = ly_cycles;
    }

    // Find the minimum cycles until the next event
    uint32_t cycles = src[0];
    if (src[1] < cycles)
        cycles = src[1];
    if (src[2] < cycles)
        cycles = src[2];

    // ensure positive
    cycles = (cycles < 16) ? 16 : cycles;

    if (gb->cgb_speed_switch_halt_period)
    {
        uint32_t max_cycles = gb->cgb_speed_switch_halt_period;
        if (cycles > max_cycles)
            cycles = max_cycles;

        gb->cgb_speed_switch_halt_period = max_cycles - cycles;
        if (gb->cgb_speed_switch_halt_period == 0)
            gb->gb_halt = 0;
    }

    return (uint16_t)cycles;
}

/**
 * Internal function used to step the CPU.
 */
__core unsigned int $(__gb_step_cpu)(gb_s* gb)
{
    unsigned inst_cycles = 16;

    /* Handle interrupts */
    if unlikely (
        (gb->gb_ime || gb->gb_halt || gb->gb_stop) && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR)
    )
    {
        /* Timer-sourced HALT wake takes 6 M-cycles (all other sources: 5).
         * The timer's CLK9-aligned DFF misses one setup window. */
        if (gb->gb_halt && !gb->gb_ime)
        {
            uint8_t pending = gb->gb_reg.IF & gb->gb_reg.IE;
            if (pending & TIMER_INTR)
                inst_cycles += 4;
        }
        __gb_interrupt(gb);
    }

    if unlikely (gb->gb_halt || gb->gb_stop || gb->gb_hle)
    {
        inst_cycles = $(__gb_calc_halt_cycles)(gb);
        goto done_instr_timing;
    }

#if CPU_VALIDATE == 0
    inst_cycles = 0;
    // Cycle-budget batching: worst-case event window = budget + 23 (one
    // max-length instruction of overshoot). Budget doubles in CGB
    // double-speed mode; inst_cycles is shifted back down (>>1) after the
    // batch, so the PPU-domain window matches DMG.
    const unsigned batch_budget = CPU_BATCH_CYCLE_BUDGET
                                  << (PGB_IS_CGB ? gb->cgb_fast_mode_active : 0);
    // Stop batching once an interrupt is dispatchable.
    while (
        !(gb->gb_halt || gb->gb_stop || gb->gb_hle ||
          (gb->gb_ime && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))))
    {
        inst_cycles += $(__gb_run_instruction_micro)(gb);
        pgb_batch_elapsed = inst_cycles;
        if (gb->gb_ime_countdown > 0 && --gb->gb_ime_countdown == 0)
            gb->gb_ime = 1;
        if (inst_cycles >= batch_budget)
            break;
    }
    pgb_batch_elapsed = 0;
#else
    // run once as each, verify

    if (gb->cpu_reg.pc < 0x8000 && __gb_read_full(gb, gb->cpu_reg.pc) == CB_HW_BREAKPOINT_OPCODE)
    {
        // can't validate if breakpoint
        $(__gb_run_instruction_micro)(gb);
    }
    else
    {
        const u16 pc = gb->cpu_reg.pc;
        static u8 _wram[2][WRAM_SIZE_CGB];
        static u8 _vram[2][VRAM_SIZE_CGB];
        static u8 _cart_ram[2][0x20000];
        static gb_s _gb[2];

        memcpy(_wram[0], gb->wram, WRAM_SIZE_CGB);
        memcpy(_vram[0], gb->vram, VRAM_SIZE_CGB);
        if (gb->gb_cart_ram_size > 0)
            memcpy(_cart_ram[0], gb->gb_cart_ram, gb->gb_cart_ram_size);
        memcpy(&_gb[0], gb, sizeof(_gb));

        uint8_t opcode = (gb->gb_halt ? 0 : $(__gb_fetch8)(gb));
        if unlikely (gb->gb_halt_bug)
        {
            /* HALT bug: PC increment is inhibited for this fetch, so the
             * byte after HALT is re-read as the next opcode's first byte. */
            gb->cpu_reg.pc--;
            gb->gb_halt_bug = 0;
        }
        inst_cycles = __gb_run_instruction(gb, opcode);

        gb->cpu_reg.f_bits.unused = 0;

        memcpy(_wram[1], gb->wram, WRAM_SIZE_CGB);
        memcpy(_vram[1], gb->vram, VRAM_SIZE_CGB);
        memcpy(&_gb[1], gb, sizeof(gb_s));
        if (gb->gb_cart_ram_size > 0)
            memcpy(_cart_ram[1], gb->gb_cart_ram, gb->gb_cart_ram_size);

        memcpy(gb->wram, _wram[0], WRAM_SIZE_CGB);
        memcpy(gb->vram, _vram[0], VRAM_SIZE_CGB);
        memcpy(gb, &_gb[0], sizeof(gb_s));
        if (gb->gb_cart_ram_size > 0)
            memcpy(gb->gb_cart_ram, _cart_ram[0], gb->gb_cart_ram_size);

        uint8_t inst_cycles_m = $(__gb_run_instruction_micro)(gb);

        gb->cpu_reg.f_bits.unused = 0;

        if (memcmp(gb->wram, _wram[1], WRAM_SIZE_CGB))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in wram on opcode %x", opcode);
        }
        if (memcmp(gb->vram, _vram[1], VRAM_SIZE_CGB))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in vram on opcode %x", opcode);
        }
        if (memcmp(gb->gb_cart_ram, _cart_ram[1], gb->gb_cart_ram_size))
        {
            gb->gb_frame = 1;
            playdate->system->error("difference in cart ram on opcode %x", opcode);
        }

        if (memcmp(&gb->cpu_reg, &_gb[1].cpu_reg, sizeof(struct PGB_VERSIONED(cpu_registers_s))))
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
        CB_ASSERT(sizeof(gb_s) - sizeof(audio_data) == offsetof(gb_s, audio));
        if (memcmp(gb, &_gb[1], offsetof(gb_s, audio)))
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

    // EI delay handling
    if (gb->gb_ime_countdown > 0)
    {
        if (--gb->gb_ime_countdown == 0)
        {
            gb->gb_ime = 1;
        }
    }
#endif

    /* OAM DMA transfer: 1 byte per M-cycle (4 T-cycles) */
    if (gb->dma_active && !gb->gb_halt && !gb->gb_stop)
    {
        unsigned dma_bytes = inst_cycles >> 2;

        while (dma_bytes > 0 && gb->dma_dest < 0xA0)
        {
            gb->oam[gb->dma_dest++] = $(__gb_read)(gb, gb->dma_src++);
            dma_bytes--;
        }

        if (gb->dma_dest >= 0xA0)
            gb->dma_active = false;
    }

    // cycles are halved/quartered during overclocked vblank
    if (gb->lcd_mode == LCD_VBLANK)
    {
        inst_cycles >>= gb->overclock;
    }

#if PGB_IS_CGB
    inst_cycles >>= gb->cgb_fast_mode_active;

    // FIXME: we can avoid having to do this if we change the cycle units
    // to allow more fixed-point precision here.
    inst_cycles = MAX(1, inst_cycles);
#endif

done_instr_timing:
{
#if PGB_IS_CGB
    unsigned cgb_fast = gb->cgb_fast_mode_active;
#endif
    if (gb->counter.serial_count > 0)
    {
        /* Overshoot-safe expiry: subtracting inst_cycles can skip past zero
         * (an interrupt mid-wait shifts the cycle grid), and with an unsigned
         * counter that wraps to a huge value and never completes, hanging any
         * game that polls SC bit 7 (F-1 Pole Position on hardware). */
        if (gb->counter.serial_count <= inst_cycles)
        {
            if ((gb->gb_reg.SC & SERIAL_SC_TX_START) && (gb->gb_reg.SC & SERIAL_SC_CLOCK_SRC))
            {
                // Simulate disconnected cable input
                gb->gb_reg.SB = 0xFF;
                // Request Serial interrupt
                gb->gb_reg.IF |= SERIAL_INTR;
                // Clear transfer start flag
                gb->gb_reg.SC &= ~SERIAL_SC_TX_START;
            }
            gb->counter.serial_count = 0;
        }
        else
        {
            gb->counter.serial_count -= inst_cycles;
        }
    }

    if (gb->direct.joypad_interrupt_delay > 0)
    {
        if (gb->direct.joypad_interrupt_delay <= (int)inst_cycles)
        {
            gb->gb_reg.IF |= CONTROL_INTR;
            gb->direct.joypad_interrupt_delay = 0;
        }
        else
        {
            gb->direct.joypad_interrupt_delay -= inst_cycles;
        }
    }

    /* Handle delayed TIMA reload from the previous cycle. */
    if (gb->gb_reg.tima_overflow_delay)
    {
        gb->gb_reg.IF |= TIMER_INTR;
        gb->gb_reg.tima_overflow_delay = 0;
    }

    /* TIMA register timing */
    if (gb->gb_reg.tac_enable)
    {
        uint16_t tima_threshold = gb->gb_reg.tac_cycles;
#if PGB_IS_CGB
        tima_threshold >>= cgb_fast;
#endif
        gb->counter.tima_count += inst_cycles;
        while (gb->counter.tima_count >= tima_threshold)
        {
            gb->counter.tima_count -= tima_threshold;
            gb->gb_reg.TIMA++;

            if (gb->gb_reg.TIMA == 0x00)
            {
                gb->gb_reg.TIMA = gb->gb_reg.TMA;
                gb->gb_reg.tima_overflow_delay = 1;
            }
        }
    }

    /* DIV register timing */
    // update DIV timer
    uint16_t div_threshold = DIV_CYCLES;
#if PGB_IS_CGB
    div_threshold >>= cgb_fast;
#endif
    gb->counter.div_count += inst_cycles;

    if (gb->counter.div_count >= div_threshold)
    {
#if PGB_IS_CGB
        if (cgb_fast)
        {
            uint8_t old_div = gb->gb_reg.DIV;
            uint8_t div_inc = gb->counter.div_count >> 7;
            gb->gb_reg.DIV += div_inc;
            gb->counter.div_count &= 0x7F;

            if (preferences_sound_mode == 1)
                __apu_div_tick_detect(&gb->audio, old_div, div_inc, 0x20u);
        }
        else
#endif
        {
            uint8_t old_div = gb->gb_reg.DIV;
            uint8_t div_inc = gb->counter.div_count >> 8;
            gb->gb_reg.DIV += div_inc;
            gb->counter.div_count &= 0xFF;

            if (preferences_sound_mode == 1)
                __apu_div_tick_detect(&gb->audio, old_div, div_inc, 0x10u);
        }
    }

    gb->counter.lcd_count += inst_cycles;
    gb->counter.apu_count += inst_cycles;

    if (!(gb->gb_reg.LCDC & LCDC_ENABLE))
    {
        gb->counter.lcd_off_count += inst_cycles;
        if (gb->counter.lcd_off_count >= LCD_FRAME_CYCLES)
        {
            gb->counter.lcd_off_count -= LCD_FRAME_CYCLES;
            gb->gb_frame = 1;
            if (!gb->direct.frame_skip)
            {
                uint8_t fill = (gb->gb_reg.BGP & 3) * 0x55;
                uint32_t fill_word = (uint32_t)fill * 0x01010101u;
                for (int i = 0; i < LCD_BUFFER_BYTES / 4; i++)
                    ((uint32_t*)gb->lcd)[i] = fill_word;
                if (gb->lcd_alt)
                    for (int i = 0; i < LCD_BUFFER_BYTES / 4; i++)
                        ((uint32_t*)gb->lcd_alt)[i] = fill_word;
                if (pgb_dirty_prev && pgb_dirty_flags && !pgb_dirty_skip)
                {
                    for (int i = 0; i < LCD_BUFFER_BYTES / 4; i++)
                        ((uint32_t*)pgb_dirty_prev)[i] = fill_word;
                    for (int i = 0; i < LCD_HEIGHT / 16; i++)
                        pgb_dirty_flags[i] = 0xFFFF;
                }
            }
        }
    }
    else
    {
        /* LCD Timing */
        bool ticked;
        do
        {
            ticked = false;
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

                    DRAW_CALL($(__gb_ppu_mode3_setup), gb);
                    DRAW_CALL($(__gb_update_stat_irq), gb);
                    ticked = true;
                }
                break;

            // Mode 3: Pixel Transfer (variable, 172-289 cycles on hardware).
            case LCD_TRANSFER:
                if (gb->counter.lcd_count >= gb->display.current_mode3_cycles)
                {
                    gb->counter.lcd_count -= gb->display.current_mode3_cycles;

                    if likely (!gb->direct.frame_skip && gb->lcd_master_enable)
                    {
                        // draw cluster may be relocated into the main DTCM
                        // pool (rev A); call via the offset-adjusted pointer.
                        void (*draw_line)(gb_s*) =
                            (void (*)(gb_s*))((char*)$(__gb_draw_line) + pgb_draw_reloc_offset);
                        draw_line(gb);
                    }

                    gb->lcd_mode = LCD_HBLANK;
                    gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_HBLANK;
                    DRAW_CALL($(__gb_update_stat_irq), gb);
#if PGB_IS_CGB
                    if (gb->cgb_hdma_active)
                        __gb_do_hdma(gb);
#endif
                    ticked = true;
                }
                break;

            // Mode 0: H-Blank (remaining cycles of the 456 total)
            // The PPU is idle until the end of the scanline.
            case LCD_HBLANK:
                if (gb->counter.lcd_count >= gb->display.current_mode0_cycles)
                {
                    gb->counter.lcd_count -= gb->display.current_mode0_cycles;
                    gb->gb_reg.LY++;

                    if (gb->gb_reg.LY == LCD_HEIGHT)
                    {
                        gb->lcd_mode = LCD_VBLANK;
                        gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_VBLANK;
                        gb->gb_frame = 1;
                        gb->gb_reg.IF |= VBLANK_INTR;
                        gb->direct.wy_latched = 0;

                        // VBlank entry STAT glitch (Case 1): if LYC and Mode 1
                        // interrupts are both enabled, the LYC leg drops (fast)
                        // before Mode 1 rises (slow), creating a brief through-zero.
                        if ((gb->gb_reg.STAT & STAT_LYC_COINC) &&
                            (gb->gb_reg.STAT & STAT_LYC_INTR) &&
                            (gb->gb_reg.STAT & STAT_MODE_1_INTR))
                        {
                            gb->gb_reg.IF |= LCDC_INTR;
                            gb->direct.stat_line = 1;
                        }

#if PGB_IS_CGB
                        // FIXME: is this correct?
                        while (gb->cgb_hdma_active)
                            __gb_do_hdma(gb);
#endif
                        DRAW_CALL($(__gb_update_stat_irq), gb);

                        DRAW_CALL($(__gb_update_lyc_and_stat_irq), gb);
                    }
                    else
                    {
                        gb->lcd_mode = LCD_SEARCH_OAM;
                        gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_SEARCH_OAM;

                        DRAW_CALL($(__gb_update_lyc_and_stat_irq), gb);
                    }
                    ticked = true;
                }
                break;

            // Mode 1: V-Blank (10 lines, 4560 cycles total)
            // The PPU is idle, giving the CPU time to update VRAM.
            case LCD_VBLANK:
                if (gb->counter.lcd_count >= LCD_LINE_CYCLES)
                {
                    gb->counter.lcd_count -= LCD_LINE_CYCLES;

                    if (gb->gb_reg.LY == 0)
                    {
                        gb->lcd_mode = LCD_SEARCH_OAM;
                        gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_SEARCH_OAM;

                        // VBlank exit STAT glitch (Case 4): if Mode 1 and Mode 2
                        // interrupts are both enabled, Mode 1 drops (fast) before
                        // Mode 2 rises (slow), creating a brief through-zero.
                        if ((gb->gb_reg.STAT & STAT_MODE_1_INTR) &&
                            (gb->gb_reg.STAT & STAT_MODE_2_INTR))
                        {
                            gb->gb_reg.IF |= LCDC_INTR;
                            gb->direct.stat_line = 1;
                        }

                        gb->display.window_clear = 0;

                        DRAW_CALL($(__gb_update_lyc_and_stat_irq), gb);
                    }
                    else
                    {
                        gb->gb_reg.LY++;
                        DRAW_CALL($(__gb_update_lyc_and_stat_irq), gb);
                    }
                    ticked = true;
                }
                // "Short Line 153" Fix: during VBlank line 153, LY wraps to 0 very early
                // (after just a few cycles), but the PPU remains in VBlank for the full
                // line duration. Placed inside the case so the check only evaluates
                // during VBlank steps, not every CPU step.
                else if (gb->gb_reg.LY == 153)
                {
                    gb->gb_reg.LY = 0;
                    DRAW_CALL($(__gb_update_lyc_and_stat_irq), gb);
                    ticked = true;
                }
                break;
            }
        } while (ticked);
    }
    return inst_cycles;
}
}

__core void $(gb_run_frame)(gb_s* gb)
{
    gb->direct.has_read_accelerometer_this_frame = false;

#if PGB_IS_CGB
    gb->cgb_fast_mode_active = gb->cgb_fast_mode && (preferences_cgb_speed == 0);
#endif

    gb->gb_frame = 0;
    gb->counter.apu_count = 0;

    unsigned int total_cycles = 0;

#ifdef TARGET_SIMULATOR
    bool trace_this_frame = (g_trace_frames_remaining > 0);
    if (trace_this_frame)
    {
        playdate->system->logToConsole(
            "=== TRACE frame begin (rom_bank=%x pc=%04x) ===", gb->selected_rom_bank, gb->cpu_reg.pc
        );
    }
#endif

    while (!gb->gb_frame && total_cycles < SCREEN_REFRESH_CYCLES)
    {
#ifdef TARGET_SIMULATOR
        if (trace_this_frame)
        {
            playdate->system->logToConsole(
                "%x:%04x op=%02x af=%02x%02x bc=%02x%02x de=%02x%02x hl=%02x%02x sp=%04x ime=%d "
                "ly=%02x",
                gb->selected_rom_bank, gb->cpu_reg.pc, __gb_read_full(gb, gb->cpu_reg.pc),
                gb->cpu_reg.a, gb->cpu_reg.f, gb->cpu_reg.b, gb->cpu_reg.c, gb->cpu_reg.d,
                gb->cpu_reg.e, gb->cpu_reg.h, gb->cpu_reg.l, gb->cpu_reg.sp, gb->gb_ime,
                gb->gb_reg.LY
            );
        }
#endif
        total_cycles += $(__gb_step_cpu)(gb);
    }

    /* Mark the frame's end in the APU write-event stream (accurate sound
     * mode): gives audio event replay an explicit frame boundary even
     * when the frame had no register writes. */
    if (gb->direct.sound)
        audio_note_frame_end(&gb->audio, gb->counter.apu_count);

#ifdef TARGET_SIMULATOR
    if (trace_this_frame)
    {
        playdate->system->logToConsole("=== TRACE frame end (cycles=%u) ===", total_cycles);
        g_trace_frames_remaining--;
    }
#endif
}

#undef PGB_TEMPLATE
