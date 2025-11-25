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

/**
 * Checks all STAT interrupt sources and requests an interrupt on a rising edge.
 * Note: __cgb and __dmg implementations should be identical
 */
__core static void $(__gb_update_stat_irq)(gb_s* gb)
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
 * Note: __cgb and __dmg versions should have the same implementation;
 * this is used outside of core.
 */
__core static void $(__gb_check_lyc)(gb_s* gb)
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

__core_section("short") static uint8_t $(__gb_read)(gb_s* gb, const uint16_t addr)
{
    if likely (addr < 0x4000)
    {
        return gb->gb_zero_bank[addr];
    }
    if likely (addr < 0x8000)
    {
        return gb->selected_bank_addr[addr];
    }
    if likely (addr >= 0xC000 && addr < 0xE000)
    {
        return gb->wram_base[(addr % 0x2000) / 0x1000][addr];
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

__core_section("short") static void $(__gb_write)(gb_s* restrict gb, const uint16_t addr, uint8_t v)
{
    if likely (addr >= 0xC000 && addr < 0xE000)
    {
        gb->wram_base[(addr % 0x2000) / 0x1000][addr] = v;
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

__core_section("util") clalign
    void $(gb_fast_memcpy_64)(void* restrict _dst, const void* restrict _src, size_t len)
{
    CB_ASSERT((len & 7) == 0);

#if TARGET_PLAYDATE
    uint8_t* dst8 = (uint8_t*)_dst;
    const uint8_t* src8 = (const uint8_t*)_src;

    // Process the main part of the data in 16-byte chunks.
    while (len >= 16)
    {
        asm volatile(
            "ldmia %1!, {r3, r4, r5, r6} \n"
            "stmia %0!, {r3, r4, r5, r6} \n"
            : "+r"(dst8), "+r"(src8)
            : /* no inputs */
            : "r3", "r4", "r5", "r6", "memory"
        );
        len -= 16;
    }

    // If there is a final 8-byte chunk remaining, copy it.
    if (len > 0)
    {
        *(uint64_t*)dst8 = *(const uint64_t*)src8;
    }

#else
    if ((((uintptr_t)_dst | (uintptr_t)_src) & 7) != 0)
    {
        memcpy(_dst, _src, len);
        return;
    }

    const uint64_t* src = (const uint64_t*)_src;
    uint64_t* dst = (uint64_t*)_dst;

    for (size_t n = len >> 3; n; --n)
    {
        *dst++ = *src++;
    }
#endif
}

__core_section("short") static uint16_t $(__gb_read16)(gb_s* restrict gb, u16 addr)
{
    // Fast path for WRAM
    if (addr >= WRAM_0_ADDR && addr < (ECHO_ADDR - 1))
    {
        void* ptr = &gb->wram_base[(addr % 0x2000) / 0x1000][addr];
        return *(uint16_t*)ptr;
    }
    // Fast path for HRAM
    else if (addr >= HRAM_ADDR && addr < (INTR_EN_ADDR - 1))
    {
        void* ptr = &gb->hram[addr - IO_ADDR];
        return *(uint16_t*)ptr;
    }

    // Fallback for all other cases (unaligned, ROM, I/O, etc.)
    u16 v = $(__gb_read)(gb, addr);
    v |= (u16)$(__gb_read)(gb, addr + 1) << 8;
    return v;
}

__core_section("short") static void $(__gb_write16)(gb_s* restrict gb, u16 addr, u16 v)
{
    // Fast path for WRAM
    if (addr >= WRAM_0_ADDR && addr < (ECHO_ADDR - 1))
    {
        void* ptr = &gb->wram_base[(addr % 0x2000) / 0x1000][addr];
        *(uint16_t*)ptr = v;
        return;
    }
    // Fast path for HRAM
    else if (addr >= HRAM_ADDR && addr < (INTR_EN_ADDR - 1))
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
    return $(__gb_read)(gb, gb->cpu_reg.pc++);
}

__core_section("short") static uint16_t $(__gb_fetch16)(gb_s* restrict gb)
{
    u16 addr = gb->cpu_reg.pc;

    if (addr == 0x3FFF || addr == 0x7FFF)
    {
        gb->cpu_reg.pc += 2;
        return $(__gb_read16)(gb, addr);
    }

    uint8_t* rom_ptr;
    if likely (addr < 0x4000)
    {
        rom_ptr = &gb->gb_zero_bank[addr];
    }
    else if likely (addr < 0x8000)
    {
        rom_ptr = &gb->selected_bank_addr[addr];
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

#if ENABLE_LCD

__core_section("draw") static void $(__gb_draw_pixel)(uint8_t* line, u8 x, u8 v)
{
    u8* pix = line + x / LCD_PACKING;
    x = (x % LCD_PACKING) * (8 / LCD_PACKING);
    *pix &= ~(((1 << LCD_BITS_PER_PIXEL) - 1) << x);
    *pix |= (v & 3) << x;
}

__core_section("draw") static u8 $(__gb_get_pixel)(uint8_t* line, u8 x)
{
    u8* pix = line + x / LCD_PACKING;
    x = (x % LCD_PACKING) * LCD_BITS_PER_PIXEL;
    return (*pix >> x) % (1 << LCD_BITS_PER_PIXEL);
}

__core_section("draw") static inline int $(compare_sprites)(
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

__core_section("draw") static void $(__gb_draw_line_sprites)(
    gb_s* restrict gb, const uint8_t* oam_src, bool is_ghost, const uint32_t* line_priority,
    uint8_t* pixels
)
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
        const uint8_t* oam = &oam_src[s * 4];
        const uint8_t oam_y = oam[0];
        const uint8_t oam_x = oam[1];

        if (oam_x > 0 && (current_ly + 16 >= oam_y) && (current_ly + 16 < oam_y + sprite_height))
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

    /* Render sprites from lowest priority to highest priority. */
    for (int8_t i = number_of_sprites - 1; i >= 0; i--)
    {
        uint8_t s_idx = sprites_to_render[i].sprite_number;
        uint8_t s_4 = s_idx * 4;

        if (is_ghost)
        {
            // To prevent thickening, check if the ghost sprite is within 4 pixel of the
            // real sprite. If so, skip rendering the ghost.

            // FIXME: this doesn't work if sprites are re-ordered, as they likely are in many games.

            const uint8_t* ghost_oam = &oam_src[s_4];
            const uint8_t* visible_oam = &gb->oam[s_4];
            if (abs(ghost_oam[1] - visible_oam[1]) <= 4 && abs(ghost_oam[0] - visible_oam[0]) <= 4)
            {
                continue;
            }
        }

        uint8_t OY = oam_src[s_4 + 0];
        uint8_t OX = oam_src[s_4 + 1];
        uint8_t OT = oam_src[s_4 + 2] & (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0xFE : 0xFF);
        uint8_t OF = oam_src[s_4 + 3];  // flags

        unsigned bank = 0;
#if PGB_IS_CGB
        if (OF & OBJ_CGB_BANK)
        {
            bank = VRAM_SIZE;
        }
#endif

        if (!is_ghost && (OF & OBJ_PALETTE))
        {
            int16_t sprite_x = OX - 8;
            int16_t sprite_y = OY - 16;
            uint8_t sprite_w = 8;
            uint8_t sprite_h = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE) ? 16 : 8;
            int16_t sprite_x2 = sprite_x + sprite_w;
            int16_t sprite_y2 = sprite_y + sprite_h;

            if (sprite_x < gb->direct.blend_rect_x_min)
                gb->direct.blend_rect_x_min = (sprite_x < 0) ? 0 : sprite_x;
            if (sprite_y < gb->direct.blend_rect_y_min)
                gb->direct.blend_rect_y_min = (sprite_y < 0) ? 0 : sprite_y;
            if (sprite_x2 > gb->direct.blend_rect_x_max)
                gb->direct.blend_rect_x_max = (sprite_x2 > LCD_WIDTH) ? LCD_WIDTH : sprite_x2;
            if (sprite_y2 > gb->direct.blend_rect_y_max)
                gb->direct.blend_rect_y_max = (sprite_y2 > LCD_HEIGHT) ? LCD_HEIGHT : sprite_y2;
        }

        uint8_t py = gb->gb_reg.LY - (OY - 16);
        if (OF & OBJ_FLIP_Y)
            py = (sprite_height - 1) - py;

        uint16_t t1_i = bank + VRAM_TILES_1 + OT * 0x10 + 2 * py;
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

        uint8_t c_add = (OF & OBJ_PALETTE) ? 4 : 0;

        for (int disp_x = start; disp_x != end; disp_x += dir)
        {
            if unlikely (disp_x < 0 || disp_x >= LCD_WIDTH)
                goto next_sprite_pixel;

            uint8_t c = ((t1 & 0x80) >> 7) | ((t2 & 0x80) >> 6);
            if (c != 0)
            {
                int P_segment_index = disp_x / 32;
                int P_bit_in_segment = disp_x % 32;
                uint8_t bg_is_transparent =
                    (line_priority[P_segment_index] >> P_bit_in_segment) & 1;

                if (!((OF & OBJ_PRIORITY) && !bg_is_transparent))
                {
                    uint8_t color_value = (OBP >> (c * 2 + c_add * 2)) & 3;
                    if (is_ghost)
                    {
                        uint8_t old_color = $(__gb_get_pixel)(pixels, disp_x);
                        if (color_value > old_color)
                        {
                            $(__gb_draw_pixel)(pixels, disp_x, color_value);
                        }
                    }
                    else
                    {
                        $(__gb_draw_pixel)(pixels, disp_x, color_value);
                    }
                }
            }
        next_sprite_pixel:
            t1 <<= 1;
            t2 <<= 1;
        }
    }
}

// renders one scanline
__core_section("draw") void $(__gb_draw_line)(gb_s* restrict gb)
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

    __builtin_prefetch(&gb->gb_reg.LCDC, 0);
    __builtin_prefetch(&gb->gb_reg.WX, 0);
    __builtin_prefetch(&gb->gb_reg.BGP, 0);
    __builtin_prefetch(&gb->display.WY, 0);

    uint8_t* pixels = &gb->lcd[gb->gb_reg.LY * LCD_WIDTH_PACKED];
    uint32_t line_priority[((LCD_WIDTH + 31) / 32)];

#if PGB_IS_CGB
    // allows bg to overrule obj priority
    uint32_t line_cgb_priority[((LCD_WIDTH + 31) / 32)];
#endif

    const uint32_t line_priority_len = PEANUT_GB_ARRAYSIZE(line_priority);

    __builtin_prefetch(pixels, 1);

    for (int i = 0; i < line_priority_len; ++i)
    {
        line_priority[i] = 0;
#if PGB_IS_CGB
        line_cgb_priority[i] = 0;
#endif
    }

    uint32_t priority_bits = 0;

    int wx = LCD_WIDTH;
    bool master_priority = true;
#if PGB_IS_CGB
    master_priority = !!(gb->gb_reg.LCDC & LCDC_CGB_MASTER_PRIORITY);
#endif

    if ((gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE) &&
#if PGB_IS_DMG
        // non-CGB mode: window is also disabled if BG is disabled
        (gb->gb_reg.LCDC & LCDC_BG_ENABLE) &&
#endif
        (gb->gb_reg.LY >= gb->display.WY) && (gb->gb_reg.WX < LCD_WIDTH + 7))
    {
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

    /* If background is enabled, draw it. */
    if ((gb->gb_reg.LCDC & LCDC_BG_ENABLE) && wx > 0)
    {
        /* Calculate current background line to draw. Constant because
         * this function draws only this one line each time it is
         * called. */
        const uint8_t bg_y = gb->gb_reg.LY + gb->gb_reg.SCY;

        uint8_t bg_x = gb->gb_reg.SCX;
        int addr_mode_2 = !(gb->gb_reg.LCDC & LCDC_TILE_SELECT);
        int addr_mode_vram_tiledata_offset = addr_mode_2 ? 0x800 : 0;

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
        uint16_t* vram_tile_data_flipped_y = (void*)&vram[2 * ((7 - bg_y) % 8)];
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

        uint8_t tile_hi = vram_line_tiles[(bg_x / 8) % 32];

        unsigned bank_offset = 0;
#if PGB_IS_CGB
        uint8_t tile_attributes = vram_line_tile_attrs[(bg_x / 8) % 32];
        if (tile_attributes & BG_MAP_ATTR_BANK)
        {
            bank_offset = VRAM_SIZE / sizeof(uint16_t);
        }
#endif

        uint16_t vram_tile_data_hi =
#if PGB_IS_CGB
            // cgb can flip tiles
            ((tile_attributes & BG_MAP_ATTR_Y_FLIP) ? vram_tile_data_flipped_y : vram_tile_data)
#else
            vram_tile_data
#endif
                [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
                 (8 * (unsigned)tile_hi)];

#if PGB_IS_CGB
        vram_tile_data_hi = reverse_bits_in_each_byte_conditional_u16(
            vram_tile_data_hi, !!(tile_attributes & BG_MAP_ATTR_X_FLIP)
        );
#endif

        for (int x = 0; x < (wx + 7) / 8; ++x)
        {
            uint8_t* out = pixels + (x % 2) + (x / 2) * 4;
            uint16_t vram_tile_data_lo = vram_tile_data_hi;
            uint16_t tile_hi = vram_line_tiles[(bg_x / 8 + x + 1) % 32];

            unsigned bank_offset = 0;
#if PGB_IS_CGB
            uint8_t tile_attributes = vram_line_tile_attrs[(bg_x / 8 + x + 1) % 32];
            if (tile_attributes & BG_MAP_ATTR_BANK)
            {
                bank_offset = VRAM_SIZE / sizeof(uint16_t);
            }
#endif

            vram_tile_data_hi =
#if PGB_IS_CGB
                // cgb can flip tiles
                ((tile_attributes & BG_MAP_ATTR_Y_FLIP) ? vram_tile_data_flipped_y : vram_tile_data)
#else
                vram_tile_data
#endif
                    [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
                     (8 * (unsigned)tile_hi)];

#if PGB_IS_CGB
            vram_tile_data_hi = reverse_bits_in_each_byte_conditional_u16(
                vram_tile_data_hi, !!(tile_attributes & BG_MAP_ATTR_X_FLIP)
            );
#endif

            uint8_t raw1 = (vram_tile_data_lo & 0x00FF) >> subx;
            uint8_t raw2 = (uint16_t)vram_tile_data_lo >> (subx | 8);
            raw1 |= (vram_tile_data_hi & 0x00FF) << (8 - subx);
            raw2 |= ((vram_tile_data_hi & 0xFF00) >> subx) & 0xFF;

            out[0] = raw1;
            out[2] = raw2;
        }
    }

    /* draw window */
    if (wx < LCD_WIDTH)
    {
        uint8_t bg_x = 256 - wx;
        uint8_t bg_y = gb->display.window_clear;
        int addr_mode_2 = !(gb->gb_reg.LCDC & LCDC_TILE_SELECT);
        int addr_mode_vram_tiledata_offset = addr_mode_2 ? 0x800 : 0;

        uint8_t* vram = gb->vram;

        // tiles on this line
        uint8_t* vram_line_tiles = gb->display.window_map_base + (32 * (bg_y / 8));

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
                &vram_tile_data
                    [bank_offset | (tile < 0x80 ? addr_mode_vram_tiledata_offset : 0) | (8 * (unsigned)tile)],
                0
            );
        }
#endif

        uint8_t tile_hi = vram_line_tiles[(bg_x / 8 + wx / 8) % 32];

        unsigned bank_offset = 0;
#if PGB_IS_CGB
        uint8_t tile_attributes = vram_line_tile_attrs[(bg_x / 8) % 32];
        if (tile_attributes & BG_MAP_ATTR_BANK)
        {
            bank_offset = VRAM_SIZE / sizeof(uint16_t);
        }
#endif

        uint16_t vram_tile_data_hi =
#if PGB_IS_CGB
            // cgb can flip tiles
            ((tile_attributes & BG_MAP_ATTR_Y_FLIP) ? vram_tile_data_flipped_y : vram_tile_data)
#else
            vram_tile_data
#endif
                [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
                 (8 * (unsigned)tile_hi)];

#if PGB_IS_CGB
        vram_tile_data_hi = reverse_bits_in_each_byte_conditional_u16(
            vram_tile_data_hi, !!(tile_attributes & BG_MAP_ATTR_X_FLIP)
        );
#endif

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

            unsigned bank_offset = 0;
#if PGB_IS_CGB
            uint8_t tile_attributes = vram_line_tile_attrs[(bg_x / 8 + x + 1) % 32];
            if (tile_attributes & BG_MAP_ATTR_BANK)
            {
                bank_offset = VRAM_SIZE / sizeof(uint16_t);
            }
#endif

            vram_tile_data_hi =
#if PGB_IS_CGB
                // cgb can flip tiles
                ((tile_attributes & BG_MAP_ATTR_Y_FLIP) ? vram_tile_data_flipped_y : vram_tile_data)
#else
                vram_tile_data
#endif
                    [bank_offset | (tile_hi < 0x80 ? addr_mode_vram_tiledata_offset : 0) |
                     (8 * (unsigned)tile_hi)];

#if PGB_IS_CGB
            vram_tile_data_hi = reverse_bits_in_each_byte_conditional_u16(
                vram_tile_data_hi, !!(tile_attributes & BG_MAP_ATTR_X_FLIP)
            );
#endif

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
        gb->display.window_clear++;
    }

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

    uint32_t* used_line_priority = line_priority;

#if IS_CGB_MODE
    if (!(gb->gb_reg.LCDC & LCDC_CGB_MASTER_PRIORITY))
    {
        used_line_priority = gb->zero32;
    }
#endif

    // draw sprites
    if (gb->gb_reg.LCDC & LCDC_OBJ_ENABLE)
    {
        $(__gb_draw_line_sprites)(gb, gb->oam, false, used_line_priority, pixels);
    }

    // draw ghost sprites
    if (gb->direct.oam_ghost_buffer)
    {
        $(__gb_draw_line_sprites)(
            gb, gb->direct.oam_ghost_buffer, true, used_line_priority, pixels
        );
    }
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
#endif

__core static unsigned $(__gb_run_instruction_micro)(gb_s* gb)
{
#define FETCH8(gb) $(__gb_fetch8)(gb)

#define FETCH16(gb) $(__gb_fetch16)(gb)

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
                break;           // nop
            if (opcode == 0x10)  // STOP
            {
#if PGB_IS_DMG
                // We check for the button-press glitch on DMG.
                // A button is pressed, and a direction/action line is selected.
                if ((gb->direct.joypad != 0xFF) && ((gb->gb_reg.P1 & 0x30) != 0x30))
                {
                    cycles = 1;
                    break;
                }
#endif

                if (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR)
                {
                    if (gb->gb_ime == 0)
                    {
                        // HALT Bug: pc fails to increment correctly.
                        // We rewind the pc to re-execute the byte after the opcode.
                        gb->cpu_reg.pc--;
                    }
                    else
                    {
                        gb->cpu_reg.pc++;
                    }
                }
                else
                {
                    // NORMAL OPERATION: Enter low-power STOP mode.
                    gb->cpu_reg.pc++;
                    gb->gb_stop = 1;
                    gb->gb_reg.DIV = 0;
                }

                cycles = 1;
                break;
            }
            if (opcode < 0x18)
                return __gb_rare_instruction(gb, opcode);
            {
                // jr
                cycles = 2;
                bool flag = $(__gb_get_op_flag)(gb, op8);
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
            cycles = 2;
        }
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

            gb->cpu_reg.f_bits.z = (tmp == 0);
            gb->cpu_reg.f_bits.n = is_dec;
            gb->cpu_reg.f_bits.h = ((tmp & 0xF) == (0xF & -(s8)is_dec));

            if (reg8 == 7)
            {
                cycles = 3;
                $(__gb_write)(gb, gb->cpu_reg.hl, tmp);
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
            gb->cpu_reg.hl = $(__gb_add16)(gb, gb->cpu_reg.hl, gb->cpu_reg_raw16[reg16]);
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
            src = $(__gb_read)(gb, gb->cpu_reg.hl);
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
                    return __gb_rare_instruction(gb, opcode);
                }
                else
                {
                    cycles++;
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
            cycles = 2;
            if (flag)
            {
                goto ret;
            }
            break;
        case 0x01:
        case 0x11:  // pop
            cycles = 3;
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
            $(__gb_push16)(gb, src);
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
            $(__gb_push16)(gb, gb->cpu_reg.pc);
            gb->cpu_reg.pc = 8 * (op8 ^ 1);
            break;
        case 0x09:  // ret, reti
            if unlikely (opcode == 0xD9)
            {
                gb->gb_ime = 1;
                gb->gb_ime_countdown = 0;
            }
        ret:
            cycles += 3;
            gb->cpu_reg.pc = $(__gb_pop16)(gb);
            break;
        case 0x0B:  // CB opcodes
            return $(__gb_execute_cb)(gb);
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
                $(__gb_push16)(gb, gb->cpu_reg.pc);
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

/**
 * Internal function used to step the CPU.
 */
__core unsigned int $(__gb_step_cpu)(gb_s* gb)
{
    unsigned inst_cycles = 16;

    /* Handle interrupts */
    if unlikely ((gb->gb_ime || gb->gb_halt) && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
    {
        __gb_interrupt(gb);
    }

    if unlikely (gb->gb_halt || gb->gb_stop)
    {
        inst_cycles = __gb_calc_halt_cycles(gb);
        goto done_instr_timing;
    }

#if CPU_VALIDATE == 0
    inst_cycles = $(__gb_run_instruction_micro)(gb);
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
#endif

    // EI delay handling
    if (gb->gb_ime_countdown > 0)
    {
        if (--gb->gb_ime_countdown == 0)
        {
            gb->gb_ime = 1;
        }
    }

    // cycles are halved/quartered during overclocked vblank
    if (gb->lcd_mode == LCD_VBLANK)
    {
        inst_cycles >>= gb->cgb_fast_mode;
    }

#if PGB_IS_CGB
    inst_cycles >>= gb->cgb_fast_mode;

    // FIXME: we can avoid having to do this if we change the cycle units
    // to allow more fixed-point precision here.
    inst_cycles = MAX(1, inst_cycles);
#endif

done_instr_timing:
{
    if (gb->counter.serial_count > 0)
    {
        gb->counter.serial_count -= inst_cycles;
        if (gb->counter.serial_count <= 0)
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
    }

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
#if PGB_IS_CGB
        gb->counter.tima_count += (inst_cycles >> gb->cgb_fast_mode);
        gb->counter.tima_count += inst_cycles;
        uint16_t tima_threshold = gb->gb_reg.tac_cycles >> gb->cgb_fast_mode;
        while (gb->counter.tima_count >= tima_threshold)
        {
            gb->counter.tima_count -= tima_threshold;
            gb->gb_reg.TIMA++;

            if (gb->gb_reg.TIMA == 0x00)
            {
                gb->gb_reg.tima_overflow_delay = 1;
            }
        }
#else
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
#endif
    }

/* DIV register timing */
// update DIV timer
#if PGB_IS_CGB
    uint16_t div_threshold = DIV_CYCLES >> gb->cgb_fast_mode;
    gb->counter.div_count += inst_cycles;
    if (gb->counter.div_count >= div_threshold)
    {
        gb->gb_reg.DIV += gb->counter.div_count / div_threshold;
        gb->counter.div_count %= div_threshold;
    }
#else
    gb->counter.div_count += inst_cycles;
    if (gb->counter.div_count >= DIV_CYCLES)
    {
        gb->gb_reg.DIV += gb->counter.div_count / DIV_CYCLES;
        gb->counter.div_count %= DIV_CYCLES;
    }
#endif

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

        // "Short Line 153" Fix:
        // On real hardware, during Line 153 (end of VBlank), LY wraps to 0 very early
        // (after just a few cycles), but the PPU remains in VBlank (Mode 1) for the
        // rest of the scanline duration (456 cycles).
        // This allows LY=LYC interrupts for Line 0 to fire *before* the new frame starts.
        if (gb->lcd_mode == LCD_VBLANK && gb->gb_reg.LY == 153)
        {
            gb->gb_reg.LY = 0;
            $(__gb_check_lyc)(gb);
            $(__gb_update_stat_irq)(gb);
        }

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

                uint16_t mode3_cycles = PPU_MODE_3_VRAM_MIN_CYCLES;

                mode3_cycles += (gb->gb_reg.SCX % 8);

                bool win_visible = (gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE) &&
                                   (gb->gb_reg.WX <= 166) && (gb->gb_reg.LY >= gb->display.WY);
#if PGB_IS_DMG
                win_visible &= (gb->gb_reg.LCDC & LCDC_BG_ENABLE);
#endif
                if (win_visible)
                {
                    mode3_cycles += 6;
                }

                uint8_t sprites_found = 0;
                const uint8_t sprite_height = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE) ? 16 : 8;

                for (uint8_t s = 0; s < NUM_SPRITES && sprites_found < MAX_SPRITES_LINE; s++)
                {
                    const uint8_t y = gb->oam[s * 4];
                    const uint8_t x = gb->oam[s * 4 + 1];

                    // Check if sprite Y intersects current line
                    if (y <= gb->gb_reg.LY + 16 && gb->gb_reg.LY + 16 < y + sprite_height)
                    {
                        // Exception: OAM X=0 always incurs the max 11-dot penalty
                        if (x == 0)
                        {
                            mode3_cycles += 11;
                        }
                        else
                        {
                            int penalty = 6;
                            int alignment = (gb->gb_reg.SCX + x) & 7;
                            int extra = 5 - alignment;

                            if (extra > 0)
                            {
                                penalty += extra;
                            }
                            mode3_cycles += penalty;
                        }
                        sprites_found++;
                    }
                }

                gb->display.current_mode3_cycles = MIN(mode3_cycles, PPU_MODE_3_VRAM_MAX_CYCLES);
                gb->display.current_mode0_cycles =
                    LCD_LINE_CYCLES - PPU_MODE_2_OAM_CYCLES - gb->display.current_mode3_cycles;

                $(__gb_update_stat_irq)(gb);
            }
            break;

        // Mode 3: Pixel Transfer (variable, 172-289 cycles on hardware).
        case LCD_TRANSFER:
            if (gb->counter.lcd_count >= gb->display.current_mode3_cycles)
            {
                gb->counter.lcd_count -= gb->display.current_mode3_cycles;

#if ENABLE_LCD
                if (gb->lcd_master_enable && !gb->lcd_blank && !gb->direct.frame_skip)
                    $(__gb_draw_line)(gb);
#endif

                gb->lcd_mode = LCD_HBLANK;
                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_HBLANK;
                $(__gb_update_stat_irq)(gb);
#if PGB_IS_CGB
                if (gb->cgb_hdma_active)
                    __gb_do_hdma(gb);
#endif
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
                    gb->lcd_blank = 0;

#if PGB_IS_CGB
                    // FIXME: is this correct?
                    while (gb->cgb_hdma_active)
                        __gb_do_hdma(gb);
#endif
                    $(__gb_update_stat_irq)(gb);

                    $(__gb_check_lyc)(gb);
                    $(__gb_update_stat_irq)(gb);
                }
                else
                {
                    gb->lcd_mode = LCD_SEARCH_OAM;
                    gb->gb_reg.STAT = (gb->gb_reg.STAT & ~STAT_MODE) | LCD_SEARCH_OAM;

                    $(__gb_check_lyc)(gb);
                    $(__gb_update_stat_irq)(gb);
                }
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

                    gb->display.window_clear = 0;
                    gb->display.WY = gb->gb_reg.WY;

                    $(__gb_check_lyc)(gb);
                    $(__gb_update_stat_irq)(gb);
                }
                else
                {
                    gb->gb_reg.LY++;
                    $(__gb_check_lyc)(gb);
                    $(__gb_update_stat_irq)(gb);
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

__core void $(gb_run_frame)(gb_s* gb)
{
    gb->gb_frame = 0;

    gb->direct.blend_rect_x_min = 255;
    gb->direct.blend_rect_y_min = 255;
    gb->direct.blend_rect_x_max = 0;
    gb->direct.blend_rect_y_max = 0;

    unsigned int total_cycles = 0;

    static bool latch_log = true;

    while (!gb->gb_frame && total_cycles < SCREEN_REFRESH_CYCLES)
    {
        total_cycles += $(__gb_step_cpu)(gb);
#if defined(TRACE_LOG)
        if (latch_log)
            playdate->system->logToConsole(
                "%x:%04x af=%02x%02x bc=%02x%02x ly=%02x", gb->selected_rom_bank, gb->cpu_reg.pc,
                gb->cpu_reg.a, gb->cpu_reg.f, gb->cpu_reg.b, gb->cpu_reg.c, gb->gb_reg.LY
            );
        else
        {
            // enable/disable logging at specified points
        }
#endif
    }
}

typedef typeof(playdate->graphics->markUpdatedRows) markUpdateRows_t;

// this is not really part of gameboy emulation,
// but we're placing it in ITCM regardless for acceleration purposes.
__core_section("fb") void $(update_fb_dirty_lines)(
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
    scale_index &= (scaling - 1);
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

#undef PGB_TEMPLATE
