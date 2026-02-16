#include "../scriptutil.h"

#define DESCRIPTION \
    "- Moves HUD to sidebar\n" \
    "- Can press Ⓐ or Ⓑ in most situations where Start/Select would be needed.\n" \
    "- Automatically enables frame blending while under water (flicker transparency effect)\n." \
    "\nCreated by: NaOH (Sodium Hydroxide)"

#define ASSETS_DIR SCRIPT_ASSETS_DIR "trip-world/"

typedef struct ScriptData
{
    bool prev_show_sidebar;
    
    int prev_lives, prev_hp, prev_score;
    
    int flags_changing;
    
    uint16_t glyphs12[0x60][12];
} ScriptData;

#define USERDATA ScriptData* data

static void handle_flicker(gb_s* gb, ScriptData* data)
{
    data->flags_changing = 5;
}

SCRIPT_BREAKPOINT(0x0892)
{
    handle_flicker(gb, data);
}

SCRIPT_BREAKPOINT(0x08D2)
{
    handle_flicker(gb, data);
}

SCRIPT_BREAKPOINT(0x0BC7)
{
    handle_flicker(gb, data);
}

SCRIPT_BREAKPOINT(0x0D81)
{
    handle_flicker(gb, data);
}

SCRIPT_BREAKPOINT(0x1106)
{
    handle_flicker(gb, data);
}

SCRIPT_BREAKPOINT(0x1937)
{
    handle_flicker(gb, data);
}

SCRIPT_BREAKPOINT(0x1C01A)
{
    handle_flicker(gb, data);
}

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    ScriptData* data = allocz(ScriptData);
    
    force_pref(crank_mode, CRANK_MODE_OFF);
    force_pref(crank_dock_button, PREF_BUTTON_NONE);
    force_pref(crank_undock_button, PREF_BUTTON_NONE);

    game_picture_background_color = kColorBlack;
    int s = script_load_tiles12(SCRIPT_ASSETS_DIR "glyph12", data->glyphs12, 0x60);
    playdate->system->logToConsole("script tiles loaded: %d", s);
    
    // press A, intro cutscene
    poke_verify(2, 0x6B7C, 0x5F, 0x47);
    
    // press A, main menu
    poke_verify(2, 0x68EA, 0x5F, 0x47);
    poke_verify(2, 0x68AA, 0x5F, 0x47);
    poke_verify(2, 0x694C, 0x5F, 0x47);
    poke_verify(2, 0x6A62, 0x5F, 0x47);
    
    // press A, game over
    poke_verify(2, 0x6FF3, 0x5F, 0x47);
    
    // TODO: breakpoint to reduce frequency of FF9C flickering (depth inversion)
    
    // 0:0892: RRA for player flickering.
    // 0:0BC7: for enemy flickering?
    // 7:4D30?
    // 7:401A?
    // 7:403C?
    // 7:4EE2?
    
    SET_BREAKPOINTS(0);

    return data;
}

static int get_score(gb_s* gb)
{
    int score = 0;
    for (int addr = 0x9C26; addr <= 0x9C2B; ++addr)
    {
        score *= 10;
        score += (ram_peek(addr) - 0x50) % 10;
    }
    return score;
}

static void on_tick(gb_s* gb, ScriptData* data, int frames_elapsed)
{
    bool show_sidebar = gb->gb_reg.WY == 0x80;
    
    force_pref(blend_frames, data->flags_changing > 0);
    
    if (data->flags_changing > 0)
    {
        data->flags_changing -= frames_elapsed;
    }
    
    if (show_sidebar)
    {
        // flush left
        game_picture_x_offset = 0;
        
        // 100% vertical scaling
        game_picture_scaling = 0;
        game_picture_y_top = 3;
        game_picture_y_bottom = 123;
    }
    else
    {
        // standard
        game_picture_x_offset = CB_LCD_X;
        game_picture_scaling = 3;
        game_picture_y_top = 0;
        game_picture_y_bottom = LCD_HEIGHT;
    }
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    uint8_t* lcd = playdate->graphics->getFrame();
    int rowbytes = PLAYDATE_ROW_STRIDE;
    
    bool show_sidebar = gb->gb_reg.WY == 0x80;
    
    int lives = ram_peek(0xC0E1);
    int hp = ram_peek(0xFFA0);
    int score = get_score(gb);
    
    bool refresh_lives = lives != data->prev_lives;
    bool refresh_hp = hp != data->prev_hp;
    bool refresh_score = score != data->prev_score;
    
    if (show_sidebar)
    {
        if (show_sidebar && (!data->prev_show_sidebar || gbScreenRequiresFullRefresh))
        {
            refresh_lives = true;
            refresh_hp = true;
            refresh_score = true;
            
            playdate->graphics->fillRect(320, 0, 80, 240, kColorWhite);
            playdate->graphics->fillRect(320, 0, 1, 240, kColorBlack);
            playdate->graphics->fillRect(321, 0, 2, 240, (uintptr_t)&lcdp_50);
            
            script_draw_string12(data->glyphs12, lcd, rowbytes, "LIFE", ' ', 328, 50);
            script_draw_string12(data->glyphs12, lcd, rowbytes, "SCORE", ' ', 325, 240-16-12-2);
            
            playdate->graphics->markUpdatedRows(0, 240);
        }
        
        if (refresh_score)
        {
            int s = score;
            for (int i = 0; i < 6; ++i)
            {
                int d = s % 10;
                s /= 10;
                script_draw_tiles12(data->glyphs12, lcd, rowbytes, '0' - ' ' + d, 325 + (5-i)*12, 240-14);
            }
            
            playdate->graphics->markUpdatedRows(240 - 15, 240);
        }
        
        if (refresh_lives)
        {
            for (int i = 0; i < 4; ++i)
            {
                int t = ram_peek(0x9C0E + i);
                draw_vram_tile(t, true, 2, 320 + 16*i + 8, 4);
            }
            playdate->graphics->markUpdatedRows(0, 32);
        }
        
        if (refresh_hp)
        {
            for (int i = 0; i < 4; ++i)
            {
                int t = ram_peek(0x9C06+ i);
                draw_vram_tile(t, true, 2, 320 + 16*i + 8, 50+12+4);
            }
            playdate->graphics->markUpdatedRows(50+12, 50+12+32);
        }
    }
    
    data->prev_show_sidebar = show_sidebar;
}

static void on_end(gb_s* gb, ScriptData* data)
{
    cb_free(data);
}

C_SCRIPT{
    .rom_name = "TRIP WORLD",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};
