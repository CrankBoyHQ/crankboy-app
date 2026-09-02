#include "romcategory.h"
#include "pd_api/pd_api_file.h"
#include "pd_api/pd_api_json.h"
#include "utility.h"
#include "jparse.h"
#include "app.h"
#include "global.h"

static void romcategories_append(RomCategory***, RomCategory*);
static void romcategories_has_type(RomCategory***, enum RomCategoryType rt);

void for_rom_in_category(const RomCategory* cat, int (*cb)(CB_GameName*, void* ud), void* ud)
{
    switch(cat->type)
    {
    case ROMCAT_STANDARD:
        for (size_t i = 0; i < CB_App->gameNameCache->length; ++i)
        {
            if (cat->roms[i/8] & (1 << (i % 8)))
            {
                if (cb(CB_App->gameNameCache->items[i], ud) < 0) return;
            }
        }
        break;
    case ROMCAT_ALL:
        for (size_t i = 0; i < CB_App->gameNameCache->length; ++i)
        {
            if (cb(CB_App->gameNameCache->items[i], ud) < 0) return;
        }
        break;
    case ROMCAT_INCLUDED:
        // TODO -- only the packaged-with-crankboy included roms
    default:
        assert(false);
    };
}

// returns null-terminated list of categories.
RomCategory** romcategories_load_all(size_t* o_count)
{
    json_value j;
    RomCategory** cats;
    if (parse_json(CATEGORY_PATH, &j, kFileReadData | kFileRead))
    {
        // romcategories_append ROMCAT_STANDARD for each in "categories"
    }
    
    json_value jmisc = json_get_table_value(j, "misc");
    
    // TODO: set icon on fixed cats
    #define ROMCAT_FIXED_DEFAULT(E, key, NAME, icon, default) { \
        const bool enabled = (json_get_table_value(jmisc, key).type == kJsonTrue || (json_get_table_value(jmisc, key).type == kJsonNull && default)); \
        RomCategory* cat = allocz(RomCategory); \
        if (cat) { \
            cat->type = E; \
            cat->enabled = enabled; \
            cat->icon_slug = cb_strdup(icon); \
            memcpy(cat->name, NAME, strlen(NAME)+1); \
        } \
    }
    
    ROMCAT_FIXED_DEFAULT(ROMCAT_INCLUDED, "inc", "Included", "cat-inc", true);
    ROMCAT_FIXED_DEFAULT(ROMCAT_ALL, "all", "All", "cat-all", true);
}

static bool romcat_to_json(RomCategory* cat, json_value* j, json_value* jfixed)
{
    *j = json_new_table();
    
    switch(cat->type)
    {
    case ROMCAT_STANDARD:
        {
            json_set_table_value(j, "name", json_new_string(cat->name));
            if (cat->icon_slug)
                json_set_table_value(j, "icon-slug", json_new_string(cat->icon_slug));
            else if (cat->icon_path)
                json_set_table_value(j, "icon-path", json_new_string(cat->icon_path));
            // TODO: list roms by path under "roms" key.
            // for_rom_in_category(const RomCategory *, void (*cb)(CB_GameName *))
        }
        break;
        
    case ROMCAT_ALL:
        json_set_table_value(jfixed, "all", json_new_bool(cat->enabled));
        return false;
    case ROMCAT_INCLUDED:
        json_set_table_value(jfixed, "inc", json_new_bool(cat->enabled));
        return false;
    default:
        // not serialized in this way.
        return false;
    }
    
    return true;
}

int romcategories_write_all(RomCategory** cats)
{
    json_value jcats;
    jcats.type = kJSONArray;
    size_t n = len_nullterm((void const* const*)cats);
    jcats.data.arrayval = mallocz(sizeof(JsonArray) + n*sizeof(json_value));
    if (!jcats.data.arrayval) return -1;
    ((JsonArray*)(jcats.data.arrayval))->n = n;
    
    json_value j = json_new_table();
    if (j.type == kJSONNull)
    {
        free_json_data(jcats);
        return -4;
    }
    
    json_value jfixed = json_new_table();
    
    size_t i = 0;
    for (RomCategory** cat = cats; *cat; ++cat, ++i)
    {
        ((JsonArray*)(jcats.data.arrayval))->data[i] = romcat_to_json(*cat, &jfixed);
    }
    
    // user-defined
    json_set_table_value(&j, "categories", jcats);
    
    // built-in categories, flags
    json_set_table_value(&j, "misc", jfixed);

    write_json_to_disk(CATEGORY_PATH, j);
    
    free_json_data(j);
}

void romcategory_free(RomCategory* cat)
{
    cb_free(cat->icon_path);
    cb_free(cat->icon_slug);
    cb_free(cat);
}

void romcategories_free_all(RomCategory** cats)
{
    for (RomCategory** cat = cats; *cat; ++cat)
    {
        romcategory_free(*cat);
    }
    
    cb_free(cats);
}