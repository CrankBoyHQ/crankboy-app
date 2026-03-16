#include "../preferences.h"
#include "../scriptutil.h"

#define DESCRIPTION                                                                             \
    "- Widescreen display\n"                                                                    \
    "- Can press Ⓐ or Ⓑ in most situations where Start/Select would be needed.\n"               \
    "- Automatically enables frame blending in certain rooms where flicker is used to imitate " \
    "transparency\n"                                                                            \
    "\nCreated by: NaOH (Sodium Hydroxide)"

#define ASSETS_DIR SCRIPT_ASSETS_DIR "cv2br/"

typedef struct ScriptData
{
    bool prev_show_sidebar;

    LCDBitmap* sidebar;

    int time;
    int hearts;
    int score;
    int rest;
    int hp[2];
    int subweapon;
    int subweapon_refresh_countdown;

    int level_start_timer;
    unsigned is_in_stage_select;
    int frames_elapsed;

    // 12x12 tiles
    uint16_t tiles12[15][12];
} ScriptData;

static void drawTile12(ScriptData* data, uint8_t* lcd, int rowbytes, int idx, int x, int y)
{
    uint16_t* tiles12 = &data->tiles12[idx][0];

    for (int i = 0; i < 12; ++i)
    {
        for (int j = 0; j < 12; ++j)
        {
            int _y = (i + y);
            int _x = (x + j);
            int x8 = 7 - (_x % 8);
            lcd[rowbytes * _y + _x / 8] &= ~(1 << x8);
            if (tiles12[i] & (1 << (15 - j)))
            {
                lcd[rowbytes * _y + _x / 8] |= (1 << x8);
            }
        }
    }
}

static void drawBCD12(
    ScriptData* data, uint8_t* lcd, int rowbytes, int bcd, int digits, int x, int y
)
{
    while (digits > 0)
    {
        --digits;
        int v = (bcd >> (digits * 4)) & 0xF;
        drawTile12(data, lcd, rowbytes, v, x, y);
        x += 12;
    }
}

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    ScriptData* data = allocz(ScriptData);

    force_pref(crank_mode, CRANK_MODE_OFF);

    data->sidebar = playdate->graphics->loadBitmap(ASSETS_DIR "sidebar", NULL);
    if (data->sidebar)
    {
        for (int i = 0; i < 15; ++i)
        {
            for (int j = 0; j < 12; ++j)
            {
                data->tiles12[i][j] = 0;
                for (int k = 0; k < 12; ++k)
                {
                    int x = (i % 5) * 12 + k;
                    int y = 240 + (i / 5) * 12 + j;
                    data->tiles12[i][j] |= playdate->graphics->getBitmapPixel(data->sidebar, x, y)
                                           << (15 - k);
                }
            }
        }
    }

    force_pref(dither_stable, false);

    // press A/B to skip logo
    poke_verify(0, 0x3AE, 0xC0, 0xF0);

    // press A/B to skip title reveal
    poke_verify(0, 0x479, 0xC0, 0xF0);

    // press A on title screen
    poke_verify(0, 0x420, 0x80, 0x90);

    // press A/B in crawl
    poke_verify(1, 0x755B, 0xC0, 0xF0);

    // press B in password
    poke_verify(6, 0x5850, 0x40, 0x60);

    // press A in password
    poke_verify(6, 0x58DE, 0x80, 0x90);

    // press A waiting for stage to start
    poke_verify(3, 0x70F1, 0x7E, 0x66);

    return data;
}

#define GAME_STATE_LOGO 0
#define GAME_STATE_TITLE 1
#define GAME_STATE_REEL 3
#define GAME_STATE_PASSWORD 4
#define GAME_STATE_STAGE_SELECT 5
#define GAME_STATE_STAGE_LOADING 6
#define GAME_STATE_IN_GAME 7
#define GAME_STATE_GAME_OVER 8
#define GAME_STATE_SOUND_TEST 9
#define GAME_STATE_UNKNOWN 10

static int get_game_mode(gb_s* gb, ScriptData* data)
{
    int game_state = ram_peek(0xC880);

    if (pd_rev == PD_REV_B)
    {
        force_pref(overclock, 2);  // means overclock ×4
    }

    // just to make QA easier, reduce variables
    force_pref(dither_line, 0);

    switch (game_state)
    {
    case 0:
        return GAME_STATE_LOGO;
    case 1:
    case 2:
        return GAME_STATE_TITLE;
    case 3:
    {
        int test_tile = gb->vram[0x9989 - VRAM_ADDR];
        if (test_tile == 0x6F)
            return GAME_STATE_STAGE_SELECT;
        return GAME_STATE_TITLE;
    }
    break;
    case 4:
        return GAME_STATE_STAGE_LOADING;

    case 5:
    case 6:
        return GAME_STATE_IN_GAME;
    case 7:
        return GAME_STATE_GAME_OVER;
    case 0xA:
        return GAME_STATE_SOUND_TEST;
    case 0xD:
        return GAME_STATE_PASSWORD;
    case 0xE:
        if (gb->gb_reg.SCX == 0)
            return GAME_STATE_TITLE;
        return GAME_STATE_REEL;
    default:
        return GAME_STATE_UNKNOWN;
    }
}

static void on_tick(gb_s* gb, ScriptData* data, int frames_elapsed)
{
    data->frames_elapsed = frames_elapsed;
    int game_state = get_game_mode(gb, data);
    int game_stage = ram_peek(0xC8C0);
    int game_substage = ram_peek(0xC8C1);
    int stages_beaten = ram_peek(0xC8A0);

    game_picture_background_color = kColorWhite;

    // standard 5:6 compression
    game_picture_scaling = 3;
    game_picture_y_top = 0;
    game_picture_x_offset = CB_LCD_X;

    switch (game_state)
    {
    case GAME_STATE_LOGO:
        force_pref(blend_frames, true);
        force_pref(dynamic_rate, DYNAMIC_RATE_ON);
        force_pref(dither_stable, false);
        game_picture_scaling = 0;
        game_picture_y_top = 6;  // eyeballed
        break;
    case GAME_STATE_TITLE:
    case GAME_STATE_REEL:
        force_pref(blend_frames, true);
        force_pref(dynamic_rate, DYNAMIC_RATE_ON);
        force_pref(dither_stable, false);
        game_picture_scaling = 4;
        game_picture_y_top = 4;  // eyeballed
        if (game_state == GAME_STATE_TITLE)
            game_picture_y_top = 8;  // eyeballed
        break;
    case GAME_STATE_PASSWORD:
    case GAME_STATE_SOUND_TEST:
        force_pref(blend_frames, false);
        force_pref(dynamic_rate, DYNAMIC_RATE_OFF);
        force_pref(dither_stable, false);
        game_picture_background_color = kColorBlack;
        break;
    case GAME_STATE_STAGE_SELECT:
        force_pref(blend_frames, false);
        force_pref(dynamic_rate, DYNAMIC_RATE_OFF);
        force_pref(dither_stable, false);
        // 4 scanlines : 7 rows
        game_picture_scaling = 4;
        game_picture_y_top = 4;  // eyeballed
        break;
    case GAME_STATE_STAGE_LOADING:
    case GAME_STATE_IN_GAME:
        game_picture_x_offset = 0;
        game_picture_scaling = 0;
        game_picture_y_top = 4;
        {
            // enable blending only in certain rooms
            bool blend = false;
            if (game_stage == 3 && game_substage == 0)
            {
                blend = true;
            }
            if (game_stage == 3 && game_substage == 3 && ram_peek(0xCA8D) == 2)
            {
                blend = true;
            }
            if (game_stage == 3 && game_substage == 4)
            {
                blend = true;
            }
            force_pref(blend_frames, blend);
            force_pref(dynamic_rate, blend ? DYNAMIC_RATE_ON : DYNAMIC_RATE_OFF);
            force_pref(dither_stable, !blend);
        }
        break;
    case GAME_STATE_GAME_OVER:
        force_pref(blend_frames, true);
        force_pref(dynamic_rate, DYNAMIC_RATE_ON);
        force_pref(dither_stable, false);
        break;
    default:
        break;
    }

    if (game_state == GAME_STATE_STAGE_SELECT)
    {
        data->is_in_stage_select++;
    }
    else
    {
        data->is_in_stage_select = 0;
    }

    // has level intro timer started
    if (game_state == GAME_STATE_STAGE_SELECT && ram_peek(0xCB81) == 7 &&
        (stages_beaten & 0x0F) != 0x0F)
    {
        data->level_start_timer += frames_elapsed;

        int scroll_offset = (data->level_start_timer - 30) / 4;
        if (scroll_offset > 3)
            scroll_offset = 3;

        if (scroll_offset > 0)
        {
            game_picture_y_top += scroll_offset;
        }
    }
    else
    {
        data->level_start_timer = 0;
    }

    // calculate bounds correctly
    if (game_picture_scaling > 0)
    {
        game_picture_y_bottom = (LCD_ROWS * game_picture_scaling) / (2 * game_picture_scaling - 1);
    }
    else
    {
        game_picture_y_bottom = 120;
    }
    game_picture_y_bottom += game_picture_y_top;
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    int game_state = get_game_mode(gb, data);
    int stages_beaten = ram_peek(0xC8A0);
    uint8_t* lcd = playdate->graphics->getFrame();
    int rowbytes = PLAYDATE_ROW_STRIDE;

    int screen_x0 = game_picture_x_offset;
    int screen_x1 = game_picture_x_offset + LCD_WIDTH * 2;

    bool show_sidebar = (game_state == GAME_STATE_IN_GAME);
    bool refresh = show_sidebar != data->prev_show_sidebar || gbScreenRequiresFullRefresh;

    if (refresh)
    {
        data->prev_show_sidebar = show_sidebar;
        if (show_sidebar)
        {
            playdate->graphics->drawBitmap(data->sidebar, 320, 0, kBitmapUnflipped);
        }
        else
        {
            playdate->graphics->fillRect(
                screen_x1, 0, LCD_COLUMNS - screen_x1, LCD_ROWS, kColorWhite
            );
        }
    }

    if (show_sidebar)
    {
        int time = (ram_peek(0xCC81) << 8) | ram_peek(0xCC80);
        int score = (ram_peek(0xC8C4) << 16) | (ram_peek(0xC8C3) << 8) | ram_peek(0xC8C2);
        int hearts = ram_peek(0xCC86);
        int rest = ram_peek(0xC8C5);
        int hp[2] = {ram_peek(0xCC89), ram_peek(0xCC90)};
        int subweapon = ram_peek(0xC8D0);

#define DRAW_BCD(field, digits, x, y)                        \
    if (field != data->field || refresh)                     \
    {                                                        \
        data->field = field;                                 \
        drawBCD12(data, lcd, rowbytes, field, digits, x, y); \
        playdate->graphics->markUpdatedRows(y, y + 11);      \
    }

        DRAW_BCD(time, 3, LCD_COLUMNS - 12 * 3, 2);
        DRAW_BCD(score, 6, LCD_COLUMNS - 12 * 6, LCD_ROWS - 12);
        DRAW_BCD(hearts, 2, screen_x1 + 42, 66);
        DRAW_BCD(rest, 2, LCD_COLUMNS - 12 * 2, 202);

        for (int k = 0; k <= 1; ++k)
        {
            if (hp[k] != data->hp[k] || refresh)
            {
                data->hp[k] = hp[k];

                int x = 25 + screen_x1 + (41 - 25) * k;
                int y = 98;

                for (int i = 0; i < 6; ++i)
                {
                    int tidx = 12;
                    if (data->hp[k] > i * 2)
                        tidx = 11;
                    if (data->hp[k] > i * 2 + 1)
                        tidx = 10;

                    drawTile12(data, lcd, rowbytes, tidx, x, y + (5 - i) * 12);
                }
                playdate->graphics->markUpdatedRows(y, y + 6 * 12 - 1);
            }
        }

        bool refresh_subweapon = refresh;
        if (subweapon != data->subweapon)
        {
            data->subweapon = subweapon;
            data->subweapon_refresh_countdown = 12;
            refresh_subweapon = true;
        }
        if (data->subweapon_refresh_countdown > 0)
        {
            data->subweapon_refresh_countdown -= data->frames_elapsed;
            if (data->subweapon_refresh_countdown <= 0)
            {
                refresh_subweapon = true;
            }
        }

        if (refresh_subweapon)
        {
            int x = screen_x1 + 24;
            int y = 24;

            int sw_t0 = gb->vram_base[0x9E09];
            int sw_t1 = gb->vram_base[0x9E0A];
            int sw_t2 = gb->vram_base[0x9E29];
            int sw_t3 = gb->vram_base[0x9E2A];
            draw_vram_tile(sw_t0, true, 2, x, y);
            draw_vram_tile(sw_t1, true, 2, x + 16, y);
            draw_vram_tile(sw_t2, true, 2, x, y + 16);
            draw_vram_tile(sw_t3, true, 2, x + 16, y + 16);

            playdate->graphics->markUpdatedRows(y, y + 31);
        }
    }

    switch (game_state)
    {
    case GAME_STATE_TITLE:
        // cover up mysterious playdate-only glitch
        playdate->graphics->fillRect(0, LCD_ROWS - 2, LCD_COLUMNS, 2, kColorWhite);
        break;
    case GAME_STATE_PASSWORD:
    case GAME_STATE_SOUND_TEST:
        // mysterious
        playdate->graphics->fillRect(0, 0, screen_x0, LCD_ROWS, kColorBlack);
        playdate->graphics->fillRect(screen_x1, 0, LCD_COLUMNS - screen_x1, LCD_ROWS, kColorBlack);
        break;
    case GAME_STATE_STAGE_SELECT:
    {
        // don't draw bar in castle-appear cutscene
        if ((stages_beaten & 0x0F) == 0x0F)
            break;

        int y0 = get_game_scanline_row(
            game_picture_scaling, preferences_dither_line, 8 - game_picture_y_top
        );
        int y1 = get_game_scanline_row(
            game_picture_scaling, preferences_dither_line, 17 - game_picture_y_top
        );

        // not sure why this is needed

        if (y1 - y0 >= 12 && y1 - y0 <= 18 && data->is_in_stage_select >= 2)
        {
            // bar
            playdate->graphics->fillRect(0, y0, screen_x0, y1 - y0, kColorBlack);
            playdate->graphics->fillRect(
                screen_x1, y0, LCD_COLUMNS - screen_x1, y1 - y0, kColorBlack
            );

            playdate->graphics->fillRect(0, y0, LCD_COLUMNS, 1, kColorBlack);

            playdate->graphics->fillRect(0, y1 - 1, LCD_COLUMNS, 2, kColorBlack);
        }
        if (y0 <= 3 && y0 > 0)
        {
            // top bar
            playdate->graphics->fillRect(0, 0, LCD_COLUMNS, y0 + 1, kColorBlack);
        }
    }
    break;

    default:
        break;
    }
}

static void on_end(gb_s* gb, ScriptData* data)
{
    if (data->sidebar)
        playdate->graphics->freeBitmap(data->sidebar);

    cb_free(data);
}

C_SCRIPT{
    .rom_name = "CASTLEVANIA2 BEL",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};
