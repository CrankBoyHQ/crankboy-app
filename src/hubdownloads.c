#include "hubdownloads.h"

#include "app.h"
#include "jparse.h"
#include "userstack.h"
#include "utility.h"

#include <stdlib.h>
#include <string.h>

#define HUB_DOWNLOADS_PATH MISC_PATH "/hub_downloads.json"

static const struct
{
    const char* hub;
    const char* genre;
} TAG_TO_GENRE[] = {
    {"Platform", "Platform"},
    {"Puzzle", "Puzzle"},
    {"Action", "Action"},
    {"RPG", "Role-playing (RPG)"},
};

static const struct
{
    const char* hub;
    const char* genre;
} TYPETAG_TO_GENRE[] = {
    {"demo", "Demo"},
    {"music", "Music / Dancing"},
    {"tool", "Various"},
};

const char* hub_downloads_map_tag(const char* tag)
{
    if (!tag || !*tag)
        return NULL;
    for (size_t i = 0; i < sizeof(TAG_TO_GENRE) / sizeof(TAG_TO_GENRE[0]); ++i)
    {
        if (strcasecmp(tag, TAG_TO_GENRE[i].hub) == 0)
            return TAG_TO_GENRE[i].genre;
    }
    return NULL;
}

const char* hub_downloads_map_typetag(const char* typetag)
{
    if (!typetag || !*typetag)
        return NULL;
    for (size_t i = 0; i < sizeof(TYPETAG_TO_GENRE) / sizeof(TYPETAG_TO_GENRE[0]); ++i)
    {
        if (strcasecmp(typetag, TYPETAG_TO_GENRE[i].hub) == 0)
            return TYPETAG_TO_GENRE[i].genre;
    }
    return NULL;
}

static void hub_downloads_entry_append_tag(HubDownload* entry, const char* tag)
{
    size_t n = entry->tags ? len_nullterm((void const* const*)entry->tags) : 0;
    char** grown = mallocz(sizeof(char*) * (n + 2));
    if (!grown)
        return;

    for (size_t i = 0; i < n; ++i)
        grown[i] = entry->tags[i];
    grown[n] = cb_strdup(tag);
    cb_free(entry->tags);
    entry->tags = grown;
}

static void hub_downloads_list_append(HubDownload*** plist, HubDownload* entry)
{
    size_t n = len_nullterm((void const* const*)(HubDownload**)*plist);
    HubDownload** grown = mallocz(sizeof(HubDownload*) * (n + 2));
    if (!grown)
        return;

    HubDownload** old = *plist;
    for (size_t i = 0; old && old[i]; ++i)
        grown[i] = old[i];
    grown[n] = entry;
    cb_free(old);
    *plist = grown;
}

static HubDownload* hub_downloads_parse_entry(json_value je)
{
    const char* filename = json_as_string(json_get_table_value(je, "filename"));
    if (!filename || !*filename)
        return NULL;

    HubDownload* entry = allocz(HubDownload);
    entry->filename = cb_strdup(filename);

    const char* typetag = json_as_string(json_get_table_value(je, "typetag"));
    if (typetag && *typetag)
        entry->typetag = cb_strdup(typetag);

    json_value jtags = json_get_table_value(je, "tags");
    if (jtags.type == kJSONArray)
    {
        JsonArray* arr = jtags.data.arrayval;
        for (size_t i = 0; arr && i < arr->n; ++i)
        {
            const char* tag = json_as_string(arr->data[i]);
            if (tag && *tag)
                hub_downloads_entry_append_tag(entry, tag);
        }
    }
    return entry;
}

HubDownload** hub_downloads_entries_load(void)
{
    json_value j;
    if (!parse_json(HUB_DOWNLOADS_PATH, &j, kFileReadData))
        return NULL;

    json_value jarr = json_get_table_value(j, "downloads");
    JsonArray* arr = (jarr.type == kJSONArray) ? jarr.data.arrayval : NULL;

    HubDownload** list = NULL;
    for (size_t i = 0; arr && i < arr->n; ++i)
    {
        if (arr->data[i].type != kJSONTable)
            continue;
        HubDownload* entry = hub_downloads_parse_entry(arr->data[i]);
        if (entry)
            hub_downloads_list_append(&list, entry);
    }

    free_json_data(j);
    return list;
}

bool hub_downloads_entries_contains(HubDownload* const* list, const char* filename)
{
    if (!list || !filename)
        return false;
    for (HubDownload* const* it = list; *it; ++it)
    {
        if (strcmp((*it)->filename, filename) == 0)
            return true;
    }
    return false;
}

static int _hub_downloads_write(HubDownload** list)
{
    size_t n = len_nullterm((void const* const*)(HubDownload**)list);

    json_value j = json_new_table();
    if (j.type != kJSONTable)
        return -1;

    JsonArray* arr = mallocz(sizeof(JsonArray) + n * sizeof(json_value));
    if (!arr)
    {
        free_json_data(j);
        return -1;
    }
    arr->n = 0;
    for (HubDownload** it = list; it && *it; ++it)
    {
        HubDownload* entry = *it;
        json_value e = json_new_table();
        json_set_table_value(&e, "filename", json_new_string(entry->filename));
        if (entry->typetag && *entry->typetag)
            json_set_table_value(&e, "typetag", json_new_string(entry->typetag));
        size_t ntags = entry->tags ? len_nullterm((void const* const*)entry->tags) : 0;
        if (ntags)
        {
            JsonArray* tarr = mallocz(sizeof(JsonArray) + ntags * sizeof(json_value));
            if (tarr)
            {
                tarr->n = 0;
                for (char** tag = entry->tags; *tag; ++tag)
                    tarr->data[tarr->n++] = json_new_string(*tag);
                json_value jtags = {.type = kJSONArray};
                jtags.data.arrayval = tarr;
                json_set_table_value(&e, "tags", jtags);
            }
        }
        arr->data[arr->n++] = e;
    }

    json_value jarr = {.type = kJSONArray};
    jarr.data.arrayval = arr;
    json_set_table_value(&j, "downloads", jarr);

    full_mkdir(MISC_PATH);
    int result = write_json_to_disk(HUB_DOWNLOADS_PATH, j);

    free_json_data(j);
    return result;
}

static void hub_downloads_free_entry(HubDownload* entry)
{
    if (!entry)
        return;
    cb_free(entry->filename);
    cb_free(entry->typetag);
    if (entry->tags)
    {
        for (char** it = entry->tags; *it; ++it)
            cb_free(*it);
        cb_free(entry->tags);
    }
    cb_free(entry);
}

static void _hub_downloads_add(const char* filename, const char* typetag, char* const* tags)
{
    if (!filename || !*filename)
        return;

    HubDownload* entry = allocz(HubDownload);
    entry->filename = cb_strdup(filename);
    if (typetag && *typetag)
        entry->typetag = cb_strdup(typetag);
    for (char* const* tag = tags; tag && *tag; ++tag)
        hub_downloads_entry_append_tag(entry, *tag);

    HubDownload** list = hub_downloads_entries_load();

    if (list)
    {
        for (HubDownload** it = list; *it; ++it)
        {
            if (strcmp((*it)->filename, filename) == 0)
            {
                hub_downloads_free_entry(*it);
                *it = entry;
                entry = NULL;
                break;
            }
        }
    }

    if (entry)
        hub_downloads_list_append(&list, entry);

    _hub_downloads_write(list);
    hub_downloads_free_all(list);
}

void hub_downloads_add(const char* filename, const char* typetag, char* const* tags)
{
    call_with_main_stack_3(_hub_downloads_add, filename, typetag, tags);
}

HubDownload** hub_downloads_prune_and_save(HubDownload** list)
{
    if (!list)
        return NULL;

    char* games_dir = aprintf("%s", cb_gb_directory_path(CB_gamesPath));
    if (!games_dir)
        return list;

    bool removed = false;
    HubDownload** pruned = NULL;
    for (size_t r = 0; list[r]; ++r)
    {
        char* fullpath = aprintf("%s/%s", games_dir, list[r]->filename);
        bool exists = fullpath && cb_file_exists(fullpath, kFileReadData | kFileRead);
        cb_free(fullpath);

        if (exists)
            hub_downloads_list_append(&pruned, list[r]);
        else
        {
            hub_downloads_free_entry(list[r]);
            removed = true;
        }
    }
    cb_free(games_dir);
    cb_free(list);

    if (removed)
        _hub_downloads_write(pruned);

    return pruned;
}

void hub_downloads_free_all(HubDownload** list)
{
    if (!list)
        return;
    for (HubDownload** it = list; *it; ++it)
        hub_downloads_free_entry(*it);
    cb_free(list);
}
