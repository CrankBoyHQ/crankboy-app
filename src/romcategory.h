#pragma once

#include "pd_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_CATEGORY_NAME 21  // 20 chars + NUL — fits the library filter bar

// stable tokens for the persisted display order
#define CATEGORY_ORDER_ALL_TOKEN "all"
#define CATEGORY_ORDER_UNCATEGORIZED_TOKEN "uncategorized"
#define CATEGORY_ORDER_PACKED_TOKEN "packed"

// typed entry prefixes for the persisted display order
#define CATEGORY_ORDER_ID_PREFIX "i:"
#define CATEGORY_ORDER_GENRE_PREFIX "g:"

// user-category stable identity: 8 hex chars + NUL
#define CATEGORY_ID_LEN 9

enum RomCategoryType
{
    ROMCAT_STANDARD = 0,
    ROMCAT_ALL = 1,
    ROMCAT_PACKED,
    ROMCAT_GENRE,
    ROMCAT_UNCATEGORIZED,
};

typedef struct CB_GameName CB_GameName;

typedef struct RomCategory
{
    bool enabled : 1;  // only intended for fixed categories
    bool requires_catalog : 1;
    enum RomCategoryType type;

    char name[MAX_CATEGORY_NAME];
    char id[CATEGORY_ID_LEN];  // stable identity, user categories only
    char* icon_path;           // owned / can be NULL
    char* icon_slug;           // can be NULL
    uint8_t roms[];            // one bit per CB_App->gameNameCache
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

RomCategory* romcategory_new(enum RomCategoryType type, const char* name);
bool romcategories_append(RomCategory*** pcats, RomCategory* cat);  // false on OOM
void romcategories_remove(RomCategory*** pcats, RomCategory* cat);  // also frees

RomCategory* romcategories_find_type(RomCategory** cats, enum RomCategoryType type);

// first user category with the given id
RomCategory* romcategories_find_by_id(RomCategory** cats, const char* id);

int romcategories_index_of_type(RomCategory** cats, enum RomCategoryType type);

// STANDARD/GENRE match type+name; single-instance categories resolve by type
RomCategory* romcategories_find_by_type_and_name(
    RomCategory** cats, enum RomCategoryType type, const char* name
);

bool romcategory_requires_catalog(const RomCategory* cat);

// localized display; identity stays the raw DB genre
const char* romcategory_display_name(const RomCategory* cat);

// "Show Genres": hides genre categories when false; All/Uncategorized unaffected
bool romcategories_genres_visible(void);
void romcategories_set_genres_visible(bool visible);  // persists immediately

// true when the category may be listed by editors/library
bool romcategory_is_listed(const RomCategory* cat);

// GameName pointer to gameNameCache index
typedef struct RomCategoryNameIndex RomCategoryNameIndex;

RomCategoryNameIndex* romcategory_name_index_build(void);

int romcategory_name_index_lookup(const RomCategoryNameIndex* index, const CB_GameName* name);

void romcategory_name_index_free(RomCategoryNameIndex* index);
