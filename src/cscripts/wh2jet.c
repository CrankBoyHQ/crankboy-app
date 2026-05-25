#include "../scenes/game_scene.h"
#include "../scriptutil.h"

#define DESCRIPTION                                                \
    "- Custom in-game HUD at the top, for clean 2x scaling.\n"     \
    "- Press Ⓐ on the title and other screens instead of Start.\n" \
    "\nCreated by: stonerl"

#define SETTLE_TICKS 30
#define A_BLOCK_TICKS 15
#define START_PULSE_TICKS 2

typedef struct ScriptData
{
    bool prev_in_game;
    bool prev_cinematic;
    bool in_game;
    bool cinematic;
    bool logo;
    bool title;
    bool demo;
    bool mode;
    bool option;
    bool char_select;
    bool results;
    bool sidebar_cleared;
    int refresh_frames;
    bool start_armed;
    int send_start;
    int suppress_start;
    int a_blocked;
    bool hud_seeded;
    uint8_t prev_portrait_l[9];
    uint8_t prev_portrait_r[9];
} ScriptData;

#define USERDATA ScriptData* data

static const int PORTRAIT_L_ADDRS[9] = {
    /* clang-format off */
    0x1800, 0x1801, 0x1802,
    0x1820, 0x1821, 0x1822,
    0x1840, 0x1841, 0x1842,
    /* clang-format on */
};
static const int PORTRAIT_R_ADDRS[9] = {
    /* clang-format off */
    0x1811, 0x1812, 0x1813,
    0x1831, 0x1832, 0x1833,
    0x1851, 0x1852, 0x1853,
    /* clang-format on */
};

static const int PORTRAIT_Y[9] = {0, 0, 0, 16, 16, 16, 32, 32, 32};
static const int PORTRAIT_L_X[9] = {0, 16, 32, 0, 16, 32, 0, 16, 32};
static const int PORTRAIT_R_X[9] = {352, 368, 384, 352, 368, 384, 352, 368, 384};

static const int NAME_L_ADDRS[7] = {
    0x1804, 0x1805, 0x1806, 0x1807, 0x1808, 0x1809, 0x180A,
};
static const int NAME_L_DST[7] = {
    0x18A1, 0x18A2, 0x18A3, 0x18A4, 0x18A5, 0x18A6, 0x18A7,
};
static const int NAME_R_ADDRS[7] = {
    0x1849, 0x184A, 0x184B, 0x184C, 0x184D, 0x184E, 0x184F,
};
static const int NAME_R_DST[7] = {
    0x18AC, 0x18AD, 0x18AE, 0x18AF, 0x18B0, 0x18B1, 0x18B2,
};

static const int WIN_L_ADDRS[3] = {
    0x1824,
    0x1825,
    0x1826,
};
static const int WIN_L_DST[3] = {
    0x1861,
    0x1862,
    0x1863,
};
static const int WIN_R_ADDRS[3] = {
    0x182D,
    0x182E,
    0x182F,
};
static const int WIN_R_DST[3] = {
    0x1870,
    0x1871,
    0x1872,
};

static const int CHAR_SRC[14] = {
    0x1843, 0x1844, 0x1845, 0x1846, 0x1847, 0x1848, 0x1849,
    0x184A, 0x184B, 0x184C, 0x184D, 0x184E, 0x184F, 0x1850,
};
static const int CHAR_DST[14] = {
    0x1863, 0x1864, 0x1865, 0x1866, 0x1867, 0x1868, 0x1869,
    0x186A, 0x186B, 0x186C, 0x186D, 0x186E, 0x186F, 0x1870,
};

static bool is_logo_screen(gb_s* gb)
{
    return gb->vram[0x190F] == 0xDD;
}

static bool is_title_screen(gb_s* gb)
{
    return gb->vram[0x190F] == 0x4F && gb->vram[0x1910] == 0x50 && gb->vram[0x19C4] == 0x58;
}

static bool is_demo_game(gb_s* gb)
{
    return gb->vram[0x1869] == 0x41 && gb->vram[0x186A] == 0x41;
}

static bool is_mode_screen(gb_s* gb)
{
    return gb->vram[0x190F] == 0x4F && gb->vram[0x1910] == 0x50 && gb->vram[0x19C4] != 0x58;
}

static bool is_option_screen(gb_s* gb)
{
    return gb->vram[0x1821] == 0xA2 && gb->vram[0x1832] == 0xA4 && gb->vram[0x1A01] == 0xA6 &&
           gb->vram[0x1A12] == 0xA7;
}

static bool is_in_game(gb_s* gb)
{
    return gb->vram[0x1869] == 0x5B && gb->vram[0x186A] == 0x5C;
}

static bool is_char_select(gb_s* gb)
{
    return gb->vram[0x1814] == 0x4D && gb->vram[0x1A34] == 0x4D;
}

static bool is_results(gb_s* gb)
{
    return gb->vram[0x1814] == 0x59 && gb->vram[0x1A34] == 0x59;
}

static bool is_cinematic(gb_s* gb)
{
    return gb->vram[0x1800] == 0x01 && gb->vram[0x181F] == 0x01;
}

static uint16_t tile_addr_for_idx(int tile_idx, bool unsigned_addr)
{
    if (unsigned_addr)
        return ((uint16_t)tile_idx * 16) & 0x1FFF;
    else
        return (0x1000 + ((int8_t)tile_idx) * 16) & 0x1FFF;
}

static LCDColor tile_pixel_to_color(uint8_t lo, uint8_t hi, int bit)
{
    static const LCDColor pal[4] = {
        kColorWhite,
        (LCDColor)&lcdp_75,
        (LCDColor)&lcdp_50,
        kColorBlack,
    };
    return pal[((hi >> bit) & 1) << 1 | ((lo >> bit) & 1)];
}

static void draw_tile(
    gb_s* gb, int tile_idx, int dst_x, int dst_y, int scale, bool flip_x, bool unsigned_addr
)
{
    uint16_t tile_addr = tile_addr_for_idx(tile_idx, unsigned_addr);
    uint8_t* tile = &gb->vram[tile_addr];

    for (int dy = 0; dy < 8; ++dy)
    {
        uint8_t lo = tile[2 * dy + 0];
        uint8_t hi = tile[2 * dy + 1];

        int run_x = 0;
        while (run_x < 8)
        {
            int sx = flip_x ? run_x : (7 - run_x);
            LCDColor c = tile_pixel_to_color(lo, hi, sx);
            int run_len = 1;
            while (run_x + run_len < 8)
            {
                int sx2 = flip_x ? (run_x + run_len) : (7 - (run_x + run_len));
                if (tile_pixel_to_color(lo, hi, sx2) != c)
                    break;
                run_len++;
            }
            playdate->graphics->fillRect(
                dst_x + run_x * scale, dst_y + dy * scale, run_len * scale, scale, c
            );
            run_x += run_len;
        }
    }
}

static void draw_hud(gb_s* gb, ScriptData* data)
{
    bool seed = !data->hud_seeded || gbScreenRequiresFullRefresh;
    data->hud_seeded = true;
    bool unsigned_addr = gb->gb_reg.LCDC & 0x10;

    for (int i = 0; i < 9; ++i)
    {
        uint8_t tl = gb->vram[PORTRAIT_L_ADDRS[i]];
        uint8_t tr = gb->vram[PORTRAIT_R_ADDRS[i]];

        bool changed_l = seed || tl != data->prev_portrait_l[i];
        bool changed_r = seed || tr != data->prev_portrait_r[i];
        data->prev_portrait_l[i] = tl;
        data->prev_portrait_r[i] = tr;

        bool overlap_l = (i == 2 || i == 5 || i == 8);
        bool overlap_r = (i == 0 || i == 3 || i == 6);

        if (changed_l || overlap_l)
            draw_tile(gb, tl, PORTRAIT_L_X[i], PORTRAIT_Y[i], 2, true, unsigned_addr);
        if (changed_r || overlap_r)
            draw_tile(gb, tr, PORTRAIT_R_X[i], PORTRAIT_Y[i], 2, true, unsigned_addr);
    }
}

static ScriptData* on_begin(gb_s* gb, const char* header_name)
{
    (void)gb;
    (void)header_name;

    force_pref(batching, 0);

    ScriptData* data = allocz(ScriptData);
    data->start_armed = true;
    return data;
}

static void on_end(gb_s* gb, ScriptData* data)
{
    (void)gb;
    cb_free(data);
}

static void on_tick(gb_s* gb, ScriptData* data, int frames_elapsed)
{
    (void)frames_elapsed;

    data->in_game = is_in_game(gb);
    data->logo = is_logo_screen(gb);
    data->cinematic = is_cinematic(gb);
    data->title = is_title_screen(gb);
    data->mode = is_mode_screen(gb);
    data->option = is_option_screen(gb);
    data->char_select = is_char_select(gb);
    data->results = is_results(gb);
    data->demo = is_demo_game(gb);

    if (data->in_game != data->prev_in_game)
    {
        force_pref(batching, data->in_game ? 1 : 0);
        data->prev_in_game = data->in_game;
    }

    if (data->cinematic != data->prev_cinematic)
    {
        force_pref(overclock, data->cinematic ? 2 : 0);
        data->prev_cinematic = data->cinematic;
    }

    if (data->logo || data->cinematic || data->title)
    {
        PDButtons pushed;
        playdate->system->getButtonState(NULL, &pushed, NULL);

        if ((pushed & kButtonA) && data->start_armed)
        {
            data->send_start = START_PULSE_TICKS;
            data->a_blocked = A_BLOCK_TICKS;
            data->start_armed = false;
        }

        if (data->send_start > 0)
        {
            script_gb->direct.joypad_bits.a = 1;
            script_gb->direct.joypad_bits.b = 1;
            script_gb->direct.joypad_bits.start = 0;
            data->send_start--;
            if (data->send_start == 0)
                data->suppress_start = 1;
        }

        if (!(pushed & kButtonA) && data->send_start == 0)
            data->start_armed = true;
    }

    if (data->a_blocked > 0)
    {
        script_gb->direct.joypad_bits.a = 1;
        script_gb->direct.joypad_bits.b = 1;
        data->a_blocked--;
    }

    if (data->suppress_start > 0)
    {
        script_gb->direct.joypad_bits.start = 1;
        data->suppress_start--;
    }

    if (data->char_select)
    {
        for (int i = 0; i < 14; i++)
            gb->vram[CHAR_DST[i]] = gb->vram[CHAR_SRC[i]];
        gb->vram[0x1881] = 0;
        gb->vram[0x1882] = 0;
        gb->vram[0x188C] = 0;
        gb->vram[0x188D] = 0;
        gb->vram[0x188E] = 0;
    }

    if (data->in_game || data->demo)
    {
        for (int i = 0; i < 7; i++)
        {
            gb->vram[NAME_L_DST[i]] = gb->vram[NAME_L_ADDRS[i]];
            gb->vram[NAME_R_DST[i]] = gb->vram[NAME_R_ADDRS[i]];
        }
        for (int i = 0; i < 3; i++)
        {
            gb->vram[WIN_L_DST[i]] = gb->vram[WIN_L_ADDRS[i]];
            gb->vram[WIN_R_DST[i]] = gb->vram[WIN_R_ADDRS[i]];
        }
    }
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    game_picture_x_offset = CB_LCD_X;
    game_hide_indicator = true;

    if (data->in_game || data->demo)
    {
        game_picture_y_top = 0;
        game_picture_y_bottom = 120;
        game_picture_scaling = 0;
        game_picture_background_color = kColorBlack;
    }
    else if (data->cinematic)
    {
        game_picture_y_top = 24;
        game_picture_y_bottom = LCD_HEIGHT;
        game_picture_scaling = 0;
        game_picture_background_color = kColorBlack;
    }
    else if (data->results)
    {
        game_picture_y_top = 24;
        game_picture_y_bottom = LCD_HEIGHT;
        game_picture_scaling = 0;
        game_picture_background_color = kColorWhite;
    }
    else if (data->char_select)
    {
        game_picture_y_top = 24;
        game_picture_y_bottom = LCD_HEIGHT;
        game_picture_scaling = 0;
        game_picture_background_color = kColorWhite;
    }
    else if (data->title || data->mode)
    {
        game_picture_y_top = 17;
        game_picture_y_bottom = 133;
        game_picture_scaling = 0;
        game_picture_background_color = kColorWhite;
    }
    else if (data->option)
    {
        game_picture_y_top = 0;
        game_picture_y_bottom = LCD_HEIGHT;
        game_picture_scaling = 3;
        game_picture_background_color = kColorWhite;
    }
    else if (data->logo)
    {
        game_picture_y_top = 0;
        game_picture_y_bottom = 132;
        game_picture_scaling = 0;
        game_picture_background_color = kColorWhite;
    }
    else
    {
        game_picture_y_top = 17;
        game_picture_y_bottom = 133;
        game_picture_scaling = 0;
        game_picture_background_color = kColorWhite;
    }

    if (!data->in_game && !data->demo)
    {
        data->sidebar_cleared = false;
        data->refresh_frames = 0;
        data->hud_seeded = false;
        return;
    }

    bool entering = !data->sidebar_cleared;

    if (entering)
    {
        const int left_w = game_picture_x_offset;
        const int right_x = game_picture_x_offset + LCD_WIDTH * 2;
        const int right_w = LCD_COLUMNS - right_x;
        if (left_w > 0)
            playdate->graphics->fillRect(0, 0, left_w, LCD_ROWS, kColorBlack);
        if (right_w > 0)
            playdate->graphics->fillRect(right_x, 0, right_w, LCD_ROWS, kColorBlack);

        data->refresh_frames = SETTLE_TICKS;
        data->sidebar_cleared = true;
    }

    if (data->refresh_frames > 0)
    {
        data->refresh_frames--;
        return;
    }

    draw_hud(gb, data);
}

C_SCRIPT{
    .rom_name = "WH2JET",
    .description = DESCRIPTION,
    .experimental = false,
    .launch_color = ScriptPreferredLaunchColor_White,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};
