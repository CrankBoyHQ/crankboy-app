#include "version.h"

#include "app.h"
#include "http.h"
#include "jparse.h"
#include "pd_api/pd_api_file.h"
#include "pd_api/pd_api_json.h"
#include "utility.h"

#define ERRMEM -255
#define STR_ERRMEM "malloc error"
#define UPDATE_CHECK_TIMESTAMP_PATH "check_update_timestamp.bin"
#define LOCAL_VERSION_PATH "version.json"
#define UPDATE_INFO_PATH "update_info.json"

static json_value localVersionInfo;
static json_value newVersionInfo;

static int read_local_version(void)
{
    free_json_data(localVersionInfo);
    localVersionInfo.type = kJSONNull;
    if (parse_json(LOCAL_VERSION_PATH, &localVersionInfo, kFileRead) != 0)
    {
        return 1;
    }
    return -1;
}

const char* get_current_version(void)
{
    if (read_local_version() == 1)
    {
        json_value name = json_get_table_value(localVersionInfo, "name");
        if (name.type == kJSONString) return name.data.stringval;
    }
    return NULL;
}

static void on_get(unsigned flags, char* data, size_t data_len, void* ud)
{
    if ((flags & (HTTP_ENABLE_DENIED | HTTP_WIFI_NOT_AVAILABLE | ~HTTP_ENABLE_ASKED)) != 0)
    {
        playdate->system->logToConsole("Update check failed (HTTP error)");
        return;
    }
    
    if (!data)
    {
        playdate->system->logToConsole("Update check failed (Null response)");
        return;
    }

    // skip leading garbage (FIXME: unsure why this is present...)
    char* json_start = strchr(data, '{');
    if (!json_start)
    {
        playdate->system->logToConsole("Update check failed (Invalid json)");
        return;
    }
    
    free_json_data(newVersionInfo);
    if (parse_json_string(json_start, &newVersionInfo) != 0)
    {
        json_value local_name = json_get_table_value(localVersionInfo, "name");
        json_value new_name = json_get_table_value(newVersionInfo, "name");
        if (local_name.type == kJSONString && new_name.type == kJSONString && strcmp(local_name.data.stringval, new_name.data.stringval))
        {
            // New version available. Check if we already notified the user about this one.
            
            bool should_write = true;
            json_value cachedVersionInfo;
            cachedVersionInfo.type = kJSONNull;
            
            if (parse_json(UPDATE_INFO_PATH, &cachedVersionInfo, kFileReadData) != 0)
            {
                json_value cached_name = json_get_table_value(cachedVersionInfo, "name");
                if (cached_name.type == kJSONString && !strcmp(cached_name.data.stringval, new_name.data.stringval))
                {
                    should_write = false;
                }
            }
    
            if (should_write)
            {
                write_json_to_disk(UPDATE_INFO_PATH, newVersionInfo);
            }
            
            free_json_data(cachedVersionInfo);
        }
    }
    else
    {
        playdate->system->logToConsole("Update check failed (Invalid json)");
    }
}

void check_for_updates(void)
{
    if (read_local_version() < 0)
    {
        playdate->system->logToConsole("Cannot check for update -- no local version.json");
        return;
    }

#define TIMEOUT_MS (10 * 1000)
    playdate->system->logToConsole("Checking for update...");

    json_value jdomain = json_get_table_value(localVersionInfo, "domain");
    json_value jpath = json_get_table_value(localVersionInfo, "path");

    if (jdomain.type == kJSONString && jpath.type == kJSONString)
    {
        http_get(
            jdomain.data.stringval, jpath.data.stringval, "to check for a version update", 
            on_get,
            TIMEOUT_MS, NULL
        );
    }
}

typedef uint32_t timestamp_t;

void write_update_timestamp(timestamp_t time)
{
    SDFile* f = playdate->file->open(UPDATE_CHECK_TIMESTAMP_PATH, kFileWrite);
    if (f)
    {
        playdate->file->write(f, &time, sizeof(time));
        playdate->file->close(f);
    }
}

#define DAYLEN (60 * 60 * 24)
#define TIME_BEFORE_CHECK_FIRST_UPDATE (DAYLEN * 2)
#define TIME_BETWEEN_SUBSEQUENT_UPDATE_CHECKS (DAYLEN)

void possibly_check_for_updates(void)
{
    timestamp_t now = playdate->system->getSecondsSinceEpoch(NULL);

    SDFile* f = playdate->file->open(UPDATE_CHECK_TIMESTAMP_PATH, kFileReadData);

    if (!f)
    {
        write_update_timestamp(now + TIME_BEFORE_CHECK_FIRST_UPDATE);
    }
    else
    {
        timestamp_t timestamp;
        int read = playdate->file->read(f, &timestamp, sizeof(timestamp));
        playdate->file->close(f);
        if (read != sizeof(timestamp) || timestamp < (365 * DAYLEN * 20))
        {
            write_update_timestamp(now + TIME_BETWEEN_SUBSEQUENT_UPDATE_CHECKS / 2);
        }
        else if (now >= timestamp)
        {
            write_update_timestamp(now + TIME_BETWEEN_SUBSEQUENT_UPDATE_CHECKS);
            check_for_updates();
        }
    }
}

PendingUpdateInfo* get_pending_update(void)
{
    PendingUpdateInfo* result = NULL;
    json_value jv_root;

    if (parse_json(UPDATE_INFO_PATH, &jv_root, kFileReadData) == 1 && jv_root.type == kJSONTable)
    {
        json_value jv_show = json_get_table_value(jv_root, "show");
        if (jv_show.type != kJSONFalse)
        {
            json_value jv_version = json_get_table_value(jv_root, "name");
            
            // prefer download-v2 to download (legacy)
            json_value jv_url = json_get_table_value(jv_root, "download-v2");
            json_value jv_w = json_get_table_value(jv_root, "download-v2-width");
            json_value jv_h = json_get_table_value(jv_root, "download-v2-height");
            if (jv_url.type == kJSONNull)
            {
                jv_url = json_get_table_value(jv_root, "download");
            }

            if (jv_version.type == kJSONString && jv_url.type == kJSONString)
            {
                result = cb_malloc(sizeof(PendingUpdateInfo));
                if (result)
                {
                    result->version = cb_strdup(jv_version.data.stringval);
                    result->url = cb_strdup(jv_url.data.stringval);
                    result->w = -1;
                    if (jv_w.type == kJSONInteger && jv_w.data.intval > 0) result->w = jv_w.data.intval;
                    
                    result->h = -1;
                    if (jv_h.type == kJSONInteger && jv_h.data.intval > 0) result->h = jv_h.data.intval;
                }
            }
        }
        free_json_data(jv_root);
    }

    return result;
}

void free_pending_update_info(PendingUpdateInfo* info)
{
    if (info)
    {
        cb_free(info->version);
        cb_free(info->url);
        cb_free(info);
    }
}

void mark_update_as_seen(void)
{
    json_value jv_root;

    if (parse_json(UPDATE_INFO_PATH, &jv_root, kFileReadData) == 1 && jv_root.type == kJSONTable)
    {
        json_set_table_value(&jv_root, "show", json_new_bool(false));
        write_json_to_disk(UPDATE_INFO_PATH, jv_root);
    }
    free_json_data(jv_root);
}

void version_quit(void)
{
    free_json_data(localVersionInfo);
    free_json_data(newVersionInfo);
}
