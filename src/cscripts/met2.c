#include "../scriptutil.h"

#include "met2.inc"

#define DESCRIPTION \
    "- Adds a map!"

#define HALFTILE_W 8
#define HALFTILE_H 8

#define MET2_ASSETS_DIR SCRIPT_ASSETS_DIR "metroid2/"

#define MAP_FIRST_BANK 9
#define MAP_BANK_COUNT 7

#pragma pack(push, 1)
struct AreaAssociation
{
    uint8_t room_idx;
    uint8_t area_idx : 4;
    uint8_t explored : 1;
    bool empty : 1;
    bool unmapped : 1;
};
#pragma pack(pop)

typedef struct ScriptData
{
    int map_area;

    uint8_t samus_bank;
    uint8_t samus_x;
    uint8_t samus_y;
    bool map_flicker;
    
    uint8_t samus_max_etanks;
    uint16_t samus_disp_energy;
    uint16_t samus_disp_missiles;
    uint16_t metroid_disp;
    uint8_t samus_weapon;
    
    struct AreaAssociation* area_associations;
    
    LCDBitmap* htimg;
    LCDBitmap** glyphs;
    size_t glyph_c;
    
    uint8_t* special_base_tiles;
    uint8_t* halftiles;
    uint8_t* area_explored;
    uint8_t* map_explored;
    uint8_t weapons_collected;
    uint8_t* masked;
    size_t masked_size;
    
    uint8_t door_transition_suppress_map_update;
    
    float crank_save_p;
    float crank_prev;
    const char* prev_msg;
    float prev_xorw;
} ScriptData;

// this define is used by SCRIPT_BREAKPOINT
#define USERDATA ScriptData* data

bool get_coords_in_area(ScriptData* data, unsigned room_idx, unsigned rom_bank, unsigned rom_x, unsigned rom_y, unsigned* x, unsigned* y);

void set_map_explored(ScriptData* data, unsigned bank, unsigned bx, unsigned by, bool explored)
{
    int idx = (bank - MAP_FIRST_BANK)*0x100 + by * 0x10 + bx;
    if (idx != explored)
    {
        data->map_explored[idx] = explored;
        
        struct AreaAssociation association = data->area_associations[(bank-MAP_FIRST_BANK)*0x100 + by*0x10 + bx];
        if (association.area_idx == data->map_area)
        {
            unsigned x, y;
            struct Area* area = &areas[data->map_area];
            get_coords_in_area(data, association.room_idx, bank, bx, by, &x, &y);
            data->area_explored[y*area->w + x] = explored;
        }
    }
}

bool get_map_explored(ScriptData* data, unsigned bank, unsigned bx, unsigned by)
{
    return data->map_explored[(bank - MAP_FIRST_BANK) * 0x100 + by * 0x10 + bx];
}

#define AREA_SECRET_WORLD 0xF

// new save file
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x4E1C))
{
    memset(data->map_explored, 0, 0x100*MAP_BANK_COUNT);
    
    data->weapons_collected = 0;
    
    // explore starting room
    set_map_explored(data, 0xF, 0x5, 0x6, true);
    set_map_explored(data, 0xF, 0x6, 0x6, true);
    set_map_explored(data, 0xF, 0x5, 0x7, true);
    set_map_explored(data, 0xF, 0x6, 0x7, true);
}

// during door transition
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x23A7))
{
    data->door_transition_suppress_map_update = 16;
}

// loading save file
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x4E39))
{
    unsigned save_slot = ram_peek(0xD0A3);
    
    size_t size;
    char* buff = script_load_from_disk(save_slot, &size);
    if (!buff || size != 0x100*MAP_BANK_COUNT/8)
    {
        playdate->system->logToConsole("no save data for slot %x.", save_slot);
        memset(data->map_explored, 0, 0x100*MAP_BANK_COUNT);
    }
    else
    {
        playdate->system->logToConsole("saving script data to slot %x.", save_slot);
        for (int i = 0; i < 0x100*MAP_BANK_COUNT; ++i)
        {
            data->map_explored[i] = bitvec_read_bits((uint8_t*)buff, i, 1);
        }
    }
    
    cb_free(buff);
    
    // force refresh
    data->map_area = AREA_SECRET_WORLD;
    data->samus_bank = 0;
}

// writing save file
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x7B82))
{
    unsigned save_slot = ram_peek(0xD0A3);
    char buff[0x100*MAP_BANK_COUNT/8];
    
    for (int i = 0; i < 0x100*MAP_BANK_COUNT; ++i)
    {
        bitvec_write_bits((void*)&buff[0], i, 1, data->map_explored[i]);
    }
    
    playdate->system->logToConsole("script: saving to slot %x.", save_slot);
    
    script_save_to_disk((void*)&buff[0], sizeof(buff), save_slot);
}

// easy unpause
SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x2D08))
{
    if ($A & 0xF)
    {
        $Fz = false;
    }
}

static void set_z_should_missile_toggle(void)
{
    bool is_crank_mapped = preferences_crank_dock_button != PREF_BUTTON_NONE || preferences_crank_undock_button != PREF_BUTTON_NONE;
    bool is_select_mapped = CB_App->hasSystemAccess && preferences_lock_button == PREF_BUTTON_SELECT;
    if (!is_crank_mapped && !is_select_mapped)
    {
        bool isMissiles = !!(ram_peek(0xD04D) & 8);
        bool should_be_missiles = false;
        if (!playdate->system->isCrankDocked())
        {
            should_be_missiles = true;
            
            float crank_angle = playdate->system->getCrankAngle();
            if (crank_angle >= 90 && crank_angle <= 270)
            {
                should_be_missiles = false;
            }
        }
        
        $Fz = (should_be_missiles == isMissiles);
    }
}

// missile toggle (A)
SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x2D08))
{
    set_z_should_missile_toggle();
}

// missile toggle (B)
SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x220F))
{
    set_z_should_missile_toggle();
}

// clear
SCRIPT_BREAKPOINT(BANK_ADDR(5, 0x41D6))
{
    bool is_crank_mapped = preferences_crank_dock_button == PREF_BUTTON_NONE && preferences_crank_undock_button == PREF_BUTTON_NONE;
    bool is_select_mapped = CB_App->hasSystemAccess && preferences_lock_button == PREF_BUTTON_SELECT;
    if (!is_crank_mapped && !is_select_mapped)
    {
        bool isClear = !!ram_peek(0xD0A4);
        bool should_be_clear = !playdate->system->isCrankDocked();
        
        $Fz = (should_be_clear == isClear);
    }
}

// crank to save
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x5838))
{
    if (data->crank_save_p >= 1)
    {
        data->crank_save_p = 0;
        $Fz = 1;
    }
}

unsigned get_room_h(struct Room* room)
{
    if (room->h == 0) return 16;
    return room->h;
}

uint32_t read_bits(const uint8_t* base, size_t* offset, int count)
{
    uint32_t v = 0;
    while (count--)
    {
        uint8_t b = base[*offset/8];
        b >>= 7 - (*offset%8);
        v |= (b & 1) << count;
        (*offset)++;
    }
    return v;
}

void read_embedding_header(const uint8_t* base, size_t* offset, unsigned* _bank, int* _em_x, int *_em_y)
{
    unsigned em_bank = read_bits(room_embeddings, offset, 3) + MAP_FIRST_BANK;
    CB_ASSERT(em_bank <= 0xF);
    int em_x = read_bits(room_embeddings, offset, 5);
    if (em_x >= 16) em_x -= 32;
    int em_y = read_bits(room_embeddings, offset, 5);
    if (em_y >= 16) em_y -= 32;
    *_bank = em_bank;
    *_em_x = em_x;
    *_em_y = em_y;
}

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    // press A to resume from game-over.
    // also a poor-man's rom check.
    if (rom_peek(0x372A) == 0x8)
    {
        rom_poke(0x372A, 1);
    }
    else
    {
        return NULL;
    }
    
    // press A to start on main menu
    if (rom_peek(5*0x4000 + 0x0249) == 0x8)
    {
        rom_poke(5*0x4000 + 0x0249, 1);
    }
    else
    {
        return NULL;
    }
    
    LCDBitmap* htimg = playdate->graphics->loadBitmap(MET2_ASSETS_DIR "pdimg", NULL);
    if (!htimg) return NULL;
    
    force_pref(crank_mode, CRANK_MODE_OFF);
    
    SET_BREAKPOINTS(0);
    
    ScriptData* data = allocz(ScriptData);
    data->map_area = AREA_SECRET_WORLD;
    data->htimg = htimg;
    data->area_associations = mallocz(0x100*MAP_BANK_COUNT*sizeof(struct AreaAssociation));
    data->map_explored = mallocz(0x100 * MAP_BANK_COUNT);
    
    data->glyphs = split_subimages(playdate->graphics->loadBitmap(MET2_ASSETS_DIR "font", NULL), 16, 16, &data->glyph_c);
    
    for (int i = 0; i < 0x100*MAP_BANK_COUNT; ++i)
    {
        data->area_associations[i].unmapped = true;
    }
    
    for (int ridx = 0; ridx < sizeof(rooms)/sizeof(struct Room); ++ridx)
    {
        struct Room* room = &rooms[ridx];
        struct Area* area = &areas[room->area];
        
        size_t offset = room->data_offset;
        for (int i = 0; i < room->embeddings; ++i)
        {
            unsigned em_bank; int em_x, em_y;
            read_embedding_header(room_embeddings, &offset, &em_bank, &em_x, &em_y);
            
            for (int y = 0; y < get_room_h(room); ++y)
            {
                for (int x = 0; x < room->w; ++x)
                {
                    size_t assoc_idx = 0x100*(em_bank - MAP_FIRST_BANK) + em_x + x + (em_y + y)*0x10;
                    if (read_bits(room_embeddings, &offset, 1))
                    {
                        data->area_associations[assoc_idx].unmapped = false;
                        data->area_associations[assoc_idx].empty = false;
                        data->area_associations[assoc_idx].area_idx = room->area;
                        data->area_associations[assoc_idx].room_idx = ridx;
                    }
                    
                    for (int i = 0; i < area->special_tile_c; ++i)
                    {
                        struct SpecialTile* special_tile = &area->special_tiles[i];
                        if (special_tile->type == TILE_EMPTY)
                        {
                            if (special_tile->area_x == x + room->area_x && special_tile->area_y == y + room->area_y)
                            {
                                data->area_associations[assoc_idx].empty = true;
                                data->area_associations[assoc_idx].unmapped = true;
                            }
                        }
                    }
                }
            }
        }
    }

    return data;
}

const int k = 4;

#define TILE_DARK 32

#define GLYPH_A (272/16)
#define GLYPH_0 (0)
#define GLYPH_DASH (240/16)
#define GLYPH_MISSILES (688/16)
#define GLYPH_MISSILES_EQUIPPED (GLYPH_MISSILES+1)

static void load_map_halftiles(ScriptData* data, int area_idx)
{
    if (data->map_area == area_idx) return;
    data->map_area = area_idx;
    struct Area* area = &areas[area_idx];
    cb_free(data->halftiles);
    cb_free(data->special_base_tiles);
    cb_free(data->area_explored);
    data->halftiles = mallocz(area->w * area->h * 4 * sizeof(data->halftiles[0]));
    data->special_base_tiles = mallocz(area->w * area->h);
    data->area_explored = mallocz(area->w * area->h);
    
    for (int i = 0; i < area->special_tile_c; ++i)
    {
        struct SpecialTile* special_tile = &area->special_tiles[i];
        int x = special_tile->area_x;
        int y = special_tile->area_y;
        
        if (special_tile->type < TILE_REPLACEMENTS)
        {
            data->special_base_tiles[y*area->w + x] = special_tile->type;
            if (special_tile->dark)
            {
                data->special_base_tiles[y*area->w + x] = TILE_DARK;
            }
        }
    }
    
    for (int ridx = 0; ridx < sizeof(rooms)/sizeof(struct Room); ++ridx)
    {
        struct Room* room = &rooms[ridx];
        if (room->area != area_idx) continue;
        size_t tile_present_size = (room->w + 2) * (get_room_h(room) + 2);
        uint8_t tile_present[tile_present_size];
        memset(tile_present, 0, tile_present_size);
        
        size_t offset = room->data_offset;
        for (int i = 0; i < room->embeddings; ++i)
        {
            unsigned em_bank; int em_x, em_y;
            read_embedding_header(room_embeddings, &offset, &em_bank, &em_x, &em_y);
            
            CB_ASSERT(em_bank >= MAP_FIRST_BANK);
            
            for (int y = 0; y < get_room_h(room); ++y)
            {
                for (int x = 0; x < room->w; ++x)
                {
                    if (read_bits(room_embeddings, &offset, 1))
                    {
                        int tile_idx = (y + 1)*(room->w+2) + (x+1);
                        int special_tile = data->special_base_tiles[(y + room->area_y)*area->w + (x + room->area_x)];
                        if (special_tile == TILE_EMPTY)
                        {
                            tile_present[tile_idx] = 0;
                        }
                        else if (special_tile == 0)
                        {
                            tile_present[tile_idx] = 1;
                        }
                        else
                        {
                            tile_present[tile_idx] = special_tile;
                        }
                        
                        if (get_map_explored(data, em_bank, em_x + x, em_y + y) && tile_present[tile_idx])
                        {
                            int area_explore_idx = (y + room->area_y)*area->w + x + room->area_x;
                            CB_ASSERT(area_explore_idx < area->w * area->h);
                            data->area_explored[area_explore_idx] = 1;
                        }
                    }
                }
            }
        }
        
        // decide on all tiles
        for (int y = 1; y <= get_room_h(room); ++y)
        {
            for (int x = 1; x <= room->w; ++x)
            {
                int ht_x = (x-1 + room->area_x)*2;
                int ht_y = (y-1 + room->area_y)*2;
                
                uint8_t* ht_tl = &data->halftiles[(ht_y + 0) * area->w*2 + (ht_x + 0)];
                uint8_t* ht_tr = &data->halftiles[(ht_y + 0) * area->w*2 + (ht_x + 1)];
                uint8_t* ht_bl = &data->halftiles[(ht_y + 1) * area->w*2 + (ht_x + 0)];
                uint8_t* ht_br = &data->halftiles[(ht_y + 1) * area->w*2 + (ht_x + 1)];
                
                int w = room->w+2; // stride
                int tt = tile_present[y*w + x];
                
                if (tt == 1 || tt == TILE_DARK)
                {
                    *ht_tl = 1;
                    *ht_tr = 1;
                    *ht_bl = 1;
                    *ht_br = 1;
                    
                    if (!tile_present[(y-1)*w+x-1])
                        *ht_tl = (k*3 + 0);
                    if (!tile_present[(y+1)*w+x-1])
                        *ht_bl = (k*3 + 1);
                    if (!tile_present[(y+1)*w+x+1])
                        *ht_br = (k*3 + 2);
                    if (!tile_present[(y-1)*w+x+1])
                        *ht_tr = (k*3 + 3);
                    
                    if (!tile_present[(y-1)*w+x-0])
                    {
                        *ht_tl = k*1 + 0;
                        *ht_tr = k*1 + 0;
                    }
                    if (!tile_present[(y-0)*w+x-1])
                    {
                        *ht_tl = k*1 + 1;
                        *ht_bl = k*1 + 1;
                    }
                    if (!tile_present[(y+1)*w+x+0])
                    {
                        *ht_bl = k*1 + 2;
                        *ht_br = k*1 + 2;
                    }
                    if (!tile_present[(y+0)*w+x+1])
                    {
                        *ht_tr = k*1 + 3;
                        *ht_br = k*1 + 3;
                    }
                    
                    if (!tile_present[(y-1)*w+x-0] && !tile_present[(y-0)*w+x-1])
                        *ht_tl = k*2 + 0;
                    if (!tile_present[(y+1)*w+x-0] && !tile_present[(y-0)*w+x-1])
                        *ht_bl = k*2 + 1;
                    if (!tile_present[(y+1)*w+x-0] && !tile_present[(y-0)*w+x+1])
                        *ht_br = k*2 + 2;
                    if (!tile_present[(y-1)*w+x-0] && !tile_present[(y-0)*w+x+1])
                        *ht_tr = k*2 + 3;
                        
                    if (tt == TILE_DARK)
                    {
                        if (*ht_tl == 1) *ht_tl = 2; else *ht_tl += 6*k;
                        if (*ht_tr == 1) *ht_tr = 2; else *ht_tr += 6*k;
                        if (*ht_bl == 1) *ht_bl = 2; else *ht_bl += 6*k;
                        if (*ht_br == 1) *ht_br = 2; else *ht_br += 6*k;
                    }
                }
            }
        }
    }
}

static void on_end(gb_s* gb, ScriptData* data)
{
    if (data->htimg) playdate->graphics->freeBitmap(data->htimg);
    cb_free(data->halftiles);
    cb_free(data->special_base_tiles);
    cb_free(data->area_explored);
    cb_free(data->area_associations);
    cb_free(data->map_explored);
    cb_free(data->masked);
    free_subimages(data->glyphs);
    
    cb_free(data);
}

static void on_tick(gb_s* gb, ScriptData* data, int frames_elapsed)
{
    if (data->door_transition_suppress_map_update > 0)
    {
        data->door_transition_suppress_map_update -= frames_elapsed;
    }
    
    data->weapons_collected |= ram_peek(0xd04D);
    
    if (!ram_peek(0xD07D))
    {
        // not touching save point
        data->crank_save_p = 0;
        data->crank_prev = -1;
    }
    else
    {
        // touching save point
        if (data->crank_save_p < 0 || data->crank_save_p > 20) data->crank_save_p = 0;
        if (playdate->system->isCrankDocked())
        {
            data->crank_prev = -1;
        }
        else
        {
            float crank_angle = playdate->system->getCrankAngle();
            if (data->crank_prev >= 0)
            {
                float crank_amount = fabsf(crank_angle - data->crank_prev) / (float)(frames_elapsed);
                
                crank_amount /= 1.7f;
                
                if (crank_amount > 1)
                {
                    crank_amount = MIN(crank_amount, 6);
                    data->crank_save_p += crank_amount/60.0f;
                }
            }
            data->crank_prev = crank_angle;
        }
        
        // reduce over time
        data->crank_save_p = toward(data->crank_save_p, 0, frames_elapsed / 110.0f);
    }
}

static void draw_halftile(ScriptData* data, int ht_idx, int dst_x, int dst_y)
{
    int w, h, stride;
    uint8_t *mask, *pdata;
    playdate->graphics->getBitmapData(data->htimg, &w, &h, &stride, &mask, &pdata);
    uint8_t* frame = playdate->graphics->getFrame();
    
    int src_x = (ht_idx % k) * HALFTILE_W;
    int src_y = (ht_idx / k) * HALFTILE_H;
    
    CB_ASSERT(HALFTILE_W == 8);
    CB_ASSERT(dst_x % 8 == 0);
    dst_x /= 8;
    src_x /= 8;
    
    for (int i = 0; i < HALFTILE_H; ++i)
    {
        uint8_t pm = mask[(i + src_y) * stride + src_x];
        uint8_t p = pdata[(i + src_y) * stride + src_x] & pm;
        
        int dst_idx = (i + dst_y)*LCD_ROWSIZE + dst_x;
        frame[dst_idx] &= ~pm;
        frame[dst_idx] |= p;
    }
}

static void draw_fulltile(ScriptData* data, int ft_idx, int dst_x, int dst_y)
{
    uint8_t ht[4] = {3, 3, 3, 3};
    switch(ft_idx)
    {
    case TILE_HPIPE:
        ht[0] = 5*k + 0;
        ht[1] = 5*k + 0;
        ht[2] = 5*k + 1;
        ht[3] = 5*k + 1;
        break;
    case TILE_VPIPE:
        ht[0] = 5*k + 2;
        ht[1] = 5*k + 3;
        ht[2] = 5*k + 2;
        ht[3] = 5*k + 3;
        break;
    case TILE_DOOR_NORTH:
        ht[0] = 10*k + 0;
        ht[1] = 10*k + 1;
        break;
    case TILE_DOOR_WEST:
        ht[0] = 10*k + 3;
        ht[2] = 11*k + 3;
        break;
    case TILE_DOOR_SOUTH:
        ht[2] = 11*k + 0;
        ht[3] = 11*k + 1;
        break;
    case TILE_DOOR_EAST:
        ht[1] = 10*k + 2;
        ht[3] = 11*k + 2;
        break;
    case TILE_SHUNT_UL:
        ht[1] = 16*k + 2;
        ht[3] = 17*k + 2;
        break;
    case TILE_SHUNT_UR:
        ht[0] = 16*k + 3;
        ht[2] = 17*k + 3;
        break;
    case TILE_SHUNT_BL:
        ht[0] = 16*k + 0;
        ht[2] = 17*k + 0;
        break;
    case TILE_SHUNT_BR:
        ht[0] = 16*k + 1;
        ht[2] = 17*k + 1;
        break;
    case TILE_EXIT_NORTH:
    case TILE_EXIT_WEST:
    case TILE_EXIT_EAST:
    case TILE_EXIT_SOUTH: {
            ft_idx -= TILE_EXIT_NORTH;
            int a = (ft_idx % 2);
            int b = (ft_idx / 2);
            ht[0] = (12 + b*2)*k + 2*a;
            ht[1] = (12 + b*2)*k + 2*a + 1;
            ht[2] = (12 + b*2 + 1)*k + 2*a;
            ht[3] = (12 + b*2 + 1)*k + 2*a + 1;
        }
        break;
    case TILE_HJUMP:
        ht[0] = 6*k + 0;
        ht[1] = 6*k + 0;
        ht[2] = 6*k + 1;
        ht[3] = 6*k + 1;
        break;
    case TILE_ITEM ... TILE_SHIP_RIGHT: {
            ft_idx -= TILE_ITEM;
            int a = (ft_idx % 2);
            int b = (ft_idx / 2);
            ht[0] = (20 + b*2)*k + 2*a;
            ht[1] = (20 + b*2)*k + 2*a + 1;
            ht[2] = (20 + b*2 + 1)*k + 2*a;
            ht[3] = (20 + b*2 + 1)*k + 2*a + 1;
        }
        break;
    default:
        break;
    }
    
    draw_halftile(data, ht[0], dst_x, dst_y);
    draw_halftile(data, ht[1], dst_x+HALFTILE_W, dst_y);
    draw_halftile(data, ht[2], dst_x, dst_y+HALFTILE_H);
    draw_halftile(data, ht[3], dst_x+HALFTILE_W, dst_y+HALFTILE_H);
}

static void draw_map(ScriptData* data, unsigned area_idx, int dst_x, int dst_y, int window_w, int window_h, int window_x, int window_y)
{
    if (area_idx == AREA_SECRET_WORLD)
    {
    secret_world:
        // noise?
        return;
    }
    
    struct Area* area = &areas[area_idx];
    
    // get area for current cell
    load_map_halftiles(data, area_idx);
    
    if (!data->halftiles) goto secret_world;
    
    for (int y = window_y; y < window_h + window_y; ++y)
    {
        for (int x = window_x; x < window_x + window_w; ++x)
        {
            int dst_x_px = dst_x + (x-window_x) * HALFTILE_W * 2;
            int dst_y_px = dst_y + (y-window_y) * HALFTILE_H * 2;
            
            bool blank = false;
            if (y < 0 || y >= area->h || x < 0 || x >= area->w)
            {
                blank = true;
            }
            else if (!data->area_explored[y*area->w + x])
            {
                blank = true;
            }
            
            for (int yi = 0; yi < 2; ++yi)
            {
                for (int xi = 0; xi < 2; ++xi)
                {
                    int halftile = 0;
                    if (!blank)
                    {
                        halftile = data->halftiles[(y*2 + yi)*area->w*2 + x*2 + xi];
                    }
                    draw_halftile(data, halftile, dst_x_px + HALFTILE_W*xi, dst_y_px + HALFTILE_H*yi);
                }
            }
        }
    }
    
    size_t masked_size = window_w * window_h;
    if (masked_size >= data->masked_size)
    {
        data->masked_size = masked_size;
        data->masked = cb_realloc(data->masked, masked_size);
    }
    for (int i = 0; i < masked_size; ++i) data->masked[i] = 0;
    for (int i = 0; i < area->special_tile_c; ++i)
    {
        struct SpecialTile* special_tile = &area->special_tiles[i];
        if (special_tile->area_x < window_x || special_tile->area_x >= window_x + window_w) continue;
        if (special_tile->area_y < window_y || special_tile->area_y >= window_y + window_h) continue;
        
        int mask_idx = (special_tile->area_y - window_y) * window_w + (special_tile->area_x - window_x);
        CB_ASSERT(mask_idx >= 0 && mask_idx < masked_size);
        
        if (data->masked[mask_idx])
        {
            continue;
        }
        
        if (special_tile->type == TILE_HJUMP)
        // special behaviour for hjump tiles
        {
            int xroot = special_tile->ridx;
            if (!data->area_explored[special_tile->area_y*area->w + xroot]) continue;
        }
        else if (special_tile->type >= TILE_EXIT_NORTH && special_tile->type <= TILE_EXIT_EAST)
        // special behaviour for exit tiles
        {
            int dx = 0;
            int dy = 0;
            
            if (special_tile->type == TILE_EXIT_NORTH) dy = 1;
            if (special_tile->type == TILE_EXIT_SOUTH) dy = -1;
            if (special_tile->type == TILE_EXIT_WEST) dx = 1;
            if (special_tile->type == TILE_EXIT_EAST) dx = -1;
            if (!data->area_explored[(special_tile->area_y + dy)*area->w + (special_tile->area_x + dx)]) continue;
        }
        else if (special_tile->ridx != 0xFF)
        //  check if tile was explored
        {
            if (!data->area_explored[special_tile->area_y*area->w + special_tile->area_x]) continue;
        }
        
        int slot = special_tile->object_slot;
        if (slot >= 0x40 && slot <= 0x7F)
        {
            uint8_t flags = ram_peek(0xC500 + slot);
            
            if (flags != 0xFF) // default state
            {
                // indicates object dead / destroyed / collected
                if (flags & 2) continue;
            }
        }
        
        int dst_x_px = dst_x + (special_tile->area_x - window_x) * HALFTILE_W * 2;
        int dst_y_px = dst_y + (special_tile->area_y - window_y) * HALFTILE_H * 2;
        
        draw_fulltile(data, special_tile->type, dst_x_px, dst_y_px);
        
        // don't draw anything after this tile
        if (special_tile->masking)
        {
            data->masked[mask_idx] = true;
        }
    }
    
    playdate->graphics->markUpdatedRows(dst_y, dst_y + window_h * HALFTILE_H*2 - 1);
}

bool get_coords_in_area(ScriptData* data, unsigned room_idx, unsigned rom_bank, unsigned rom_x, unsigned rom_y, unsigned* x, unsigned* y)
{
    struct Room* room = &rooms[room_idx];
    size_t offset = room->data_offset;
    for (int i = 0; i < room->embeddings; ++i)
    {
        unsigned em_bank; int em_x, em_y;
        read_embedding_header(room_embeddings, &offset, &em_bank, &em_x, &em_y);
        read_bits(room_embeddings, &offset, room->w * get_room_h(room));
        
        if (em_bank == rom_bank)
        {
            *x = (rom_x - em_x) + room->area_x;
            *y = (rom_y - em_y) + room->area_y;
            
            return true;
        }
    }
    
    return false;
}

static void draw_glyph(ScriptData* data, size_t glyph_idx, int x, int y, LCDBitmapFlip flip)
{
    if (glyph_idx >= data->glyph_c) return;
    
    playdate->graphics->drawBitmap(data->glyphs[glyph_idx], x, y, flip);
    playdate->graphics->markUpdatedRows(y, y + 16);
}

static void draw_msg(ScriptData* data, const char* msg)
{
    int len = strlen(msg);
    int x = MAX(0, 160 - len*8);
    for (int i = 0; i < len; ++i)
    {
        char c = msg[i];
        if (c >= 'A' && c <= 'Z')
        {
            draw_glyph(data, GLYPH_A + c - 'A', x, LCD_ROWS - 16, kBitmapUnflipped);
        }
        x += 16;
    }
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    unsigned game_mode = ram_peek(0xFF9B);
    
    game_picture_x_offset = CB_LCD_X;
    game_picture_scaling = 3;
    game_picture_y_top = 0;
    game_picture_y_bottom = LCD_HEIGHT;
    game_hide_indicator = false;
    
    if (game_mode == 4 || game_mode == 5 || game_mode == 6 || game_mode == 7 || game_mode == 8 || game_mode == 9 || game_mode == 10)
    {
        game_hide_indicator = true;
        
        // flush left, expand to hide HUD
        game_picture_x_offset = 0;
        game_picture_scaling = 5;
        game_picture_y_top = 2;
        game_picture_y_bottom = 136;
        
        // cover up top-pixel scaling glitch
        playdate->graphics->fillRect(
            0, 0, LCD_COLUMNS - 80, 1, kColorBlack
        );
        
        if (game_mode == 7) // game over
        {
            game_picture_x_offset = CB_LCD_X;
        }
        else
        {
            if (gb->display.WY <= 0x80)
            {
                unsigned saveMessageCooldownTimer = ram_peek(0xd088);
                unsigned saveContact = ram_peek(0xD07D);
                unsigned itemCollected = ram_peek(0xD093);
                
                float xorw = data->prev_xorw;
                
                // bottom window
                playdate->graphics->fillRect(
                    0, LCD_ROWS - 16, LCD_COLUMNS - 80, 16, kColorBlack
                );
                
                // message
                const char* msg = data->prev_msg;
                if (saveContact)
                {
                    if (saveMessageCooldownTimer)
                    {
                        msg = "GAME SAVED";
                        
                        xorw = 1;
                    }
                    else
                    {
                        if (ram_peek(0xFF97) & 8)
                        {
                            msg = "CRANK TO SAVE";
                        }
                        
                        xorw = data->crank_save_p;
                    }
                }
                else 
                {
                    switch(itemCollected)
                    {
                    case 1:
                        msg = "PLASMA BEAM";
                        break;
                    case 2:
                        msg = "ICE BEAM";
                        break;
                    case 3:
                        msg = "WAVE BEAM";
                        break;
                    case 4:
                        msg = "SPAZER";
                        break;
                    case 5:
                        msg = "BOMB";
                        break;
                    case 6:
                        msg = "SCREW ATTACK";
                        break;
                    case 7:
                        msg = "VARIA";
                        break;
                    case 8:
                        msg = "HIGH JUMP BOOTS";
                        break;
                    case 9:
                        msg = "SPACE JUMP";
                        break;
                    case 10:
                        msg = "SPIDER BALL";
                        break;
                    case 11:
                        msg = "SPRING BALL";
                        break;
                    default:
                        break;
                    }
                    xorw = 0;
                }
                
                data->prev_msg = msg;
                data->prev_xorw = xorw;
                
                if (msg)
                {
                    draw_msg(data, msg);
                }
                
                if (xorw > 0)
                {
                    playdate->graphics->fillRect(
                        0, LCD_ROWS - 16, (LCD_COLUMNS - 80)*MIN(1.0f, xorw), 16, kColorXOR
                    );
                }
                
                playdate->graphics->markUpdatedRows(LCD_ROWS - 16, LCD_ROWS-1);
            }
            else
            {
                // for screen refresh reasons, the window must linger 1 frame longer than it otherwise would
                if (data->prev_msg)
                {
                    // fixes graphical glitch when window disappears
                    CB_GameSceneContext* gameSceneContext = gb->direct.priv;
                    gameSceneContext->scene->scene->forceFullRefresh = true;
                    
                    playdate->graphics->fillRect(
                        0, LCD_ROWS - 16, LCD_COLUMNS - 80, 16, kColorBlack
                    );
                    
                    draw_msg(data, data->prev_msg);
                    
                    if (data->prev_xorw > 0)
                    {
                        playdate->graphics->fillRect(
                            0, LCD_ROWS - 16, (LCD_COLUMNS - 80)*MIN(1.0f, data->prev_xorw), 16, kColorXOR
                        );
                    }
                }
                data->prev_msg = NULL;
            }
            
            unsigned samus_bank = ram_peek(0xD058);
            unsigned samus_x = ram_peek(0xFFC3);
            unsigned samus_y = ram_peek(0xFFC1);
            
            unsigned frame_counter = ram_peek(0xFF97);
            bool flicker_on = !!(frame_counter & 0x10);
            
            unsigned samus_max_etanks = ram_peek(0xD050);
            unsigned samus_disp_energy = ram_peek_u16(0xd084);
            unsigned samus_disp_missiles = ram_peek_u16(0xd086);
            unsigned metroid_disp = ram_peek(0xD09A) | (flicker_on << 12);
            unsigned samus_weapon = ram_peek(0xD04D);
            
            bool samus_weapon_change = samus_weapon != data->samus_weapon;
            data->samus_weapon = samus_weapon;
            
            unsigned shuffle = ram_peek(0xD096);
            if (shuffle > 0 && shuffle < 0x80)
            {
                // shuffle metroid display
                metroid_disp = (rand() % 0x10) | ((rand()%0x10) << 4);
            }
            
            if (gbScreenRequiresFullRefresh)
            {
                playdate->graphics->fillRect(LCD_COLUMNS-80, 0, 80, LCD_COLUMNS, kColorBlack);
            }
            
            bool door_transition = ram_peek(0xD00E) || ram_peek(0xD08E) || ram_peek(0xD08F);
            
            // reuse previous position if we're in an empty cell, scrolling screen, or reached gunship state
            struct AreaAssociation association = data->area_associations[(samus_bank-MAP_FIRST_BANK)*0x100 + samus_y*0x10 + samus_x];
            if (association.empty || game_mode == 10 || door_transition || data->door_transition_suppress_map_update > 0)
            {
                samus_x = data->samus_x;
                samus_y = data->samus_y;
                samus_bank = data->samus_bank;
            }
            
            if (gbScreenRequiresFullRefresh || data->samus_x != samus_x || data->samus_y != samus_y || data->samus_bank != samus_bank || data->map_flicker != flicker_on)
            {
                data->samus_bank = samus_bank;
                data->samus_x = samus_x;
                data->samus_y = samus_y;
                data->map_flicker = flicker_on;
                
                if (samus_bank >= MAP_FIRST_BANK)
                {
                    set_map_explored(data, samus_bank, samus_x, samus_y, true);
                }
                
                struct AreaAssociation association = data->area_associations[((unsigned)data->samus_bank-9)*0x100 + data->samus_y*0x10 + data->samus_x];
                unsigned x, y;
                
                if (association.unmapped)
                {
                draw_secret_world:;
                    draw_map(data, AREA_SECRET_WORLD, LCD_COLUMNS-80, 0, 5, 5, 0, 0);
                }
                else if (get_coords_in_area(data, association.room_idx, data->samus_bank, data->samus_x, data->samus_y, &x, &y))
                {
                    draw_map(data, association.area_idx, LCD_COLUMNS-80, 0, 5, 5, (int)x-2, (int)y-2);
                    if (flicker_on)
                    {
                        playdate->graphics->fillRect(LCD_COLUMNS-80 + 16*2, 0 + 16*2, 16, 16, kColorXOR);
                    }
                }
                else
                {
                    draw_map(data, AREA_SECRET_WORLD, LCD_COLUMNS-80, 0, 5, 5, 0, 0);
                }
            }
            
            int ui_y = 6*16 + 8;
            int ui_x = LCD_COLUMNS-80;
            
            if (gbScreenRequiresFullRefresh || data->samus_disp_energy != samus_disp_energy || data->samus_max_etanks != samus_max_etanks)
            {
                data->samus_disp_energy = samus_disp_energy;
                data->samus_max_etanks = samus_max_etanks;
                int etanks = data->samus_disp_energy >> 8;
                
                int row1y = ui_y + 16 + 4;
                int row2y = ui_y;
                
                for (int i = 0; i < 5 && i < data->samus_max_etanks; ++i)
                {
                    bool etank_full = i < etanks;
                    int x = LCD_COLUMNS - 16*(i + 1);
                    draw_glyph(data, 208/16 + etank_full, x, row1y, kBitmapUnflipped);
                }
                
                int digitlo = samus_disp_energy & 0xF;
                int digithi = (samus_disp_energy >> 4) & 0xF;
                
                // energy
                draw_glyph(data, GLYPH_DASH, LCD_COLUMNS - 16*3 + 1, row2y, kBitmapUnflipped);
                draw_glyph(data, 10, LCD_COLUMNS - 16*4 + 2, row2y, kBitmapUnflipped);
                draw_glyph(data, digithi + GLYPH_0, LCD_COLUMNS - 16*2, row2y, kBitmapUnflipped);
                draw_glyph(data, digitlo + GLYPH_0, LCD_COLUMNS - 16*1, row2y, kBitmapUnflipped);
            }
            
            ui_y = 5*16 + 4;
            
            if (gbScreenRequiresFullRefresh || data->samus_disp_missiles != samus_disp_missiles || samus_weapon_change)
            {
                data->samus_disp_missiles = samus_disp_missiles;
                
                int digitlo = samus_disp_missiles & 0xF;
                int digithi = (samus_disp_missiles >> 4) & 0xF;
                int digithihi = (samus_disp_missiles >> 8) & 0xF;
                
                draw_glyph(data, digithihi + GLYPH_0, LCD_COLUMNS - 16*3 + 1, ui_y, kBitmapUnflipped);
                draw_glyph(data, (samus_weapon & 0x8) ? GLYPH_MISSILES_EQUIPPED : GLYPH_MISSILES, LCD_COLUMNS - 16*5 + 2, ui_y, kBitmapUnflipped);
                draw_glyph(data, GLYPH_DASH, LCD_COLUMNS - 16*4, ui_y, kBitmapUnflipped);
                draw_glyph(data, digithi + GLYPH_0, LCD_COLUMNS - 16*2, ui_y, kBitmapUnflipped);
                draw_glyph(data, digitlo + GLYPH_0, LCD_COLUMNS - 16*1, ui_y, kBitmapUnflipped);
            }
            
            ui_y = LCD_ROWS - 18;
            
            if (gbScreenRequiresFullRefresh || data->metroid_disp != metroid_disp)
            {
                data->metroid_disp = metroid_disp;
                int metroid_glyph = (176/16) + flicker_on;
                
                int digitlo = metroid_disp & 0xF;
                int digithi = (metroid_disp >> 4) & 0xF;
                
                draw_glyph(data, GLYPH_DASH, LCD_COLUMNS - 16*3 + 1, ui_y, kBitmapUnflipped);
                draw_glyph(data, metroid_glyph, LCD_COLUMNS - 16*5 + 2, ui_y, kBitmapUnflipped);
                draw_glyph(data, metroid_glyph, LCD_COLUMNS - 16*4 + 2, ui_y, kBitmapFlippedX);
                draw_glyph(data, digithi + GLYPH_0, LCD_COLUMNS - 16*2, ui_y, kBitmapUnflipped);
                draw_glyph(data, digitlo + GLYPH_0, LCD_COLUMNS - 16*1, ui_y, kBitmapUnflipped);
            }
            
            // divider
            playdate->graphics->fillRect(LCD_COLUMNS - 80, 0, 1, LCD_ROWS, (uintptr_t)&lcdp_50);
            playdate->graphics->fillRect(LCD_COLUMNS - 80, 80-1, 80, 1, (uintptr_t)&lcdp_50);
        }
    }
}

size_t query_serial_size(ScriptData* data)
{
    return 3
        // map exploration
        + MAP_BANK_COUNT*0x100;
}

#define SERIAL_VERSION 0x30

bool serialize(char* out, ScriptData* data)
{
    out[0] = SERIAL_VERSION;
    out[1] = data->door_transition_suppress_map_update;
    out[2] = data->weapons_collected;
    for (int i = 0; i < MAP_BANK_COUNT*0x100; ++i)
    {
        out[i + 3] = data->map_explored[i];
    }
    
    return true;
}

bool deserialize(const char* in, size_t size, ScriptData* data)
{
    if (size != query_serial_size(data))
    {
        return false;
    }
    
    if ((unsigned)in[0] != (unsigned)SERIAL_VERSION) return false;
    
    data->door_transition_suppress_map_update = in[1];
    data->weapons_collected = in[2];
    
    for (int i = 0; i < 0x100*MAP_BANK_COUNT; ++i)
    {
        data->map_explored[i] = in[i+3];
    }
    
    // force reload stuff
    data->map_area = AREA_SECRET_WORLD;
    data->samus_bank = 0;
    
    return true;
}


C_SCRIPT{
    .rom_name = "METROID2",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
    
    .query_serial_size = (CS_QuerySerialSize)query_serial_size,
    .serialize = (CS_Serialize)serialize,
    .deserialize = (CS_Deserialize)deserialize,
};