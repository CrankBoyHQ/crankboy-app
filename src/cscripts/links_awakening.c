#include "../scriptutil.h"

#define DESCRIPTION                                                              \
    "- HUD is now on the side of the screen, to take advantage of widescreen.\n" \
    "- Full aspect ratio; no vertical squishing.\n"                              \
    "- Open save menu by docking the Crank.\n"                                   \
    "- In fishing mini-game: CCW Crank = Reel, CW Crank = Cast.\n"               \
    "- Intro cutscene can be skipped with Ⓐ.\n"

#define FISHING_SCENE_CHECKSUM 11276

#define CRANK_INPUT_THRESHOLD 45.0f
#define MAX_CRANK_VELOCITY_DEG_PER_FRAME 70.0f
#define PRESS_COOLDOWN_FRAMES_60FPS 8
#define PRESS_COOLDOWN_FRAMES_30FPS 4
#define LOCKOUT_DURATION_FRAMES_60FPS 30
#define LOCKOUT_DURATION_FRAMES_30FPS 15

typedef struct ScriptData
{
    int sidebar_x_prev;
    uint8_t bgp_prev;
    unsigned inventoryB;
    unsigned inventoryA;
    unsigned rupees;
    unsigned hearts;
    unsigned heartsMax;

    // Link's Awakening: DX
    bool is_ladx;

    // --- fishing mini-game ---
    bool is_in_fishing_scene;
    float crank_cw_accumulator;
    float crank_ccw_accumulator;
    int input_cooldown_timer;
    int lockout_timer;

    uint32_t tilemap_checksum;
    uint32_t tiledata_checksum;
} ScriptData;

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    ScriptData* data = allocz(ScriptData);

    force_pref(crank_mode, CRANK_MODE_START_SELECT);
    force_pref(crank_down_action, 0);
    force_pref(crank_dock_button, PREF_BUTTON_START_SELECT);
    force_pref(crank_undock_button, PREF_BUTTON_NONE);

    force_pref(dither_stable, false);
    force_pref(dither_line, 0);

    data->is_in_fishing_scene = false;
    data->crank_cw_accumulator = 0.0f;
    data->crank_ccw_accumulator = 0.0f;
    data->input_cooldown_timer = 0;
    data->lockout_timer = 0;

    data->is_ladx = (rom_peek(0xE64) == 0xF0);

    // can pause using just start+select --
    if (rom_peek(0xE64) == 0xF0)
    {
        // LADX
        rom_poke(0xE64, 0xC0);
    }
    else if (rom_peek(0xAB9) == 0xF0)
    {
        // non-LADX
        rom_poke(0xAB9, 0xC0);
    }

    // press A to finish intro cut-scene
    if (data->is_ladx && rom_peek(0x6E2B) == 0x80)
    {
        // LADX
        rom_poke(0x6E2B, 0x90);
    }
    else if (rom_peek(0x6E4B) == 0x80)
    {
        rom_poke(0x6E4B, 0x90);
    }

    return data;
}

static void on_end(gb_s* gb, ScriptData* data)
{
    cb_free(data);
}

static void on_tick(gb_s* gb, ScriptData* data, int frames_elapsed)
{
    uint32_t vram_checksum = 0;
    for (int y = 0; y < 10; y++)
    {
        for (int x = 0; x < 10; x++)
        {
            vram_checksum += gb->vram[0x1800 + y * 32 + x];
        }
    }

    bool is_fishing_now = (vram_checksum == FISHING_SCENE_CHECKSUM);

    if (is_fishing_now != data->is_in_fishing_scene)
    {
        if (is_fishing_now)
        {
            force_pref(crank_mode, CRANK_MODE_OFF);
        }
        else
        {
            force_pref(crank_mode, CRANK_MODE_START_SELECT);
        }
        data->is_in_fishing_scene = is_fishing_now;
    }

    if (data->input_cooldown_timer > 0)
    {
        data->input_cooldown_timer--;
    }
    if (data->lockout_timer > 0)
    {
        data->lockout_timer--;
    }

    if (data->is_in_fishing_scene)
    {
        if (data->lockout_timer > 0)
        {
            data->crank_cw_accumulator = 0;
            data->crank_ccw_accumulator = 0;
        }
        else
        {
            float crank_change = playdate->system->getCrankChange();

            if (fabsf(crank_change) > MAX_CRANK_VELOCITY_DEG_PER_FRAME)
            {
                data->lockout_timer = (frames_elapsed == 2) ? LOCKOUT_DURATION_FRAMES_30FPS
                                                             : LOCKOUT_DURATION_FRAMES_60FPS;
            }
            else
            {
                if (crank_change > 0)
                {
                    data->crank_cw_accumulator += crank_change;
                }
                if (crank_change < 0)
                {
                    data->crank_ccw_accumulator += crank_change;
                }
            }
        }

        if (data->input_cooldown_timer == 0 && data->lockout_timer == 0)
        {
            bool button_pressed = false;

            if (data->crank_cw_accumulator >= CRANK_INPUT_THRESHOLD)
            {
                script_gb->direct.joypad_bits.up = 0;
                data->crank_cw_accumulator = 0.0f;
                button_pressed = true;
            }
            else if (data->crank_ccw_accumulator <= -CRANK_INPUT_THRESHOLD)
            {
                script_gb->direct.joypad_bits.a = 0;
                data->crank_ccw_accumulator = 0.0f;
                button_pressed = true;
            }

            if (button_pressed)
            {
                data->input_cooldown_timer = (frames_elapsed == 2) ? PRESS_COOLDOWN_FRAMES_30FPS
                                                                    : PRESS_COOLDOWN_FRAMES_60FPS;
            }
        }
    }
    else
    {
        data->input_cooldown_timer = 0;
        data->lockout_timer = 0;
        data->crank_cw_accumulator = 0.0f;
        data->crank_ccw_accumulator = 0.0f;
    }

    int game_state = ram_peek(0xDB95);
    bool gameOver = ram_peek(0xFF9C) >= 3;  // not positive about this

    game_hide_indicator = false;

    switch (game_state)
    {
    case 0:  // intro
    case 2:  // file select
    case 6:  // save
        game_picture_background_color = kColorBlack;
        break;
    case 7:  // map
        game_picture_background_color = get_palette_color(1);
        game_hide_indicator = true;
        break;
    case 0xB:
        game_picture_background_color = get_palette_color(3);
        if (gameOver)
            game_picture_background_color = kColorBlack;
        break;
    default:
        game_picture_background_color = get_palette_color(gb->gb_reg.BGP & 3);
        break;
    }

    game_picture_x_offset = CB_LCD_X;
    game_picture_y_top = 0;
    game_picture_y_bottom = LCD_HEIGHT;
    game_picture_scaling = 3;

    unsigned menu_y = ram_peek(0xDB9A);

    // in regular gameplay and/or paused
    if (game_state == 0xB && !gameOver)
    {
        game_hide_indicator = true;

        // scroll screen to side
        game_picture_x_offset = MIN((0x80 - MIN(menu_y, 0x80)) / 3, CB_LCD_X);

        // corresponding vertical scaling
        switch (game_picture_x_offset)
        {
        case 0 ... 7:
            game_picture_y_top = 3;
            game_picture_scaling = 0;
            break;
        case 8 ... 15:
            game_picture_y_top = 3;
            game_picture_scaling = 24;
            break;
        case 16 ... 23:
            game_picture_y_top = 2;
            game_picture_scaling = 12;
            break;
        case 24 ... 31:
            game_picture_y_top = 2;
            game_picture_scaling = 6;
            break;
        case 32 ... 39:
            game_picture_y_top = 1;
            game_picture_scaling = 4;
            break;
        }

        // calculate bounds correctly
        if (game_picture_scaling > 0)
        {
            game_picture_y_bottom =
                (LCD_ROWS * game_picture_scaling) / (2 * game_picture_scaling - 1);
        }
        else
        {
            game_picture_y_bottom = 120;
        }
        game_picture_y_bottom += game_picture_y_top;
    }
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    int sidebar_x = game_picture_x_offset * 2 + 320;
    int sidebar_w = 80;
    uint8_t game_state = ram_peek(0xDB95);

    bool hud_data_changed = false;
    if (game_state == 0xB)
    {
        // Checksum for tile map (the 2 rows of 13 tiles for the inventory)
        uint32_t tilemap_checksum = 0;
        for (int i = 0; i < 13; i++)
            tilemap_checksum += gb->vram[0x1C00 + i];
        for (int i = 0; i < 13; i++)
            tilemap_checksum += gb->vram[0x1C20 + i];

        if (tilemap_checksum != data->tilemap_checksum)
        {
            data->tilemap_checksum = tilemap_checksum;
            hud_data_changed = true;
        }

        // Checksum for tile data (hearts and inventory items)
        uint32_t tiledata_checksum = 0;
        uint8_t indices[30];
        int num_indices = 0;

        // Heart tile indices
        indices[num_indices++] = 0x7F;  // Empty heart slot
        indices[num_indices++] = 0xCD;  // Heart container
        indices[num_indices++] = 0xCE;  // Half heart
        indices[num_indices++] = 0xA9;  // Full heart

        // Inventory tile indices from the tile map
        for (int i = 0; i < 13; i++)
            indices[num_indices++] = gb->vram[0x1C00 + i];
        for (int i = 0; i < 13; i++)
            indices[num_indices++] = gb->vram[0x1C20 + i];

        for (int i = 0; i < num_indices; i++)
        {
            uint8_t tile_idx = indices[i];
            uint16_t tile_addr = 0x8000 | (16 * (uint16_t)tile_idx);
            if (tile_idx < 0x80)
                tile_addr += 0x1000;

            uint8_t* tile_data = &gb->vram[tile_addr & 0x1FFF];
            for (int byte = 0; byte < 16; byte++)
            {
                tiledata_checksum += tile_data[byte];
            }
        }

        if (tiledata_checksum != data->tiledata_checksum)
        {
            data->tiledata_checksum = tiledata_checksum;
            hud_data_changed = true;
        }
    }

    bool refresh = gbScreenRequiresFullRefresh || (data->sidebar_x_prev != sidebar_x) ||
                   (data->bgp_prev != gb->gb_reg.BGP) || hud_data_changed;
    data->sidebar_x_prev = sidebar_x;
    data->bgp_prev = gb->gb_reg.BGP;

    if (game_state == 0xB && game_picture_x_offset < CB_LCD_X)
    {
        if (refresh)
        {
            // draw sidebar
            playdate->graphics->fillRect(sidebar_x, 0, sidebar_w, LCD_ROWS, kColorWhite);
        }

        unsigned hearts = ram_peek(0xDB5A);
        unsigned heartsMax = ram_peek(0xDB5B);
        unsigned invB = ram_peek(0xDB00) ^ gb->vram[16 * gb->vram[0x9C26]];
        unsigned invA = ram_peek(0xDB01) ^ gb->vram[16 * gb->vram[0x9C21]];
        unsigned rupees =
            (gb->vram[0x1C2A] << 16) | (gb->vram[0x1C2B] << 8) | (gb->vram[0x1C2C] << 0);

        const bool inventory_changed =
            (invB != data->inventoryB || invA != data->inventoryA || rupees != data->rupees);

        const bool fade = data->bgp_prev == 0x0 || data->bgp_prev == 0xFF;

        if (refresh || inventory_changed)
        {
            // margins
            int xm = 2;
            int ym = 4;

            bool changed[] = {
                invB != data->inventoryB, invA != data->inventoryA, rupees != data->rupees
            };

            // items and rupees
            // rupees (k=2) are only 3 tiles wide
            // items are 5 tiles wide.
            for (int k = 0; k < 3; ++k)
            {
                if (!changed[k] && !refresh)
                    continue;

                for (int y = 0; y <= 1; ++y)
                {
                    int twidth = (k == 2 ? 3 : 5);
                    for (int x = 0; x < twidth; ++x)
                    {
                        int src_x = k * 5 + x;
                        int src_y = y;

                        int dst_x = sidebar_x + sidebar_w / 2 - 8 * twidth + 16 * x + xm;
                        int dst_y = ym + y * 16 + k * 38;

                        uint8_t tile_idx = gb->vram[0x1C00 + 0x20 * src_y + src_x];

                        if (fade)
                        {
                            playdate->graphics->fillRect(dst_x, dst_y, 16, 16, kColorWhite);
                        }
                        else
                        {
                            draw_vram_tile(tile_idx, true, 2, dst_x, dst_y);
                        }
                        playdate->graphics->markUpdatedRows(dst_y, dst_y + 15);
                    }
                }
            }
        }

        if (refresh)
        {
            playdate->graphics->fillRect(
                sidebar_x, 0, 2, LCD_ROWS, fade ? kColorWhite : kColorBlack
            );
        }

        // hearts
        if (hearts != data->hearts || heartsMax != data->heartsMax || refresh)
        {
            for (int i = 0; i < 14; ++i)
            {
                int y = 120 + 16 * (i % 7);
                int x = sidebar_x + sidebar_w / 2 - 8 + 16 * (i >= 7);
                if (heartsMax >= 8)
                    x -= 8;

                uint8_t idx = 0x7F;
                if (i < heartsMax)
                {
                    idx = 0xCD;
                }
                if (i * 8 < hearts && i * 8 + 7 >= hearts)
                {
                    // half-heart
                    idx = 0xCE;
                }
                else if (i * 8 < hearts)
                {
                    idx = 0xA9;
                }

                if (idx == 0x7F || fade)
                {
                    playdate->graphics->fillRect(x, y, 16, 16, kColorWhite);
                }
                else
                {
                    draw_vram_tile(idx, true, 2, x, y);
                }

                playdate->graphics->markUpdatedRows(y, y + 15);
            }
        }
        data->hearts = hearts;
        data->heartsMax = heartsMax;
        data->rupees = rupees;
        data->inventoryA = invA;
        data->inventoryB = invB;
    }
    else
    {
        // TODO -- things to draw in other states?
    }
}

C_SCRIPT{
    .rom_name = "ZELDA",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};
