#pragma once

#include "pd_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_CATEGORY_NAME 32

enum RomCategoryType
{
    ROMCAT_STANDARD = 0,
    ROMCAT_ALL = 1,
    ROMCAT_PACKED,
};

typedef struct CB_GameName CB_GameName;

typedef struct RomCategory
{
    bool enabled;  // only intended for fixed categories
    bool requires_catalog; // don't show if this is not a catalog build (wouldn't be useful anyway)
    enum RomCategoryType type;

    char name[MAX_CATEGORY_NAME];
    char* icon_path;  // owned / can be NULL
    char* icon_slug;  // can be NULL
    uint8_t roms[];   // one bit per CB_App->gameNameCache
} RomCategory;

// number of roms in this category
size_t romcategory_count(const RomCategory*);

// cb: return negative value to stop.
void for_rom_in_category(const RomCategory*, int (*cb)(CB_GameName*, void* ud), void* ud);

bool romcategory_contains(const RomCategory* cat, size_t index);
void romcategory_put(RomCategory* cat, size_t index, bool contains);

// returns null-terminated list of categories.
RomCategory** romcategories_load_all(size_t* o_count);

// returns negative on failure, 0 on success
int romcategories_write_all(RomCategory**);

void romcategory_free(RomCategory*);
void romcategories_free_all(RomCategory**);
