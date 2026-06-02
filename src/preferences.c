//
//  preferences.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "preferences.h"

#include "app.h"
#include "emucore_prefs.h"
#include "jparse.h"
#include "revcheck.h" 
#include "userstack.h"
#include "utility.h"

#include <string.h>

static const int pref_version = 1;

#define PREF(x, ...) preference_t preferences_##x;
#include "prefs.x"

void* preferences_bundle_default = NULL;
preferences_bitfield_t preferences_bundle_hidden = 0;
preferences_bitfield_t prefs_locked_by_script = 0;

static void cpu_endian_to_big_endian(
    unsigned char* src, unsigned char* buffer, size_t size, size_t len
);

static uint8_t preferences_read_uint8(SDFile* file);
static void preferences_write_uint8(SDFile* file, uint8_t value);
static uint32_t preferences_read_uint32(SDFile* file);
static void preferences_write_uint32(SDFile* file, uint32_t value);

int preference_default_value[PREFI_COUNT];

void preferences_set_defaults(void)
{
#define PREF(x, d) preferences_##x = d;
#include "prefs.x"

    // check bundle
    if (preferences_bundle_default)
        preferences_restore_subset(preferences_bundle_default);
}

void preferences_init(void)
{
    // if this fails, re-engineer this to be based on a struct instead of bitfield size
    CB_ASSERT(PREFI_COUNT <= 8 * sizeof(preferences_bitfield_t));

    cb_emucore_prefs_init();

    // set default values
    {
        int i = 0;
#define PREF(x, d) preference_default_value[i++] = d;
#include "prefs.x"
    }

    preferences_set_defaults();

    if (playdate->file->stat(CB_globalPrefsPath, NULL) != 0)
    {
        preferences_save_to_disk(CB_globalPrefsPath, 0);
    }
    else
    {
        preferences_read_from_disk(CB_globalPrefsPath);
        cb_emucore_prefs_read_from_disk(CB_globalPrefsPath, true);
    }

    // paranoia
    preferences_per_game = 0;
}

void preferences_merge_from_disk(const char* filename)
{
    json_value j;
    if (!parse_json(filename, &j, kFileReadData))
    {
        return;
    }

    if (j.type == kJSONTable)
    {
        JsonObject* obj = j.data.tableval;
        for (size_t i = 0; i < obj->n; ++i)
        {
#define PREF(x, ...)                                      \
    if (!strcmp(obj->data[i].key, #x))                    \
    {                                                     \
        preferences_##x = obj->data[i].value.data.intval; \
    };
#include "prefs.x"
        }
    }

    free_json_data(j);

    // migration: old saved blend_frames=2 (removed Auto) -> On (1)
    if (preferences_blend_frames > 1)
        preferences_blend_frames = 1;
}

void preferences_read_from_disk(const char* filename)
{
    preferences_set_defaults();
    preferences_merge_from_disk(filename);
}

int _preferences_save_to_disk(const char* filename, preferences_bitfield_t* leave_as_is)
{
    preferences_bitfield_t final_leave_as_is_mask = *leave_as_is | PREFBITS_TRANSIENT;

    playdate->system->logToConsole("Save preferences to %s...", filename);

    json_value root;
    if (!parse_json(filename, &root, kFileReadData) || root.type != kJSONTable)
    {
        if (root.type != kJSONNull) free_json_data(root);
        root = json_new_table();
        if (root.type != kJSONTable)
        {
            playdate->system->logToConsole("Save preferences: alloc failed");
            return 0;
        }
    }

    int i = 0;
#define PREF(x, ...)                                                    \
    if (!((final_leave_as_is_mask) & ((preferences_bitfield_t)1 << i))) \
        json_set_table_value(&root, #x, json_new_int(preferences_##x)); \
    ++i;
#include "prefs.x"

    int error = write_json_to_disk(filename, root);
    free_json_data(root);

    playdate->system->logToConsole("Save preferences status code %d", error);

    return !error;
}

int preferences_save_to_disk(const char* filename, preferences_bitfield_t leave_as_is)
{
    return (int)(intptr_t)call_with_main_stack_2(_preferences_save_to_disk, filename, &leave_as_is);
}

int prefvar_to_index(preference_t* pref)
{
#define PREF(a, b)                \
    if (&preferences_##a == pref) \
        return PREFI_##a;
#include "prefs.x"

    return -1;
}

static void cpu_endian_to_big_endian(
    unsigned char* src, unsigned char* buffer, size_t size, size_t len
)
{
    int x = 1;

    if (*((char*)&x) == 1)
    {
        // little endian machine, swap
        for (size_t i = 0; i < len; i++)
        {
            for (size_t ix = 0; ix < size; ix++)
            {
                buffer[size * i + ix] = src[size * i + (size - 1 - ix)];
            }
        }
    }
    else
    {
        memcpy(buffer, src, size * len);
    }
}

void* preferences_store_subset(preferences_bitfield_t subset)
{
    int count = 0;
    int i = 0;
#define PREF(x, ...)                               \
    if (subset & ((preferences_bitfield_t)1 << i)) \
    {                                              \
        count++;                                   \
    }                                              \
    ++i;
#include "prefs.x"

    void* data = cb_malloc(sizeof(preferences_bitfield_t) + sizeof(preference_t) * count);
    if (!data)
        return NULL;

    preferences_bitfield_t* dbits = data;
    *dbits = subset;
    preference_t* prefs = data + sizeof(preferences_bitfield_t);

    count = 0;
    i = 0;
#define PREF(x, ...)                      \
    if (subset & (1ll << i))              \
    {                                     \
        prefs[count++] = preferences_##x; \
    }                                     \
    ++i;
#include "prefs.x"

    return data;
}

void preferences_restore_subset(void* data)
{
    preferences_bitfield_t subset = *(preferences_bitfield_t*)data;
    preference_t* prefs = data + sizeof(preferences_bitfield_t);

    int count = 0;
    int i = 0;
#define PREF(x, ...)                               \
    if (subset & ((preferences_bitfield_t)1 << i)) \
    {                                              \
        preferences_##x = prefs[count++];          \
    }                                              \
    ++i;
#include "prefs.x"
}
