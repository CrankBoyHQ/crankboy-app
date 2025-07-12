#include "scriptutil.h"

#define DESCRIPTION                                \
    "(Experimental)\n\n- Use the crank to flap!\n" \
    "- Start/Select buttons are no longer required anywhere."

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
    CodeReplacement* patch_continue_flying;
    CodeReplacement* patch_fly_accel_down;
    CodeReplacement* patch_fly_accel_up;
    
    LCDBitmap* sidebar;
    
    // 12x12 tiles
    uint16_t tiles12[20][12];
    uint8_t lives;
    uint8_t health;
    uint8_t boss;
    
    uint32_t score;
    
} ScriptData;

// this define is used by SCRIPT_BREAKPOINT
#define USERDATA ScriptData* data

#define ROM_US_EU
#define ROM_JP

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
SCRIPT_BREAKPOINT(BANK_ADDR(6, 0x4096))
{
    if ($A == 0x8)
    {
        $A = 1;
    }
}

// force immediate unpause
SCRIPT_BREAKPOINT(BANK_ADDR(6, 0x460E))
{
    $A = 0x8;
}

// suck via crank
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x437F))
{
    if (data->suck)
        $A |= K_BUTTON_B;
}

// continue to suck via crank
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x479C))
{
    if (data->suck)
        $A |= K_BUTTON_B;
}

// Start flying via crank
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x4494))
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

static void force_prefs(void)
{
    // we're replacing the crank functionality entirely
    force_pref(crank_mode, CRANK_MODE_OFF);
    force_pref(crank_dock_button, PREF_BUTTON_NONE);
    force_pref(crank_undock_button, PREF_BUTTON_NONE);
}

static void drawTile12(ScriptData* data, uint8_t* lcd, int rowbytes, int idx, int x, int y)
{
    uint16_t* tiles12 = &data->tiles12[idx][0];
    
    for (int i = 0; i < 12; ++i)
    {
        for (int j = 0; j < 12; ++j)
        {
            int _y = (i + y);
            int _x = (x + j);
            int x8 = 7 - (_x%8);
            lcd[rowbytes * _y + _x/8] &= ~(1 << x8);
            if (tiles12[i] & (1 << (15 - j)))
            {
                lcd[rowbytes * _y + _x/8] |= (1 << x8);
            }
        }
    }
}

static ScriptData* on_begin(struct gb_s* gb, char* header_name)
{
    printf("Hello from C!\n");
    
    game_picture_background_color = kColorWhite;
    
    force_prefs();

    ScriptData* data = allocz(ScriptData);
    
    const char* err = NULL;
    data->sidebar = playdate->graphics->loadBitmap(KIRBY_ASSETS_DIR "sidebar", &err);
    
    if (err || !data->sidebar) script_error("Script error loading bitmap: %s", err);
    if (data->sidebar)
    {
        for (int i = 0; i < 20; ++i)
        {
            for (int j = 0; j < 12; ++j)
            {
                data->tiles12[i][j] = 0;
                for (int k = 0; k < 12; ++k)
                {
                    int x = (i % 5)*12 + k;
                    int y = 240 + (i/5)*12 + j;
                    data->tiles12[i][j] |= playdate->graphics->getBitmapPixel(data->sidebar, x, y) << (15 - k);
                }
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

    unsigned cave_1_addr, cave_1_size;
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

    data->patch_no_door = code_replacement(0, 0x04C5, (0x28, 0x06), (0x00, 0x00), true);

    data->patch_start_flying = code_replacement(1, 0x4498, (0x2A, 0x45), (0x9A, 0x44), true);

    data->patch_continue_flying =
        code_replacement(1, 0x467C, (0xF0, 0x8B), (0x3E, K_BUTTON_UP), true);

    data->patch_fly_accel_down =
        code_replacement(0, 0x3C5, (0xFA, 0x7E, 0xD0), (0x3E, PLACEHOLDER, 0x00), true);

    data->patch_fly_accel_up =
        code_replacement(0, 0x3F8, (0xFA, 0x7E, 0xD0), (0x3E, PLACEHOLDER, 0x00), true);

    SET_BREAKPOINTS(!!strcmp(header_name, "KIRBY DREAM LAND"));

    return data;
}

static void on_end(struct gb_s* gb, ScriptData* data)
{
    code_replacement_free(data->patch_no_door);
    code_replacement_free(data->patch_start_flying);
    code_replacement_free(data->patch_continue_flying);
    code_replacement_free(data->patch_fly_accel_down);
    code_replacement_free(data->patch_fly_accel_up);

    pgb_free(data);
}

static void on_tick(struct gb_s* gb, ScriptData* data)
{
    bool in_game = gb->gb_reg.WY >= 100 && gb->gb_reg.WX < 100;
    
    if (in_game)
    {
        // flush left
        game_picture_x_offset = 0;
        
        // 100% vertical scaling
        game_picture_scaling = 0;
        game_picture_y_top = 4;
        game_picture_y_bottom = 124;
    }
    else
    {
        // standard display
        game_picture_x_offset = PGB_LCD_X;
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

    // crank to suck
    if (data->crank_angle >= 0 && data->crank_hyst >= 0)
    {
        if (data->suck ||
            circle_difference(data->crank_hyst, data->crank_angle) + data->crank_delta <=
                -MIN_HYST_CRANK_BEGIN_SUCK)
        {
            data->suck = false;
            if (data->crank_delta_smooth < -MIN_RATE_CRANK_SUCK)
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
    int fly_thrust;
    bool has_fly_thrust = false;
    if (($JOYPAD & (K_BUTTON_UP | K_BUTTON_DOWN) && !data->suck) == 0)
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

    if (continue_flying)
    {
        u8 buttons = K_BUTTON_UP | $JOYPAD;
        if (buttons != data->patch_continue_flying->tval[1])
        {
            data->patch_continue_flying->applied = false;
            data->patch_continue_flying->tval[1] = buttons;
        }
        code_replacement_apply(data->patch_continue_flying, true);
    }
    else
    {
        code_replacement_apply(data->patch_continue_flying, false);
    }

    if (has_fly_thrust)
    {
        data->patch_fly_accel_down->tval[1] = MAX(-fly_thrust, 0);
        data->patch_fly_accel_down->applied = false;
        data->patch_fly_accel_up->tval[1] = MAX(fly_thrust, 0);
        data->patch_fly_accel_up->applied = false;
        code_replacement_apply(data->patch_fly_accel_down, true);
        code_replacement_apply(data->patch_fly_accel_up, true);
    }
    else
    {
        data->patch_fly_accel_down->applied = true;
        data->patch_fly_accel_up->applied = true;
        code_replacement_apply(data->patch_fly_accel_down, false);
        code_replacement_apply(data->patch_fly_accel_up, false);
    }

    data->crank_angle = new_crank_angle;
}

static void on_draw(struct gb_s* gb, ScriptData* data)
{
    if (game_picture_x_offset != 0)
    {
        return;
    }
    
    uint8_t* lcd = playdate->graphics->getFrame();
    int rowbytes = PLAYDATE_ROW_STRIDE;
    
    if (gbScreenRequiresFullRefresh)
    {
        playdate->graphics->drawBitmap(
            data->sidebar, 320, 0, kBitmapUnflipped
        );
    }
    
    // lives
    uint8_t newlives = ram_peek(0xD089);
    if (newlives != data->lives || gbScreenRequiresFullRefresh)
    {
        data->lives = newlives;
        
        int y = 0;
        int x = 376;
        drawTile12(data, lcd, rowbytes, newlives / 10, x, y);
        drawTile12(data, lcd, rowbytes, newlives % 10, x + 12, y);
        
        playdate->graphics->markUpdatedRows(y, y + 11);
    }
    
    // health
    uint8_t newhealth = ram_peek(0xD086);
    if (newhealth != data->health || gbScreenRequiresFullRefresh)
    {
        data->health = newhealth;
        
        for (int i = 0; i < 6; ++i)
        {
            int x = 350 - 4;
            int y = 58 + 14 * i;
            
            int idx = (i < newhealth)
                ? 10
                : 15;
                
            drawTile12(data, lcd, rowbytes, idx, x, y);
            playdate->graphics->markUpdatedRows(y, y + 11);
        }
    }
    
    // boss
    uint8_t boss = ram_peek(0xD093);
    
    // visible, but empty
    if ((ram_peek(0xFF8F) & 0x80) == 0)
    {
        boss = 0xFF;
    }
    
    if (boss != data->boss || gbScreenRequiresFullRefresh)
    {
        data->boss = boss;
        
        const bool show = boss != 0xFF;
        
        // boss display
        int x = 370;
        int y = 66;
        
        drawTile12(data, lcd, rowbytes, show ? 12 : 19, x, y);
        drawTile12(data, lcd, rowbytes, show ? 13 : 19, x + 12, y);
        drawTile12(data, lcd, rowbytes, show ? 17 : 19, x, y + 12);
        drawTile12(data, lcd, rowbytes, show ? 18 : 19, x + 12, y + 12);
        
        y += 24;
        x += 6;
        
        for (int i = 0; i < 6; ++i)
        {
            const bool disp = (i < boss && show);
            
            drawTile12(data, lcd, rowbytes, disp ? 11 : 19, x, y);
            drawTile12(data, lcd, rowbytes, disp ? 16 : 19, x, y+12);
            playdate->graphics->markUpdatedRows(y, y + 13);
            
            y += 14;
        }
    }
    
    // score
    uint32_t newscore = ram_peek(0xD070)
        | (ram_peek(0xD071) << 8)
        | (ram_peek(0xD072) << 16)
        | (ram_peek(0xD073) << 24);
        
    if (newscore != data->score || gbScreenRequiresFullRefresh)
    {
        int y = 240 - 13;
        bool isDrawing = 0;
        for (int i = 0; i < 5; ++i)
        {
            int digit = (newscore >> (8*i)) & 0xFF;
            if (i == 4) digit = 0;
            int x = 320 + 12 + 12*i;
            if (digit > 0 || isDrawing || i == 4)
            {
                isDrawing = 1;
                drawTile12(
                    data, lcd, rowbytes, digit, x, y
                );
            }
            else
            {
                // clear
                drawTile12(
                    data, lcd, rowbytes, 19, x, y
                );
            }
        }
        
        playdate->graphics->markUpdatedRows(y, y + 11);
        
        data->score = newscore;
    }
}

C_SCRIPT{
    .rom_name = "KIRBY DREAM LAND",
    .description = DESCRIPTION,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};

C_SCRIPT{
    .rom_name = "HOSHI NO KIRBY",
    .description = DESCRIPTION,
    .experimental = true,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};
