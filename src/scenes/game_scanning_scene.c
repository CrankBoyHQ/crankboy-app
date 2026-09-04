// game_scanning_scene.c
#include "game_scanning_scene.h"

#include "../../libs/pdll/pdll.h"
#include "../app.h"
#include "../jparse.h"
#include "../preferences.h"
#include "../script.h"
#include "../utility.h"
#include "image_conversion_scene.h"

#include <string.h>

#define SCAN_BATCH_SIZE 20

typedef struct CB_LibraryScene
{
    CB_Scene* scene;
} CB_LibraryScene;
CB_LibraryScene* CB_LibraryScene_new(void);

struct ScriptInfo;

void script_info_free(struct ScriptInfo* info);
void CB_GameScanningScene_update(void* object, uint32_t u32enc_dt);
void CB_GameScanningScene_free(void* object);

void collect_game_filenames_callback(const char* filename, void* userdata);

typedef struct
{
    uint32_t crc;
    uint32_t size;
    uint32_t m_time;  // for staleness
    enum cgb_support_e sys;
    bool battery;  // "sram"
    char name_header[17];
} CB_RomCacheEntry;

// caller-freed
static char* cache_key(const char* slug, const char* filename)
{
    return aprintf("%s:%s", slug, filename);
}

// stat path and compute its mtime
static bool stat_rom(const char* fullpath, FileStat* o_stat, uint32_t* o_mtime, FileOptions fopts)
{
    if (playdate->file->stat(fullpath, o_stat) == 0)
    {
        struct PDDateTime dt = {
            .year = o_stat->m_year,
            .month = o_stat->m_month,
            .day = o_stat->m_day,
            .hour = o_stat->m_hour,
            .minute = o_stat->m_minute,
            .second = o_stat->m_second
        };
        *o_mtime = playdate->system->convertDateTimeToEpoch(&dt);
        return true;
    }

    SDFile* f = playdate->file->open(fullpath, fopts);
    if (!f)
        return false;

    int size = playdate->file->seek(f, 0, SEEK_END);
    playdate->file->close(f);

    memset(o_stat, 0, sizeof(*o_stat));
    o_stat->size = size > 0 ? size : 0;
    *o_mtime = 0;
    return true;
}

static bool cache_lookup(
    CB_GameScanningScene* scanScene, const char* key, uint32_t size, uint32_t m_time,
    CB_RomCacheEntry* out
)
{
    if (scanScene->crc_cache.type != kJSONTable)
        return false;

    JsonObject* obj = scanScene->crc_cache.data.tableval;
    TableKeyPair key_to_find = {.key = (char*)key};
    TableKeyPair* found = (TableKeyPair*)bsearch(
        &key_to_find, obj->data, obj->n, sizeof(TableKeyPair), compare_key_pairs
    );
    if (!found || found->value.type != kJSONTable)
        return false;

    json_value e = found->value;
    json_value crc = json_get_table_value(e, "crc32");
    json_value sz = json_get_table_value(e, "size");
    json_value mt = json_get_table_value(e, "m_time");
    json_value sram = json_get_table_value(e, "sram");
    json_value sys = json_get_table_value(e, "sys");
    json_value hdr = json_get_table_value(e, "name_header");

    if (crc.type != kJSONInteger || sz.type != kJSONInteger || mt.type != kJSONInteger ||
        sram.type != kJSONInteger || sys.type != kJSONInteger || hdr.type != kJSONString)
        return false;
    if ((uint32_t)sz.data.intval != size || (uint32_t)mt.data.intval != m_time)
        return false;

    out->crc = (uint32_t)crc.data.intval;
    out->size = size;
    out->m_time = m_time;
    out->battery = sram.data.intval;
    out->sys = sys.data.intval;
    strncpy(out->name_header, hdr.data.stringval, sizeof(out->name_header) - 1);
    out->name_header[sizeof(out->name_header) - 1] = 0;
    return true;
}

// queue a freshly-computed entry for `key` to be merged into crc_cache.json.
static void cache_store(CB_GameScanningScene* scanScene, const char* key, const CB_RomCacheEntry* e)
{
    json_value entry = {.type = kJSONTable, .data.tableval = cb_calloc(1, sizeof(JsonObject))};
    json_set_table_value(&entry, "crc32", json_new_int((int)e->crc));
    json_set_table_value(&entry, "size", json_new_int((int)e->size));
    json_set_table_value(&entry, "m_time", json_new_int((int)e->m_time));
    json_set_table_value(&entry, "sram", json_new_int(e->battery));
    json_set_table_value(&entry, "sys", json_new_int(e->sys));
    json_set_table_value(&entry, "name_header", json_new_string(e->name_header));

    json_value* file_entry = cb_calloc(1, sizeof(json_value));
    file_entry->type = kJSONTable;
    file_entry->data.tableval = cb_calloc(1, sizeof(JsonObject));
    json_set_table_value(file_entry, key, entry);

    array_push(scanScene->new_cache_entries, file_entry);
    scanScene->crc_cache_modified = true;
}

static bool games_dir_is_packed(const char* games_dir)
{
    return strcmp(games_dir, "packed") == 0;
}

static void fill_basic_names(CB_GameName* newName, const char* filename, const char* slug)
{
    newName->filename = cb_strdup(filename);
    newName->name_filename = cb_basename(filename, true);
    newName->name_filename_leading_article = common_article_form(newName->name_filename);
    newName->system_slug = cb_strdup(slug);
}

static void process_one_game(
    CB_GameScanningScene* scanScene, const char* filename, const char* games_dir
)
{
    CB_GameName* newName = allocz(CB_GameName);
    fill_basic_names(newName, filename, GB_SYSTEM_SLUG);
    newName->packed = games_dir_is_packed(games_dir);

    char* fullpath;
    playdate->system->formatString(&fullpath, "%s/%s", games_dir, filename);
    newName->fullpath = fullpath;

    FileStat stat;
    uint32_t m_time;
    if (!stat_rom(fullpath, &stat, &m_time, kFileReadDataOrPacked))
    {
        playdate->system->logToConsole("Failed to stat file: %s", fullpath);
        free_game_names(newName);
        cb_free(newName);
        return;
    }

    char* key = cache_key(GB_SYSTEM_SLUG, filename);
    CB_RomCacheEntry entry = {0};
    bool ok = cache_lookup(scanScene, key, stat.size, m_time, &entry);

    if (!ok)
    {
        int is_gbz = 0;
        uint32_t gbz_checksum = 0;
        enum cgb_support_e cgb = 0;
        unsigned battery = false;
        struct ScriptInfo* info = script_get_info_by_rom_path_and_get_header_info(
            fullpath, entry.name_header, &cgb, &battery, &is_gbz, &gbz_checksum
        );
        if (info)
            script_info_free(info);
        for (int i = strlen(entry.name_header) - 1; i >= 0; --i)
        {
            if (entry.name_header[i] == ' ')
                entry.name_header[i] = 0;
            else
                break;
        }

        uint32_t crc = 0;
        if (is_gbz)
        {
            crc = gbz_checksum;
            ok = true;
        }
        else if (cb_calculate_crc32(fullpath, kFileReadDataOrPacked, &crc))
        {
            ok = true;
        }

        if (ok)
        {
            entry.crc = crc;
            entry.size = stat.size;
            entry.m_time = m_time;
            entry.sys = cgb;
            entry.battery = battery;
            cache_store(scanScene, key, &entry);
        }
    }

    cb_free(key);

    if (!ok)  // couldn't open/checksum the ROM; drop it
    {
        free_game_names(newName);
        cb_free(newName);
        return;
    }

    newName->name_header = cb_strdup(entry.name_header);
    newName->crc32 = entry.crc;
    newName->rom_cgb_support = entry.sys;
    newName->rom_has_battery = entry.battery;

    CB_FetchedNames fetched = cb_get_titles_from_db_by_crc(entry.crc);
    newName->name_database = fetched.detailed_name ? cb_strdup(fetched.detailed_name) : NULL;
    newName->name_short =
        fetched.short_name ? cb_strdup(fetched.short_name) : cb_strdup(newName->name_filename);
    newName->name_detailed = fetched.detailed_name ? cb_strdup(fetched.detailed_name)
                                                   : cb_strdup(newName->name_filename);
    newName->name_short_leading_article = common_article_form(newName->name_short);
    newName->name_detailed_leading_article = common_article_form(newName->name_detailed);
    if (fetched.short_name)
        cb_free(fetched.short_name);
    if (fetched.detailed_name)
        cb_free(fetched.detailed_name);

    array_push(CB_App->gameNameCache, newName);
}

static void process_one_emucore_game(
    CB_GameScanningScene* scanScene, const char* filename, const char* games_dir, const char* slug
)
{
    char* fullpath = aprintf("%s/%s", games_dir, filename);

    FileStat stat;
    uint32_t m_time;
    if (!stat_rom(fullpath, &stat, &m_time, kFileReadDataOrPacked))
    {
        cb_free(fullpath);
        return;
    }

    char* key = cache_key(slug, filename);
    CB_RomCacheEntry entry = {0};
    bool ok = cache_lookup(scanScene, key, stat.size, m_time, &entry);

    if (!ok)
    {
        size_t rom_size = 0;
        uint8_t* rom =
            (uint8_t*)cb_read_entire_file(fullpath, &rom_size, kFileReadData | kFileRead);
        if (rom)
        {
            entry.crc = crc32_for_buffer(rom, rom_size);
            entry.size = stat.size;
            entry.m_time = m_time;
            entry.sys = NON_GB_SYSTEM;
            entry.battery =
                scanScene->save_size_fn ? (scanScene->save_size_fn(rom, rom_size) > 0) : false;
            cb_free(rom);
            cache_store(scanScene, key, &entry);
            ok = true;
        }
    }

    cb_free(key);
    cb_free(fullpath);

    if (!ok)  // unreadable, or a subdirectory
        return;

    CB_GameName* newName = allocz(CB_GameName);
    fill_basic_names(newName, filename, slug);
    newName->packed = games_dir_is_packed(games_dir);

    newName->crc32 = entry.crc;
    newName->rom_cgb_support = NON_GB_SYSTEM;
    newName->rom_has_battery = entry.battery;
    newName->name_header = cb_strdup("");

    newName->name_short = cb_strdup(newName->name_filename);
    newName->name_detailed = cb_strdup(newName->name_filename);
    newName->name_short_leading_article = common_article_form(newName->name_short);
    newName->name_detailed_leading_article = common_article_form(newName->name_detailed);

    array_push(CB_App->gameNameCache, newName);
}

static void collect_all_filenames_callback(const char* filename, void* userdata)
{
    array_push((CB_Array*)userdata, cb_strdup(filename));
}

static void clear_game_filenames(CB_GameScanningScene* scanScene)
{
    for (unsigned int i = 0; i < scanScene->game_filenames->length; ++i)
        cb_free(scanScene->game_filenames->items[i]);
    array_clear(scanScene->game_filenames);
}

static CB_ScanSource* scan_source_new(char* games_dir, const char* slug, int emucore_index)
{
    CB_ScanSource* src = allocz(CB_ScanSource);
    src->games_dir = cb_strdup(games_dir);
    src->slug = cb_strdup(slug);
    src->emucore_index = emucore_index;
    return src;
}

static void scan_source_free(CB_ScanSource* src)
{
    cb_free(src->games_dir);
    cb_free(src->slug);
    cb_free(src);
}

#ifdef CRANKBOY_OFFICIAL_CATALOG
static void collect_game_filenames_skip_packed_callback(const char* filename, void* userdata)
{
    char* packed_path = aprintf("packed/%s", filename);
    bool in_packed = cb_file_exists(packed_path, kFileRead);
    cb_free(packed_path);
    if (!in_packed)
    {
        collect_game_filenames_callback(filename, userdata);
    }
}
#endif

static void build_scan_sources(CB_GameScanningScene* scanScene)
{
#ifdef CRANKBOY_OFFICIAL_CATALOG
    if (preferences_show_bundled_games)
        array_push(scanScene->sources, scan_source_new("packed", GB_SYSTEM_SLUG, -1));
#endif

    char* gb_dir = cb_system_directory_path_for_slug(GB_SYSTEM_SLUG, CB_gamesPath);
    array_push(scanScene->sources, scan_source_new(gb_dir, GB_SYSTEM_SLUG, -1));
    cb_free(gb_dir);

    for (size_t i = 0; i < CB_App->cores_n; ++i)
    {
        emucore_t* emucore = &CB_App->cores[i];
        for (size_t j = 0; j < emucore->n_system_slugs; ++j)
        {
            const char* slug = emucore->system_slugs[j];
            char* dir = cb_system_directory_path_for_slug(slug, CB_gamesPath);
            array_push(scanScene->sources, scan_source_new(dir, slug, (int)i));
            cb_free(dir);
        }
    }
}

static void open_emucore_for_source(CB_GameScanningScene* scanScene, const CB_ScanSource* src)
{
    scanScene->emucore_pdll = NULL;
    scanScene->save_size_fn = NULL;
    cb_free(scanScene->progress_title);
    scanScene->progress_title = NULL;

    const char* system_name = NULL;
    if (src->emucore_index >= 0)
    {
        emucore_t* emucore = &CB_App->cores[src->emucore_index];
        pdll_t* pdll = pdll_open(playdate, emucore->path, PDLL_FILE_PDX | PDLL_FILE_DATA, 2);
        if (pdll)
        {
            scanScene->emucore_pdll = pdll;
            cb_emucore_set_frontend(pdll);
            scanScene->save_size_fn =
                (ce_rom_save_size_fn)pdll_symbol(pdll, "ce_get_rom_save_size");

            const char* (*name_from_slug)(const char*) =
                pdll_symbol(pdll, "ce_get_system_name_from_slug");
            if (name_from_slug)
                system_name = name_from_slug(src->slug);
        }
        else
        {
            playdate->system->logToConsole(
                "game-scanning: could not open emucore '%s': %s", emucore->path, pdll_get_error()
            );
        }
    }

    // " (<system name>)" if derivable, otherwise " (<system slug>)".
    scanScene->progress_title =
        aprintf(T(status_scanning_fmt), system_name ? system_name : src->slug);
}

static void close_emucore_for_source(CB_GameScanningScene* scanScene)
{
    if (scanScene->emucore_pdll)
    {
        pdll_close((pdll_t*)scanScene->emucore_pdll);
        scanScene->emucore_pdll = NULL;
    }
    scanScene->save_size_fn = NULL;
    cb_free(scanScene->progress_title);
    scanScene->progress_title = NULL;
}

static void checkForPngCallback(const char* filename, void* userdata)
{
    if (filename_has_stbi_extension(filename))
    {
        *(bool*)userdata = true;
    }
}

void CB_GameScanningScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    CB_GameScanningScene* scanScene = object;

    switch (scanScene->state)
    {
    case kScanningStateInit:
    {
        cb_clear_title_db_cache();
        build_scan_sources(scanScene);
        scanScene->source_index = 0;
        scanScene->state = kScanningStateListSource;
        break;
    }

    case kScanningStateListSource:
    {
        if (scanScene->source_index >= (int)scanScene->sources->length)
        {
            scanScene->state = kScanningStateDone;
            break;
        }

        CB_ScanSource* src = scanScene->sources->items[scanScene->source_index];

        if (src->emucore_index < 0)
        {
            // .gb/.gbc/.gbz
#ifdef CRANKBOY_OFFICIAL_CATALOG
            bool is_packed = games_dir_is_packed(src->games_dir);
            playdate->file->listfiles(
                src->games_dir,
                is_packed ? collect_game_filenames_callback
                          : collect_game_filenames_skip_packed_callback,
                scanScene->game_filenames, 0
            );
            if (is_packed)
            {
                if (CB_App->packed_filenames)
                {
                    for (int i = 0; i < CB_App->packed_filenames->length; i++)
                        cb_free(CB_App->packed_filenames->items[i]);
                    array_clear(CB_App->packed_filenames);
                }
                else
                {
                    CB_App->packed_filenames = array_new();
                }
                for (int i = 0; i < scanScene->game_filenames->length; i++)
                    array_push(
                        CB_App->packed_filenames, cb_strdup(scanScene->game_filenames->items[i])
                    );
                if (CB_App->packed_filenames->length > 1)
                {
                    qsort(
                        CB_App->packed_filenames->items, CB_App->packed_filenames->length,
                        sizeof(char*), cb_compare_strings
                    );
                }
            }
#else
            playdate->file->listfiles(
                src->games_dir, collect_game_filenames_callback, scanScene->game_filenames, 0
            );
#endif
        }
        else
        {
            // emucore plugins
            open_emucore_for_source(scanScene, src);
            playdate->file->listfiles(
                src->games_dir, collect_all_filenames_callback, scanScene->game_filenames, 0
            );
        }

        (void)array_reserve(
            CB_App->gameNameCache, CB_App->gameNameCache->length + scanScene->game_filenames->length
        );
        scanScene->progress_max_width =
            cb_calculate_progress_max_width(CB_App->subheadFont, PROGRESS_STYLE_PERCENT, 0);
        scanScene->current_index = 0;
        scanScene->state = kScanningStateScanning;
        break;
    }

    case kScanningStateScanning:
    {
        CB_ScanSource* src = scanScene->sources->items[scanScene->source_index];

        if (scanScene->current_index < scanScene->game_filenames->length)
        {
            int batch_end = scanScene->current_index + SCAN_BATCH_SIZE;
            if (batch_end > scanScene->game_filenames->length)
                batch_end = scanScene->game_filenames->length;

            for (int i = scanScene->current_index; i < batch_end; i++)
            {
                const char* filename = scanScene->game_filenames->items[i];

                if (src->emucore_index < 0)
                {
                    process_one_game(scanScene, filename, src->games_dir);
                }
                else
                {
                    process_one_emucore_game(scanScene, filename, src->games_dir, src->slug);
                }
            }

            scanScene->current_index = batch_end;

            if (src->emucore_index < 0)
            {
                char progress_message[32];
                int pct = scanScene->game_filenames->length > 0
                              ? (int)((float)scanScene->current_index /
                                      scanScene->game_filenames->length * 100)
                              : 99;
                if (pct >= 100)
                    pct = 99;
                snprintf(progress_message, sizeof(progress_message), "%d%%", pct);
#ifdef CRANKBOY_OFFICIAL_CATALOG
                const char* scan_label = games_dir_is_packed(src->games_dir)
                                             ? T(status_scanning_catalog)
                                             : T(status_scanning);
#else
                const char* scan_label = T(status_scanning);
#endif
                cb_draw_logo_screen_centered_split(
                    CB_App->subheadFont, scan_label, progress_message, scanScene->progress_max_width
                );
            }
            else
            {
                cb_draw_logo_screen_and_display(CB_App->subheadFont, scanScene->progress_title);
            }
        }
        else
        {
            close_emucore_for_source(scanScene);
            clear_game_filenames(scanScene);
            scanScene->source_index++;
            scanScene->state = kScanningStateListSource;
        }
        break;
    }

    case kScanningStateDone:
    {
        if (scanScene->crc_cache_modified && scanScene->new_cache_entries->length > 0)
        {
            for (int i = 0; i < scanScene->new_cache_entries->length; i++)
            {
                json_value* file_entry = (json_value*)scanScene->new_cache_entries->items[i];
                JsonObject* file_obj = file_entry->data.tableval;

                const char* filename = file_obj->data[0].key;
                json_value value = file_obj->data[0].value;
                json_set_table_value(&scanScene->crc_cache, filename, value);

                file_obj->data[0].value.type = kJSONNull;
            }

            JsonObject* obj = scanScene->crc_cache.data.tableval;
            if (obj && obj->n > 1)
            {
                qsort(obj->data, obj->n, sizeof(TableKeyPair), compare_key_pairs);
            }

            char* path;
            playdate->system->formatString(&path, "%s", CRC_CACHE_FILE);
            if (path)
            {
                write_json_to_disk(path, scanScene->crc_cache);
                cb_free(path);
            }
        }

        bool png_found = false;
        {
            const char* coversPath = cb_gb_directory_path(CB_coversPath);
            if (coversPath)
            {
                playdate->file->listfiles(coversPath, checkForPngCallback, &png_found, false);
            }
        }

        if (png_found)
        {
            CB_ImageConversionScene* imageConversionScene = CB_ImageConversionScene_new();
            CB_present(imageConversionScene->scene);
        }
        else
        {
            CB_LibraryScene* libraryScene = CB_LibraryScene_new();
            CB_present(libraryScene->scene);
        }
        break;
    }
    }
}

void CB_GameScanningScene_free(void* object)
{
    CB_GameScanningScene* scanScene = object;

    close_emucore_for_source(scanScene);

    if (scanScene->sources)
    {
        for (unsigned int i = 0; i < scanScene->sources->length; i++)
            scan_source_free(scanScene->sources->items[i]);
        array_free(scanScene->sources);
    }

    if (scanScene->game_filenames)
    {
        for (int i = 0; i < scanScene->game_filenames->length; i++)
        {
            cb_free(scanScene->game_filenames->items[i]);
        }
        array_free(scanScene->game_filenames);
    }

    if (scanScene->new_cache_entries)
    {
        for (int i = 0; i < scanScene->new_cache_entries->length; i++)
        {
            json_value* item_to_free = (json_value*)scanScene->new_cache_entries->items[i];

            if (item_to_free)
            {
                free_json_data(*item_to_free);
                cb_free(item_to_free);
            }
        }
        array_free(scanScene->new_cache_entries);
    }

    free_json_data(scanScene->crc_cache);
    CB_Scene_free(scanScene->scene);
    cb_free(scanScene);
}

static void reset_crc_cache(CB_GameScanningScene* scanScene)
{
    scanScene->crc_cache.type = kJSONTable;
    JsonObject* obj = cb_malloc(sizeof(JsonObject));
    obj->n = 0;
    scanScene->crc_cache.data.tableval = obj;
}

CB_GameScanningScene* CB_GameScanningScene_new(void)
{
    CB_GameScanningScene* scanScene = cb_calloc(1, sizeof(CB_GameScanningScene));

    scanScene->scene = CB_Scene_new();
    scanScene->scene->id = "game-scanning";
    scanScene->scene->managedObject = scanScene;
    scanScene->scene->update = CB_GameScanningScene_update;
    scanScene->scene->free = CB_GameScanningScene_free;
    scanScene->scene->use_user_stack = false;

    scanScene->game_filenames = array_new();
    scanScene->new_cache_entries = array_new();
    scanScene->sources = array_new();
    scanScene->current_index = 0;
    scanScene->state = kScanningStateInit;
    scanScene->crc_cache_modified = false;

    char* path;
    playdate->system->formatString(&path, "%s", CRC_CACHE_FILE);
    if (path)
    {
        if (parse_json(path, &scanScene->crc_cache, kFileReadData))
        {
            if (scanScene->crc_cache.type == kJSONTable)
            {
                JsonObject* obj = scanScene->crc_cache.data.tableval;

                // discard legacy keys (missing slug)
                bool legacy = false;
                for (size_t i = 0; obj && i < obj->n; ++i)
                    if (!strchr(obj->data[i].key, ':'))
                    {
                        legacy = true;
                        break;
                    }

                if (legacy)
                {
                    free_json_data(scanScene->crc_cache);
                    reset_crc_cache(scanScene);
                }
                else if (obj && obj->n > 1)
                {
                    qsort(obj->data, obj->n, sizeof(TableKeyPair), compare_key_pairs);
                }
            }
            else
            {
                // unexpected type (array/string/etc.): discard, rebuild empty
                free_json_data(scanScene->crc_cache);
                reset_crc_cache(scanScene);
            }
        }
        else
        {
            reset_crc_cache(scanScene);
        }
        cb_free(path);
    }

    return scanScene;
}
