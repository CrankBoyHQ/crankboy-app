#pragma once

#include "pd_api.h"

#define MAX_CATEGORY_NAME 32

enum RomCategoryType
{
    ROMCAT_STANDARD = 0,
    ROMCAT_ALL = 1,
    ROMCAT_INCLUDED,
};

typedef struct CB_GameName CB_GameName;

typedef struct RomCategory
{
    bool enabled; // only intended for fixed categories
    enum RomCategoryType type;
    
    char name[MAX_CATEGORY_NAME];
    char* icon_path; // owned / can be NULL
    char* icon_slug; // can be NULL
    uint8_t roms[]; // one bit per CB_App->gameNameCache
} RomCategory;

// cb: return negative value to stop.
void for_rom_in_category(const RomCategory*, int (*cb)(CB_GameName*, void* ud), void* ud);

// returns null-terminated list of categories.
RomCategory** romcategories_load_all(size_t* o_count);

// returns negative on failure, 0 on success
int romcategories_write_all(RomCategory**);

void romcategories_free_all(RomCategory**);