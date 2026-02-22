// game_scanning_scene.c
#include "game_scanning_scene.h"

#include "../app.h"
#include "cover_cache_scene.h"
#include "image_conversion_scene.h"
#include "library_scene.h"
#include "script.h"
#include "pd_api.h"

struct ScriptInfo;

void script_info_free(struct ScriptInfo* info);
void CB_GameScanningScene_update(void* object, uint32_t u32enc_dt);
void CB_GameScanningScene_free(void* object);

void collect_game_filenames_callback(const char* filename, void* userdata);

static void process_one_game(CB_GameScanningScene* scanScene, const char* filename)
{
    CB_GameName* newName = allocz(CB_GameName);

    newName->filename = cb_strdup(filename);
    newName->name_filename = cb_basename(filename, true);
    newName->name_filename_leading_article = common_article_form(newName->name_filename);

    char* fullpath;
    playdate->system->formatString(&fullpath, "%s/%s", cb_gb_directory_path(CB_gamesPath), filename);

    FileStat stat;
    if (playdate->file->stat(fullpath, &stat) != 0)
    {
        playdate->system->logToConsole("Failed to stat file: %s", fullpath);
        cb_free(fullpath);
        free_game_names(newName);
        cb_free(newName);
        return;
    }

    struct PDDateTime dt = {
        .year = stat.m_year,
        .month = stat.m_month,
        .day = stat.m_day,
        .hour = stat.m_hour,
        .minute = stat.m_minute,
        .second = stat.m_second
    };
    uint32_t m_time_epoch = playdate->system->convertDateTimeToEpoch(&dt);

    uint32_t crc = 0;
    bool needs_calculation = true;
    char header_name_buffer[17] = {0};
    enum cgb_support_e cgb = 0;
    unsigned battery = false;

    json_value cached_entry = {.type = kJSONNull};
    if (scanScene->crc_cache.type == kJSONTable)
    {
        JsonObject* obj = scanScene->crc_cache.data.tableval;

        TableKeyPair key_to_find;
        key_to_find.key = (char*)filename;

        TableKeyPair* found_pair = (TableKeyPair*)bsearch(
            &key_to_find, obj->data, obj->n, sizeof(TableKeyPair), compare_key_pairs
        );

        if (found_pair)
        {
            cached_entry = found_pair->value;
        }
    }

    if (cached_entry.type == kJSONTable)
    {
        json_value cached_crc_val = json_get_table_value(cached_entry, "crc32");
        json_value cached_size_val = json_get_table_value(cached_entry, "size");
        json_value cached_mtime_val = json_get_table_value(cached_entry, "m_time");
        json_value cached_header_val = json_get_table_value(cached_entry, "name_header");
        json_value cached_battery = json_get_table_value(cached_entry, "sram");
        json_value cached_cgb_val = json_get_table_value(cached_entry, "cgb");

        if (cached_crc_val.type == kJSONInteger && cached_size_val.type == kJSONInteger &&
            cached_mtime_val.type == kJSONInteger && cached_header_val.type == kJSONString
            && cached_battery.type == kJSONInteger && cached_cgb_val.type == kJSONInteger)
        {
            if ((uint32_t)cached_size_val.data.intval == stat.size &&
                (uint32_t)cached_mtime_val.data.intval == m_time_epoch)
            {
                crc = (uint32_t)cached_crc_val.data.intval;
                strncpy(
                    header_name_buffer, cached_header_val.data.stringval,
                    sizeof(header_name_buffer) - 1
                );
                battery = cached_battery.data.intval;
                cgb = cached_cgb_val.data.intval;
                needs_calculation = false;
            }
        }
    }

    CB_FetchedNames fetched = {NULL, NULL, 0, true};

    if (needs_calculation)
    {
        int is_gbz = 0;
        uint32_t gbz_checksum = 0;
        struct ScriptInfo* info =
            script_get_info_by_rom_path_and_get_header_info(fullpath, header_name_buffer, &cgb, &battery, &is_gbz, &gbz_checksum);
        if (info)
        {
            script_info_free(info);
        }
        for (int i = strlen(header_name_buffer) - 1; i >= 0; --i)
        {
            if (header_name_buffer[i] == ' ')
            {
                header_name_buffer[i] = 0;
            }
            else
            {
                break;
            }
        }
        
        bool valid = false;
        if (is_gbz)
        {
            valid = true;
            crc = gbz_checksum;
        }
        else if (cb_calculate_crc32(fullpath, kFileReadDataOrBundle, &crc))
        {
            valid = true;
        }
        
        if (valid) {
            fetched.failedToOpenROM = false;

            json_value new_entry_val;
            new_entry_val.type = kJSONTable;
            JsonObject* obj = cb_calloc(1, sizeof(JsonObject));
            new_entry_val.data.tableval = obj;

            json_value crc_val = {.type = kJSONInteger, .data.intval = crc};
            json_value size_val = {.type = kJSONInteger, .data.intval = stat.size};
            json_value mtime_val = {.type = kJSONInteger, .data.intval = m_time_epoch};
            json_value header_val = {
                .type = kJSONString, .data.stringval = cb_strdup(header_name_buffer)
            };
            json_value cgb_val = {
                .type = kJSONInteger, .data.intval = cgb
            };
            json_value bat_val = {
                .type = kJSONInteger, .data.intval = battery
            };

            json_set_table_value(&new_entry_val, "name_header", header_val);
            json_set_table_value(&new_entry_val, "crc32", crc_val);
            json_set_table_value(&new_entry_val, "size", size_val);
            json_set_table_value(&new_entry_val, "m_time", mtime_val);
            json_set_table_value(&new_entry_val, "sram", bat_val);
            json_set_table_value(&new_entry_val, "cgb", cgb_val);

            json_value* new_file_entry = cb_calloc(1, sizeof(json_value));
            new_file_entry->type = kJSONTable;
            JsonObject* file_obj = cb_calloc(1, sizeof(JsonObject));
            new_file_entry->data.tableval = file_obj;

            json_set_table_value(new_file_entry, filename, new_entry_val);

            array_push(scanScene->new_cache_entries, new_file_entry);
            scanScene->crc_cache_modified = true;
        }
    }
    else
    {
        fetched.failedToOpenROM = false;
    }

    newName->name_header = cb_strdup(header_name_buffer);

    if (!fetched.failedToOpenROM)
    {
        CB_FetchedNames names_from_db = cb_get_titles_from_db_by_crc(crc);
        fetched.short_name = names_from_db.short_name;
        fetched.detailed_name = names_from_db.detailed_name;
    }

    fetched.crc32 = crc;
    newName->crc32 = fetched.crc32;
    newName->rom_cgb_support = cgb;
    newName->rom_has_battery = battery;
    cb_free(fullpath);

    newName->name_database = (fetched.detailed_name) ? cb_strdup(fetched.detailed_name) : NULL;
    newName->name_short =
        (fetched.short_name) ? cb_strdup(fetched.short_name) : cb_strdup(newName->name_filename);
    newName->name_detailed = (fetched.detailed_name) ? cb_strdup(fetched.detailed_name)
                                                     : cb_strdup(newName->name_filename);

    newName->name_short_leading_article = common_article_form(newName->name_short);
    newName->name_detailed_leading_article = common_article_form(newName->name_detailed);

    if (fetched.short_name)
        cb_free(fetched.short_name);
    if (fetched.detailed_name)
        cb_free(fetched.detailed_name);

    if (!fetched.failedToOpenROM)
    {
        array_push(CB_App->gameNameCache, newName);
    }
    else
    {
        free_game_names(newName);
        cb_free(newName);
    }
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
        playdate->file->listfiles(
            cb_gb_directory_path(CB_gamesPath), collect_game_filenames_callback, scanScene->game_filenames, 0
        );

        array_reserve(CB_App->gameNameCache, scanScene->game_filenames->length);

        if (scanScene->game_filenames->length == 0)
        {
            scanScene->state = kScanningStateDone;
        }
        else
        {
            scanScene->progress_max_width = cb_calculate_progress_max_width(
                CB_App->subheadFont, PROGRESS_STYLE_FRACTION, scanScene->game_filenames->length
            );

            scanScene->state = kScanningStateScanning;
        }
        break;
    }

    case kScanningStateScanning:
    {
        if (scanScene->current_index < scanScene->game_filenames->length)
        {
            const char* filename = scanScene->game_filenames->items[scanScene->current_index];

            char progress_message[32];
            snprintf(
                progress_message, sizeof(progress_message), "%d/%d", scanScene->current_index + 1,
                scanScene->game_filenames->length
            );

            cb_draw_logo_screen_centered_split(
                CB_App->subheadFont, "Scanning Games... ", progress_message,
                scanScene->progress_max_width
            );

            process_one_game(scanScene, filename);
            scanScene->current_index++;
        }
        else
        {
            scanScene->state = kScanningStateDone;
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
        playdate->file->listfiles(cb_gb_directory_path(CB_coversPath), checkForPngCallback, &png_found, false);

        if (png_found)
        {
            CB_ImageConversionScene* imageConversionScene = CB_ImageConversionScene_new();
            CB_present(imageConversionScene->scene);
        }
        else
        {
            CB_CoverCacheScene* cacheScene = CB_CoverCacheScene_new();
            CB_present(cacheScene->scene);
        }
        break;
    }
    }
}

void CB_GameScanningScene_free(void* object)
{
    CB_GameScanningScene* scanScene = object;

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

CB_GameScanningScene* CB_GameScanningScene_new(void)
{
    CB_GameScanningScene* scanScene = cb_calloc(1, sizeof(CB_GameScanningScene));

    scanScene->scene = CB_Scene_new();
    scanScene->scene->managedObject = scanScene;
    scanScene->scene->update = CB_GameScanningScene_update;
    scanScene->scene->free = CB_GameScanningScene_free;
    scanScene->scene->use_user_stack = false;

    scanScene->game_filenames = array_new();
    scanScene->new_cache_entries = array_new();
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
                if (obj && obj->n > 1)
                {
                    qsort(obj->data, obj->n, sizeof(TableKeyPair), compare_key_pairs);
                }
            }
        }
        else
        {
            scanScene->crc_cache.type = kJSONTable;
            JsonObject* obj = cb_malloc(sizeof(JsonObject));
            obj->n = 0;
            scanScene->crc_cache.data.tableval = obj;
        }
        cb_free(path);
    }

    return scanScene;
}
