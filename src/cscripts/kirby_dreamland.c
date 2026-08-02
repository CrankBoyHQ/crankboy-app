#include "../scenes/game_scene.h"
#include "../scriptutil.h"

#define DESCRIPTION                                                              \
    "- HUD is now on the side of the screen, to take advantage of widescreen.\n" \
    "- Full aspect ratio; no vertical squishing.\n"                              \
    "- Use the crank to flap!\n"                                                 \
    "- Start/Select buttons are no longer required anywhere.\n"                  \
    "\nCreated by: NaOH (Sodium Hydroxide)"

#define CRANK_DELTA_SMOOTH_FACTOR 0.8f
#define MIN_RATE_CRANK_BEGIN_FLAP 0.5f
#define MIN_RATE_CRANK_SUCK 2.3f
#define MIN_RATE_CRANK_FLAP 0.3f
#define MAX_RATE_CRANK_FLAP 45.0f
#define MIN_HYST_CRANK_BEGIN_FLAP 9.0f
#define MIN_HYST_CRANK_BEGIN_SUCK 9.0f
#define CRANK_MAX_HYST 10.0f

#define KIRBY_ASSETS_DIR SCRIPT_ASSETS_DIR "kirby-dreamland/"

// -- ram addr --
// y speed - d078
// input - ff8b
// flags -- ff8f
// player state flags - ff8e

#define KIRBY_HOLDING_MASK 0x0C
#define KIRBY_HOLDING_VALUE 0x08
#define flip_spit_enabled (preferences_script_A == 0)

// custom data for script.
typedef struct ScriptData
{
    float crank_angle;
    float crank_delta;
    float crank_delta_smooth;
    float crank_hyst;
    bool suck;

    CodeReplacement* patch_no_door;
    CodeReplacement* patch_start_flying;

    LCDBitmap* sidebar;

    // 12x12 tiles
    uint16_t tiles12[20][12];
    uint8_t lives;
    uint8_t health;
    uint8_t boss;

    uint32_t score;

    bool fly_thrust_enabled;
    int fly_thrust;
    bool continue_flying;

    // Previous values for dirty tracking
    uint8_t prev_lives;
    uint8_t prev_health;
    uint8_t prev_boss;
    uint32_t prev_score;
    bool prev_in_game;

} ScriptData;

// this define is used by SCRIPT_BREAKPOINT
#define USERDATA ScriptData* data

#define CFG_US 0
#define CFG_JP 1

static float circle_difference(float a, float b)
{
    float diff = b - a;
    diff = fmodf(diff + 180.0f, 360.0f);
    if (diff < 0.0f)
    {
        diff += 360.0f;
    }
    return diff - 180.0f;
}

// can also start the game with 'start'
SCRIPT_BREAKPOINT(BANK_ADDR(6, 0x4096), BANK_ADDR(6, 0x4096))
{
    if ($A == 0x8)
    {
        $A = 1;
    }
}

// force immediate unpause
SCRIPT_BREAKPOINT(BANK_ADDR(6, 0x460E), BANK_ADDR(6, 0x460E))
{
    $A = 0x8;
}

// suck via crank
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x437F), BANK_ADDR(1, 0x437C))
{
    if (data->suck)
        $A |= K_BUTTON_B;
}

// continue to suck via crank
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x479C), BANK_ADDR(1, 0x4799))
{
    if (data->suck)
        $A |= K_BUTTON_B;
}

// Start flying via crank
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x4494), BANK_ADDR(1, 0x4491))
{
    if (data->crank_angle >= 0 && data->crank_hyst >= 0)
    {
        if (circle_difference(data->crank_hyst, data->crank_angle) >= MIN_HYST_CRANK_BEGIN_FLAP)
        {
            if (data->crank_delta > MIN_RATE_CRANK_BEGIN_FLAP)
            {
                $A |= 0x40;
            }
        }
    }
}

SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x3c8), BANK_ADDR(0, 0x3c8))
{
    if (data->fly_thrust_enabled && data->fly_thrust < 0)
    {
        $A = -data->fly_thrust;
    }
}

SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x3FB), BANK_ADDR(0, 0x3FB))
{
    if (data->fly_thrust_enabled && data->fly_thrust >= 0)
    {
        $A = data->fly_thrust;
    }
}

SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x467E), BANK_ADDR(1, 0x467B))
{
    if (data->continue_flying)
    {
        $A |= K_BUTTON_UP;
    }
}

static void force_prefs(void)
{
    // we're replacing the crank functionality entirely
    force_pref(crank_mode, CRANK_MODE_OFF);
    force_pref(crank_dock_button, PREF_BUTTON_NONE);
    force_pref(crank_undock_button, PREF_BUTTON_NONE);
    force_pref(dither_stable, false);
    force_pref(dither_line, 0);
}

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    printf("Hello from C!\n");

    game_picture_background_color = kColorWhite;
    game_menu_button_input_enabled = 0;

    force_prefs();

    ScriptData* data = allocz(ScriptData);

    const char* err = NULL;
    data->sidebar =
        playdate->graphics->loadBitmap(CB_get_forwarded_path(KIRBY_ASSETS_DIR "sidebar"), &err);

    if (err || !data->sidebar)
    {
        printf("Script error loading bitmap: %s\n", err);
        cb_free(data);
        return NULL;
    }

    for (int i = 0; i < 20; ++i)
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

    // no pausing
    poke_verify(0, 0x22C, 0xCB, 0xAF);
    poke_verify(0, 0x22D, 0x5F, 0xAF);

    // Configuration mode with down+'B'
    poke_verify(6, 0x4083, 0x86, 0x82);

    // Extra game mode with up+'A'
    poke_verify(6, 0x4088, 0x45, 0x41);

    // can start game with 'A'
    poke_verify(6, 0x4096, 0xE6, 0xFE);
    poke_verify(6, 0x4097, 0x08, 0x01);
    poke_verify(6, 0x4098, 0x28, 0x20);

    romaddr_t cave_1_addr, cave_1_size;
    find_code_cave(1, &cave_1_addr, &cave_1_size);

    if (cave_1_size < 40)
    {
        script_error("Failed to find bank 1 code cave.");
        return NULL;
    }

    // margins
    cave_1_addr += 4;
    cave_1_size -= 8;

#define PLACEHOLDER 0x00

    const unsigned config = strcmp(header_name, "KIRBY DREAM LAND") ? CFG_JP : CFG_US;

    data->patch_no_door = code_replacement(0, 0x04C5, (0x28, 0x06), (0x00, 0x00), true);

    data->patch_start_flying = (config == CFG_JP)
                                   ? code_replacement(1, 0x4495, (0x27, 0x45), (0x97, 0x44), true)
                                   : code_replacement(1, 0x4498, (0x2A, 0x45), (0x9A, 0x44), true);

    SET_BREAKPOINTS(config);

    return data;
}

static void on_settings(ScriptData* data)
{
    const char* off_on_options[] = {"Reversed", "Normal", NULL};
    script_custom_setting_add(
        "Spit Crank", "While Kirby has something inhaled, reverse crank inputs", off_on_options
    );
}

static void on_end(gb_s* gb, ScriptData* data)
{
    if (data->sidebar)
        playdate->graphics->freeBitmap(data->sidebar);
    code_replacement_free(data->patch_no_door);
    code_replacement_free(data->patch_start_flying);

    cb_free(data);
}

static void on_tick(gb_s* gb, ScriptData* data)
{
    bool in_game = gb->gb_reg.WY >= 100 && gb->gb_reg.WX < 100;

    if (in_game)
    {
        // flush left
        game_picture_x_offset = 0;

        // 100% vertical scaling
        game_picture_scaling = 0;
        game_picture_y_top = 2;  // bias to show more of top of screen than bottom
        game_picture_y_bottom = 122;
    }
    else
    {
        // standard display
        game_picture_x_offset = CB_LCD_X;
        game_picture_scaling = 3;
        game_picture_y_top = 0;
        game_picture_y_bottom = LCD_HEIGHT;
    }

    bool start_flying_via_crank = false;
    bool continue_flying = false;

    float new_crank_angle = playdate->system->getCrankAngle();
    if (playdate->system->isCrankDocked())
        new_crank_angle = -1;

    if (new_crank_angle >= 0 && data->crank_angle >= 0)
    {
        data->crank_delta = circle_difference(data->crank_angle, new_crank_angle);
        if (data->crank_hyst < 0)
            data->crank_hyst = new_crank_angle;
        else
        {
            float cd = circle_difference(data->crank_hyst, new_crank_angle);
            if (cd > CRANK_MAX_HYST)
                data->crank_hyst = nnfmodf(new_crank_angle - CRANK_MAX_HYST, 360.0f);
            else if (cd < -CRANK_MAX_HYST)
                data->crank_hyst = nnfmodf(new_crank_angle + CRANK_MAX_HYST, 360.0f);
        }

        data->crank_delta_smooth = data->crank_delta_smooth * CRANK_DELTA_SMOOTH_FACTOR +
                                   (1 - CRANK_DELTA_SMOOTH_FACTOR) * data->crank_delta;
    }
    else
    {
        data->crank_delta = 0;
        data->crank_hyst = new_crank_angle;
    }

    uint8_t kirby_state = ram_peek(0xFF8E);

    bool ignore_crank = (kirby_state & KIRBY_HOLDING_MASK) == KIRBY_HOLDING_MASK;

    bool flip_spit = flip_spit_enabled && (kirby_state & KIRBY_HOLDING_MASK) == KIRBY_HOLDING_VALUE;
    float suck_dir = flip_spit ? -1.0f : 1.0f;

    // crank to suck
    if (!ignore_crank && data->crank_angle >= 0 && data->crank_hyst >= 0)
    {
        if (data->suck || suck_dir * (circle_difference(data->crank_hyst, data->crank_angle) +
                                      data->crank_delta) <=
                              -MIN_HYST_CRANK_BEGIN_SUCK)
        {
            data->suck = false;
            if (suck_dir * data->crank_delta_smooth < -MIN_RATE_CRANK_SUCK)
            {
                data->suck = true;
            }
        }
        else
        {
            data->suck = false;
        }
    }
    else
    {
        data->suck = false;
    }

    // crank to flap
    // While spitting (flip_spit) the crank is dedicated to the spit action and
    // Kirby cannot fly with a full mouth, so suppress flap to avoid the same
    // forward rotation also triggering flight inputs.
    int fly_thrust;
    bool has_fly_thrust = false;
    if (!ignore_crank && !flip_spit &&
        ($JOYPAD & (K_BUTTON_UP | K_BUTTON_DOWN) && !data->suck) == 0)
    {
        if (data->crank_angle >= 0 && data->crank_hyst >= 0)
        {
            if (circle_difference(data->crank_hyst, data->crank_angle) + data->crank_delta >=
                MIN_HYST_CRANK_BEGIN_FLAP)
            {
                if (data->crank_delta > MIN_RATE_CRANK_BEGIN_FLAP)
                {
                    start_flying_via_crank = true;
                }
            }
        }

        int fly_max_speed;

        // rather arbitrary control logic, best I could do.
        // feel free to disrespect.
        if (data->crank_delta_smooth > MIN_RATE_CRANK_FLAP)
        {
            float rate =
                MAX(0, MIN(data->crank_delta_smooth, MAX_RATE_CRANK_FLAP)) / MAX_RATE_CRANK_FLAP;
            fly_thrust = -0x20 + 0x70 * rate;
            has_fly_thrust = true;
            fly_max_speed = -0x200 * rate;
            int current_speed = (ram_peek(0xD078) << 8) | ram_peek(0xD079);
            if (current_speed >= 0x8000)
            {
                current_speed = current_speed - 0x10000;
            }
            if (current_speed < fly_max_speed)
            {
                fly_thrust = -0x20;
                has_fly_thrust = true;
            }

            if (fly_thrust >= 0)
            {
                // quadratic thrust scaling
                fly_thrust = ((float)fly_thrust / 0x50) * ((float)fly_thrust / 0x50) * 0x50;
                continue_flying = true;
            }

            // decrease downward thrust greatly
            if (fly_thrust < 0)
            {
                fly_thrust /= 4;

                if (fly_thrust < 0 && fly_thrust >= -7)
                {
                    fly_max_speed = -0x10 * fly_thrust;
                    if (current_speed > fly_max_speed)
                    {
                        // cap out
                        fly_thrust = 4;
                        continue_flying = 0;
                    }
                }
            }
            else if (current_speed < 0 && fly_thrust < 4)
            {
                fly_thrust = 4;
            }
        }
        else
        {
            has_fly_thrust = false;
        }

        if (has_fly_thrust)
        {
            // TODO
        }
    }

    code_replacement_apply(data->patch_start_flying, start_flying_via_crank);
    code_replacement_apply(data->patch_no_door, start_flying_via_crank);

    data->continue_flying = continue_flying;

    if (has_fly_thrust)
    {
        data->fly_thrust_enabled = true;
        data->fly_thrust = fly_thrust;
    }
    else
    {
        data->fly_thrust_enabled = false;
    }

    data->crank_angle = new_crank_angle;
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    if (game_picture_x_offset != 0)
    {
        data->prev_in_game = false;
        return;
    }

    bool full_refresh = !data->prev_in_game || gbScreenRequiresFullRefresh;

    uint8_t newlives = ram_peek(0xD089);
    uint8_t newhealth = ram_peek(0xD086);
    uint8_t boss = ram_peek(0xD093);
    uint8_t boss_visible = ram_peek(0xFF8F);
    uint32_t newscore = ram_peek(0xD070) | (ram_peek(0xD071) << 8) | (ram_peek(0xD072) << 16) |
                        (ram_peek(0xD073) << 24);

    uint8_t effective_boss = (boss_visible & 0x80) ? boss : 0xFF;

    if (!full_refresh)
    {
        if (newlives == data->prev_lives && newhealth == data->prev_health &&
            effective_boss == data->prev_boss && newscore == data->prev_score)
        {
            return;
        }
    }

    uint8_t* lcd = playdate->graphics->getFrame();
    if (!lcd)
        return;

    int rowbytes = PLAYDATE_ROW_STRIDE;

    if (gbScreenRequiresFullRefresh && data->sidebar)
    {
        playdate->graphics->drawBitmap(data->sidebar, 320, 0, kBitmapUnflipped);
    }

    bool refresh_lives = full_refresh || newlives != data->prev_lives;
    bool refresh_health = full_refresh || newhealth != data->prev_health;
    bool refresh_boss = full_refresh || effective_boss != data->prev_boss;
    bool refresh_score = full_refresh || newscore != data->prev_score;

    // lives
    if (refresh_lives)
    {
        data->lives = newlives;
        data->prev_lives = newlives;

        int y = 0;
        int x = 376;
        script_draw_tiles12(data->tiles12, lcd, rowbytes, (newlives / 10) % 20, x, y);
        script_draw_tiles12(data->tiles12, lcd, rowbytes, (newlives % 10) % 20, x + 12, y);

        playdate->graphics->markUpdatedRows(y, y + 11);
    }

    // health
    if (refresh_health)
    {
        data->health = newhealth;
        data->prev_health = newhealth;

        for (int i = 0; i < 6; ++i)
        {
            int x = 350 - 4;
            int y = 58 + 14 * i;

            int idx = (i < newhealth) ? 10 : 15;

            script_draw_tiles12(data->tiles12, lcd, rowbytes, idx, x, y);
            playdate->graphics->markUpdatedRows(y, y + 11);
        }
    }

    // boss
    if (refresh_boss)
    {
        data->boss = effective_boss;
        data->prev_boss = effective_boss;

        const bool show = effective_boss != 0xFF;

        // boss display
        int x = 370;
        int y = 66;

        script_draw_tiles12(data->tiles12, lcd, rowbytes, show ? 12 : 19, x, y);
        script_draw_tiles12(data->tiles12, lcd, rowbytes, show ? 13 : 19, x + 12, y);
        script_draw_tiles12(data->tiles12, lcd, rowbytes, show ? 17 : 19, x, y + 12);
        script_draw_tiles12(data->tiles12, lcd, rowbytes, show ? 18 : 19, x + 12, y + 12);

        y += 24;
        x += 6;

        for (int i = 0; i < 6; ++i)
        {
            const bool disp = (i < effective_boss && show);

            script_draw_tiles12(data->tiles12, lcd, rowbytes, disp ? 11 : 19, x, y);
            script_draw_tiles12(data->tiles12, lcd, rowbytes, disp ? 16 : 19, x, y + 12);
            playdate->graphics->markUpdatedRows(y, y + 13);

            y += 14;
        }
    }

    // score
    if (refresh_score)
    {
        data->score = newscore;
        data->prev_score = newscore;

        int y = 240 - 13;
        bool isDrawing = 0;
        for (int i = 0; i < 5; ++i)
        {
            int digit = (newscore >> (8 * i)) & 0xFF;
            if (i == 4)
                digit = 0;
            int x = 320 + 12 + 12 * i;
            if (digit > 0 || isDrawing || i == 4)
            {
                isDrawing = 1;
                script_draw_tiles12(data->tiles12, lcd, rowbytes, digit % 20, x, y);
            }
            else
            {
                // clear
                script_draw_tiles12(data->tiles12, lcd, rowbytes, 19, x, y);
            }
        }

        playdate->graphics->markUpdatedRows(y, y + 11);
    }

    data->prev_in_game = true;
}

C_SCRIPT{
    .rom_name = "KIRBY DREAM LAND",
    .description = DESCRIPTION,
    .experimental = false,
    .launch_color = ScriptPreferredLaunchColor_White,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_settings = (CS_OnSettings)on_settings,
    .on_end = (CS_OnEnd)on_end,
};

C_SCRIPT{
    .rom_name = "HOSHINOKA-BI",
    .description = DESCRIPTION,
    .experimental = false,
    .launch_system = ScriptPreferredLaunchSystem_DMG,
    .launch_color = ScriptPreferredLaunchColor_White,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_settings = (CS_OnSettings)on_settings,
    .on_end = (CS_OnEnd)on_end,
};
