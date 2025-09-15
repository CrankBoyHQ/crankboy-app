#include "version.h"

#include "app.h"
#include "http.h"
#include "jparse.h"
#include "pd_api.h"
#include "utility.h"

#define ERRMEM -255
#define STR_ERRMEM "malloc error"
#define UPDATE_CHECK_TIMESTAMP_PATH "check_update_timestamp.bin"
#define UPDATE_INFO_PATH "update_info.json"

struct VersionInfo
{
    char* name;
    char* domain;
    char* path;
    char* download;
};
static struct VersionInfo* localVersionInfo = NULL;
static struct VersionInfo* newVersionInfo = NULL;

static int read_version_info(const char* text, bool ispath, struct VersionInfo* oinfo)
{
    json_value jvinfo;

    if (oinfo->name)
        cb_free(oinfo->name);
    if (oinfo->domain)
        cb_free(oinfo->domain);
    if (oinfo->path)
        cb_free(oinfo->path);
    if (oinfo->download)
        cb_free(oinfo->download);

    int jparse_result = (ispath) ? parse_json(VERSION_INFO_FILE, &jvinfo, kFileRead | kFileReadData)
                                 : parse_json_string(text, &jvinfo);

    if (jparse_result == 0 || jvinfo.type != kJSONTable)
    {
        free_json_data(jvinfo);
        return -1;
    }

    json_value jname = json_get_table_value(jvinfo, "name");
    json_value jpath = json_get_table_value(jvinfo, "path");
    json_value jdomain = json_get_table_value(jvinfo, "domain");
    json_value jdownload = json_get_table_value(jvinfo, "download");

    if (jname.type != kJSONString || jpath.type != kJSONString || jdomain.type != kJSONString ||
        jdownload.type != kJSONString)
    {
        free_json_data(jvinfo);
        return -2;
    }

    oinfo->name = cb_strdup(jname.data.stringval);
    oinfo->path = cb_strdup(jpath.data.stringval);
    oinfo->domain = cb_strdup(jdomain.data.stringval);
    oinfo->download = cb_strdup(jdownload.data.stringval);

    free_json_data(jvinfo);

    return 0;
}

static int read_local_version(void)
{
    if (!localVersionInfo)
    {
        localVersionInfo = cb_malloc(sizeof(struct VersionInfo));
        if (!localVersionInfo)
        {
            return -1;
        }
        memset(localVersionInfo, 0, sizeof(*localVersionInfo));

        int result;
        if ((result = read_version_info("version.json", true, localVersionInfo)))
        {
            cb_free(localVersionInfo);
            localVersionInfo = NULL;
            return -2;
        }
    }
    return 1;
}

const char* get_current_version(void)
{
    if (read_local_version() == 1)
    {
        return localVersionInfo->name;
    }
    else
    {
        return NULL;
    }
}

const char* get_download_url(void)
{
    if (newVersionInfo && newVersionInfo->download)
    {
        return newVersionInfo->download;
    }
    else
    {
        return "Please download it manually";
    }
}

static void CB_Get(unsigned flags, char* data, size_t data_len, void* ud)
{
    if ((flags & (HTTP_ENABLE_DENIED | HTTP_WIFI_NOT_AVAILABLE | ~HTTP_ENABLE_ASKED)) != 0)
    {
        return;
    }

    char* json_start = strchr(data, '{');
    if (!json_start)
    {
        return;
    }

    if (!newVersionInfo)
    {
        newVersionInfo = cb_malloc(sizeof(struct VersionInfo));
        if (!newVersionInfo)
        {
            return;
        }
        memset(newVersionInfo, 0, sizeof(*newVersionInfo));
    }

    if (read_version_info(json_start, false, newVersionInfo) == 0)
    {
        if (strcmp(newVersionInfo->name, localVersionInfo->name) != 0)
        {
            // New version available. Check if we already notified the user about this one.
            char* file_content = cb_read_entire_file(UPDATE_INFO_PATH, NULL, kFileReadData);
            bool should_write = true;

            if (file_content)
            {
                json_value jv_root;
                if (parse_json_string(file_content, &jv_root) == 1 && jv_root.type == kJSONTable)
                {
                    json_value jv_version = json_get_table_value(jv_root, "version");
                    if (jv_version.type == kJSONString &&
                        strcmp(jv_version.data.stringval, newVersionInfo->name) == 0)
                    {
                        should_write = false;
                    }
                    free_json_data(jv_root);
                }
                cb_free(file_content);
            }

            if (should_write)
            {
                char* json_string;
                // FIXME: use json formatting library
                playdate->system->formatString(
                    &json_string, "{\"version\":\"%s\",\"url\":\"%s\",\"show\":true}",
                    newVersionInfo->name, newVersionInfo->download
                );
                if (json_string)
                {
                    cb_write_entire_file(UPDATE_INFO_PATH, json_string, strlen(json_string));
                    cb_free(json_string);
                }
            }
        }
    }

    if (data)
    {
        cb_free(data);
    }
}

static void check_for_updates(void)
{
    switch (read_local_version())
    {
    case -1:
    case -2:
        return;
    default:
        break;
    }

#define TIMEOUT_MS (10 * 1000)

    http_get(
        localVersionInfo->domain, localVersionInfo->path, "to check for a version update", CB_Get,
        TIMEOUT_MS, NULL
    );
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
    char* content = cb_read_entire_file(UPDATE_INFO_PATH, NULL, kFileReadData);
    if (!content)
    {
        return NULL;
    }

    PendingUpdateInfo* result = NULL;
    json_value jv_root;

    if (parse_json_string(content, &jv_root) == 1 && jv_root.type == kJSONTable)
    {
        json_value jv_show = json_get_table_value(jv_root, "show");
        if (jv_show.type == kJSONTrue)
        {
            json_value jv_version = json_get_table_value(jv_root, "version");
            json_value jv_url = json_get_table_value(jv_root, "url");

            if (jv_version.type == kJSONString && jv_url.type == kJSONString)
            {
                result = cb_malloc(sizeof(PendingUpdateInfo));
                if (result)
                {
                    result->version = cb_strdup(jv_version.data.stringval);
                    result->url = cb_strdup(jv_url.data.stringval);
                }
            }
        }
        free_json_data(jv_root);
    }

    cb_free(content);
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
    char* content = cb_read_entire_file(UPDATE_INFO_PATH, NULL, kFileReadData);
    if (!content)
    {
        return;
    }

    json_value jv_root;
    if (parse_json_string(content, &jv_root) == 1 && jv_root.type == kJSONTable)
    {
        json_value jv_version = json_get_table_value(jv_root, "version");
        json_value jv_url = json_get_table_value(jv_root, "url");

        if (jv_version.type == kJSONString && jv_url.type == kJSONString)
        {
            char* json_string;
            // FIXME: should use the existing write-json functionality.
            playdate->system->formatString(
                &json_string, "{\"version\":\"%s\",\"url\":\"%s\",\"show\":false}",
                jv_version.data.stringval, jv_url.data.stringval
            );
            if (json_string)
            {
                cb_write_entire_file(UPDATE_INFO_PATH, json_string, strlen(json_string));
                CB_App->shouldCheckUpdateInfo = true;
                cb_free(json_string);
            }
        }
        free_json_data(jv_root);
    }

    cb_free(content);
}

void version_quit(void)
{
    if (localVersionInfo)
    {
        cb_free(localVersionInfo->name);
        cb_free(localVersionInfo->domain);
        cb_free(localVersionInfo->path);
        cb_free(localVersionInfo->download);
        cb_free(localVersionInfo);
        localVersionInfo = NULL;
    }

    if (newVersionInfo)
    {
        cb_free(newVersionInfo->name);
        cb_free(newVersionInfo->domain);
        cb_free(newVersionInfo->path);
        cb_free(newVersionInfo->download);
        cb_free(newVersionInfo);
        newVersionInfo = NULL;
    }
}
