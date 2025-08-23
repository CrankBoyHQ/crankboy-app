#pragma once

#include <stdlib.h>
#include "../softpatch.h"
#include "library_scene.h"
#include "listview.h"

#define CB_PATCHDOWNLOAD_STACK_MAX_DEPTH 10

typedef enum PatchDownloadSceneContextType
{
    PDSCT_TOP_LEVEL,
    PDSCT_LIST_PATCHES,
    PDSCT_PATCH_INFO,
    PDSCT_PATCH_FILES_BROWSE,
    PDSCT_PATCH_FILE_DOWNLOAD,
    PDSCT_INFO,
    PDSCT_MAX
} PatchDownloadSceneContextType;

typedef struct PatchDownloadContext
{
    PatchDownloadSceneContextType type;
    CB_ListView* list;
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
    
    
    json_value game_hacks;
    
    uint32_t cached_hint_key;
    json_value rhdb;
    
    bool http_in_progress : 1;
    char header_name[17];
    PatchDownloadContext context[CB_PATCHDOWNLOAD_STACK_MAX_DEPTH];
    
} CB_PatchDownloadScene;

CB_PatchDownloadScene* CB_PatchDownloadScene_new(CB_Game* game);