//
//  preferences.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 18/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "preferences.h"

#include "app.h"
#include "jparse.h"
#include "revcheck.h"

static const int pref_version = 1;

#define PREF(x, ...) preference_t preferences_##x;
#include "prefs.x"

#define PREF(x, ...) 1 +
const int pref_count =
#include "prefs.x"
    0;

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

static void preferences_set_defaults(void)
{
#define PREF(x, d) preferences_##x = d;
#include "prefs.x"

    // check bundle
    if (preferences_bundle_default)
        preferences_restore_subset(preferences_bundle_default);
}

void preferences_init(void)
{
    // if this fails, increase bitfield to uint64_t
    PGB_ASSERT(pref_count <= 8 * sizeof(preferences_bitfield_t));

    preferences_set_defaults();

    if (playdate->file->stat(PGB_globalPrefsPath, NULL) != 0)
    {
        preferences_save_to_disk(PGB_globalPrefsPath, 0);
    }
    else
    {
        preferences_read_from_disk(PGB_globalPrefsPath);
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
}

void preferences_read_from_disk(const char* filename)
{
    preferences_set_defaults();
    preferences_merge_from_disk(filename);
}

int preferences_save_to_disk(const char* filename, preferences_bitfield_t leave_as_is)
{
    playdate->system->logToConsole("Save preferences to %s...", filename);

    void* preserved_all = preferences_store_subset(-1);
    void* preserved_to_write = preferences_store_subset(~leave_as_is);

    if (leave_as_is != 0 && preserved_to_write)
    {
        preferences_merge_from_disk(filename);
        preferences_restore_subset(preserved_to_write);
    }

    if (preserved_to_write)
        pgb_free(preserved_to_write);

    union
    {
        JsonObject obj;
        volatile char _[sizeof(JsonObject) + sizeof(TableKeyPair) * pref_count];
    } data;
    json_value j;
    j.type = kJSONTable;
    j.data.tableval = &data.obj;
    data.obj.n = pref_count;

    TableKeyPair* pairs = (TableKeyPair*)(&data.obj + 1);

    int i = 0;
#define PREF(x, ...)                              \
    pairs[i].key = #x;                            \
    pairs[i].value.type = kJSONInteger;           \
    pairs[i].value.data.intval = preferences_##x; \
    ++i;
#include "prefs.x"

    if (preserved_all)
    {
        preferences_restore_subset(preserved_all);
        pgb_free(preserved_all);
    }

    int error = write_json_to_disk(filename, j);

    playdate->system->logToConsole("Save preferences status code %d", error);

    return !error;
}

static uint8_t preferences_read_uint8(SDFile* file)
{
    uint8_t buffer[1];
    playdate->file->read(file, buffer, sizeof(uint8_t));
    return buffer[0];
}

static void preferences_write_uint8(SDFile* file, uint8_t value)
{
    playdate->file->write(file, &value, sizeof(uint8_t));
}

static uint32_t preferences_read_uint32(SDFile* file)
{
    unsigned char buffer[sizeof(uint32_t)];
    playdate->file->read(file, buffer, sizeof(uint32_t));
    return buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
}

static void preferences_write_uint32(SDFile* file, uint32_t value)
{
    unsigned char buffer[sizeof(uint32_t)];
    cpu_endian_to_big_endian((unsigned char*)&value, buffer, sizeof(uint32_t), 1);
    playdate->file->write(file, buffer, sizeof(uint32_t));
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
#define PREF(x, ...)       \
    if (subset & (1 << i)) \
    {                      \
        count++;           \
    }                      \
    ++i;
#include "prefs.x"

    void* data = pgb_malloc(sizeof(preferences_bitfield_t) + sizeof(preference_t) * count);
    if (!data)
        return NULL;

    preferences_bitfield_t* dbits = data;
    *dbits = subset;
    preference_t* prefs = data + sizeof(preferences_bitfield_t);

    count = 0;
    i = 0;
#define PREF(x, ...)                      \
    if (subset & (1 << i))                \
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
#define PREF(x, ...)                      \
    if (subset & (1 << i))                \
    {                                     \
        preferences_##x = prefs[count++]; \
    }                                     \
    ++i;
#include "prefs.x"
}
