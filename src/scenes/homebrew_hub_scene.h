#pragma once

#include "../http_safe.h"
#include "../listview.h"
#include "library_scene.h"

#include <stdlib.h>

#define CB_HBH_STACK_MAX_DEPTH 7

struct CB_SettingsScene;

typedef enum HomebrewHubSceneContextType
{
    HBSCT_TOP_LEVEL,
    HBSCT_LIST_SEARCH,
    HBSCT_LIST_FILES,

    HBSCT_MAX
} HomebrewHubSceneContextType;

typedef enum HomebrewHubDownloadType
{
    HB_DL_NONE,
    HB_DL_ROM,
    HB_DL_LIST,
} HomebrewHubDownloadType;

typedef struct HomebrewHubContext
{
    HomebrewHubSceneContextType type;
    CB_ListView* list;

    union
    {
        const char* str;
        const json_value* j;
    };
    int i;
    bool show_image : 1;
} HomebrewHubContext;

typedef struct CB_HomebrewHubScene
{
    CB_Scene* scene;
    CB_Game* game;
    struct CB_SettingsScene* settingsScene;

    float header_animation_p;
    bool started_without_header;
    bool is_dismissing;

    HTTPSafe* active_http_connection;
    HTTPSafe* active_http_connection_2;
    HomebrewHubDownloadType active_download_type;

    int max_pages;

    bool doctor_header_cgb_flag;

    char* target_rom_path;
    char* target_cover_art_path;
    char* urlpath;  // temporary; only for callbacks

    void* cover_art_data;
    size_t cover_art_len;

    LCDBitmap* download_image;
    int download_image_index;
    const char* download_image_name;

    int context_depth;
    int target_context_depth;
    float context_depth_p;
    char* cached_hint;

    uint32_t cached_hint_key;
    float option_hold_time;

    json_value jsearch;

    HomebrewHubContext context[CB_HBH_STACK_MAX_DEPTH];

    float anim_t;
    int loading_anim_step;

    char header_name[64];

} CB_HomebrewHubScene;

CB_HomebrewHubScene* CB_HomebrewHubScene_new(float initial_header_p, const char* header_name);
