#include "recommended_json.h"

#include "app.h"
#include "jparse.h"
#include "revcheck.h"
#include "script.h"
#include "utility.h"

#include <string.h>

typedef struct RecommendedJsonEntry
{
    char* rom_name;
    struct ScriptRecommendedSettings settings;
    struct ScriptRecommendedSetting* entries;
    struct RecommendedJsonEntry* next;
} RecommendedJsonEntry;

static RecommendedJsonEntry* registry = NULL;
static bool initialized = false;

static preferences_bitfield_t pref_bit_from_name(const char* name)
{
    int i = 0;
#define PREF(x, ...)                             \
    if (!strcmp(name, #x))                       \
        return ((preferences_bitfield_t)1 << i); \
    ++i;
#include "prefs.x"
    return 0;
}

static void parse_settings_table(
    json_value table, struct ScriptRecommendedSetting** out_entries, int* out_count,
    int* out_capacity
)
{
    if (table.type != kJSONTable)
        return;

    JsonObject* obj = table.data.tableval;
    for (size_t i = 0; i < obj->n; ++i)
    {
        const char* key = obj->data[i].key;
        preferences_bitfield_t bit = pref_bit_from_name(key);
        if (!bit)
        {
            playdate->system->logToConsole("recommended_json: unknown setting \"%s\"", key);
            continue;
        }

        int value = obj->data[i].value.data.intval;

        // Check if this pref already exists — if so, replace value
        int existing = -1;
        for (int j = 0; j < *out_count; ++j)
        {
            if ((*out_entries)[j].bit == bit)
            {
                existing = j;
                break;
            }
        }

        if (existing >= 0)
        {
            (*out_entries)[existing].value = value;
        }
        else
        {
            if (*out_count >= *out_capacity)
            {
                *out_capacity = *out_capacity ? *out_capacity * 2 : 8;
                *out_entries = cb_realloc(
                    *out_entries, sizeof(struct ScriptRecommendedSetting) * *out_capacity
                );
            }

            (*out_entries)[*out_count].bit = bit;
            (*out_entries)[*out_count].value = value;
            ++(*out_count);
        }
    }
}

static void parse_one_json(const char* path)
{
    json_value j;
    if (!parse_json(path, &j, kFileReadData))
    {
        playdate->system->logToConsole("recommended_json: failed to parse \"%s\"", path);
        return;
    }

    if (j.type != kJSONTable)
    {
        playdate->system->logToConsole("recommended_json: not a table \"%s\"", path);
        free_json_data(j);
        return;
    }

    JsonObject* obj = j.data.tableval;

    char* message = NULL;
    struct ScriptRecommendedSetting* entries = NULL;
    int count = 0;
    int capacity = 0;

    for (size_t i = 0; i < obj->n; ++i)
    {
        const char* key = obj->data[i].key;
        json_value val = obj->data[i].value;

        if (!strcmp(key, "message"))
        {
            if (val.type == kJSONString)
                message = cb_strdup(val.data.stringval);
        }
        else if (!strcmp(key, "settings"))
        {
            parse_settings_table(val, &entries, &count, &capacity);
        }
        else if (!strcmp(key, "settings_A") && pd_rev == PD_REV_A)
        {
            parse_settings_table(val, &entries, &count, &capacity);
        }
        else if (!strcmp(key, "settings_B") && pd_rev == PD_REV_B)
        {
            parse_settings_table(val, &entries, &count, &capacity);
        }
    }

    free_json_data(j);

    if (count == 0)
    {
        playdate->system->logToConsole("recommended_json: no valid settings in \"%s\"", path);
        cb_free(message);
        cb_free(entries);
        return;
    }

    // Extract ROM name from filename (strip ".json" extension)
    const char* basename = strrchr(path, '/');
    if (basename)
        basename++;
    else
        basename = path;

    size_t name_len = strlen(basename);
    if (name_len <= 5)
    {
        playdate->system->logToConsole("recommended_json: filename too short \"%s\"", path);
        cb_free(message);
        cb_free(entries);
        return;
    }

    char* rom_name = cb_malloc(name_len - 4);  // - ".json"
    memcpy(rom_name, basename, name_len - 5);
    rom_name[name_len - 5] = 0;

    RecommendedJsonEntry* entry = cb_malloc(sizeof(RecommendedJsonEntry));
    entry->rom_name = rom_name;
    entry->entries = entries;
    entry->settings.message = message;
    entry->settings.entries = entries;
    entry->settings.count = count;
    entry->next = registry;
    registry = entry;
}

static void collect_json_callback(const char* filename, void* ud)
{
    (void)ud;

    size_t len = strlen(filename);
    if (len < 6 || strcasecmp(filename + len - 5, ".json"))
        return;

    const char* rec_path = cb_gb_directory_path(CB_recommendedPath);
    char* full_path;
    playdate->system->formatString(&full_path, "%s/%s", rec_path, filename);

    parse_one_json(full_path);

    cb_free(full_path);
}

void recommended_json_init(void)
{
    const char* rec_path = cb_gb_directory_path(CB_recommendedPath);

    playdate->file->listfiles(rec_path, collect_json_callback, NULL, 0);
}

const struct ScriptRecommendedSettings* recommended_json_lookup(const char* rom_name)
{
    if (!initialized)
    {
        recommended_json_init();
        initialized = true;
    }

    if (!rom_name || !rom_name[0])
        return NULL;

    // Exact match first
    for (RecommendedJsonEntry* e = registry; e; e = e->next)
    {
        if (!strcmp(e->rom_name, rom_name))
            return &e->settings;
    }

    // Prefix match — find the longest matching prefix
    RecommendedJsonEntry* best = NULL;
    size_t best_len = 0;
    size_t rom_len = strlen(rom_name);

    for (RecommendedJsonEntry* e = registry; e; e = e->next)
    {
        size_t e_len = strlen(e->rom_name);
        if (e_len > best_len && e_len <= rom_len && !strncmp(e->rom_name, rom_name, e_len))
        {
            best = e;
            best_len = e_len;
        }
    }

    return best ? &best->settings : NULL;
}

void recommended_json_quit(void)
{
    while (registry)
    {
        RecommendedJsonEntry* e = registry;
        registry = e->next;
        cb_free(e->rom_name);
        cb_free(e->settings.message);
        cb_free(e->entries);
        cb_free(e);
    }
}
