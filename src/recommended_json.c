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

static RecommendedJsonEntry* data_registry = NULL;
static RecommendedJsonEntry* bundle_registry = NULL;
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

        json_value val = obj->data[i].value;
        if (val.type != kJSONInteger)
        {
            playdate->system->logToConsole(
                "recommended_json: setting \"%s\" value is not an integer, skipping", key
            );
            continue;
        }

        int value = val.data.intval;

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

static void parse_one_json(const char* path_arg, FileOptions fopts, RecommendedJsonEntry** registry)
{
    // Strip trailing .gz — parse_json auto-detects .json.gz from .json paths
    size_t plen = strlen(path_arg);
    char json_path_buf[256];
    const char* path;
    if (plen > 3 && !strcasecmp(path_arg + plen - 3, ".gz"))
    {
        memcpy(json_path_buf, path_arg, plen - 3);
        json_path_buf[plen - 3] = '\0';
        path = json_path_buf;
    }
    else
    {
        path = path_arg;
    }

    json_value j;
    if (!parse_json(path, &j, fopts))
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

    entries = cb_realloc(entries, sizeof(struct ScriptRecommendedSetting) * (count + 1));
    entries[count] = (struct ScriptRecommendedSetting)RECOMMENDED_SETTINGS_END;

    const char* basename = strrchr(path, '/');
    if (basename)
        basename++;
    else
        basename = path;

    size_t name_len = strlen(basename);
    size_t json_ext_len = 5;  // strlen(".json")
    if (name_len <= json_ext_len)
    {
        playdate->system->logToConsole("recommended_json: filename too short \"%s\"", path);
        cb_free(message);
        cb_free(entries);
        return;
    }

    char* rom_name = cb_malloc(name_len - json_ext_len + 1);
    memcpy(rom_name, basename, name_len - json_ext_len);
    rom_name[name_len - json_ext_len] = 0;

    RecommendedJsonEntry* entry = cb_malloc(sizeof(RecommendedJsonEntry));
    entry->rom_name = rom_name;
    entry->entries = entries;
    entry->settings.message = message;
    entry->settings.settings = entries;
    entry->next = *registry;
    *registry = entry;
}

static void collect_data_json_callback(const char* filename, void* ud)
{
    (void)ud;

    size_t len = strlen(filename);
    if (len < 6 || strcasecmp(filename + len - 5, ".json"))
        return;

    const char* rec_path = cb_gb_directory_path(CB_customSettingsPath);
    char* full_path;
    playdate->system->formatString(&full_path, "%s/%s", rec_path, filename);

    parse_one_json(full_path, kFileReadData, &data_registry);

    cb_free(full_path);
}

static void collect_bundle_json_callback(const char* filename, void* ud)
{
    (void)ud;

    size_t len = strlen(filename);

    bool is_json = (len > 5 && !strcasecmp(filename + len - 5, ".json"));
    bool is_json_gz = (len > 8 && !strcasecmp(filename + len - 8, ".json.gz"));
    if (!is_json && !is_json_gz)
        return;

    char* full_path;
    playdate->system->formatString(&full_path, "csettings/%s", filename);

    parse_one_json(full_path, kFileRead, &bundle_registry);

    cb_free(full_path);
}

static const struct ScriptRecommendedSettings* search_registry(
    RecommendedJsonEntry* registry, const char* rom_name
)
{
    if (!rom_name || !rom_name[0] || !registry)
        return NULL;

    // Exact match — highest priority
    for (RecommendedJsonEntry* e = registry; e; e = e->next)
    {
        if (!strcmp(e->rom_name, rom_name))
            return &e->settings;
    }

    // Wildcard match — lower priority
    for (RecommendedJsonEntry* e = registry; e; e = e->next)
    {
        if (wildcard_match(e->rom_name, rom_name))
            return &e->settings;
    }

    return NULL;
}

void recommended_json_init(void)
{
    // Low priority: shipped settings from bundle
    playdate->file->listfiles("csettings", collect_bundle_json_callback, NULL, 0);

    // High priority: user-added settings from data dir
    const char* rec_path = cb_gb_directory_path(CB_customSettingsPath);
    playdate->file->listfiles(rec_path, collect_data_json_callback, NULL, 0);
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

    // Highest priority: user data dir
    const struct ScriptRecommendedSettings* rec = search_registry(data_registry, rom_name);
    if (rec)
        return rec;

    // Lower priority: shipped bundle
    return search_registry(bundle_registry, rom_name);
}

static void free_registry(RecommendedJsonEntry* registry)
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

void recommended_json_quit(void)
{
    free_registry(data_registry);
    data_registry = NULL;
    free_registry(bundle_registry);
    bundle_registry = NULL;
    initialized = false;
}
