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
    
    struct AreaAssociation* area_associations;
    
    LCDBitmap* htimg;
    
    uint8_t* special_base_tiles;
    uint8_t* halftiles;
} ScriptData;

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

#define AREA_SECRET_WORLD 0xF

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    LCDBitmap* htimg = playdate->graphics->loadBitmap(MET2_ASSETS_DIR "pdimg", NULL);
    if (!htimg) return NULL;
    
    ScriptData* data = allocz(ScriptData);
    data->map_area = AREA_SECRET_WORLD;
    data->htimg = htimg;
    data->area_associations = mallocz(0x100*MAP_BANK_COUNT*sizeof(struct AreaAssociation));
    
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

static void load_map_halftiles(ScriptData* data, int area_idx)
{
    if (data->map_area == area_idx) return;
    data->map_area = area_idx;
    struct Area* area = &areas[area_idx];
    cb_free(data->halftiles);
    cb_free(data->special_base_tiles);
    data->halftiles = mallocz(area->w * area->h * 4 * sizeof(data->halftiles[0]));
    data->special_base_tiles = mallocz(area->w * area->h);
    
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
    cb_free(data->area_associations);
    
    cb_free(data);
}

static void on_tick(gb_s* gb, ScriptData* data)
{
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
            
            for (int yi = 0; yi < 2; ++yi)
            {
                for (int xi = 0; xi < 2; ++xi)
                {
                    int halftile = 0;
                    if (y >= 0 && y < area->h && x >= 0 && x < area->w)
                    {
                        halftile = data->halftiles[(y*2 + yi)*area->w*2 + x*2 + xi];
                    }
                    draw_halftile(data, halftile, dst_x_px + HALFTILE_W*xi, dst_y_px + HALFTILE_H*yi);
                }
            }
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

static void on_draw(gb_s* gb, ScriptData* data)
{
    unsigned game_mode = ram_peek(0xFF9B);
    
    game_picture_x_offset = CB_LCD_X;
    game_picture_scaling = 3;
    game_picture_y_top = 0;
    game_picture_y_bottom = LCD_HEIGHT;
    
    if (game_mode == 4 || game_mode == 6 || game_mode == 7 || game_mode == 10)
    {
        // flush left, expand to hide HUD
        game_picture_x_offset = 0;
        game_picture_scaling = 5;
        game_picture_y_top = 2;
        game_picture_y_bottom = 136;
        
        unsigned samus_bank = ram_peek(0xD058);
        unsigned samus_x = ram_peek(0xFFC3);
        unsigned samus_y = ram_peek(0xFFC1);
        
        unsigned door_transition = ram_peek(0xD08E);
        
        // reuse previous position if we're in an empty cell, scrolling screen, or reached gunship state
        struct AreaAssociation association = data->area_associations[(samus_bank-MAP_FIRST_BANK)*0x100 + samus_y*0x10 + samus_x];
        if (association.empty || game_mode == 10 || door_transition)
        {
            samus_x = data->samus_x;
            samus_y = data->samus_y;
            samus_bank = data->samus_bank;
        }
        
        if (data->samus_x != samus_x || data->samus_y != samus_y || data->samus_bank != samus_bank)
        {
            data->samus_bank = samus_bank;
            data->samus_x = samus_x;
            data->samus_y = samus_y;
            
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
            }
            else
            {
                draw_map(data, AREA_SECRET_WORLD, LCD_COLUMNS-80, 0, 5, 5, 0, 0);
            }
        }
    }
}

C_SCRIPT{
    .rom_name = "METROID2",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_draw = (CS_OnDraw)on_draw,
    .on_end = (CS_OnEnd)on_end,
};