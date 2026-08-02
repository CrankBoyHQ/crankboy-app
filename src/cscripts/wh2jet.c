#include "../scenes/game_scene.h"
#include "../scriptutil.h"

#include <pd_api/pd_api_gfx.h>

#define DESCRIPTION                                                 \
    "- Custom in-game HUD at the top, for clean 2x scaling.\n"      \
    "- Press Ⓐ on the Title and other screens instead of Start.\n"  \
    "- Press Ⓑ on the Options screen instead of Start to return.\n" \
    "\nCreated by: stonerl"

#define SETTLE_TICKS 60
#define A_BLOCK_TICKS 15
#define START_PULSE_TICKS 2

typedef struct ScriptData
{
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
    uint32_t prev_data_hash_l[9];
    uint32_t prev_data_hash_r[9];
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
    return gb->vram[0x1869] == 0x84 && gb->vram[0x1814] == 0xA1 && gb->vram[0x19C8] == 0x84 &&
           gb->vram[0x1905] == 0x84;
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

static void draw_hud(gb_s* gb, ScriptData* data)
{
    bool seed = !data->hud_seeded || gbScreenRequiresFullRefresh;
    data->hud_seeded = true;
    bool unsigned_addr = gb->gb_reg.LCDC & 0x10;

    for (int i = 0; i < 9; ++i)
    {
        int idx_l = gb->vram[PORTRAIT_L_ADDRS[i]];
        int idx_r = gb->vram[PORTRAIT_R_ADDRS[i]];

        uint16_t addr_l = script_vram_tile_addr(idx_l, unsigned_addr);
        uint16_t addr_r = script_vram_tile_addr(idx_r, unsigned_addr);
        uint32_t hash_l = *(uint32_t*)&gb->vram[addr_l];
        uint32_t hash_r = *(uint32_t*)&gb->vram[addr_r];

        bool changed_l = seed || hash_l != data->prev_data_hash_l[i];
        bool changed_r = seed || hash_r != data->prev_data_hash_r[i];
        data->prev_data_hash_l[i] = hash_l;
        data->prev_data_hash_r[i] = hash_r;

        bool overlap_l = (i == 2 || i == 5 || i == 8);
        bool overlap_r = (i == 0 || i == 3 || i == 6);

        if (changed_l || overlap_l)
            script_draw_vram_tile_fixed(
                gb, script_vram_tile_addr(idx_l, unsigned_addr), PORTRAIT_L_X[i], PORTRAIT_Y[i], 2,
                2, true, false, false
            );
        if (changed_r || overlap_r)
            script_draw_vram_tile_fixed(
                gb, script_vram_tile_addr(idx_r, unsigned_addr), PORTRAIT_R_X[i], PORTRAIT_Y[i], 2,
                2, true, false, false
            );
    }
}

static void draw_options_sidebar(gb_s* gb)
{
    script_draw_vram_tile_fixed(gb, 0x0A20, 0, 0, 2, 2, true, false, false);
    for (int y = 16; y < 224; y += 16)
        script_draw_vram_tile_fixed(gb, 0x0A50, 0, y, 2, 2, true, false, false);
    script_draw_vram_tile_fixed(gb, 0x0A60, 0, 224, 2, 2, true, false, false);

    script_draw_vram_tile_fixed(gb, 0x0A40, 384, 0, 2, 2, true, false, false);
    for (int y = 16; y < 224; y += 16)
        script_draw_vram_tile_fixed(gb, 0x0A50, 384, y, 2, 2, true, false, false);
    script_draw_vram_tile_fixed(gb, 0x0A70, 384, 224, 2, 2, true, false, false);

    for (int x = 16; x < 384; x += 16)
        script_draw_vram_tile_fixed(gb, 0x0A30, x, 0, 2, 2, true, false, false);
    for (int x = 16; x < 384; x += 16)
        script_draw_vram_tile_fixed(gb, 0x0A30, x, 224, 2, 2, true, false, false);
}

static ScriptData* on_begin(gb_s* gb, const char* header_name)
{
    (void)gb;
    (void)header_name;

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

    if (data->option)
    {
        PDButtons pushed;
        playdate->system->getButtonState(NULL, &pushed, NULL);
        if (pushed & kButtonB)
        {
            script_gb->direct.joypad_bits.b = 1;
            script_gb->direct.joypad_bits.start = 0;
        }

        for (int i = 0x1820; i <= 0x1A00; i += 0x20)
            gb->vram[i] = 0xA1;
        for (int i = 0x1821; i <= 0x1A01; i += 0x20)
            gb->vram[i] = 0xA1;
        for (int i = 0x1832; i <= 0x1A12; i += 0x20)
            gb->vram[i] = 0xA1;
        for (int i = 0x1833; i <= 0x1A13; i += 0x20)
            gb->vram[i] = 0xA1;
        for (int i = 0x1820; i <= 0x1833; i++)
            gb->vram[i] = 0xA1;
        for (int i = 0x1A00; i <= 0x1A13; i++)
            gb->vram[i] = 0xA1;
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
        game_picture_y_top = 20;
        game_picture_y_bottom = 132;
        game_picture_scaling = 0;
        game_picture_background_color = kColorBlack;
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

    if (data->option)
    {
        const int left_w = game_picture_x_offset;
        const int right_x = game_picture_x_offset + LCD_WIDTH * 2;
        const int right_w = LCD_COLUMNS - right_x;
        if (left_w > 0)
            playdate->graphics->fillRect(0, 0, left_w, LCD_ROWS, kColorBlack);
        if (right_w > 0)
            playdate->graphics->fillRect(right_x, 0, right_w, LCD_ROWS, kColorBlack);
        draw_options_sidebar(gb);
        return;
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
