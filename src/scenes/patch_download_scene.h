#pragma once

#include "../listview.h"
#include "library_scene.h"

#include <stdlib.h>

#define CB_PATCHDOWNLOAD_STACK_MAX_DEPTH 10

typedef enum PatchDownloadSceneContextType
{
    PDSCT_TOP_LEVEL,
    PDSCT_LIST_PATCHES,
    PDSCT_PATCH_CHOOSE_INTERACTION,
    PDSCT_PATCH_FILES_BROWSE,
    PDSCT_PATCH_FILE_DOWNLOAD,
    PDSCT_MAX
} PatchDownloadSceneContextType;

typedef struct PatchDownloadContext
{
    PatchDownloadSceneContextType type;
    CB_ListView* list;
    json_value j;
} PatchDownloadContext;

typedef struct CB_PatchDownloadScene
{
    CB_Scene* scene;
    CB_Game* game;

    int context_depth;
    int target_context_depth;
    float context_depth_p;
    char* patches_dir_path;
    char* cached_hint;

    const char* gamekey;
    const char* prefix;
    const char* filekey;
    const char* domain;
    union
    {
        const char* text_file_title;
        const char* basename;
    };

    json_value game_hacks;
    json_value hack_fs;
    json_value selected_hack;
    int selected_hack_key;

    uint32_t cached_hint_key;
    json_value rhdb;

    bool http_in_progress : 1;
    bool is_fetching_list : 1;
    char header_name[17];
    PatchDownloadContext context[CB_PATCHDOWNLOAD_STACK_MAX_DEPTH];

    float anim_t;
    float loading_anim_timer;
    int loading_anim_step;

} CB_PatchDownloadScene;

CB_PatchDownloadScene* CB_PatchDownloadScene_new(CB_Game* game);
