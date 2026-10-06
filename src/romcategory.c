#include "romcategory.h"

#include "app.h"
#include "global.h"
#include "jparse.h"
#include "utility.h"

#include <stdlib.h>
#include <string.h>

static bool romcategories_has_type(RomCategory* const*, enum RomCategoryType rt);

// "Show Genres" switch; defaults on until the persisted state loads
static bool s_show_genres = true;

static int genre_sort_cb(const void* a, const void* b)
{
    const CB_GameName* const* ga = a;
    const CB_GameName* const* gb = b;
    return strcmp((*ga)->genre, (*gb)->genre);
}

// bytes needed for roms[]
static size_t romcategory_bitc(void);

static size_t romcategory_bitc(void)
{
    size_t n = (CB_App && CB_App->gameNameCache) ? CB_App->gameNameCache->length : 0;
    return (n + 7) / 8;
}

static uint32_t s_cat_id_counter = 0;

// stable user-category identity: order/last-active persistence keys on it;
// counter keeps same-ms creations distinct, time separates sessions
static void category_generate_id(RomCategory* cat)
{
    uint32_t value = (uint32_t)playdate->system->getCurrentTimeMilliseconds() ^
                     (s_cat_id_counter++ * 2654435761u);
    snprintf(cat->id, sizeof(cat->id), "%08x", value);
}

RomCategory* romcategory_new(enum RomCategoryType type, const char* name)
{
    RomCategory* cat = mallocz(sizeof(RomCategory) + romcategory_bitc());
    if (!cat)
        return NULL;

    cat->type = type;
    cat->enabled = true;
    if (name)
        snprintf(cat->name, sizeof(cat->name), "%s", name);
    if (type == ROMCAT_STANDARD)
        category_generate_id(cat);
    return cat;
}

bool romcategory_contains(const RomCategory* cat, size_t index)
{
    if (!cat || index >= (size_t)CB_App->gameNameCache->length)
        return false;

    switch (cat->type)
    {
    case ROMCAT_STANDARD:
        return !!(cat->roms[index / 8] & (1 << (index % 8)));
    case ROMCAT_ALL:
        return true;
    case ROMCAT_PACKED:
        return ((const CB_GameName*)CB_App->gameNameCache->items[index])->packed;
    case ROMCAT_GENRE:
    {
        const CB_GameName* name = (const CB_GameName*)CB_App->gameNameCache->items[index];
        // prefix-compare: genres longer than the name cap still match
        return name->genre && strncmp(name->genre, cat->name, MAX_CATEGORY_NAME - 1) == 0;
    }
    case ROMCAT_UNCATEGORIZED:
    {
        // no DB genre and no membership in any enabled user category
        const CB_GameName* name = (const CB_GameName*)CB_App->gameNameCache->items[index];
        if (name->genre && *name->genre)
            return false;

        for (RomCategory** user_cat = CB_App->romcategories; user_cat && *user_cat; ++user_cat)
        {
            if ((*user_cat)->type == ROMCAT_STANDARD && (*user_cat)->enabled &&
                romcategory_contains(*user_cat, index))
            {
                return false;
            }
        }
        return true;
    }
    default:
        CB_ASSERT(false);
        return false;
    }
}

void romcategory_put(RomCategory* cat, size_t index, bool contains)
{
    if (!cat || cat->type != ROMCAT_STANDARD)
        return;
    if (index >= (size_t)CB_App->gameNameCache->length)
        return;
    if (contains)
        cat->roms[index / 8] |= (1 << (index % 8));
    else
        cat->roms[index / 8] &= ~(1 << (index % 8));
}

static int romcategory_count_helper(CB_GameName* name, void* n)
{
    ++*(size_t*)n;
    return 0;
}

size_t romcategory_count(const RomCategory* cat)
{
    size_t n = 0;
    for_rom_in_category(cat, romcategory_count_helper, &n);
    return n;
}

void for_rom_in_category(const RomCategory* cat, int (*cb)(CB_GameName*, void* ud), void* ud)
{
    for (size_t i = 0; i < CB_App->gameNameCache->length; ++i)
    {
        if (!romcategory_contains(cat, i))
            continue;
        if (cb(CB_App->gameNameCache->items[i], ud) < 0)
            return;
    }
}

// -1 if no such rom is present.
static ssize_t get_rom_index_for_path(const char* path)
{
    if (!path)
        return -1;

    for (size_t i = 0; i < CB_App->gameNameCache->length; ++i)
    {
        const CB_GameName* name = CB_App->gameNameCache->items[i];
        if (name->fullpath && !strcmp(name->fullpath, path))
            return (ssize_t)i;
    }

    return -1;
}

// persisted-bool read: true/false honored; null/missing/junk -> default
static bool json_flag(json_value j, const char* key, bool default_value)
{
    json_value jv = json_get_table_value(j, key);
    if (jv.type == kJSONTrue)
        return true;
    if (jv.type == kJSONFalse)
        return false;
    return default_value;
}

static RomCategory* romcat_from_json(json_value j)
{
    const char* name = json_as_string(json_get_table_value(j, "name"));
    if (!name)
        return NULL;

    RomCategory* cat = romcategory_new(ROMCAT_STANDARD, name);
    if (!cat)
        return NULL;

    const char* icon_slug = json_as_string(json_get_table_value(j, "icon-slug"));
    const char* icon_path = json_as_string(json_get_table_value(j, "icon-path"));
    if (icon_slug)
        cat->icon_slug = cb_strdup(icon_slug);
    else if (icon_path)
        cat->icon_path = cb_strdup(icon_path);

    cat->enabled = json_flag(j, "enabled", true);

    const char* id = json_as_string(json_get_table_value(j, "id"));
    if (id && strlen(id) < sizeof(cat->id))
        snprintf(cat->id, sizeof(cat->id), "%s", id);

    json_value jroms = json_get_table_value(j, "roms");
    if (jroms.type == kJSONArray)
    {
        JsonArray* roms = jroms.data.arrayval;
        for (size_t i = 0; roms && i < roms->n; ++i)
        {
            int index = get_rom_index_for_path(json_as_string(roms->data[i]));
            if (index >= 0)
                romcategory_put(cat, index, true);
        }
    }

    return cat;
}

RomCategory** romcategories_load_all(size_t* o_count)
{
    json_value j;
    RomCategory** cats = NULL;

    if (!parse_json(CATEGORY_PATH, &j, kFileReadData | kFileRead))
        j.type = kJSONNull;

    json_value jmisc = json_get_table_value(j, "misc");

// fixed categories first: positions arbitrary — lookups are by type/name;
// persistence uses tokens
// TODO: set icon on the fixed categories
#define ROMCAT_FIXED_DEFAULT(E, key, NAME, icon, default, CATALOG) \
    if (!romcategories_has_type(cats, E))                          \
    {                                                              \
        RomCategory* cat = romcategory_new(E, NAME);               \
        if (cat)                                                   \
        {                                                          \
            cat->enabled = json_flag(jmisc, key, default);         \
            cat->icon_slug = cb_strdup(icon);                      \
            romcategories_append(&cats, cat);                      \
            cat->requires_catalog = CATALOG;                       \
        }                                                          \
    }

    ROMCAT_FIXED_DEFAULT(ROMCAT_ALL, "all", T(cat_all), "cat-all", true, false);
    ROMCAT_FIXED_DEFAULT(ROMCAT_PACKED, "packed", T(cat_packed), "cat-packed", true, true);

#undef ROMCAT_FIXED_DEFAULT

    json_value jcats = json_get_table_value(j, "categories");
    if (jcats.type == kJSONArray)
    {
        JsonArray* arr = jcats.data.arrayval;
        for (size_t i = 0; arr && i < arr->n; ++i)
        {
            RomCategory* cat = romcat_from_json(arr->data[i]);
            if (cat)
                romcategories_append(&cats, cat);
        }
    }

    // auto-generated read-only genre categories, alphabetical
    {
        size_t n_genres = 0;
        for (size_t i = 0; i < CB_App->gameNameCache->length; ++i)
        {
            const CB_GameName* name = CB_App->gameNameCache->items[i];
            if (name->genre)
                ++n_genres;
        }

        const CB_GameName** genre_names = mallocz(sizeof(CB_GameName*) * (n_genres ? n_genres : 1));
        if (genre_names)
        {
            size_t n = 0;
            for (size_t i = 0; i < CB_App->gameNameCache->length; ++i)
            {
                const CB_GameName* name = CB_App->gameNameCache->items[i];
                if (name->genre)
                    genre_names[n++] = name;
            }

            qsort(genre_names, n, sizeof(CB_GameName*), genre_sort_cb);

            for (size_t i = 0; i < n; ++i)
            {
                if (i > 0 &&
                    strncmp(
                        genre_names[i]->genre, genre_names[i - 1]->genre, MAX_CATEGORY_NAME - 1
                    ) == 0)
                    continue;

                RomCategory* cat = romcategory_new(ROMCAT_GENRE, genre_names[i]->genre);
                if (cat)
                {
                    cat->enabled = true;
                    romcategories_append(&cats, cat);
                }
            }

            cb_free(genre_names);
        }
    }

    s_show_genres = json_flag(jmisc, "show-genres", true);

    {
        json_value jgenres = json_get_table_value(jmisc, "genres");
        if (jgenres.type == kJSONTable)
        {
            for (RomCategory** cat = cats; cat && *cat; ++cat)
            {
                if ((*cat)->type != ROMCAT_GENRE)
                    continue;
                (*cat)->enabled = json_flag(jgenres, (*cat)->name, true);
            }
        }
    }

    // fixed category for games with no genre and no enabled category membership
    if (!romcategories_has_type(cats, ROMCAT_UNCATEGORIZED))
    {
        RomCategory* cat = romcategory_new(ROMCAT_UNCATEGORIZED, T(cat_uncategorized));
        if (cat)
        {
            cat->enabled = true;
            romcategories_append(&cats, cat);
        }
    }

    for (RomCategory** cat = cats; cat && *cat; ++cat)
    {
        if ((*cat)->type == ROMCAT_UNCATEGORIZED)
            (*cat)->enabled = json_flag(jmisc, "uncategorized", true);
    }

    // persisted display order: saved categories first, leftover categories appended
    {
        json_value jorder = json_get_table_value(jmisc, "categories-order");
        if (jorder.type == kJSONArray)
        {
            JsonArray* arr = jorder.data.arrayval;

            size_t n_total = len_nullterm((void const* const*)cats);

            size_t slot = 0;
            for (size_t s = 0; arr && s < arr->n && slot < n_total; ++s)
            {
                const char* name = json_as_string(arr->data[s]);
                if (!name)
                    continue;

                bool token_all = strcmp(name, CATEGORY_ORDER_ALL_TOKEN) == 0;
                bool token_uncategorized = strcmp(name, CATEGORY_ORDER_UNCATEGORIZED_TOKEN) == 0;
                bool typed_id =
                    strncmp(name, CATEGORY_ORDER_ID_PREFIX, strlen(CATEGORY_ORDER_ID_PREFIX)) == 0;
                bool typed_genre =
                    strncmp(
                        name, CATEGORY_ORDER_GENRE_PREFIX, strlen(CATEGORY_ORDER_GENRE_PREFIX)
                    ) == 0;

                // typed entries carry the key after the prefix
                const char* match_key = name;
                if (typed_id)
                    match_key += strlen(CATEGORY_ORDER_ID_PREFIX);
                else if (typed_genre)
                    match_key += strlen(CATEGORY_ORDER_GENRE_PREFIX);

                size_t found = n_total;
                for (size_t i = slot; i < n_total; ++i)
                {
                    bool match;
                    if (token_all)
                    {
                        match = cats[i]->type == ROMCAT_ALL;
                    }
                    else if (token_uncategorized)
                    {
                        match = cats[i]->type == ROMCAT_UNCATEGORIZED;
                    }
                    else if (typed_id)
                    {
                        match =
                            cats[i]->type == ROMCAT_STANDARD && strcmp(cats[i]->id, match_key) == 0;
                    }
                    else if (typed_genre)
                    {
                        match =
                            cats[i]->type == ROMCAT_GENRE && strcmp(cats[i]->name, match_key) == 0;
                    }
                    if (match)
                    {
                        found = i;
                        break;
                    }
                }
                if (found == n_total)
                    continue;  // saved entry no longer present -> skip

                // rotate: shift [slot .. found-1] right, place found at slot
                if (found != slot)
                {
                    RomCategory* moved = cats[found];
                    for (size_t i = found; i > slot; --i)
                        cats[i] = cats[i - 1];
                    cats[slot] = moved;
                }
                ++slot;
            }
        }
    }

    free_json_data(j);

    if (!cats)
    {
        // always return a (empty) null-terminated list.
        cats = mallocz(sizeof(RomCategory*));
    }

    if (o_count)
        *o_count = len_nullterm((void const* const*)cats);

    return cats;
}

bool romcategories_append(RomCategory*** cats, RomCategory* cat)
{
    size_t n = len_nullterm((void const* const*)*cats);
    RomCategory** grown = cb_realloc(*cats, sizeof(RomCategory*) * (n + 2));
    if (!grown)
    {
        romcategory_free(cat);
        return false;
    }

    grown[n] = cat;
    grown[n + 1] = NULL;
    *cats = grown;
    return true;
}

RomCategory* romcategories_find_type(RomCategory** cats, enum RomCategoryType type)
{
    for (RomCategory** cat = cats; cat && *cat; ++cat)
    {
        if ((*cat)->type == type)
            return *cat;
    }
    return NULL;
}

int romcategories_index_of_type(RomCategory** cats, enum RomCategoryType type)
{
    for (int i = 0; cats && cats[i]; ++i)
    {
        if (cats[i]->type == type)
            return i;
    }
    return -1;
}

RomCategory* romcategories_find_by_type_and_name(
    RomCategory** cats, enum RomCategoryType type, const char* name
)
{
    for (RomCategory** cat = cats; cat && *cat; ++cat)
    {
        if ((*cat)->type != type)
            continue;

        // user categories and genres may share names across kinds: match by name
        if (type == ROMCAT_STANDARD || type == ROMCAT_GENRE)
        {
            if (name && strcmp((*cat)->name, name) == 0)
                return *cat;
        }
        else
        {
            return *cat;  // single-instance categories resolve by type
        }
    }
    return NULL;
}

RomCategory* romcategories_find_by_id(RomCategory** cats, const char* id)
{
    if (!id || !*id)
        return NULL;
    for (RomCategory** cat = cats; cat && *cat; ++cat)
    {
        if ((*cat)->type == ROMCAT_STANDARD && strcmp((*cat)->id, id) == 0)
            return *cat;
    }
    return NULL;
}

bool romcategory_requires_catalog(const RomCategory* cat)
{
#ifndef CRANKBOY_OFFICIAL_CATALOG
    return cat->requires_catalog;
#else
    (void)cat;
    return false;  // catalog builds always show packed entries
#endif
}

// genre -> l10n key; identity stays the raw DB genre
typedef struct
{
    const char* genre;
    const char* key;
} CB_GenreL10nEntry;

static const CB_GenreL10nEntry GENRE_L10N[] = {
    {"Platform", "genre_platform"},
    {"Role-playing (RPG)", "genre_roleplaying_rpg"},
    {"Action", "genre_action"},
    {"Sports", "genre_sports"},
    {"Puzzle", "genre_puzzle"},
    {"Racing", "genre_racing"},
    {"Strategy", "genre_strategy"},
    {"Adventure", "genre_adventure"},
    {"Board", "genre_board"},
    {"Simulation", "genre_simulation"},
    {"Shooter", "genre_shooter"},
    {"Compilation", "genre_compilation"},
    {"Music / Dancing", "genre_music_dancing"},
    {"Shoot'em Up", "genre_shootem_up"},
    {"Gambling", "genre_gambling"},
    {"Beat'em Up", "genre_beatem_up"},
    {"Fighting", "genre_fighting"},
    {"Card", "genre_card"},
    {"Hunting and Fishing", "genre_hunting_and_fishing"},
    {"Educational", "genre_educational"},
    {"ROM Hack", "genre_rom_hack"},
    {"Pinball", "genre_pinball"},
    {"Quiz", "genre_quiz"},
    {"Sports with Animals", "genre_sports_with_animals"},
    {"Homebrew", "genre_homebrew"},
    {"Various", "genre_various"},
    {"Casual Game", "genre_casual_game"},
    {"Demo", "genre_demo"},
};

static const char* genre_l10n_key(const char* genre)
{
    if (!genre)
        return NULL;
    for (size_t i = 0; i < sizeof(GENRE_L10N) / sizeof(GENRE_L10N[0]); ++i)
    {
        if (strcmp(GENRE_L10N[i].genre, genre) == 0)
            return GENRE_L10N[i].key;
    }
    return NULL;
}

const char* romcategory_display_name(const RomCategory* cat)
{
    if (cat->type == ROMCAT_GENRE)
    {
        const char* key = genre_l10n_key(cat->name);
        if (key)
            return l10n(key);  // runtime key: T() would stringize the variable name
    }
    // empty = still-default -> localized fallback
    return cat->name[0] ? cat->name : T(cat_default_name);
}

bool romcategories_genres_visible(void)
{
    return s_show_genres;
}

void romcategories_set_genres_visible(bool visible)
{
    if (s_show_genres == visible)
        return;

    s_show_genres = visible;
    romcategories_write_all(CB_App->romcategories);
}

bool romcategory_is_listed(const RomCategory* cat)
{
    if (romcategory_requires_catalog(cat))
        return false;
    if (cat->type == ROMCAT_GENRE && !s_show_genres)
        return false;
    return true;
}

typedef struct
{
    const CB_GameName* name;
    int index;
} CB_NameIndexEntry;

struct RomCategoryNameIndex
{
    CB_NameIndexEntry* entries;
    int n;
};

static int name_index_cmp(const void* a, const void* b)
{
    const CB_NameIndexEntry* ga = a;
    const CB_NameIndexEntry* gb = b;
    return (ga->name == gb->name) ? 0 : ((uintptr_t)ga->name > (uintptr_t)gb->name ? 1 : -1);
}

RomCategoryNameIndex* romcategory_name_index_build(void)
{
    RomCategoryNameIndex* map = allocz(RomCategoryNameIndex);
    if (!map)
        return NULL;

    map->n = CB_App->gameNameCache ? CB_App->gameNameCache->length : 0;
    map->entries = mallocz(sizeof(CB_NameIndexEntry) * (map->n ? map->n : 1));
    if (!map->entries)
    {
        cb_free(map);
        return NULL;
    }

    for (int i = 0; i < map->n; ++i)
    {
        map->entries[i].name = CB_App->gameNameCache->items[i];
        map->entries[i].index = i;
    }

    if (map->n)
        qsort(map->entries, map->n, sizeof(CB_NameIndexEntry), name_index_cmp);

    return map;
}

int romcategory_name_index_lookup(const RomCategoryNameIndex* index, const CB_GameName* name)
{
    if (!index || !index->n || !name)
        return -1;

    CB_NameIndexEntry key = {.name = name, .index = 0};
    const CB_NameIndexEntry* found =
        bsearch(&key, index->entries, index->n, sizeof(CB_NameIndexEntry), name_index_cmp);
    return found ? found->index : -1;
}

void romcategory_name_index_free(RomCategoryNameIndex* index)
{
    if (!index)
        return;
    cb_free(index->entries);
    cb_free(index);
}

void romcategories_remove(RomCategory*** cats, RomCategory* cat)
{
    RomCategory** list = *cats;
    if (!list || !cat)
        return;

    size_t n = len_nullterm((void const* const*)list);
    for (size_t i = 0; i < n; ++i)
    {
        if (list[i] == cat)
        {
            memmove(&list[i], &list[i + 1], (n - i) * sizeof(RomCategory*));
            romcategory_free(cat);
            return;
        }
    }
}

static bool romcategories_has_type(RomCategory* const* cats, enum RomCategoryType rt)
{
    for (RomCategory* const* cat = cats; cat && *cat; ++cat)
    {
        if ((*cat)->type == rt)
            return true;
    }
    return false;
}

static int rom_path_append(CB_GameName* name, void* ud)
{
    JsonArray** roms = ud;

    const char* path = name->fullpath ? name->fullpath : name->filename;
    if (!path)
        return 0;

    JsonArray* arr = *roms;
    size_t n = arr ? arr->n : 0;
    arr = cb_realloc(arr, sizeof(JsonArray) + sizeof(json_value) * (n + 1));
    if (!arr)
        return -1;

    arr->data[n] = json_new_string(path);
    arr->n = n + 1;
    *roms = arr;
    return 0;
}

static bool romcat_to_json(RomCategory* cat, json_value* j, json_value* jfixed, json_value* jgenres)
{
    switch (cat->type)
    {
    case ROMCAT_STANDARD:
    {
        *j = json_new_table();
        if (j->type != kJSONTable)
            return false;

        json_set_table_value(j, "name", json_new_string(cat->name));
        if (cat->id[0])
            json_set_table_value(j, "id", json_new_string(cat->id));
        json_set_table_value(j, "enabled", json_new_bool(cat->enabled));
        if (cat->icon_slug)
            json_set_table_value(j, "icon-slug", json_new_string(cat->icon_slug));
        else if (cat->icon_path)
            json_set_table_value(j, "icon-path", json_new_string(cat->icon_path));

        JsonArray* roms = NULL;
        for_rom_in_category(cat, rom_path_append, &roms);

        json_value jroms = {.type = kJSONArray};
        jroms.data.arrayval = roms ? roms : mallocz(sizeof(JsonArray));
        json_set_table_value(j, "roms", jroms);
    }
    break;

    case ROMCAT_ALL:
        json_set_table_value(jfixed, "all", json_new_bool(cat->enabled));
        return false;
    case ROMCAT_PACKED:
        json_set_table_value(jfixed, "packed", json_new_bool(cat->enabled));
        return false;
    case ROMCAT_GENRE:
        json_set_table_value(jgenres, cat->name, json_new_bool(cat->enabled));
        return false;
    case ROMCAT_UNCATEGORIZED:
        json_set_table_value(jfixed, "uncategorized", json_new_bool(cat->enabled));
        return false;
    default:
        // not serialized in this way.
        return false;
    }

    return true;
}

int romcategories_write_all(RomCategory** cats)
{
    size_t n = len_nullterm((void const* const*)cats);

    json_value jcats;
    jcats.type = kJSONArray;
    jcats.data.arrayval = mallocz(sizeof(JsonArray) + n * sizeof(json_value));
    if (!jcats.data.arrayval)
        return -1;

    json_value j = json_new_table();
    if (j.type != kJSONTable)
    {
        free_json_data(jcats);
        return -4;
    }

    json_value jfixed = json_new_table();
    if (jfixed.type != kJSONTable)
    {
        free_json_data(jcats);
        free_json_data(j);
        return -4;
    }

    json_value jgenres = json_new_table();
    if (jgenres.type != kJSONTable)
    {
        free_json_data(jcats);
        free_json_data(j);
        free_json_data(jfixed);
        return -4;
    }

    JsonArray* arr = jcats.data.arrayval;
    for (RomCategory** cat = cats; cat && *cat; ++cat)
    {
        json_value jcat;
        if (romcat_to_json(*cat, &jcat, &jfixed, &jgenres))
        {
            arr->data[arr->n++] = jcat;
        }
    }

    // display order; packed excluded
    JsonArray* order_arr = mallocz(sizeof(JsonArray) + n * sizeof(json_value));
    if (!order_arr)
    {
        free_json_data(jcats);
        free_json_data(j);
        free_json_data(jfixed);
        free_json_data(jgenres);
        return -1;
    }
    order_arr->n = 0;
    for (RomCategory** cat = cats; cat && *cat; ++cat)
    {
        if ((*cat)->type == ROMCAT_ALL)
        {
            order_arr->data[order_arr->n++] = json_new_string(CATEGORY_ORDER_ALL_TOKEN);
        }
        else if ((*cat)->type == ROMCAT_UNCATEGORIZED)
        {
            order_arr->data[order_arr->n++] = json_new_string(CATEGORY_ORDER_UNCATEGORIZED_TOKEN);
        }
        else if ((*cat)->type == ROMCAT_STANDARD)
        {
            char* typed = aprintf(CATEGORY_ORDER_ID_PREFIX "%s", (*cat)->id);
            if (!typed)
                continue;
            order_arr->data[order_arr->n++] = json_new_string(typed);
            cb_free(typed);
        }
        else if ((*cat)->type == ROMCAT_GENRE)
        {
            char* typed = aprintf(CATEGORY_ORDER_GENRE_PREFIX "%s", (*cat)->name);
            if (!typed)
                continue;
            order_arr->data[order_arr->n++] = json_new_string(typed);
            cb_free(typed);
        }
    }

    json_value jorder = {.type = kJSONArray};
    jorder.data.arrayval = order_arr;
    json_set_table_value(&jfixed, "categories-order", jorder);

    json_set_table_value(&jfixed, "show-genres", json_new_bool(s_show_genres));
    json_set_table_value(&jfixed, "genres", jgenres);

    json_set_table_value(&j, "categories", jcats);

    // built-in categories, flags
    json_set_table_value(&j, "misc", jfixed);

    full_mkdir(MISC_PATH);
    int result = write_json_to_disk(CATEGORY_PATH, j);

    free_json_data(j);

    return result;
}

void romcategory_free(RomCategory* cat)
{
    if (!cat)
        return;
    cb_free(cat->icon_path);
    cb_free(cat->icon_slug);
    cb_free(cat);
}

void romcategories_free_all(RomCategory** cats)
{
    if (!cats)
        return;

    for (RomCategory** cat = cats; *cat; ++cat)
    {
        romcategory_free(*cat);
    }

    cb_free(cats);
}
