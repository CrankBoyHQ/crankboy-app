#include "../scenes/game_scene.h"
#include "../scriptutil.h"

#include <string.h>

#define DESCRIPTION                                                                        \
    "- Lifebars are moved to the sidebars for clean 2x scaling.\n"                         \
    "- Set the round timer position in Settings (permanent) or the menu (session only).\n" \
    "- Press Ⓐ on the Title and Continue screen instead of Start.\n"                       \
    "- Press Ⓑ on the Options screen instead of Start to return.\n"                        \
    "\nCreated by: stonerl"

typedef struct ScriptData
{
    bool in_game;
    uint8_t screen_flag;
    bool sidebar_drawn;
    uint8_t name1_tiles[9];
    uint8_t name2_tiles[9];
    uint8_t p1_bar_tiles[7];
    uint8_t p1_round_tiles[2];
    uint8_t p2_bar_tiles[7];
    uint8_t p2_round_tiles[2];
    uint8_t time_tiles[2];
    int refresh_frames;
    int sidebar_dirty_frames;
} ScriptData;

#define TIME_POS preferences_script_A
enum
{
    TIME_POS_TOP = 0,
    TIME_POS_BOTTOM = 1,
    TIME_POS_OFF = 2,
};

static const char* time_pos_options[] = {"Top", "Bottom", "Hidden", NULL};
static PDMenuItem* time_pos_menu_item;

// Alternating black/white rows, starting with black on row 0.
static const uint8_t lcdp_stripe_bw[8] = {
    0x55, 0xFF, 0x55, 0xFF, 0x55, 0xFF, 0x55, 0xFF,
};

static uint16_t tile_addr_for_idx(gb_s* gb, int tile_idx)
{
    uint16_t tile_addr;
    if (gb->gb_reg.LCDC & 0x10)
    {
        // Unsigned indexing, base 0x8000.
        tile_addr = (uint16_t)tile_idx * 16;
    }
    else
    {
        // Signed indexing, base 0x8800.
        tile_addr = 0x1000 + ((int8_t)tile_idx) * 16;
    }
    return tile_addr & 0x1FFF;
}

static LCDColor tile_pixel_to_color(uint8_t lo, uint8_t hi, int bit)
{
    const uint8_t color = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
    switch (color)
    {
    case 0:
        return kColorWhite;
    case 1:
        return (LCDColor)&lcdp_75;  // light gray
    case 2:
        return (LCDColor)&lcdp_50;  // dark gray
    default:
        return kColorBlack;
    }
}

// Draw a single 8x8 tile from VRAM into the sidebar. Supports flip, 90° rotation, and dithering.
static void draw_tile_attr(
    gb_s* gb, int tile_idx, uint8_t attr, int dst_x, int dst_y, int scale_x, int scale_y,
    bool rotate90
)
{
    uint16_t tile_addr = tile_addr_for_idx(gb, tile_idx);

    bool fx = (attr & 0x20) != 0;
    bool fy = (attr & 0x40) != 0;

    uint8_t* tile = &gb->vram[tile_addr];

    for (int dy = 0; dy < 8; ++dy)
    {
        for (int dx = 0; dx < 8; ++dx)
        {
            int sx = rotate90 ? (7 - dy) : dx;  // rotate clockwise
            int sy = rotate90 ? dx : dy;
            if (fx)
                sx = 7 - sx;
            if (fy)
                sy = 7 - sy;
            uint8_t lo = tile[2 * sy + 0];
            uint8_t hi = tile[2 * sy + 1];
            LCDColor c = tile_pixel_to_color(lo, hi, 7 - sx);
            int px = dst_x + dx * scale_x;
            int py = dst_y + dy * scale_y;
            playdate->graphics->fillRect(px, py, scale_x, scale_y, c);
        }
    }
}

static bool tile_is_transient_black(uint8_t tile_idx)
{
    return tile_idx == 0x4D || tile_idx == 0x40 || tile_idx == 0x00;
}

static bool is_title_screen(gb_s* gb)
{
    // Title tilemap signature in VRAM (0x996A, 0x996B, 0x996C, 0x995D, 0x996E).
    return gb->vram[0x196A] == 0x9D && gb->vram[0x196B] == 0x9E && gb->vram[0x196C] == 0x8B &&
           gb->vram[0x196D] == 0x9C && gb->vram[0x196E] == 0x9E;
}

static bool name_tile_is_blank(gb_s* gb, int tile_idx)
{
    if (tile_idx == 0x4F || tile_is_transient_black((uint8_t)tile_idx))
        return true;

    uint16_t tile_addr = tile_addr_for_idx(gb, tile_idx);

    uint8_t* tile = &gb->vram[tile_addr];

    for (int i = 0; i < 16; i += 2)
    {
        if (tile[i] | tile[i + 1])
            return false;
    }
    return true;
}

static void draw_tile_or_black(
    gb_s* gb, int tile_idx, uint8_t attr, int dst_x, int dst_y, int scale_x, int scale_y,
    bool rotate90
)
{
    if (tile_is_transient_black((uint8_t)tile_idx))
    {
        playdate->graphics->fillRect(dst_x, dst_y, 8 * scale_x, 8 * scale_y, kColorBlack);
        return;
    }
    draw_tile_attr(gb, tile_idx, attr, dst_x, dst_y, scale_x, scale_y, rotate90);
}

static bool update_tile_block(gb_s* gb, int base, int count, uint8_t* tiles_out)
{
    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        uint8_t tidx = gb->vram[base + i];

        if (tiles_out[i] != tidx)
        {
            tiles_out[i] = tidx;
            changed = true;
        }
    }
    return changed;
}

static void draw_name_column(
    gb_s* gb, const uint8_t* tiles, int count, int x, int y, int scale_x, int scale_y, int step,
    bool skip_blanks, bool clear_column, LCDColor clear_color
)
{
    int first = 0;
    int last = count - 1;

    if (skip_blanks)
    {
        first = count;
        last = -1;
        for (int i = 0; i < count; ++i)
        {
            if (!name_tile_is_blank(gb, tiles[i]))
            {
                if (i < first)
                    first = i;
                if (i > last)
                    last = i;
            }
        }
        if (first == count)
        {
            first = 0;
            last = -1;
        }
    }

    if (clear_column)
    {
        playdate->graphics->fillRect(x, y, 8 * scale_x, count * step, clear_color);
    }

    int draw_y = y;
    for (int i = first; i <= last; ++i, draw_y += step)
    {
        draw_tile_or_black(gb, tiles[i], 0x20, x, draw_y, scale_x, scale_y, false);
    }

    playdate->graphics->markUpdatedRows(y, y + count * step);
}

static void draw_lifebar(
    gb_s* gb, const uint8_t* tiles, int segments, int x, int start_y, int step, int scale_x,
    int scale_y, uint8_t attr, bool reverse_order
)
{
    int y = start_y;
    for (int i = 0; i < segments; ++i)
    {
        int idx = reverse_order ? (segments - 1 - i) : i;
        draw_tile_or_black(gb, tiles[idx], attr, x, y, scale_x, scale_y, true);
        y += step;
    }
}

static void draw_rounds(
    gb_s* gb, const uint8_t* tiles, int rounds, int x, int start_y, int step, int scale_x,
    int scale_y, bool bottom_is_first
)
{
    int y = start_y;
    for (int i = 0; i < rounds; ++i)
    {
        int idx = bottom_is_first ? i : (rounds - 1 - i);
        draw_tile_or_black(gb, tiles[idx], 0, x, y, scale_x, scale_y, false);
        y += step;
    }
}

static void reset_sidebar_cache(ScriptData* data)
{
    memset(data->name1_tiles, 0xFF, sizeof data->name1_tiles);
    memset(data->name2_tiles, 0xFF, sizeof data->name2_tiles);
    memset(data->p1_bar_tiles, 0xFF, sizeof data->p1_bar_tiles);
    memset(data->p1_round_tiles, 0xFF, sizeof data->p1_round_tiles);
    memset(data->p2_bar_tiles, 0xFF, sizeof data->p2_bar_tiles);
    memset(data->p2_round_tiles, 0xFF, sizeof data->p2_round_tiles);
    memset(data->time_tiles, 0xFF, sizeof data->time_tiles);
    data->sidebar_dirty_frames = 0;
}

static ScriptData* on_begin(gb_s* gb, const char* header_name)
{
    ScriptData* data = allocz(ScriptData);

    force_pref(dither_stable, false);
    force_pref(dither_line, 2);

    gbScreenRequiresFullRefresh = true;

    data->screen_flag = 0xFF;  // force an initial refresh
    reset_sidebar_cache(data);

    return data;
}

static void on_end(gb_s* gb, ScriptData* data)
{
    cb_free(data);
}

static void on_tick(gb_s* gb, ScriptData* data, int frames_elapsed)
{
    (void)frames_elapsed;

    // Screen discriminator: WRAM 0xC9A3 values
    // ts=0x2D, csc=0x7F, vss=0x81, ingame=0x50, gameover=0x01.
    uint8_t screen_flag = ram_peek(0xC9A3);

    // Title screen: map Playdate Ⓐ to GB Start.
    if (screen_flag == 0x2D)
    {
        uint8_t title_state_0 = ram_peek(0xC43E);
        uint8_t title_state_1 = ram_peek(0xC43A);  // also used for continue screen
        PDButtons current, pushed, released;
        playdate->system->getButtonState(&current, &pushed, &released);
        if ((is_title_screen(gb) || title_state_1 == 0x00) && (pushed & kButtonA))
        {
            script_gb->direct.joypad_bits.a = 1;
            script_gb->direct.joypad_bits.b = 1;
            script_gb->direct.joypad_bits.start = 0;
        }
        else if ((title_state_0 == 0x00 && title_state_1 == 0x27) && (pushed & kButtonB))
        {
            script_gb->direct.joypad_bits.a = 1;
            script_gb->direct.joypad_bits.b = 1;
            script_gb->direct.joypad_bits.start = 0;
        }
        else
        {
            script_gb->direct.joypad_bits.b = 1;
        }
    }
    else if (screen_flag == 0x50)
    {
        // Ignore start/select during gameplay.
        script_gb->direct.joypad_bits.start = 1;
        script_gb->direct.joypad_bits.select = 1;
    }
    else if (screen_flag == 0x00)
    {
        PDButtons current, pushed, released;
        playdate->system->getButtonState(&current, &pushed, &released);
        if (pushed & kButtonA)
        {
            script_gb->direct.joypad_bits.a = 1;
            script_gb->direct.joypad_bits.b = 1;
            script_gb->direct.joypad_bits.start = 0;
        }
    }
}

static void on_settings(ScriptData* data)
{
    (void)data;
    script_custom_setting_add(
        "Timer position",
        "Controls the round timer\nposition.\n \nAlways hidden when the\ntime limit is disabled.",
        time_pos_options
    );
}

static void on_time_pos_menu(void* userdata)
{
    (void)userdata;
    if (!time_pos_menu_item)
        return;

    int option = playdate->system->getMenuItemValue(time_pos_menu_item);
    if (option < 0 || option > TIME_POS_OFF)
        return;

    preferences_script_A = option;
}

static unsigned on_menu(gb_s* gb, ScriptData* data)
{
    (void)gb;
    (void)data;

    time_pos_menu_item =
        playdate->system->addOptionsMenuItem("Timer", time_pos_options, 3, on_time_pos_menu, NULL);
    if (time_pos_menu_item)
    {
        playdate->system->setMenuItemValue(time_pos_menu_item, TIME_POS);
    }

    return 0;
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    uint8_t screen_flag = ram_peek(0xC9A3);
    uint8_t screen_trans = ram_peek(0xC40E);
    bool prev_in_game = data->in_game;
    uint8_t prev_screen_flag = data->screen_flag;
    data->in_game = (screen_flag == 0x50);
    data->screen_flag = screen_flag;
    bool entering_game = data->in_game && !prev_in_game;

    if (data->in_game)
    {
        if (entering_game)
        {
            data->refresh_frames = 8;  // a few frames to let VRAM settle after transitions
            reset_sidebar_cache(data);
        }

        if (gbScreenRequiresFullRefresh && data->sidebar_dirty_frames < 2)
        {
            data->sidebar_dirty_frames = 2;
        }

        // Center the gameplay with equal sidebars on both sides.
        game_picture_x_offset = CB_LCD_X;  // symmetric sidebars
        game_picture_y_top = 24;  // skip the HUD rows (120 lines left = perfect 2x to 240px)
        game_picture_y_bottom = LCD_HEIGHT;
        game_picture_scaling = 0;
        game_picture_background_color = kColorBlack;
        game_hide_indicator = true;
    }
    else
    {
        // Reset to normal viewport.
        game_picture_x_offset = CB_LCD_X;
        game_picture_y_top = 0;
        game_picture_y_bottom = LCD_HEIGHT;
        game_picture_scaling = 3;

        // Background: white by default; black on title, vs, and game over.
        switch (screen_flag)
        {
        case 0x2D:  // title
        case 0x01:  // game over
        case 0x81:  // vs screen
            game_picture_background_color = kColorBlack;
            game_hide_indicator = true;
            break;
        case 0x7F:
            // Force black/white stripes (black on first row).
            game_picture_background_color =
                (screen_trans != 0x00) ? (LCDColor)&lcdp_stripe_bw : kColorWhite;
            game_hide_indicator = true;
            break;
        default:
            game_picture_background_color = kColorWhite;
            game_hide_indicator = true;
            break;
        }
    }

    if (data->in_game)
    {
        const int left_x = 0;
        const int left_w = game_picture_x_offset;  // left sidebar width
        const int right_x = game_picture_x_offset + LCD_WIDTH * 2;
        const int right_w = LCD_COLUMNS - right_x;  // right sidebar width
        const int sidebar_pad = 4;
        const int name_tiles = 9;
        const int name_scale_x = 2;
        const int name_scale_y = 2;
        const int name_step = 8 * name_scale_y + 2;  // 2px spacing
        const int name_y = 4;
        const int rounds = 2;
        const int rounds_scale_x = 2;
        const int rounds_scale_y = 2;
        const int rounds_step = 8 * rounds_scale_y + 2;  // matches name spacing
        const int bottom_margin = 8;
        const int rounds_start = LCD_ROWS - bottom_margin - rounds * rounds_step;  // bottom aligned
        const int bar_segments = 7;
        const int bar_scale_x = 2;
        const int bar_scale_y = 4;  // double height to fill sidebar
        const int bar_step = 8 * bar_scale_y;
        const int bar_height = bar_segments * bar_step;
        const int bar_start = LCD_ROWS - bottom_margin - bar_height;  // bottom aligned

        if (left_w > 0 || right_w > 0)
        {
            const bool refresh_sidebar = gbScreenRequiresFullRefresh || entering_game ||
                                         !data->sidebar_drawn ||
                                         (screen_flag != prev_screen_flag) ||
                                         data->refresh_frames > 0 || data->sidebar_dirty_frames > 0;

            if (refresh_sidebar)
            {
                if (left_w > 0)
                {
                    playdate->graphics->fillRect(
                        left_x, 0, left_w, LCD_ROWS, game_picture_background_color
                    );
                }
                if (right_w > 0)
                {
                    playdate->graphics->fillRect(
                        right_x, 0, right_w, LCD_ROWS, game_picture_background_color
                    );
                }
                playdate->graphics->markUpdatedRows(0, LCD_ROWS);
                data->sidebar_drawn = true;
            }
            // During the initial settle window, stop after clearing to avoid showing garbage.
            if (data->refresh_frames > 0)
            {
                data->refresh_frames--;
                return;
            }

            // Fighter 1 name from tilemap 0x9AA0..0x9AA8 (VRAM offset 0x1AA0).
            {
                const int name_base = 0x1AA0;
                const int name_x = left_x + sidebar_pad;  // left aligned
                bool changed = update_tile_block(gb, name_base, name_tiles, data->name1_tiles);

                if (changed || refresh_sidebar)
                {
                    draw_name_column(
                        gb, data->name1_tiles, name_tiles, name_x, name_y, name_scale_x,
                        name_scale_y, name_step, false, false, game_picture_background_color
                    );
                }
            }

            // Lifebar tiles for player 1: map entries 0x9A82..0x9A88 (VRAM offset 0x1A82..0x1A88).
            {
                const int map_base = 0x1A82;
                const int bar_x = left_x + left_w - sidebar_pad - (8 * bar_scale_x);  // right side
                const int rounds_x = left_x + sidebar_pad;                            // bottom left

                bool bar_changed =
                    update_tile_block(gb, map_base, bar_segments, data->p1_bar_tiles);
                bool rounds_changed = update_tile_block(gb, 0x1A80, rounds, data->p1_round_tiles);

                if (bar_changed || refresh_sidebar)
                {
                    draw_lifebar(
                        gb, data->p1_bar_tiles, bar_segments, bar_x, bar_start, bar_step,
                        bar_scale_x, bar_scale_y, 0, false
                    );
                    playdate->graphics->markUpdatedRows(bar_start, bar_start + bar_height);
                }

                // Round indicators for player 1: tiles at 0x9A80, 0x9A81.
                if (rounds_changed || refresh_sidebar)
                {
                    draw_rounds(
                        gb, data->p1_round_tiles, rounds, rounds_x, rounds_start, rounds_step,
                        rounds_scale_x, rounds_scale_y, false
                    );
                    playdate->graphics->markUpdatedRows(
                        rounds_start, rounds_start + rounds * rounds_step
                    );
                }
            }

            // Fighter 2 name (drawn vertically on the right), tilemap 0x9AAB..0x9AB3.
            {
                const int name_base = 0x1AAB;  // 0x9AAB
                const int name_x =
                    right_x + right_w - sidebar_pad - (8 * name_scale_x);  // right aligned

                bool changed = update_tile_block(gb, name_base, name_tiles, data->name2_tiles);

                if (changed || refresh_sidebar)
                {
                    draw_name_column(
                        gb, data->name2_tiles, name_tiles, name_x, name_y, name_scale_x,
                        name_scale_y, name_step, true, true, game_picture_background_color
                    );
                }
            }

            // Time indicator (2 tiles at 0x9AA9, 0x9AAA) in the main view.
            {
                const int time_base = 0x1AA9;
                const int time_tiles = 2;
                const int scale_x = 2;
                const int scale_y = 2;
                const int max_w = time_tiles * 8 * scale_x;
                const int time_mode = TIME_POS;

                const int main_left = left_w;
                const int main_right = right_x;
                const int main_w = main_right - main_left;
                int time_x = main_left + (main_w - max_w) / 2;
                if (time_x < main_left)
                    time_x = main_left;

                if (time_mode != TIME_POS_OFF)
                {
                    update_tile_block(gb, time_base, time_tiles, data->time_tiles);
                    bool time_unlimited =
                        (data->time_tiles[0] == 0x4A && data->time_tiles[1] == 0x4B);

                    if (!time_unlimited)
                    {
                        int time_y =
                            (time_mode == TIME_POS_TOP)
                                ? 2
                                : (LCD_ROWS - (8 * scale_y) - 2);  // bottom aligned with 2px border

                        const int inner_w = max_w;
                        const int inner_h = 8 * scale_y;
                        int border_x = time_x - 2;
                        int border_y = time_y - 2;
                        int border_w = inner_w + 4;
                        int border_h = inner_h + 4;

                        if (border_x < 0)
                        {
                            border_w += border_x;
                            border_x = 0;
                        }
                        if (border_y < 0)
                        {
                            border_h += border_y;
                            border_y = 0;
                        }
                        if (border_x + border_w > LCD_COLUMNS)
                            border_w = LCD_COLUMNS - border_x;
                        if (border_y + border_h > LCD_ROWS)
                            border_h = LCD_ROWS - border_y;

                        if (border_w > 0 && border_h > 0)
                        {
                            playdate->graphics->fillRect(
                                border_x, border_y, border_w, border_h, kColorBlack
                            );
                        }

                        int x = time_x;
                        for (int i = 0; i < time_tiles; ++i, x += 8 * scale_x)
                        {
                            draw_tile_attr(
                                gb, data->time_tiles[i], 0x20, x, time_y, scale_x, scale_y, false
                            );
                        }

                        playdate->graphics->markUpdatedRows(border_y, border_y + border_h);
                    }
                }
            }

            // Lifebar tiles for player 2: map entries 0x9A8B..0x9A91 (VRAM offset 0x1A8B..0x1A91),
            // stored right-to-left in VRAM.
            {
                const int map_start = 0x1A8B;
                const int bar_x = right_x + sidebar_pad;  // left side
                const int rounds_x =
                    right_x + right_w - sidebar_pad - (8 * rounds_scale_x);  // bottom right

                bool bar_changed =
                    update_tile_block(gb, map_start, bar_segments, data->p2_bar_tiles);
                bool rounds_changed =
                    update_tile_block(gb, 0x1A93 - (rounds - 1), rounds, data->p2_round_tiles);

                if (bar_changed || refresh_sidebar)
                {
                    draw_lifebar(
                        gb, data->p2_bar_tiles, bar_segments, bar_x, bar_start, bar_step,
                        bar_scale_x, bar_scale_y, 0x60, true
                    );
                    playdate->graphics->markUpdatedRows(bar_start, bar_start + bar_height);
                }

                // Round indicators for player 2: tiles at 0x9A93 (bottom), 0x9A92 (above).
                if (rounds_changed || refresh_sidebar)
                {
                    draw_rounds(
                        gb, data->p2_round_tiles, rounds, rounds_x, rounds_start, rounds_step,
                        rounds_scale_x, rounds_scale_y, true
                    );
                    playdate->graphics->markUpdatedRows(
                        rounds_start, rounds_start + rounds * rounds_step
                    );
                }
            }

            if (data->sidebar_dirty_frames > 0)
            {
                data->sidebar_dirty_frames--;
            }
        }
    }
    else
    {
        data->sidebar_drawn = false;
        data->refresh_frames = 0;
    }
}

C_SCRIPT{
    .rom_name = "STREET FIGHTER 2",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_menu = (CS_OnMenu)on_menu,
    .on_settings = (CS_OnSettings)on_settings,
    .on_end = (CS_OnEnd)on_end,
};
