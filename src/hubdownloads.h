#pragma once

#include <stdbool.h>

typedef struct HubDownload
{
    char* filename;  // basename in the games directory
    char* typetag;   // raw hub typetag ("game", "demo", ...), may be NULL
    char** tags;     // raw hub tags, null-terminated, may be NULL
} HubDownload;

HubDownload** hub_downloads_entries_load(void);

bool hub_downloads_entries_contains(HubDownload* const* list, const char* filename);

void hub_downloads_add(const char* filename, const char* typetag, char* const* tags);

HubDownload** hub_downloads_prune_and_save(HubDownload** list);

const char* hub_downloads_map_tag(const char* tag);
const char* hub_downloads_map_typetag(const char* typetag);

void hub_downloads_free_all(HubDownload** list);
