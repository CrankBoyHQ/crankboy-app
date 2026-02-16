#include "../scriptutil.h"

#define DESCRIPTION \
    "- Experimental: Performance is poor and crank indicator hidden.\n" \
    "- Moves HUD to be on the sides, like the NES ones\n" \
    "\nCreated by: NaOH (Sodium Hydroxide)"

#define ASSETS_DIR SCRIPT_ASSETS_DIR "mmv/"

typedef struct ScriptData
{
    bool prev_show_sidebar;
    
    char prev_lives[2];
    char prev_hp[5];
    char prev_wp_energy[5];
    char prev_wp_icon[2];
    char prev_boss_hp[5];
    
    // FIXME: hacky way to move the crank indicator
    int org_selector_y;
    int org_start_y;
    int org_select_y;
} ScriptData;

#define USERDATA ScriptData* data

// returns true if a change occurred at the given addr
static bool cmp_swap(char* buff, unsigned len, uint16_t addr, uint16_t stride)
{
    bool same = true;
    for (int i = 0; i < len; ++i)
    {
        uint8_t v  = ram_peek(addr + i*stride);
        if (v != buff[i]) same = false;
        buff[i] = v;
    }
    return !same;
}

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    ScriptData* data = allocz(ScriptData);
    data->org_selector_y = -1;
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
    
    CB_GameSceneContext* gameSceneContext = gb->direct.priv;
    CB_GameScene* gameScene = gameSceneContext->scene;
    
    if (data->org_selector_y < 0)
    {
        data->org_selector_y = gameScene->selector.y;
        data->org_select_y = gameScene->selector.selectButtonY - data->org_selector_y;
        data->org_start_y = gameScene->selector.startButtonY - data->org_selector_y;
    }
    
    if (show_sidebar)
    {
        // 100% vertical scaling
        game_picture_scaling = 0;
        game_picture_y_top = 4;
        game_picture_y_bottom = 124;
        game_picture_background_color = kColorWhite;
        game_invert_indicator = true;
        game_hide_indicator = true;
        gameScene->selector.y = 240 - 16*3 - gameScene->selector.containerHeight;
        gameScene->staticSelectorUIDrawn = false;
    }
    else
    {
        // standard
        game_picture_scaling = 3;
        game_picture_y_top = 0;
        game_picture_y_bottom = LCD_HEIGHT;
        game_picture_background_color = kColorBlack;
        game_invert_indicator = false;
        gameScene->selector.y = data->org_selector_y;
        game_hide_indicator = false;
    }
    
    gameScene->selector.selectButtonY = data->org_select_y + gameScene->selector.y;
    gameScene->selector.startButtonY = data->org_start_y + gameScene->selector.y;
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    uint8_t* lcd = playdate->graphics->getFrame();
    int rowbytes = PLAYDATE_ROW_STRIDE;
    
    bool show_sidebar = gb->gb_reg.WY == 0x80;
    
    bool refresh_lives = cmp_swap(data->prev_lives, 2, 0x9C0E, 0x20);
    bool refresh_hp = cmp_swap(data->prev_hp, 5, 0x9C07, 1);
    bool refresh_weapon_energy = cmp_swap(data->prev_wp_energy, 5, 0x9C00, 1);
    bool refresh_weapon_icon = cmp_swap(data->prev_wp_icon, 2, 0x9C25, 1);
    bool refresh_boss_hp = cmp_swap(data->prev_boss_hp, 5, 0x9C0F, 1);
    
    if (show_sidebar)
    {
        if (show_sidebar && (!data->prev_show_sidebar || gbScreenRequiresFullRefresh))
        {
            refresh_lives = true;
            refresh_hp = true;
            refresh_weapon_energy = true;
            refresh_weapon_icon = true;
            refresh_boss_hp = true;
            playdate->graphics->markUpdatedRows(0, 240);
            playdate->graphics->fillRect(0, 0, 40, 240, kColorWhite);
            playdate->graphics->fillRect(360, 0, 40, 240, kColorWhite);
        }
        
        if (refresh_lives)
        {
            int lives_x = 364;
            int lives_y = 240-64;
            draw_vram_tile_ext(ram_peek(0x9C0C), true, 2, lives_x, lives_y, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C0D), true, 2, lives_x+16, lives_y, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C2C), true, 2, lives_x, lives_y+16, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C2D), true, 2, lives_x+16, lives_y+16, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C0E), true, 2, lives_x+8, lives_y+32, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C2E), true, 2, lives_x+8, lives_y+48, gb->gb_reg.BGP, 0);
            playdate->graphics->markUpdatedRows(lives_y, lives_y + 64);
        }
        
        if (refresh_hp)
        {
            int hp_x = 4;
            int hp_y = 0;
            for (int i = 0; i < 5; ++i)
            {
                draw_vram_tile_ext(ram_peek(0x9C07 + i), true, 2, hp_x, hp_y + (4-i)*16, gb->gb_reg.BGP, DRAW_VRAM_TILE_FLAG_TRANSPOSE | DRAW_VRAM_TILE_FLAG_FLIPY);
                draw_vram_tile_ext(ram_peek(0x9C27 + i), true, 2, hp_x + 16, hp_y + (4-i)*16, gb->gb_reg.BGP, DRAW_VRAM_TILE_FLAG_TRANSPOSE | DRAW_VRAM_TILE_FLAG_FLIPY);
            }
            playdate->graphics->markUpdatedRows(hp_y, hp_y + 16*5);
        }
        
        if (refresh_weapon_icon)
        {
            int x = 4;
            int y = 120 - 16;
            draw_vram_tile_ext(ram_peek(0x9C05), true, 2, x, y, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C06), true, 2, x+16, y, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C25), true, 2, x, y+16, gb->gb_reg.BGP, 0);
            draw_vram_tile_ext(ram_peek(0x9C26), true, 2, x+16, y+16, gb->gb_reg.BGP, 0);
            playdate->graphics->markUpdatedRows(y, y + 16*2);
        }
        
        if (refresh_weapon_energy)
        {
            int wp_x = 4;
            int wp_y = 240 - 16*5;
            for (int i = 0; i < 5; ++i)
            {
                draw_vram_tile_ext(ram_peek(0x9C00 + i), true, 2, wp_x, wp_y + (4-i)*16, gb->gb_reg.BGP, DRAW_VRAM_TILE_FLAG_TRANSPOSE | DRAW_VRAM_TILE_FLAG_FLIPY);
                draw_vram_tile_ext(ram_peek(0x9C20 + i), true, 2, wp_x + 16, wp_y + (4-i)*16, gb->gb_reg.BGP, DRAW_VRAM_TILE_FLAG_TRANSPOSE | DRAW_VRAM_TILE_FLAG_FLIPY);
            }
            playdate->graphics->markUpdatedRows(wp_y, wp_y + 16*5);
        }
        
        if (refresh_boss_hp)
        {
            int hp_x = 364;
            int hp_y =0;
            for (int i = 0; i < 5; ++i)
            {
                draw_vram_tile_ext(ram_peek(0x9C0F + i), true, 2, hp_x, hp_y + (4-i)*16, gb->gb_reg.BGP, DRAW_VRAM_TILE_FLAG_TRANSPOSE | DRAW_VRAM_TILE_FLAG_FLIPY);
                draw_vram_tile_ext(ram_peek(0x9C2F + i), true, 2, hp_x + 16, hp_y + (4-i)*16, gb->gb_reg.BGP, DRAW_VRAM_TILE_FLAG_TRANSPOSE | DRAW_VRAM_TILE_FLAG_FLIPY);
            }
            playdate->graphics->markUpdatedRows(hp_y, hp_y + 16*5);
        }
    }
    else
    {
        if (data->prev_show_sidebar)
        {
            playdate->graphics->fillRect(0, 0, 40, 240, kColorBlack);
            playdate->graphics->fillRect(360, 0, 40, 240, kColorBlack);
            playdate->graphics->markUpdatedRows(0, 240);
        }
    }
    
    data->prev_show_sidebar = show_sidebar;
}

static void on_end(gb_s* gb, ScriptData* data)
{
    cb_free(data);
}

C_SCRIPT{
    .rom_name = "MEGAMAN5",
    .description = DESCRIPTION,
    .experimental = true,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};
