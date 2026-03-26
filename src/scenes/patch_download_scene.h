#pragma once

#include "../listview.h"
#include "http.h"
#include "library_scene.h"

#include <stdlib.h>

#define CB_PATCHDOWNLOAD_STACK_MAX_DEPTH 10

struct CB_SettingsScene;

typedef enum PatchDownloadSceneContextType
{
    PDSCT_TOP_LEVEL,
    PDSCT_LIST_PATCHES,
    PDSCT_PATCH_CHOOSE_INTERACTION,
    PDSCT_PATCH_FILES_BROWSE,
    PDSCT_PATCH_FILE_DOWNLOAD,
    PDSCT_MAX
} PatchDownloadSceneContextType;

typedef enum
{
    PD_NONE = 0,
    PD_PATCH,
    PD_TEXTFILE,
    PD_PROCESSING
} PendingDownloadType;

typedef enum
{
    PDC_NONE = 0,
    PDC_DOWNLOAD_SUCCESS,
    PDC_DOWNLOAD_FAILED_NOT_FOUND,
    PDC_DOWNLOAD_FAILED_WIFI,
    PDC_DOWNLOAD_FAILED_OTHER,
    PDC_SAVE_FAILED,
    PDC_TEXTFILE_SUCCESS
} PostDownloadCommand;

typedef struct
{
    char** files;
    int count;
} CB_LocalFileSet;

typedef struct PatchDownloadContext
{
    PatchDownloadSceneContextType type;
    CB_ListView* list;
    json_value j;

    int* index_map;
    int index_map_size;
} PatchDownloadContext;

typedef struct CB_PatchDownloadScene
{
    CB_Scene* scene;
    CB_Game* game;
    struct CB_SettingsScene* settingsScene;

    float header_animation_p;
    bool started_without_header;
    bool is_dismissing;

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
    CB_LocalFileSet* local_files;

    PendingDownloadType pending_download_type;
    char* pending_http_path;

    bool http_in_progress : 1;
    bool has_local_patches : 1;
    char* list_fetch_error_message;

    PostDownloadCommand post_download_command;
    unsigned post_download_flags;
    char* post_download_text_data;

    float option_hold_time;
    http_handle_t active_http_connection;
    char header_name[17];
    PatchDownloadContext context[CB_PATCHDOWNLOAD_STACK_MAX_DEPTH];

    float anim_t;
    float loading_anim_timer;
    int loading_anim_step;

} CB_PatchDownloadScene;

CB_PatchDownloadScene* CB_PatchDownloadScene_new(
    CB_Game* game, struct CB_SettingsScene* settingsScene, float initial_header_p
);
