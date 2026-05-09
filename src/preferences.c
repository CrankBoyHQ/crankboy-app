//
//  preferences.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "preferences.h"

#include "app.h"  // IWYU pragma: keep
#include "jparse.h"
#include "revcheck.h"  // IWYU pragma: keep
#include "userstack.h"

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
    }
    
    // dither pattern default non-determinism
    preference_default_value[PREFI_dither_pattern] = preferences_dither_pattern % 2;

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
}

void preferences_read_from_disk(const char* filename)
{
    preferences_set_defaults();
    preferences_merge_from_disk(filename);
}

int _preferences_save_to_disk(const char* filename, preferences_bitfield_t* leave_as_is)
{
    // This ensures transient settings are NEVER saved to disk automatically.
    preferences_bitfield_t final_leave_as_is_mask = *leave_as_is | PREFBITS_TRANSIENT;

    playdate->system->logToConsole("Save preferences to %s...", filename);

    void* preserved_all = preferences_store_subset(-1);
    void* preserved_to_write = preferences_store_subset(~final_leave_as_is_mask);

    if (final_leave_as_is_mask != 0 && preserved_to_write)
    {
        preferences_merge_from_disk(filename);
        preferences_restore_subset(preserved_to_write);
    }

    if (preserved_to_write)
        cb_free(preserved_to_write);

    union
    {
        JsonObject obj;
        volatile char _[sizeof(JsonObject) + sizeof(TableKeyPair) * PREFI_COUNT];
    } data;
    json_value j;
    j.type = kJSONTable;
    j.data.tableval = &data.obj;

    TableKeyPair* pairs = (TableKeyPair*)(&data.obj + 1);

    int i = 0;
    int pairs_count = 0;

#define PREF(x, ...)                                                    \
    if (!((final_leave_as_is_mask) & ((preferences_bitfield_t)1 << i))) \
    {                                                                   \
        pairs[pairs_count].key = #x;                                    \
        pairs[pairs_count].value.type = kJSONInteger;                   \
        pairs[pairs_count].value.data.intval = preferences_##x;         \
        ++pairs_count;                                                  \
    }                                                                   \
    ++i;
#include "prefs.x"

    data.obj.n = pairs_count;

    if (preserved_all)
    {
        preferences_restore_subset(preserved_all);
        cb_free(preserved_all);
    }

    int error = write_json_to_disk(filename, j);

    playdate->system->logToConsole("Save preferences status code %d", error);

    return !error;
}

int preferences_save_to_disk(const char* filename, preferences_bitfield_t leave_as_is)
{
    return (int)(intptr_t)call_with_main_stack_2(_preferences_save_to_disk, filename, &leave_as_is);
}

int prefvar_to_index(preference_t* pref)
{
    #define PREF(a, b) if (&preferences_##a == pref) return PREFI_##a;
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
