#include "scriptutil.h"
#include "userstack.h"

#define GB script_gb

uint8_t __gb_read_full(gb_s* gb, const uint_fast16_t addr);
void __gb_write_full(gb_s* gb, const uint_fast16_t addr, uint8_t);

u8 rom_peek(romaddr_t addr)
{
    return GB->gb_rom[addr];
}
void rom_poke(romaddr_t addr, u8 v)
{
    GB->gb_rom[addr] = v;
}

u8 ram_peek(addr16_t addr)
{
    return __gb_read_full(GB, addr);
}
void ram_poke(addr16_t addr, u8 v)
{
    __gb_write_full(GB, addr, v);
}
u16 ram_peek_u16(addr16_t addr)
{
    return (u16)__gb_read_full(GB, addr) | ((u16)__gb_read_full(GB, addr+1) << 8);
}

romaddr_t rom_size(void)
{
    return GB->gb_rom_size;
}

void poke_verify(unsigned bank, u16 addr, u8 prev, u8 val)
{
    u32 addr32 = (bank * 0x4000) | (addr % 0x4000);
    u8 actual = rom_peek(addr32);
    if (actual != prev)
    {
        playdate->system->error(
            "SCRIPT ERROR -- is this the right ROM? Poke_verify failed at %x:%04x (%04x); expected %02x, but "
            "was %02x (should replace with %02x)",
            bank, addr, addr32, prev, actual, val
        );
    }

    rom_poke(addr32, val);
}

char* script_disk_fname(unsigned fidx)
{
    CB_GameSceneContext* context = script_gb->direct.priv;
    CB_GameScene* scene = context->scene;
    if (preferences_save_slot)
    {
        return aprintf("%s/%s.%c.script.%u.bin", cb_gb_directory_path(CB_savesPath), scene->base_filename, 'A' + preferences_save_slot, fidx);
    }
    else
    {
        return aprintf("%s/%s.script.%u.bin", cb_gb_directory_path(CB_savesPath), scene->base_filename, fidx);
    }
}

void script_save_to_disk(const char* data, size_t size, unsigned fidx)
{
    char* fname = script_disk_fname(fidx);
    
    call_with_main_stack_3(cb_write_entire_file, fname, data, size);
    
    cb_free(fname);
}

char* script_load_from_disk(unsigned fidx, size_t* o_size)
{
    char* fname = script_disk_fname(fidx);
    char* result = call_with_main_stack_3(cb_read_entire_file, fname, o_size, kFileReadData);
    cb_free(fname);
    return result;
}

int script_load_tiles12(const char* path, uint16_t (*out)[12], int max_tiles)
{
    LCDBitmap* src = playdate->graphics->loadBitmap(path, NULL);
    if (!src) return 0;
    int width, height, stride, n=0;
    playdate->graphics->getBitmapData(src, &width, &height, &stride, NULL, NULL);
    
    for (int ty = 0; ty < (height/12); ++ty)
    {
        for (int tx = 0; tx < (width/12); ++tx)
        {
            int i = ty*(width/12) + tx;
            if (++n > max_tiles) goto done;
            for (int j = 0; j < 12; ++j)
            {
                out[i][j] = 0;
                for (int k = 0; k < 12; ++k)
                {
                    int x = (tx) * 12 + k;
                    int y = ty * 12 + j;
                    out[i][j] |= playdate->graphics->getBitmapPixel(src, x, y) << (15 - k);
                }
            }
        }
    }
    
done:
    playdate->graphics->freeBitmap(src);
    
    return n;
}

void script_draw_tiles12(uint16_t (*tiles12)[12], uint8_t* lcd, int rowbytes, int idx, int x, int y)
{
    uint16_t* tile12 = &tiles12[idx][0];

    for (int i = 0; i < 12; ++i)
    {
        uint16_t v = tile12[i];
        for (int j = 0; j < 12; ++j)
        {
            int _y = (i + y);
            int _x = (x + j);
            int x8 = 7 - (_x % 8);
            lcd[rowbytes * _y + _x / 8] &= ~(1 << x8);
            if (v & (1 << (15 - j)))
            {
                lcd[rowbytes * _y + _x / 8] |= (1 << x8);
            }
        }
    }
}

void script_draw_string12(uint16_t (*tiles12)[12], uint8_t* lcd, int rowbytes, const char* s, int char_offset, int x, int y)
{
    for (int i = 0; s[i]; ++i)
    {
        int c = s[i] - char_offset;
        script_draw_tiles12(tiles12, lcd, rowbytes, c, x + i*12, y);
    }
}

void find_code_cave(int bank, romaddr_t* max_start, romaddr_t* max_size)
{
    uint32_t bank_start = (bank != -1) ? (bank * 0x4000) : 0;
    uint32_t bank_end = (bank != -1) ? (bank_start + 0x4000 - 1) : (rom_size() - 1);

    *max_start = 0;
    *max_size = 0;
    uint32_t current_start = 0;
    uint32_t current_size = 0;
    bool in_cave = false;

    for (uint32_t addr = bank_start; addr <= bank_end; addr++)
    {
        uint8_t byte = rom_peek(addr);

        if ((byte == 0x00 || byte == 0xFF) && (addr % 0x4000 != 0))
        {
            if (!in_cave)
            {
                current_start = addr;
                current_size = 1;
                in_cave = true;
            }
            else
            {
                current_size++;
            }
        }
        else
        {
            if (in_cave)
            {
                if (current_size > *max_size)
                {
                    *max_size = current_size;
                    *max_start = current_start;
                }
                in_cave = false;
            }
        }
    }

    if (in_cave && current_size > *max_size)
    {
        *max_size = current_size;
        *max_start = current_start;
    }
}

CodeReplacement* code_replacement_new(
    unsigned bank, uint16_t addr, const uint8_t* tprev, const uint8_t* tval, size_t length,
    bool unsafe
)
{
    if (length == 0)
    {
        script_error("SCRIPT ERROR -- tprev and tval must have non-zero length");
        return NULL;
    }

    uint32_t base_addr = (bank << 14) | (addr & 0x3FFF);

    // Verify ROM matches tprev
    for (size_t i = 0; i < length; i++)
    {
        uint32_t current_addr = base_addr + i;
        uint8_t current_byte = rom_peek(current_addr);
        if (current_byte != tprev[i])
        {
            script_error(
                "SCRIPT ERROR -- is this the right ROM? Patch verification failed at 0x%04X "
                "expected %02X got %02X (would replace with %02x)",
                current_addr, tprev[i], current_byte, tval[i]
            );
            return NULL;
        }
    }

    CodeReplacement* r = allocz(CodeReplacement);
    if (!r)
    {
        script_error("SCRIPT ERROR -- memory allocation failed");
        return NULL;
    }

    r->tprev = cb_malloc(length);
    r->tval = cb_malloc(length);
    if (!r->tprev || !r->tval)
    {
        cb_free(r->tprev);
        cb_free(r->tval);
        cb_free(r);
        script_error("SCRIPT ERROR -- memory allocation failed");
        return NULL;
    }

    memcpy(r->tprev, tprev, length);
    memcpy(r->tval, tval, length);

    r->bank = bank;
    r->addr = base_addr;
    r->length = length;
    r->unsafe = unsafe;
    r->applied = false;

    return r;
}

void code_replacement_apply(CodeReplacement* r, bool apply)
{
    if (!r)
        return;

    bool target_state = apply;

    if (r->applied == target_state)
    {
        return;
    }

    r->applied = target_state;

    const uint8_t* target = target_state ? r->tval : r->tprev;

    if (!r->unsafe)
    {
        // ensure PC out of target range
        while ($PC >= r->addr && $PC < r->addr + r->length)
        {
            printf("PC=%x during patch-apply!\n", $PC);
            gb_step_cpu(GB);
        }
    }

    for (size_t i = 0; i < r->length; i++)
    {
        rom_poke(r->addr + i, target[i]);
    }
}

void code_replacement_free(CodeReplacement* r)
{
    if (!r)
        return;
    cb_free(r->tprev);
    cb_free(r->tval);
    cb_free(r);
}

LCDColor get_palette_color(int c)
{
    c = 3 - c;  // high on gb is low on pd
    if (c == 0)
        return kColorBlack;
    if (c == 3)
        return kColorWhite;

    if (c >= 2)
        ++c;

    bool dither_l = (preferences_dither_pattern == 2 || preferences_dither_pattern == 3);
    bool dither_d = (preferences_dither_pattern == 4 || preferences_dither_pattern == 5);

    // dark/light patterns
    if (c == 1 && dither_l)
        c = 2;
    if (c == 3 && dither_d)
        c = 2;

    switch (c | ((preferences_dither_pattern % 2) << 4))
    {
    case 0x01:
        return (uintptr_t)&lcdp_25s;
    case 0x02:
        return (uintptr_t)&lcdp_50;
    case 0x03:
        return (uintptr_t)&lcdp_75s;

    case 0x11:
        return (uintptr_t)&lcdp_25;
    case 0x12:
        return (uintptr_t)&lcdp_50;
    case 0x13:
        return (uintptr_t)&lcdp_75;

    default:
        return kColorBlack;
    }
}

unsigned get_game_scanline_row(int scaling, int first_squished, int scanline)
{
    if (scaling <= 0)
        return 2 * scanline;

    unsigned h = (scanline / scaling) * (1 + 2 * (scaling - 1));

    if (scanline % scaling)
    {
        h += (scanline % scaling) * 2;
        h -= (scanline % scaling >= first_squished);
    }

    return h;
}

unsigned get_game_picture_height(int scaling, int first_squished)
{
    return get_game_scanline_row(scaling, first_squished, LCD_HEIGHT);
}

void draw_vram_tile(uint8_t tile_idx, bool mode9000, int scale, int x, int y)
{
    uint16_t tile_addr = 0x8000 | (16 * (uint16_t)tile_idx);
    if (tile_idx < 0x80 && mode9000)
        tile_addr += 0x1000;

    uint16_t* tile_data = (void*)&script_gb->vram[tile_addr % 0x2000];

    for (int i = 0; i < 8; ++i)
    {
        for (int j = 0; j < 8; ++j)
        {
            int c0 = (tile_data[i] >> j) & 1;
            int c1 = (tile_data[i] >> (j + 8)) & 1;

            LCDColor col = get_palette_color(c0 | (c1 << 1));
            playdate->graphics->fillRect(x + j * scale, y + i * scale, scale + 1, scale + 1, col);
        }
    }
}