#ifndef __APPLE__ /* compiler struggles with this file for some reason */

/* clang-format off */
#include "../scenes/game_scene.h"
#include "../scriptutil.h"

#include "met2.inc"
/* clang-format on */

#define DESCRIPTION                                            \
    "- Adds a map!\n"                                          \
    "- Widescreen HUD\n"                                       \
    "- Missiles controlled by crank\n"                         \
    "- Start/select not needed anymore\n"                      \
    "- Optional: cycle collected beams with crank\n"           \
    "\nNot likely to be compatible with extensive ROMhacks.\n" \
    "\nCreated by: NaOH (Sodium Hydroxide)\n\n"                \
    "Special thanks: Metroid Reverse Engineering Team"

#define HALFTILE_W 8
#define HALFTILE_H 8

#define MET2_ASSETS_DIR SCRIPT_ASSETS_DIR "metroid2/"

#define MAP_FIRST_BANK 9
#define MAP_BANK_COUNT 7

#pragma pack(push, 1)
struct AreaAssociation
{
    uint8_t room_idx;
    uint8_t embedding : 2;
    uint8_t area_idx : 4;
    uint8_t explored : 1;
    bool empty : 1;
    bool unmapped : 1;
    bool dark : 1;
};

struct AreaHex
{
    unsigned x : 4;
    unsigned y : 2;
    unsigned z : 2;
};
#pragma pack(pop)

struct AreaHex hex_caves_lower[] = {
    {
        8,
        0,
        2,
    },
    {
        6,
        1,
        0,
    },
    {
        2,
        1,
        2,
    },
    {
        4,
        2,
        0,
    },
    {
        0,
        2,
        1,
    },
    {
        2,
        3,
        0,
    },
};

struct AreaHex hex_tower[] = {
    {
        0,
        0,
        3,
    },
};

struct AreaHex hex_omega_environment[] = {
    {2, 0, 0},
    {0, 1, 1},
    {2, 2, 0},
};

struct AreaHex hex_caves_west[] = {
    {
        0,
        0,
        1,
    },
    {
        2,
        1,
        0,
    },
};

struct AreaHex hex_nest[] = {
    {
        2,
        0,
        0,
    },
    {
        0,
        1,
        1,
    },
};

struct AreaHex hex_surface[] = {
    {
        2,
        0,
        1,
    },
    {
        4,
        1,
        0,
    },
    {
        0,
        1,
        0,
    },
};

struct AreaHex hex_caves_center[] = {
    {
        0,
        0,
        2,
    },
    {
        2,
        1,
        0,
    },
    {
        0,
        2,
        0,
    },
};

struct AreaHex hex_temple_lower[] = {
    {
        0,
        0,
        0,
    },
    {
        2,
        1,
        0,
    },
};

struct AreaHex hex_temple_upper[] = {
    {
        2,
        0,
        1,
    },
    {
        0,
        1,
        1,
    },
    {
        4,
        1,
        1,
    },
    {
        2,
        2,
        0,
    },
};

struct AreaHex hex_hydro_station[] = {
    {
        2,
        0,
        1,
    },
    {
        0,
        1,
        0,
    },
    {
        4,
        1,
        0,
    },
    {
        2,
        2,
        0,
    },
};

struct AreaHex hex_caves_east[] = {
    {
        0,
        0,
        1,
    },
    {
        2,
        1,
        0,
    },
};

struct AreaHex hex_weapons_facility[] = {
    {
        2,
        0,
        1,
    },
    {
        0,
        1,
        0,
    },
    {
        4,
        1,
        0,
    },
    {
        2,
        2,
        0,
    },
};

struct AreaHex hex_jungle[] = {
    {
        2,
        0,
        2,
    },
    {
        0,
        1,
        1,
    },
};

struct HexMapArea
{
    uint16_t x, y;
    const struct AreaHex* hexes;
    uint8_t hexes_c : 4;
    uint8_t area_idx : 4;
    uint16_t area_idx_left;
    uint16_t area_idx_right;
    uint16_t area_idx_up;
    uint16_t area_idx_down;
};

#define AREA_LINK_0() 0
#define AREA_LINK_1(a) (a + 1)
#define AREA_LINK_2(a, b) ((a + 1) | ((b + 1) << 4))
#define AREA_LINK_3(a, b, c) ((a + 1) | ((b + 1) << 4) | ((c + 1) << 8))

const struct HexMapArea hex_areas[] = {
    {
        .x = 161,
        .y = 55,
        .hexes = hex_surface,
        .hexes_c = sizeof(hex_surface) / sizeof(struct AreaHex),
        .area_idx = 0,
        .area_idx_left = AREA_LINK_2(10, 11),
        .area_idx_right = AREA_LINK_1(1),
        .area_idx_up = AREA_LINK_0(),
        .area_idx_down = AREA_LINK_3(12, 4, 1),
    },
    {
        .x = 364,
        .y = 40,
        .hexes = hex_temple_upper,
        .hexes_c = sizeof(hex_temple_upper) / sizeof(struct AreaHex),
        .area_idx = 3,
        .area_idx_left = AREA_LINK_1(2),
        .area_idx_right = AREA_LINK_0(),
        .area_idx_up = AREA_LINK_0(),
        .area_idx_down = AREA_LINK_1(2),
    },
    {
        .x = 328,
        .y = 84,
        .hexes = hex_temple_lower,
        .hexes_c = sizeof(hex_temple_lower) / sizeof(struct AreaHex),
        .area_idx = 2,
        .area_idx_left = AREA_LINK_1(1),
        .area_idx_right = AREA_LINK_1(3),
        .area_idx_up = AREA_LINK_1(3),
        .area_idx_down = AREA_LINK_2(7, 5),
    },
    {
        .x = 152,
        .y = 108,
        .hexes = hex_nest,
        .hexes_c = sizeof(hex_nest) / sizeof(struct AreaHex),
        .area_idx = 12,
        .area_idx_left = AREA_LINK_1(11),
        .area_idx_right = AREA_LINK_1(1),
        .area_idx_up = AREA_LINK_1(0),
        .area_idx_down = AREA_LINK_1(4),
    },
    {
        .x = 272,
        .y = 124,
        .hexes = hex_caves_center,
        .hexes_c = sizeof(hex_caves_center) / sizeof(struct AreaHex),
        .area_idx = 1,
        .area_idx_left = AREA_LINK_3(4, 12, 0),
        .area_idx_right = AREA_LINK_1(2),
        .area_idx_up = AREA_LINK_1(0),
        .area_idx_down = AREA_LINK_3(5, 8, 4),
    },
    {
        .x = 88,
        .y = 148,
        .hexes = hex_caves_west,
        .hexes_c = sizeof(hex_caves_west) / sizeof(struct AreaHex),
        .area_idx = 11,
        .area_idx_left = AREA_LINK_1(10),
        .area_idx_right = AREA_LINK_1(4),
        .area_idx_up = AREA_LINK_2(12, 0),
        .area_idx_down = AREA_LINK_1(10),
    },
    {
        .x = 168,
        .y = 164,
        .hexes = hex_hydro_station,
        .hexes_c = sizeof(hex_hydro_station) / sizeof(struct AreaHex),
        .area_idx = 4,
        .area_idx_left = AREA_LINK_2(11, 10),
        .area_idx_right = AREA_LINK_1(1),
        .area_idx_up = AREA_LINK_2(12, 0),
        .area_idx_down = AREA_LINK_2(9, 8),
    },
    {
        .x = 360,
        .y = 156,
        .hexes = hex_weapons_facility,
        .hexes_c = sizeof(hex_weapons_facility) / sizeof(struct AreaHex),
        .area_idx = 7,
        .area_idx_left = AREA_LINK_1(5),
        .area_idx_right = AREA_LINK_1(6),
        .area_idx_up = AREA_LINK_1(2),
        .area_idx_down = AREA_LINK_2(6, 5),
    },
    {
        .x = 320,
        .y = 203,
        .hexes = hex_caves_east,
        .hexes_c = sizeof(hex_caves_east) / sizeof(struct AreaHex),
        .area_idx = 5,
        .area_idx_left = AREA_LINK_2(8, 1),
        .area_idx_right = AREA_LINK_2(7, 6),
        .area_idx_up = AREA_LINK_1(1),
        .area_idx_down = AREA_LINK_1(6),
    },
    {
        .x = 396,
        .y = 246,
        .hexes = hex_jungle,
        .hexes_c = sizeof(hex_jungle) / sizeof(struct AreaHex),
        .area_idx = 6,
        .area_idx_left = AREA_LINK_1(5),
        .area_idx_right = AREA_LINK_0(),
        .area_idx_up = AREA_LINK_2(7, 3),
        .area_idx_down = AREA_LINK_0(),
    },
    {
        .x = 222,
        .y = 234,
        .hexes = hex_tower,
        .hexes_c = sizeof(hex_tower) / sizeof(struct AreaHex),
        .area_idx = 9,
        .area_idx_left = AREA_LINK_1(10),
        .area_idx_right = AREA_LINK_1(1),
        .area_idx_up = AREA_LINK_2(4, 1),
        .area_idx_down = AREA_LINK_1(8),
    },
    {
        .x = 56,
        .y = 204,
        .hexes = hex_omega_environment,
        .hexes_c = sizeof(hex_omega_environment) / sizeof(struct AreaHex),
        .area_idx = 10,
        .area_idx_left = AREA_LINK_0(),
        .area_idx_right = AREA_LINK_1(9),
        .area_idx_up = AREA_LINK_2(11, 0),
        .area_idx_down = AREA_LINK_1(8),
    },
    {
        .x = 138,
        .y = 260,
        .hexes = hex_caves_lower,
        .hexes_c = sizeof(hex_caves_lower) / sizeof(struct AreaHex),
        .area_idx = 8,
        .area_idx_left = AREA_LINK_1(10),
        .area_idx_right = AREA_LINK_2(5, 1),
        .area_idx_up = AREA_LINK_2(9, 4),
        .area_idx_down = AREA_LINK_0(),
    },
};

enum map_mode
{
    MAP_MODE_NONE,
    MAP_MODE_HEX,
    MAP_MODE_AREA,
};

typedef struct ScriptData
{
    int map_area;

    uint8_t samus_bank;
    uint8_t samus_x;
    uint8_t samus_y;
    bool map_dark;
    bool map_flicker;

    uint8_t samus_max_etanks;
    uint16_t samus_disp_energy;
    uint16_t samus_disp_missiles;
    uint16_t metroid_disp;
    uint8_t samus_weapon;

    struct AreaAssociation* area_associations;

    LCDBitmap* htimg;
    LCDBitmap* heximg;
    LCDBitmap* heximg_dark;
    LCDBitmap** glyphs;
    size_t glyph_c;

    uint32_t* special_base_tiles;
    uint8_t* halftiles;
    uint8_t* area_explored;
    uint8_t* map_explored;
    uint8_t weapons_collected;
    uint8_t* masked;
    size_t masked_size;

    uint8_t door_transition_suppress_map_update;

    float crank_save_p;
    float crank_prev;
    int weapon_cycle_crank_region;
    int weapon_cycle_state;
    int weapon_cycle_selected;
    const char* prev_msg;
    float prev_xorw;

    uint8_t map_mode_area;
    enum map_mode map_mode;
    int8_t map_mode_x;
    int8_t map_mode_y;
    uint8_t map_mode_timer;
    int map_hex_x, map_hex_y;
} ScriptData;

#define weapon_cycle_enabled preferences_script_A
#define HUD_blinking (preferences_script_B == 0)
#define HUD_Map_blinks (HUD_blinking || (preferences_script_B == 2))

// this define is used by SCRIPT_BREAKPOINT
#define USERDATA ScriptData* data

#define AREA_SECRET_WORLD 0xF

bool get_coords_in_area(
    ScriptData* data, unsigned room_idx, unsigned embedding, unsigned rom_bank, unsigned rom_x,
    unsigned rom_y, unsigned* x, unsigned* y
);

void set_map_explored(ScriptData* data, unsigned bank, unsigned bx, unsigned by, bool explored)
{
    if (bank < MAP_FIRST_BANK)
        return;
    if (bx >= 0x10 || by >= 0x10)
        return;

    int idx = (bank - MAP_FIRST_BANK) * 0x100 + by * 0x10 + bx;
    if (idx != explored)
    {
        data->map_explored[idx] = explored;

        struct AreaAssociation association =
            data->area_associations[(bank - MAP_FIRST_BANK) * 0x100 + by * 0x10 + bx];
        if (association.area_idx == data->map_area && data->map_area != AREA_SECRET_WORLD)
        {
            unsigned x, y;
            struct Area* area = &areas[data->map_area];
            get_coords_in_area(
                data, association.room_idx, association.embedding, bank, bx, by, &x, &y
            );
            if (y < area->h && x < area->w)
            {
                data->area_explored[y * area->w + x] = explored;
            }
        }
    }
}

bool get_map_explored(ScriptData* data, unsigned bank, unsigned bx, unsigned by)
{
    return data->map_explored[(bank - MAP_FIRST_BANK) * 0x100 + by * 0x10 + bx];
}

// new save file
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x4E1C))
{
    memset(data->map_explored, 0, 0x100 * MAP_BANK_COUNT);

    data->weapons_collected = 0;

    // explore starting room
    set_map_explored(data, 0xF, 0x5, 0x6, true);
    set_map_explored(data, 0xF, 0x6, 0x6, true);
    set_map_explored(data, 0xF, 0x5, 0x7, true);
    set_map_explored(data, 0xF, 0x6, 0x7, true);
}

// during door transition
SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x23A7))
{
    data->door_transition_suppress_map_update = 16;
}

// during warp door transition
// (may be paranoia)
SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x28FB))
{
    data->door_transition_suppress_map_update = 16;
}

// loading save file
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x4E39))
{
    unsigned save_slot = ram_peek(0xD0A3);

    size_t size;
    char* buff = script_load_from_disk(save_slot, &size);
    char* orgbuff = buff;

    if (buff && size > 0x100 * MAP_BANK_COUNT / 8)
    {
        data->weapons_collected = (uint8_t)buff[0];
        ++buff;
        --size;
    }

    if (!buff || size < 0x100 * MAP_BANK_COUNT / 8)
    {
        playdate->system->logToConsole("no save data for slot %x.", save_slot);
        memset(data->map_explored, 0, 0x100 * MAP_BANK_COUNT);
    }
    else
    {
        playdate->system->logToConsole("loading script data from slot %x.", save_slot);
        for (int i = 0; i < 0x100 * MAP_BANK_COUNT; ++i)
        {
            data->map_explored[i] = bitvec_read_bits((uint8_t*)buff, i, 1);
        }
    }

    cb_free(orgbuff);

    // force refresh
    data->map_area = AREA_SECRET_WORLD;
    data->samus_bank = 0;
}

// writing save file
SCRIPT_BREAKPOINT(BANK_ADDR(1, 0x7B82))
{
    unsigned save_slot = ram_peek(0xD0A3);
    char buff[0x100 * MAP_BANK_COUNT / 8 + 1];

    buff[0] = data->weapons_collected;

    for (int i = 0; i < 0x100 * MAP_BANK_COUNT; ++i)
    {
        bitvec_write_bits((void*)&buff[1], i, 1, data->map_explored[i]);
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
    bool is_crank_mapped = preferences_crank_dock_button != PREF_BUTTON_NONE ||
                           preferences_crank_undock_button != PREF_BUTTON_NONE;
    bool is_select_mapped =
        CB_App->hasSystemAccess && preferences_lock_button == PREF_BUTTON_SELECT;
    if (!is_crank_mapped && !is_select_mapped &&
        (!ram_peek(0xD07D) || ram_peek(0xD088)) /* not touching save point*/)
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

// weapon cycling
SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x221C))
{
    if (!playdate->system->isCrankDocked() && weapon_cycle_enabled)
    {
        int weapon_change = 0;
        if (data->weapon_cycle_state == -2)
        {
            weapon_change = -1;
        }
        else if (data->weapon_cycle_state == 2)
        {
            weapon_change = 1;
        }
        data->weapon_cycle_state = 0;

        for (int _ = 0; _ < 10; ++_)
        {
            data->weapon_cycle_selected += weapon_change + 5;
            data->weapon_cycle_selected %= 5;
            if (data->weapon_cycle_selected == 0)
            {
                $A = 0;
                ram_poke(0xD055, 0);
                break;
            }
            else if ((data->weapons_collected & (1 << (data->weapon_cycle_selected))))
            {
                $A = data->weapon_cycle_selected;
                ram_poke(0xD055, $A);
                break;
            }
            else if (weapon_change == 0)
            {
                // rare error condition
                data->weapon_cycle_selected = 0;
                ram_poke(0xD055, 0);
                $A = 0;
                break;
            }
        }

        // patch in beam gfx
        unsigned vram_dst = 0x87E0;
        unsigned vram_beam_src[5] = {
            0x4000 * 6 + 0x0320 + 0x7E0,  // normal
            0x4000 * 6 + 0x0040,          // ice
            0x4000 * 6 + 0x0060,          // wave
            0x4000 * 6 + 0x0080,          // spazer
            0x4000 * 6 + 0x0080,          // plasma
        };

        for (int i = 0; i < 0x20; ++i)
        {
            uint8_t v = rom_peek(vram_beam_src[data->weapon_cycle_selected] + i);
            ram_poke(vram_dst + i, v);
        }
    }
}

// clear
SCRIPT_BREAKPOINT(BANK_ADDR(5, 0x41D6))
{
    bool is_crank_mapped = preferences_crank_dock_button == PREF_BUTTON_NONE &&
                           preferences_crank_undock_button == PREF_BUTTON_NONE;
    bool is_select_mapped =
        CB_App->hasSystemAccess && preferences_lock_button == PREF_BUTTON_SELECT;
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
    if (room->h == 0)
        return 16;
    return room->h;
}

uint32_t read_bits(const uint8_t* base, size_t* offset, int count)
{
    uint32_t v = 0;
    while (count--)
    {
        uint8_t b = base[*offset / 8];
        b >>= 7 - (*offset % 8);
        v |= (b & 1) << count;
        (*offset)++;
    }
    return v;
}

void read_embedding_header(
    const uint8_t* base, size_t* offset, unsigned* _bank, int* _em_x, int* _em_y
)
{
    unsigned em_bank = read_bits(room_embeddings, offset, 3) + MAP_FIRST_BANK;
    CB_ASSERT(em_bank <= 0xF);
    int em_x = read_bits(room_embeddings, offset, 5);
    if (em_x >= 16)
        em_x -= 32;
    int em_y = read_bits(room_embeddings, offset, 5);
    if (em_y >= 16)
        em_y -= 32;
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
    if (rom_peek(5 * 0x4000 + 0x0249) == 0x8)
    {
        rom_poke(5 * 0x4000 + 0x0249, 1);
    }
    else
    {
        return NULL;
    }

    LCDBitmap* htimg = playdate->graphics->loadBitmap(MET2_ASSETS_DIR "pdimg", NULL);
    if (!htimg)
        return NULL;

    force_pref(crank_mode, CRANK_MODE_OFF);
    force_pref(crank_undock_button, PREF_BUTTON_NONE);
    force_pref(crank_dock_button, PREF_BUTTON_NONE);

    SET_BREAKPOINTS(0);

    ScriptData* data = allocz(ScriptData);
    data->map_area = AREA_SECRET_WORLD;
    data->htimg = htimg;
    data->heximg = playdate->graphics->loadBitmap(MET2_ASSETS_DIR "hex", NULL);
    data->heximg_dark = playdate->graphics->loadBitmap(MET2_ASSETS_DIR "hexdark", NULL);
    data->area_associations = mallocz(0x100 * MAP_BANK_COUNT * sizeof(struct AreaAssociation));
    data->map_explored = mallocz(0x100 * MAP_BANK_COUNT);

    data->glyphs = split_subimages(
        playdate->graphics->loadBitmap(MET2_ASSETS_DIR "font", NULL), 16, 16, &data->glyph_c
    );

    for (int i = 0; i < 0x100 * MAP_BANK_COUNT; ++i)
    {
        data->area_associations[i].unmapped = true;
    }

    for (int ridx = 0; ridx < sizeof(rooms) / sizeof(struct Room); ++ridx)
    {
        struct Room* room = &rooms[ridx];
        struct Area* area = &areas[room->area];

        size_t offset = room->data_offset;
        for (int i = 0; i < room->embeddings; ++i)
        {
            unsigned em_bank;
            int em_x, em_y;
            read_embedding_header(room_embeddings, &offset, &em_bank, &em_x, &em_y);

            for (int y = 0; y < get_room_h(room); ++y)
            {
                for (int x = 0; x < room->w; ++x)
                {
                    size_t assoc_idx =
                        0x100 * (em_bank - MAP_FIRST_BANK) + em_x + x + (em_y + y) * 0x10;
                    if (read_bits(room_embeddings, &offset, 1))
                    {
                        data->area_associations[assoc_idx].unmapped = false;
                        data->area_associations[assoc_idx].empty = false;
                        data->area_associations[assoc_idx].area_idx = room->area;
                        data->area_associations[assoc_idx].embedding = i;
                        data->area_associations[assoc_idx].room_idx = ridx;
                    }

                    for (int i = 0; i < area->special_tile_c; ++i)
                    {
                        struct SpecialTile* special_tile = &area->special_tiles[i];
                        if (special_tile->area_x == x + room->area_x &&
                            special_tile->area_y == y + room->area_y)
                        {
                            if (special_tile->type == TILE_EMPTY)
                            {
                                data->area_associations[assoc_idx].empty = true;
                                data->area_associations[assoc_idx].unmapped = true;
                            }

                            if (special_tile->dark)
                            {
                                data->area_associations[assoc_idx].dark = true;
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
#define TILE_SHUNT_UL 33
#define TILE_SHUNT_UR 34
#define TILE_SHUNT_BL 35
#define TILE_SHUNT_BR 36

#define GLYPH_A (272 / 16)
#define GLYPH_0 (0)
#define GLYPH_DASH (240 / 16)
#define GLYPH_MISSILES (688 / 16)
#define GLYPH_MISSILES_EQUIPPED (GLYPH_MISSILES + 1)
#define GLYPH_BEAM (GLYPH_MISSILES + 2)
#define GLYPH_SELECTED (800 / 16)

static void load_map_halftiles(ScriptData* data, int area_idx)
{
    if (data->map_area == area_idx)
        return;
    data->map_area = area_idx;
    struct Area* area = &areas[area_idx];
    cb_free(data->halftiles);
    cb_free(data->special_base_tiles);
    cb_free(data->area_explored);
    data->halftiles = mallocz(area->w * area->h * 4 * sizeof(data->halftiles[0]));
    data->special_base_tiles = mallocz(area->w * area->h * sizeof(uint32_t));
    data->area_explored = mallocz(area->w * area->h);

    for (int i = 0; i < area->special_tile_c; ++i)
    {
        struct SpecialTile* special_tile = &area->special_tiles[i];
        int x = special_tile->area_x;
        int y = special_tile->area_y;

        if (special_tile->type < TILE_REPLACEMENTS)
        {
            unsigned stv = special_tile->type | ((unsigned)special_tile->ridx << 8);
            if (special_tile->dark)
            {
                stv = TILE_DARK | ((unsigned)special_tile->ridx << 8);
            }

            if (data->special_base_tiles[y * area->w + x] == 0)
            {
                data->special_base_tiles[y * area->w + x] = stv;
            }
            else
            {
                data->special_base_tiles[y * area->w + x] |= stv << 16;
            }
        }
    }

    for (int ridx = 0; ridx < sizeof(rooms) / sizeof(struct Room); ++ridx)
    {
        struct Room* room = &rooms[ridx];
        if (room->area != area_idx)
            continue;
        size_t tile_present_size = (room->w + 2) * (get_room_h(room) + 2);
        uint8_t tile_present[tile_present_size];
        memset(tile_present, 0, tile_present_size);

        size_t offset = room->data_offset;
        for (int i = 0; i < room->embeddings; ++i)
        {
            unsigned em_bank;
            int em_x, em_y;
            read_embedding_header(room_embeddings, &offset, &em_bank, &em_x, &em_y);

            CB_ASSERT(em_bank >= MAP_FIRST_BANK);

            for (int y = 0; y < get_room_h(room); ++y)
            {
                for (int x = 0; x < room->w; ++x)
                {
                    if (read_bits(room_embeddings, &offset, 1))
                    {
                        int tile_idx = (y + 1) * (room->w + 2) + (x + 1);
                        uint32_t special_tile_tr =
                            data->special_base_tiles
                                [(y + room->area_y) * area->w + (x + room->area_x)];
                        uint8_t special_tile = special_tile_tr & 0xFF;
                        uint8_t special_tile_b = (special_tile_tr >> 16) & 0xFF;
                        uint8_t special_tile_ridx = (special_tile_tr >> 8) & 0xFF;
                        uint8_t special_tile_ridx_b = special_tile_tr >> 24;
                        if (special_tile == TILE_EMPTY)
                        {
                            tile_present[tile_idx] = 0;
                        }
                        else if (special_tile == 0)
                        {
                            tile_present[tile_idx] = 1;
                        }
                        else if (special_tile_ridx == ridx)
                        {
                            tile_present[tile_idx] = special_tile;
                        }
                        else if (special_tile_ridx_b == ridx)
                        {
                            tile_present[tile_idx] = special_tile_b;
                        }
                        else
                        {
                            tile_present[tile_idx] = 1;
                        }

                        if (get_map_explored(data, em_bank, em_x + x, em_y + y) &&
                            tile_present[tile_idx])
                        {
                            int area_explore_idx = (y + room->area_y) * area->w + x + room->area_x;
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
                int ht_x = (x - 1 + room->area_x) * 2;
                int ht_y = (y - 1 + room->area_y) * 2;

                uint8_t* ht_tl = &data->halftiles[(ht_y + 0) * area->w * 2 + (ht_x + 0)];
                uint8_t* ht_tr = &data->halftiles[(ht_y + 0) * area->w * 2 + (ht_x + 1)];
                uint8_t* ht_bl = &data->halftiles[(ht_y + 1) * area->w * 2 + (ht_x + 0)];
                uint8_t* ht_br = &data->halftiles[(ht_y + 1) * area->w * 2 + (ht_x + 1)];

                int w = room->w + 2;  // stride
                int tt = tile_present[y * w + x];

                if (tt == 1 || tt == TILE_DARK)
                {
                    *ht_tl = 1;
                    *ht_tr = 1;
                    *ht_bl = 1;
                    *ht_br = 1;

                    if (!tile_present[(y - 1) * w + x - 1])
                        *ht_tl = (k * 3 + 0);
                    if (!tile_present[(y + 1) * w + x - 1])
                        *ht_bl = (k * 3 + 1);
                    if (!tile_present[(y + 1) * w + x + 1])
                        *ht_br = (k * 3 + 2);
                    if (!tile_present[(y - 1) * w + x + 1])
                        *ht_tr = (k * 3 + 3);

                    if (!tile_present[(y - 1) * w + x - 0])
                    {
                        *ht_tl = k * 1 + 0;
                        *ht_tr = k * 1 + 0;
                    }
                    if (!tile_present[(y - 0) * w + x - 1])
                    {
                        *ht_tl = k * 1 + 1;
                        *ht_bl = k * 1 + 1;
                    }
                    if (!tile_present[(y + 1) * w + x + 0])
                    {
                        *ht_bl = k * 1 + 2;
                        *ht_br = k * 1 + 2;
                    }
                    if (!tile_present[(y + 0) * w + x + 1])
                    {
                        *ht_tr = k * 1 + 3;
                        *ht_br = k * 1 + 3;
                    }

                    if (!tile_present[(y - 1) * w + x - 0] && !tile_present[(y - 0) * w + x - 1])
                        *ht_tl = k * 2 + 0;
                    if (!tile_present[(y + 1) * w + x - 0] && !tile_present[(y - 0) * w + x - 1])
                        *ht_bl = k * 2 + 1;
                    if (!tile_present[(y + 1) * w + x - 0] && !tile_present[(y - 0) * w + x + 1])
                        *ht_br = k * 2 + 2;
                    if (!tile_present[(y - 1) * w + x - 0] && !tile_present[(y - 0) * w + x + 1])
                        *ht_tr = k * 2 + 3;

                    if (tt == TILE_DARK)
                    {
                        if (*ht_tl == 1)
                            *ht_tl = 2;
                        else
                            *ht_tl += 6 * k;
                        if (*ht_tr == 1)
                            *ht_tr = 2;
                        else
                            *ht_tr += 6 * k;
                        if (*ht_bl == 1)
                            *ht_bl = 2;
                        else
                            *ht_bl += 6 * k;
                        if (*ht_br == 1)
                            *ht_br = 2;
                        else
                            *ht_br += 6 * k;
                    }
                }
            }
        }
    }
}

static void on_end(gb_s* gb, ScriptData* data)
{
    if (data->htimg)
        playdate->graphics->freeBitmap(data->htimg);
    if (data->heximg)
        playdate->graphics->freeBitmap(data->heximg);
    if (data->heximg_dark)
        playdate->graphics->freeBitmap(data->heximg_dark);
    cb_free(data->halftiles);
    cb_free(data->special_base_tiles);
    cb_free(data->area_explored);
    cb_free(data->area_associations);
    cb_free(data->map_explored);
    cb_free(data->masked);
    free_subimages(data->glyphs);

    cb_free(data);
}

static void draw_glyph(ScriptData* data, size_t glyph_idx, int x, int y, LCDBitmapFlip flip);
static void draw_map(
    uint8_t* dst_buff, unsigned dst_stride, ScriptData* data, unsigned area_idx, int dst_x,
    int dst_y, int window_w, int window_h, int window_x, int window_y
);

static bool area_is_explored(ScriptData* data, int area_idx)
{
    if (area_idx == AREA_SECRET_WORLD)
        return false;

    for (int ridx = 0; ridx < sizeof(rooms) / sizeof(struct Room); ++ridx)
    {
        struct Room* room = &rooms[ridx];
        if (room->area != area_idx)
            continue;

        size_t offset = room->data_offset;
        for (int i = 0; i < room->embeddings; ++i)
        {
            unsigned em_bank;
            int em_x, em_y;
            read_embedding_header(room_embeddings, &offset, &em_bank, &em_x, &em_y);

            for (int y = 0; y < get_room_h(room); ++y)
            {
                for (int x = 0; x < room->w; ++x)
                {
                    if (read_bits(room_embeddings, &offset, 1))
                    {
                        if (get_map_explored(data, em_bank, em_x + x, em_y + y))
                            return true;
                    }
                }
            }
        }
    }

    return false;
}

static int areas_explored(ScriptData* data)
{
    int ex = 0;
    for (int i = 0; i < sizeof(areas) / sizeof(struct Area); ++i)
    {
        ex += area_is_explored(data, i);
    }
    return ex;
}

static void get_area_center(const struct HexMapArea* hma, int* ox, int* oy)
{
    int n = 0;
    int x = 0;
    int y = 0;

    for (int i = 0; i < hma->hexes_c; ++i)
    {
        x += hma->hexes[i].x * 16;
        y += hma->hexes[i].y * 16 - hma->hexes[i].z * 6;
        n += 1;
    }

    *ox = hma->x;
    *oy = hma->y;

    if (n > 0)
    {
        *ox += x / n;
        *oy += y / n;
    }
}

static const struct HexMapArea* get_area_hex_map(int area_idx)
{
    for (int i = 0; i < sizeof(hex_areas) / sizeof(struct HexMapArea); ++i)
    {
        if (hex_areas[i].area_idx == area_idx)
            return &hex_areas[i];
    }

    return NULL;
}

static int jump_area_sequence(ScriptData* data, unsigned area_sequence)
{
    for (int i = 0; i < 4; ++i)
    {
        int area = (int)((area_sequence >> (4 * i)) & 0x0F) - 1;
        if (area < 0)
            break;
        if (!area_is_explored(data, area))
            continue;
        return area;
    }

    return AREA_SECRET_WORLD;
}

static void tick_map(ScriptData* data)
{
    ++data->map_mode_timer;
    audio_enabled = 0;

    const struct HexMapArea* hma_selected = get_area_hex_map(data->map_mode_area);

    // input
    switch (data->map_mode)
    {
    case MAP_MODE_AREA:
        if (CB_App->buttons_pressed & kButtonB)
        {
            if (!hma_selected || areas_explored(data) <= 1)
            {
                data->map_mode = MAP_MODE_NONE;
                CB_App->buttons_suppress |= kButtonB;
                audio_enabled = 1;
            }
            else
            {
                data->map_mode = MAP_MODE_HEX;
                get_area_center(hma_selected, &data->map_hex_x, &data->map_hex_y);
            }
        }
        if (data->map_mode_timer % 2 == 0)
        {
            if (CB_App->buttons_down & kButtonLeft)
            {
                if (data->map_mode_x > 0)
                    data->map_mode_x--;
            }
            if (CB_App->buttons_down & kButtonRight)
            {
                data->map_mode_x++;
            }
            if (CB_App->buttons_down & kButtonUp)
            {
                if (data->map_mode_y > 0)
                    data->map_mode_y--;
            }
            if (CB_App->buttons_down & kButtonDown)
            {
                data->map_mode_y++;
            }
        }
        break;
    case MAP_MODE_HEX:
        if (CB_App->buttons_pressed & kButtonB)
        {
            data->map_mode = MAP_MODE_NONE;
            CB_App->buttons_suppress |= kButtonB;
            audio_enabled = 1;
        }
        else if (CB_App->buttons_pressed & kButtonA)
        {
            data->map_mode = MAP_MODE_AREA;
            data->map_mode_x = 0;
            data->map_mode_y = 0;
        }

        if (hma_selected)
        {
            int goto_area = AREA_SECRET_WORLD;
            if (CB_App->buttons_pressed & kButtonLeft)
            {
                goto_area = jump_area_sequence(data, hma_selected->area_idx_left);
            }
            if (CB_App->buttons_pressed & kButtonRight)
            {
                goto_area = jump_area_sequence(data, hma_selected->area_idx_right);
            }
            if (CB_App->buttons_pressed & kButtonUp)
            {
                goto_area = jump_area_sequence(data, hma_selected->area_idx_up);
            }
            if (CB_App->buttons_pressed & kButtonDown)
            {
                goto_area = jump_area_sequence(data, hma_selected->area_idx_down);
            }

            if (goto_area != AREA_SECRET_WORLD)
            {
                data->map_mode_area = goto_area;
            }
        }

        break;
    default:
        break;
    }

    // draw
    playdate->graphics->fillRect(0, 0, LCD_COLUMNS, LCD_ROWS, kColorBlack);
    playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);

    uint8_t* frame = playdate->graphics->getFrame();

    if (data->map_mode_area == AREA_SECRET_WORLD)
    {
        data->map_mode_area = 0;
        data->map_mode = MAP_MODE_HEX;
    }

    struct Area* area = &areas[data->map_mode_area];

    if (data->map_mode == MAP_MODE_AREA)
    {
        int screen_w = LCD_COLUMNS / HALFTILE_W / 2;
        int screen_h = (LCD_ROWS - 16) / HALFTILE_H / 2;

        unsigned w = MIN(screen_w, area->w);
        unsigned h = MIN(screen_h, area->h);

        // center & bounds
        if (area->w < screen_w)
        {
            data->map_mode_x = -(screen_w - area->w) / 2;
        }
        else
        {
            if (data->map_mode_x >= (int)area->w - (int)w)
                data->map_mode_x = (int)area->w - (int)w;
            if (data->map_mode_x < 0)
                data->map_mode_x = 0;
        }
        if (area->h < screen_h)
        {
            data->map_mode_y = -(screen_h - area->h) / 2;
        }
        else
        {
            if (data->map_mode_y >= (int)area->h - (int)h)
                data->map_mode_y = (int)area->h - (int)h;
            if (data->map_mode_y < 0)
                data->map_mode_y = 0;
        }

        int offx = 0;
        int offy = 0;

        if (data->map_mode_x < 0)
        {
            offx = -data->map_mode_x;
        }
        if (data->map_mode_y < 0)
        {
            offy = -data->map_mode_y;
        }

        draw_map(
            frame, LCD_ROWSIZE, data, data->map_mode_area, 16 * offx, 16 * (offy + 1), w, h,
            data->map_mode_x + offx, data->map_mode_y + offy
        );

        // draw samus position indicator
        if (data->samus_bank >= MAP_FIRST_BANK)
        {
            struct AreaAssociation samus_assoc =
                data->area_associations
                    [((unsigned)data->samus_bank - MAP_FIRST_BANK) * 0x100 + data->samus_y * 0x10 +
                     data->samus_x];
            unsigned sx, sy;
            if (samus_assoc.area_idx == data->map_mode_area && !samus_assoc.dark &&
                get_coords_in_area(
                    data, samus_assoc.room_idx, samus_assoc.embedding, data->samus_bank,
                    data->samus_x, data->samus_y, &sx, &sy
                ))
            {
                if ((int)sy >= data->map_mode_y)
                {
                    bool flicker_on = (data->map_mode_timer % 16) >= 8;
                    if (flicker_on)
                    {
                        playdate->graphics->fillRect(
                            2 * HALFTILE_W * (sx - data->map_mode_x),
                            16 + 2 * HALFTILE_H * (sy - data->map_mode_y), HALFTILE_W * 2,
                            HALFTILE_H * 2, kColorXOR
                        );
                    }
                }
            }
        }
    }
    else if (data->map_mode == MAP_MODE_HEX)
    {
#define HEX_OFF_X (-data->map_hex_x + LCD_COLUMNS / 2 - 24)
#define HEX_OFF_Y (-data->map_hex_y + LCD_ROWS / 2 - 38)

        const struct HexMapArea* hma_selected = get_area_hex_map(data->map_mode_area);
        if (hma_selected)
        {
            int pref_x, pref_y;
            get_area_center(hma_selected, &pref_x, &pref_y);

            data->map_hex_x = toward(data->map_hex_x, pref_x, 5);
            data->map_hex_y = toward(data->map_hex_y, pref_y, 4);
        }

        for (int i = 0; i < sizeof(hex_areas) / sizeof(struct HexMapArea); ++i)
        {
            const struct HexMapArea* hma = &hex_areas[i];
            if (!area_is_explored(data, hma->area_idx))
                continue;

            for (int j = 0; j < hma->hexes_c; ++j)
            {
                const struct AreaHex* hex = &hma->hexes[j];
                for (int z = 0; z <= hex->z; ++z)
                {
                    int x = hma->x + 16 * hex->x + HEX_OFF_X;
                    int y = hma->y + 16 * hex->y - z * 16 + HEX_OFF_Y;
                    if (hma->area_idx == data->map_mode_area)
                    {
                        playdate->graphics->drawBitmap(data->heximg, x, y, kBitmapUnflipped);
                    }
                    else
                    {
                        playdate->graphics->drawBitmap(data->heximg_dark, x, y, kBitmapUnflipped);
                    }
                }
            }
        }
    }

    // area name
    playdate->graphics->fillRect(0, 0, LCD_COLUMNS, 16, kColorBlack);
    const char* msg = area->name;
    int msg_y = 0;
    if (msg)
    {
        int len = strlen(msg);
        int x = MAX(0, LCD_COLUMNS / 2 - len * 8);
        for (int i = 0; i < len; ++i)
        {
            char c = msg[i];
            if (c >= 'A' && c <= 'Z')
            {
                draw_glyph(data, GLYPH_A + c - 'A', x, msg_y, kBitmapUnflipped);
            }
            if (c >= 'a' && c <= 'z')
            {
                draw_glyph(data, GLYPH_A + c - 'a', x, msg_y, kBitmapUnflipped);
            }
            x += 16;
        }
    }

    playdate->graphics->fillRect(0, 16, LCD_COLUMNS, 1, (uintptr_t)&lcdp_50b);
}

static void on_tick(gb_s* gb, ScriptData* data, int frames_elapsed)
{
    if (data->map_mode != MAP_MODE_NONE)
    {
        tick_map(data);

        if (data->map_mode != MAP_MODE_NONE)
        {
            suppress_gb_frame = true;
            return;
        }
        else
        {
            gbScreenRequiresFullRefresh = true;
        }
    }

    if (data->door_transition_suppress_map_update > 0)
    {
        data->door_transition_suppress_map_update -= frames_elapsed;
    }

#if 0
    // hp hack
    ram_poke(0xD051, 0x99);
#endif

    data->weapons_collected |= 1 << (ram_peek(0xd04D));

    if (!ram_peek(0xD07D) || ram_peek(0xD088))
    {
        // not touching save point
        data->crank_save_p = 0;
        data->crank_prev = -1;

        // shooting cancels the toggle
        if (CB_App->buttons_down & kButtonB)
        {
            data->weapon_cycle_state = 0;
        }

        if (playdate->system->isCrankDocked())
        {
            data->weapon_cycle_crank_region = -1;
            data->weapon_cycle_state = 0;
        }
        else
        {
            float crank_angle = playdate->system->getCrankAngle();
            int crank_region = 1;
            if (crank_angle >= 90 && crank_angle < 180)
                crank_region = 2;
            if (crank_angle >= 180 && crank_angle <= 270)
                crank_region = 3;

            switch (data->weapon_cycle_crank_region)
            {
            case 1:
                if (crank_region == 2 && data->weapon_cycle_state == -1)
                {
                    data->weapon_cycle_state = -2;
                }
                if (crank_region == 3 && data->weapon_cycle_state == 1)
                {
                    data->weapon_cycle_state = 2;
                }
                break;
            case 2:
                if (crank_region == 1)
                {
                    data->weapon_cycle_state = 1;
                }
                break;
            case 3:
                if (crank_region == 1)
                {
                    data->weapon_cycle_state = -1;
                }
                break;
            default:
                data->weapon_cycle_state = 0;
                break;
            }
            data->weapon_cycle_crank_region = crank_region;
        }
    }
    else
    {
        data->weapon_cycle_crank_region = -1;
        // touching save point
        if (data->crank_save_p < 0 || data->crank_save_p > 20)
            data->crank_save_p = 0;
        if (playdate->system->isCrankDocked())
        {
            data->crank_prev = -1;
            data->weapon_cycle_state = 0;
        }
        else
        {
            float crank_angle = playdate->system->getCrankAngle();

            if (data->crank_prev >= 0)
            {
                float crank_amount =
                    fabsf(crank_angle - data->crank_prev) / (float)(frames_elapsed);

                crank_amount /= 1.7f;

                if (crank_amount > 1)
                {
                    crank_amount = MIN(crank_amount, 4.5f);
                    data->crank_save_p += crank_amount / 60.0f;
                }
            }
            data->crank_prev = crank_angle;
        }

        // reduce over time
        data->crank_save_p = toward(data->crank_save_p, 0, frames_elapsed / 140.0f);
    }
}

static void draw_halftile(
    uint8_t* dst_buff, unsigned dst_stride, ScriptData* data, int ht_idx, int dst_x, int dst_y
)
{
    int w, h, stride;
    uint8_t *mask, *pdata;
    playdate->graphics->getBitmapData(data->htimg, &w, &h, &stride, &mask, &pdata);

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

        int dst_idx = (i + dst_y) * dst_stride + dst_x;
        dst_buff[dst_idx] &= ~pm;
        dst_buff[dst_idx] |= p;
    }
}

static void draw_fulltile(
    uint8_t* dst_buff, unsigned dst_stride, ScriptData* data, int ft_idx, int dst_x, int dst_y
)
{
    uint8_t ht[4] = {3, 3, 3, 3};
    switch (ft_idx)
    {
    case TILE_HPIPE:
        ht[0] = 5 * k + 0;
        ht[1] = 5 * k + 0;
        ht[2] = 5 * k + 1;
        ht[3] = 5 * k + 1;
        break;
    case TILE_VPIPE:
        ht[0] = 5 * k + 2;
        ht[1] = 5 * k + 3;
        ht[2] = 5 * k + 2;
        ht[3] = 5 * k + 3;
        break;
    case TILE_DOOR_NORTH:
        ht[0] = 10 * k + 0;
        ht[1] = 10 * k + 1;
        break;
    case TILE_DOOR_WEST:
        ht[0] = 10 * k + 3;
        ht[2] = 11 * k + 3;
        break;
    case TILE_DOOR_SOUTH:
        ht[2] = 11 * k + 0;
        ht[3] = 11 * k + 1;
        break;
    case TILE_DOOR_EAST:
        ht[1] = 10 * k + 2;
        ht[3] = 11 * k + 2;
        break;
    case TILE_SHUNT_UL:
        ht[1] = 16 * k + 2;
        ht[3] = 17 * k + 2;
        break;
    case TILE_SHUNT_UR:
        ht[0] = 16 * k + 3;
        ht[2] = 17 * k + 3;
        break;
    case TILE_SHUNT_BL:
        ht[1] = 16 * k + 0;
        ht[3] = 17 * k + 0;
        break;
    case TILE_SHUNT_BR:
        ht[0] = 16 * k + 1;
        ht[2] = 17 * k + 1;
        break;
    case TILE_EXIT_NORTH:
    case TILE_EXIT_WEST:
    case TILE_EXIT_EAST:
    case TILE_EXIT_SOUTH:
    {
        ft_idx -= TILE_EXIT_NORTH;
        int a = (ft_idx % 2);
        int b = (ft_idx / 2);
        ht[0] = (12 + b * 2) * k + 2 * a;
        ht[1] = (12 + b * 2) * k + 2 * a + 1;
        ht[2] = (12 + b * 2 + 1) * k + 2 * a;
        ht[3] = (12 + b * 2 + 1) * k + 2 * a + 1;
    }
    break;
    case TILE_HJUMP:
        ht[0] = 6 * k + 0;
        ht[1] = 6 * k + 0;
        ht[2] = 6 * k + 1;
        ht[3] = 6 * k + 1;
        break;
    case TILE_BEAM:
        ht[0] = 4 * k + 0;
        ht[1] = 4 * k + 1;
        ht[2] = 4 * k + 2;
        ht[3] = 4 * k + 3;
        break;
    case TILE_ITEM ... TILE_SHIP_RIGHT:
    {
        ft_idx -= TILE_ITEM;
        int a = (ft_idx % 2);
        int b = (ft_idx / 2);
        ht[0] = (20 + b * 2) * k + 2 * a;
        ht[1] = (20 + b * 2) * k + 2 * a + 1;
        ht[2] = (20 + b * 2 + 1) * k + 2 * a;
        ht[3] = (20 + b * 2 + 1) * k + 2 * a + 1;
    }
    break;
    default:
        break;
    }

    draw_halftile(dst_buff, dst_stride, data, ht[0], dst_x, dst_y);
    draw_halftile(dst_buff, dst_stride, data, ht[1], dst_x + HALFTILE_W, dst_y);
    draw_halftile(dst_buff, dst_stride, data, ht[2], dst_x, dst_y + HALFTILE_H);
    draw_halftile(dst_buff, dst_stride, data, ht[3], dst_x + HALFTILE_W, dst_y + HALFTILE_H);
}

static void draw_map(
    uint8_t* dst_buff, unsigned dst_stride, ScriptData* data, unsigned area_idx, int dst_x,
    int dst_y, int window_w, int window_h, int window_x, int window_y
)
{
    if (area_idx == AREA_SECRET_WORLD)
    {
    secret_world:
        for (int y = dst_y; y < dst_y + window_h * HALFTILE_H * 2; ++y)
        {
            for (int x = dst_x / 8; x < (dst_x + window_w * HALFTILE_W * 2) / 8; ++x)
            {
                dst_buff[y * dst_stride + x] =
                    (HUD_Map_blinks) ? rand() & rand() : ((dst_y % 2) ? 0xAA : 0x55);
            }
        }
        return;
    }

    struct Area* area = &areas[area_idx];

    // get area for current cell
    load_map_halftiles(data, area_idx);

    if (!data->halftiles)
        goto secret_world;

    for (int y = window_y; y < window_h + window_y; ++y)
    {
        for (int x = window_x; x < window_x + window_w; ++x)
        {
            int dst_x_px = dst_x + (x - window_x) * HALFTILE_W * 2;
            int dst_y_px = dst_y + (y - window_y) * HALFTILE_H * 2;

            bool blank = false;
            if (y < 0 || y >= area->h || x < 0 || x >= area->w)
            {
                blank = true;
            }
            else if (!data->area_explored[y * area->w + x])
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
                        halftile = data->halftiles[(y * 2 + yi) * area->w * 2 + x * 2 + xi];
                    }
                    draw_halftile(
                        dst_buff, dst_stride, data, halftile, dst_x_px + HALFTILE_W * xi,
                        dst_y_px + HALFTILE_H * yi
                    );
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
    for (int i = 0; i < masked_size; ++i)
        data->masked[i] = 0;
    for (int i = 0; i < area->special_tile_c; ++i)
    {
        struct SpecialTile* special_tile = &area->special_tiles[i];
        if (special_tile->area_x < window_x || special_tile->area_x >= window_x + window_w)
            continue;
        if (special_tile->area_y < window_y || special_tile->area_y >= window_y + window_h)
            continue;

        int mask_idx =
            (special_tile->area_y - window_y) * window_w + (special_tile->area_x - window_x);
        CB_ASSERT(mask_idx >= 0 && mask_idx < masked_size);

        if (data->masked[mask_idx])
        {
            continue;
        }

        if (special_tile->type == TILE_HJUMP)
        // special behaviour for hjump tiles
        {
            int xroot = special_tile->ridx;
            if (!data->area_explored[special_tile->area_y * area->w + xroot])
                continue;
        }
        else if (special_tile->type >= TILE_EXIT_NORTH && special_tile->type <= TILE_EXIT_EAST)
        // special behaviour for exit tiles
        {
            int dx = 0;
            int dy = 0;

            if (special_tile->type == TILE_EXIT_NORTH)
                dy = 1;
            if (special_tile->type == TILE_EXIT_SOUTH)
                dy = -1;
            if (special_tile->type == TILE_EXIT_WEST)
                dx = 1;
            if (special_tile->type == TILE_EXIT_EAST)
                dx = -1;
            if (!data->area_explored
                     [(special_tile->area_y + dy) * area->w + (special_tile->area_x + dx)])
                continue;
        }
        else if (special_tile->ridx != 0xFF)
        //  check if tile was explored
        {
            if (!data->area_explored[special_tile->area_y * area->w + special_tile->area_x])
                continue;
        }

        int slot = special_tile->object_slot;
        if (slot >= 0x40 && slot <= 0x7F)
        {
            uint8_t flags = ram_peek(0xC500 + slot);

            if (flags == 0xFF && special_tile->ridx != 0xFF)
            {
                // load from stored buffer
                flags =
                    ram_peek(0xC900 + 0x40 * (special_tile->bank - MAP_FIRST_BANK) + (slot - 0x40));
            }

            if (flags != 0xFF)  // default state
            {
                // indicates object dead / destroyed / collected
                if (flags & 2)
                    continue;
            }
        }

        int dst_x_px = dst_x + (special_tile->area_x - window_x) * HALFTILE_W * 2;
        int dst_y_px = dst_y + (special_tile->area_y - window_y) * HALFTILE_H * 2;

        unsigned type = special_tile->type;
        if (type == TILE_SHUNT)
        // special encoding for shunt
        {
            type = TILE_SHUNT_UL + special_tile->ridx;
        }

        draw_fulltile(dst_buff, dst_stride, data, type, dst_x_px, dst_y_px);

        // don't draw anything after this tile
        if (special_tile->masking)
        {
            data->masked[mask_idx] = true;
        }
    }

    playdate->graphics->markUpdatedRows(dst_y, dst_y + window_h * HALFTILE_H * 2 - 1);
}

bool get_coords_in_area(
    ScriptData* data, unsigned room_idx, unsigned embedding, unsigned rom_bank, unsigned rom_x,
    unsigned rom_y, unsigned* x, unsigned* y
)
{
    struct Room* room = &rooms[room_idx];
    size_t offset = room->data_offset;
    for (int i = 0; i < room->embeddings; ++i)
    {
        unsigned em_bank;
        int em_x, em_y;
        read_embedding_header(room_embeddings, &offset, &em_bank, &em_x, &em_y);
        read_bits(room_embeddings, &offset, room->w * get_room_h(room));

        if (em_bank == rom_bank && i == embedding)
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
    if (glyph_idx >= data->glyph_c)
        return;

    playdate->graphics->drawBitmap(data->glyphs[glyph_idx], x, y, flip);
    playdate->graphics->markUpdatedRows(y, y + 16);
}

static void draw_msg(ScriptData* data, const char* msg)
{
    int len = strlen(msg);
    int x = MAX(0, 160 - len * 8);
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

static bool is_gameplay_mode(unsigned game_mode)
{
    return game_mode == 4 || game_mode == 5 || game_mode == 6 || game_mode == 8 || game_mode == 9;
}

#define MENU_IMG_W 200

static void cb_expand_map(ScriptData* data)
{
    data->map_mode = MAP_MODE_AREA;
    data->map_mode_area = data->map_area;

    data->map_mode_x = 0;
    data->map_mode_y = 0;

    if (data->samus_bank >= MAP_FIRST_BANK)
    {
        struct AreaAssociation assoc = data->area_associations
                                           [((unsigned)data->samus_bank - MAP_FIRST_BANK) * 0x100 +
                                            data->samus_y * 0x10 + data->samus_x];
        unsigned sx, sy;
        if (assoc.area_idx == data->map_mode_area && !assoc.dark &&
            get_coords_in_area(
                data, assoc.room_idx, assoc.embedding, data->samus_bank, data->samus_x,
                data->samus_y, &sx, &sy
            ))
        {
            int screen_w = LCD_COLUMNS / HALFTILE_W / 2;
            int screen_h = (LCD_ROWS - 16) / HALFTILE_H / 2;
            data->map_mode_x = (int)sx - screen_w / 2;
            data->map_mode_y = (int)sy - screen_h / 2;
        }
    }
}

static void on_settings(ScriptData* data)
{
    const char* off_on_options[] = {"Off", "On", NULL};
    const char* map_blink_options[] = {"On", "Off", "Map", NULL};
    script_custom_setting_add(
        "Weapon Cycling",
        "Change beams using\nthe crank.\n \nSamus must have\nobtained an alternate\nbeam first.\n "
        "\nNote: this feature not in\noriginal game at all.",
        off_on_options
    );

    script_custom_setting_add(
        "HUD animation", "Controls periodic\nblinking in HUD.", map_blink_options
    );
}

static unsigned on_menu(gb_s* gb, ScriptData* data)
{
    if (data->map_mode != MAP_MODE_NONE)
    {
        return 0;
    }

    unsigned game_mode = ram_peek(0xFF9B);

    if (is_gameplay_mode(game_mode) && data->map_area != AREA_SECRET_WORLD &&
        data->samus_bank >= MAP_FIRST_BANK)
    {
        // menu image
        LCDBitmap* img = playdate->graphics->newBitmap(LCD_COLUMNS, LCD_ROWS, kColorBlack);
        struct Area* area = &areas[data->map_area];

        struct AreaAssociation association =
            data->area_associations
                [((unsigned)data->samus_bank - MAP_FIRST_BANK) * 0x100 + data->samus_y * 0x10 +
                 data->samus_x];
        unsigned x, y;

        if (!association.dark && get_coords_in_area(
                                     data, association.room_idx, association.embedding,
                                     data->samus_bank, data->samus_x, data->samus_y, &x, &y
                                 ))
        {
            playdate->system->addMenuItem("Expand Map", (void*)cb_expand_map, data);

            int samus_area_x = x;
            int samus_area_y = y;

            int stride;
            uint8_t* buff;
            playdate->graphics->getBitmapData(img, NULL, NULL, &stride, NULL, &buff);

            // line break
            int max_glyphs_per_line = MENU_IMG_W / 16;
            int last_space = -1;
            int current_line = 0;
            int j = 0;
            char* area_name = cb_strdup(area->name);
            for (int i = 0; i < strlen(area->name); ++i, ++j)
            {
                if (area_name[i] == ' ')
                    last_space = i;

                if (j >= max_glyphs_per_line && last_space >= 0)
                {
                    j = i - last_space;
                    current_line++;
                    area_name[last_space] = '\n';
                    last_space = -1;
                }
            }

            int mapleft = 0;
            int maptop = 16 * (current_line + 1);

            int max_h = (LCD_ROWS - maptop) / HALFTILE_H / 2;
            int max_w = (MENU_IMG_W) / HALFTILE_W / 2;
            int full_w = LCD_COLUMNS / HALFTILE_W / 2;

            int w = MIN(max_w, area->w);
            int h = MIN(max_h, area->h);

            // bounds
            if (max_w >= area->w)
            {
                x = 0;
            }
            else if (x < max_w / 2)
            {
                x = 0;
            }
            else if (x >= area->w - max_w / 2)
            {
                x = area->w - max_w;
            }
            else
            {
                x -= max_w / 2;
            }

            if (max_h >= area->h)
            {
                y = 0;
            }
            else if (y < max_h / 2)
            {
                y = 0;
            }
            else if (y >= area->h - max_h / 2)
            {
                y = area->h - max_h;
            }
            else
            {
                y -= max_h / 2;
            }

            int dst_x = (MENU_IMG_W - w * HALFTILE_W * 2) / 2 + mapleft;
            dst_x &= ~7;
            if (area->w >= max_w)
                dst_x = 0;
            int dst_y = (LCD_ROWS - h * HALFTILE_H * 2) / 2 + maptop;
            dst_y &= ~7;
            if (area->h >= max_h)
                dst_y = maptop;

            // expand so that the map is visible to the right of the hidden area
            w = MIN(full_w, area->w - x);

            w = MIN(w, (LCD_COLUMNS - dst_x) / 2 / HALFTILE_W);
            h = MIN(h, (LCD_ROWS - dst_y) / 2 / HALFTILE_H);

            draw_map(buff, stride, data, data->map_area, dst_x, dst_y, w, h, x, y);

            playdate->graphics->pushContext(img);

            if (samus_area_x >= x && samus_area_y >= y)
            {
                playdate->graphics->fillRect(
                    dst_x + 2 * HALFTILE_W * (samus_area_x - x),
                    dst_y + 2 * HALFTILE_W * (samus_area_y - y), HALFTILE_H * 2, HALFTILE_W * 2,
                    kColorXOR
                );
            }

            // area name
            const char* msg = area_name;
            int msg_y = 0;
            while (msg && *msg)
            {
                int len = strlen(msg);
                if (strchr(msg, '\n'))
                {
                    len = strchr(msg, '\n') - msg;
                }
                int x = MAX(0, MENU_IMG_W / 2 - len * 8);
                for (int i = 0; i < len; ++i)
                {
                    char c = msg[i];
                    if (c >= 'A' && c <= 'Z')
                    {
                        draw_glyph(data, GLYPH_A + c - 'A', x, msg_y, kBitmapUnflipped);
                    }
                    if (c >= 'a' && c <= 'z')
                    {
                        draw_glyph(data, GLYPH_A + c - 'a', x, msg_y, kBitmapUnflipped);
                    }
                    x += 16;
                }
                if (msg[len] == '\n')
                {
                    msg = &msg[len + 1];
                    msg_y += 16;
                }
                else
                {
                    break;
                }
            }
            playdate->graphics->fillRect(0, maptop, LCD_COLUMNS, 1, (uintptr_t)&lcdp_50b);
            cb_free(area_name);

            // fill right side with black
            playdate->graphics->fillRect(
                MENU_IMG_W, 0, LCD_COLUMNS - MENU_IMG_W, LCD_ROWS, kColorBlack
            );

            playdate->graphics->popContext();
            playdate->system->setMenuImage(img, 0);

            playdate->graphics->freeBitmap(img);

            return SCRIPT_MENU_SUPPRESS_IMAGE | SCRIPT_MENU_SUPPRESS_BUTTON;
        }

        playdate->graphics->freeBitmap(img);
    }

    return 0;
}

static void on_draw(gb_s* gb, ScriptData* data)
{
    unsigned game_mode = ram_peek(0xFF9B);

    game_picture_x_offset = CB_LCD_X;
    game_picture_scaling = 3;
    game_picture_y_top = 0;
    game_picture_y_bottom = LCD_HEIGHT;
    game_hide_indicator = false;

    if (is_gameplay_mode(game_mode) || game_mode == 10 || game_mode == 7)
    {
        game_hide_indicator = true;

        // flush left, expand to hide HUD
        game_picture_x_offset = 0;
        game_picture_scaling = 5;
        game_picture_y_top = 2;
        game_picture_y_bottom = 136;

        // cover up top-pixel scaling glitch
        playdate->graphics->fillRect(0, 0, LCD_COLUMNS - 80, 1, kColorBlack);

        if (game_mode == 7)  // game over
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
                playdate->graphics->fillRect(0, LCD_ROWS - 16, LCD_COLUMNS - 80, 16, kColorBlack);

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
                    switch (itemCollected)
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
                        0, LCD_ROWS - 16, (LCD_COLUMNS - 80) * MIN(1.0f, xorw), 16, kColorXOR
                    );
                }

                playdate->graphics->markUpdatedRows(LCD_ROWS - 16, LCD_ROWS - 1);
            }
            else
            {
                // for screen refresh reasons, the window must linger 1 frame longer than it
                // otherwise would
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
                            0, LCD_ROWS - 16, (LCD_COLUMNS - 80) * MIN(1.0f, data->prev_xorw), 16,
                            kColorXOR
                        );
                    }
                }
                data->prev_msg = NULL;
            }

            unsigned samus_bank = ram_peek(0xD058);
            unsigned samus_x = (((unsigned)ram_peek(0xFFC3) << 8) | ram_peek(0xFFC2));
            samus_x += 8;
            samus_x >>= 8;
            samus_x %= 16;
            unsigned samus_y = (((unsigned)ram_peek(0xFFC1) << 8) | ram_peek(0xFFC0));
            samus_y += 8;
            samus_y >>= 8;
            samus_y %= 16;

            unsigned frame_counter = ram_peek(0xFF97);
            bool flicker_on = !!(frame_counter & 0x10);

            unsigned samus_max_etanks = ram_peek(0xD050);
            unsigned samus_disp_energy = ram_peek_u16(0xd084);
            unsigned samus_disp_missiles = ram_peek_u16(0xd086);
            unsigned metroid_disp = ram_peek(0xD09A) | (flicker_on << 12);
            unsigned samus_weapon = ram_peek(0xD04D);
            unsigned samus_equipment_weapon = ram_peek(0xD055);

            bool samus_weapon_change = samus_weapon != data->samus_weapon;
            data->samus_weapon = samus_weapon;

            unsigned shuffle = ram_peek(0xD096);
            if (shuffle > 0 && shuffle < 0x80)
            {
                // shuffle metroid display
                metroid_disp = (rand() % 0x10) | ((rand() % 0x10) << 4);
            }

            if (gbScreenRequiresFullRefresh)
            {
                playdate->graphics->fillRect(LCD_COLUMNS - 80, 0, 80, LCD_COLUMNS, kColorBlack);
            }

            bool door_transition =
                ram_peek(0xD00E) || ram_peek(0xD08E) || ram_peek(0xD08F) || ram_peek(0xC458);

            // reuse previous position if we're in an empty cell, scrolling screen, or reached
            // gunship state
            struct AreaAssociation association =
                data->area_associations
                    [(samus_bank - MAP_FIRST_BANK) * 0x100 + samus_y * 0x10 + samus_x];
            if (association.empty || game_mode == 10 || door_transition ||
                data->door_transition_suppress_map_update > 0)
            {
                samus_x = data->samus_x;
                samus_y = data->samus_y;
                samus_bank = data->samus_bank;
            }
            else
            {
                data->map_dark = association.dark;
            }

            if (gbScreenRequiresFullRefresh || data->samus_x != samus_x ||
                data->samus_y != samus_y || data->samus_bank != samus_bank ||
                data->map_flicker != flicker_on)
            {
                data->samus_bank = samus_bank;
                data->samus_x = samus_x;
                data->samus_y = samus_y;
                data->map_flicker = flicker_on;

                if (samus_bank >= MAP_FIRST_BANK)
                {
                    set_map_explored(data, samus_bank, samus_x, samus_y, true);
                }

                uint8_t* frame = playdate->graphics->getFrame();

                if (data->samus_bank == 0 || data->map_dark)
                {
                    goto draw_secret_world;
                }
                else
                {
                    struct AreaAssociation association =
                        data->area_associations
                            [((unsigned)data->samus_bank - MAP_FIRST_BANK) * 0x100 +
                             data->samus_y * 0x10 + data->samus_x];
                    unsigned x, y;

                    if (association.unmapped)
                    {
                    draw_secret_world:;
                        draw_map(
                            frame, LCD_ROWSIZE, data, AREA_SECRET_WORLD, LCD_COLUMNS - 80, 0, 5, 5,
                            0, 0
                        );
                    }
                    else if (
                        get_coords_in_area(
                            data, association.room_idx, association.embedding, data->samus_bank,
                            data->samus_x, data->samus_y, &x, &y
                        )
                    )
                    {
                        draw_map(
                            frame, LCD_ROWSIZE, data, association.area_idx, LCD_COLUMNS - 80, 0, 5,
                            5, (int)x - 2, (int)y - 2
                        );
                        if (flicker_on || !HUD_Map_blinks)
                        {
                            playdate->graphics->fillRect(
                                LCD_COLUMNS - 80 + 16 * 2, 0 + 16 * 2, 16, 16, kColorXOR
                            );
                        }
                    }
                    else
                    {
                        draw_map(
                            frame, LCD_ROWSIZE, data, AREA_SECRET_WORLD, LCD_COLUMNS - 80, 0, 5, 5,
                            0, 0
                        );
                    }
                }
            }

            int ui_y = 6 * 16 + 8;
            int ui_x = LCD_COLUMNS - 80;

            int row1y = ui_y + 16 + 4;
            int row2y = ui_y;

            ui_y += 20;

            if (data->samus_max_etanks > 0)
                ui_y += 20;

            if (gbScreenRequiresFullRefresh || samus_weapon_change ||
                data->samus_max_etanks != samus_max_etanks)
            {
                playdate->graphics->fillRect(LCD_COLUMNS - 80, ui_y, 80, 16, kColorBlack);

                if (weapon_cycle_enabled)
                {
                    for (int i = 0; i < 5; ++i)
                    {
                        if ((data->weapons_collected & (1 << i)) || i == 0)
                        {
                            int sx = 5 - i;
                            draw_glyph(
                                data, GLYPH_BEAM + i, LCD_COLUMNS - 15 * (sx), ui_y,
                                kBitmapUnflipped
                            );
                            if (i == samus_weapon)
                            {
                                draw_glyph(
                                    data, GLYPH_SELECTED, LCD_COLUMNS - 15 * (sx), ui_y,
                                    kBitmapUnflipped
                                );
                            }
                            else if (i == samus_equipment_weapon)
                            {
                                draw_glyph(
                                    data, GLYPH_SELECTED + 1, LCD_COLUMNS - 15 * (sx), ui_y,
                                    kBitmapUnflipped
                                );
                            }
                        }
                    }
                }
                else if (samus_equipment_weapon != 0)
                {
                    bool equipped = (samus_weapon == samus_equipment_weapon);
                    playdate->graphics->fillRect(
                        LCD_COLUMNS - 16 * 5 + 2, row2y, 16, 16, kColorBlack
                    );
                    draw_glyph(
                        data, GLYPH_BEAM + samus_equipment_weapon, LCD_COLUMNS - 16 * 5 + 2, row2y,
                        kBitmapUnflipped
                    );
                    if (equipped)
                    {
                        draw_glyph(
                            data, GLYPH_SELECTED, LCD_COLUMNS - 16 * 5 + 2, row2y, kBitmapUnflipped
                        );
                    }
                }
            }

            if (gbScreenRequiresFullRefresh || data->samus_disp_energy != samus_disp_energy ||
                data->samus_max_etanks != samus_max_etanks || samus_weapon_change)
            {
                data->samus_disp_energy = samus_disp_energy;
                data->samus_max_etanks = samus_max_etanks;
                int etanks = data->samus_disp_energy >> 8;

                if (data->samus_max_etanks > 0)
                {
                    playdate->graphics->fillRect(LCD_COLUMNS - 80, row1y, 80, 16, kColorBlack);
                }

                for (int i = 0; i < 5 && i < data->samus_max_etanks; ++i)
                {
                    bool etank_full = i < etanks;
                    int x = LCD_COLUMNS - 16 * (i + 1);
                    draw_glyph(data, 208 / 16 + etank_full, x, row1y, kBitmapUnflipped);
                }

                int digitlo = samus_disp_energy & 0xF;
                int digithi = (samus_disp_energy >> 4) & 0xF;

                int offx = -8;
                if (!weapon_cycle_enabled && samus_equipment_weapon != 0)
                    offx = 0;

                // energy
                draw_glyph(
                    data, GLYPH_DASH, LCD_COLUMNS - 16 * 3 + 1 + offx, row2y, kBitmapUnflipped
                );
                draw_glyph(data, 10, LCD_COLUMNS - 16 * 4 + 2 + offx, row2y, kBitmapUnflipped);
                draw_glyph(
                    data, digithi + GLYPH_0, LCD_COLUMNS - 16 * 2 + offx, row2y, kBitmapUnflipped
                );
                draw_glyph(
                    data, digitlo + GLYPH_0, LCD_COLUMNS - 16 * 1 + offx, row2y, kBitmapUnflipped
                );
            }

            ui_y = 5 * 16 + 4;

            if (gbScreenRequiresFullRefresh || data->samus_disp_missiles != samus_disp_missiles ||
                samus_weapon_change)
            {
                data->samus_disp_missiles = samus_disp_missiles;

                int digitlo = samus_disp_missiles & 0xF;
                int digithi = (samus_disp_missiles >> 4) & 0xF;
                int digithihi = (samus_disp_missiles >> 8) & 0xF;

                draw_glyph(data, GLYPH_DASH, LCD_COLUMNS - 16 * 4, ui_y, kBitmapUnflipped);
                draw_glyph(
                    data, (samus_weapon & 0x8) ? GLYPH_MISSILES_EQUIPPED : GLYPH_MISSILES,
                    LCD_COLUMNS - 16 * 5 + 2, ui_y, kBitmapUnflipped
                );
                draw_glyph(
                    data, digithihi + GLYPH_0, LCD_COLUMNS - 16 * 3 + 1, ui_y, kBitmapUnflipped
                );
                draw_glyph(data, digithi + GLYPH_0, LCD_COLUMNS - 16 * 2, ui_y, kBitmapUnflipped);
                draw_glyph(data, digitlo + GLYPH_0, LCD_COLUMNS - 16 * 1, ui_y, kBitmapUnflipped);
            }

            ui_y = LCD_ROWS - 18;

            if (gbScreenRequiresFullRefresh || data->metroid_disp != metroid_disp)
            {
                data->metroid_disp = metroid_disp;
                int metroid_glyph = (176 / 16) + (flicker_on && HUD_blinking);

                int digitlo = metroid_disp & 0xF;
                int digithi = (metroid_disp >> 4) & 0xF;

                draw_glyph(data, GLYPH_DASH, LCD_COLUMNS - 16 * 3 + 1, ui_y, kBitmapUnflipped);
                draw_glyph(data, metroid_glyph, LCD_COLUMNS - 16 * 5 + 2, ui_y, kBitmapUnflipped);
                draw_glyph(data, metroid_glyph, LCD_COLUMNS - 16 * 4 + 2, ui_y, kBitmapFlippedX);
                draw_glyph(data, digithi + GLYPH_0, LCD_COLUMNS - 16 * 2, ui_y, kBitmapUnflipped);
                draw_glyph(data, digitlo + GLYPH_0, LCD_COLUMNS - 16 * 1, ui_y, kBitmapUnflipped);
            }

            // divider
            playdate->graphics->fillRect(LCD_COLUMNS - 80, 0, 1, LCD_ROWS, (uintptr_t)&lcdp_50b);
            playdate->graphics->fillRect(LCD_COLUMNS - 80, 80 - 1, 80, 1, (uintptr_t)&lcdp_50b);
        }
    }
}

size_t query_serial_size(ScriptData* data)
{
    return 3
           // map exploration
           + MAP_BANK_COUNT * 0x100;
}

#define SERIAL_VERSION 0x30

bool serialize(char* out, ScriptData* data)
{
    out[0] = SERIAL_VERSION;
    out[1] = data->door_transition_suppress_map_update;
    out[2] = data->weapons_collected;
    for (int i = 0; i < MAP_BANK_COUNT * 0x100; ++i)
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

    data->map_mode = MAP_MODE_NONE;
    audio_enabled = 1;

    if ((unsigned)in[0] != (unsigned)SERIAL_VERSION)
        return false;

    data->door_transition_suppress_map_update = in[1];
    data->weapons_collected = in[2];

    for (int i = 0; i < 0x100 * MAP_BANK_COUNT; ++i)
    {
        data->map_explored[i] = in[i + 3];
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
    .on_menu = (CS_OnMenu)on_menu,
    .on_settings = (CS_OnSettings)on_settings,
    .on_end = (CS_OnEnd)on_end,

    .query_serial_size = (CS_QuerySerialSize)query_serial_size,
    .serialize = (CS_Serialize)serialize,
    .deserialize = (CS_Deserialize)deserialize,
};
#endif
