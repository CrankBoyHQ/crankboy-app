#include "romcategory.h"

#include "app.h"
#include "global.h"
#include "jparse.h"
#include "utility.h"

static void romcategories_append(RomCategory***, RomCategory*);
static bool romcategories_has_type(RomCategory* const*, enum RomCategoryType rt);

// bytes needed for roms[]
static size_t romcategory_bitc(void);

static RomCategory* romcategory_new(enum RomCategoryType type, const char* name);

static size_t romcategory_bitc(void)
{
    size_t n = (CB_App && CB_App->gameNameCache) ? CB_App->gameNameCache->length : 0;
    return (n + 7) / 8;
}

static RomCategory* romcategory_new(enum RomCategoryType type, const char* name)
{
    RomCategory* cat = mallocz(sizeof(RomCategory) + romcategory_bitc());
    if (!cat)
        return NULL;

    cat->type = type;
    cat->enabled = true;
    if (name)
        snprintf(cat->name, sizeof(cat->name), "%s", name);
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
    default:
        CB_ASSERT(false);
        return false;
    }
}

void romcategory_put(RomCategory* cat, size_t index, bool contains)
{
    if (!cat || index >= (size_t)CB_App->gameNameCache->length)
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

    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    for (size_t i = 0; i < CB_App->gameNameCache->length; ++i)
    {
        const CB_GameName* name = CB_App->gameNameCache->items[i];
        if (name->filename && !strcmp(name->filename, base))
            return (ssize_t)i;
    }

    return -1;
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

// returns null-terminated list of categories.
RomCategory** romcategories_load_all(size_t* o_count)
{
    json_value j;
    RomCategory** cats = NULL;

    if (!parse_json(CATEGORY_PATH, &j, kFileReadData | kFileRead))
        j.type = kJSONNull;

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

    json_value jmisc = json_get_table_value(j, "misc");

// TODO: set icon on fixed cats
#define ROMCAT_FIXED_DEFAULT(E, key, NAME, icon, default, CATALOG)                          \
    if (!romcategories_has_type(cats, E))                                                   \
    {                                                                                       \
        const json_value jv = json_get_table_value(jmisc, key);                             \
        const bool enabled = (jv.type == kJSONTrue || (jv.type == kJSONNull && (default))); \
        RomCategory* cat = romcategory_new(E, NAME);                                        \
        if (cat)                                                                            \
        {                                                                                   \
            cat->enabled = enabled;                                                         \
            cat->icon_slug = cb_strdup(icon);                                               \
            romcategories_append(&cats, cat);                                               \
            cat->requires_catalog = CATALOG;                                                \
        }                                                                                   \
    }

    ROMCAT_FIXED_DEFAULT(ROMCAT_PACKED, "packed", "Included", "cat-packed", true, true);
    ROMCAT_FIXED_DEFAULT(ROMCAT_ALL, "all", "All", "cat-all", true, false);

#undef ROMCAT_FIXED_DEFAULT

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

static void romcategories_append(RomCategory*** cats, RomCategory* cat)
{
    size_t n = len_nullterm((void const* const*)*cats);
    RomCategory** grown = cb_realloc(*cats, sizeof(RomCategory*) * (n + 2));
    if (!grown)
    {
        romcategory_free(cat);
        return;
    }

    grown[n] = cat;
    grown[n + 1] = NULL;
    *cats = grown;
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
    arr = cb_realloc(arr, sizeof(JsonArray) + (n + 1) * sizeof(json_value));
    if (!arr)
        return -1;

    arr->data[n] = json_new_string(path);
    arr->n = n + 1;
    *roms = arr;
    return 0;
}

static bool romcat_to_json(RomCategory* cat, json_value* j, json_value* jfixed)
{
    switch (cat->type)
    {
    case ROMCAT_STANDARD:
    {
        *j = json_new_table();
        if (j->type != kJSONTable)
            return false;

        json_set_table_value(j, "name", json_new_string(cat->name));
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

    JsonArray* arr = jcats.data.arrayval;
    for (RomCategory** cat = cats; cat && *cat; ++cat)
    {
        json_value jcat;
        if (romcat_to_json(*cat, &jcat, &jfixed))
        {
            arr->data[arr->n++] = jcat;
        }
    }

    // user-defined
    json_set_table_value(&j, "categories", jcats);

    // built-in categories, flags
    json_set_table_value(&j, "misc", jfixed);

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
