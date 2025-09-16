#pragma once

#include "../listview.h"
#include "library_scene.h"
#include "http.h"

#include <stdlib.h>

#define CB_HBH_STACK_MAX_DEPTH 7

struct CB_SettingsScene;

typedef enum HomebrewHubSceneContextType
{
    HBSCT_TOP_LEVEL,
    HBSCT_LIST_SEARCH,
    HBSCT_PATCH_CHOOSE_INTERACTION,
    
    HBSCT_MAX
} HomebrewHubSceneContextType;

typedef struct HomebrewHubContext
{
    HomebrewHubSceneContextType type;
    CB_ListView* list;
    
    const char* str;
    int i;
} HomebrewHubContext;

typedef struct CB_HomebrewHubScene
{
    CB_Scene* scene;
    CB_Game* game;
    struct CB_SettingsScene* settingsScene;

    http_handle_t active_http_connection;
    
    int max_pages;
    
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

} CB_HomebrewHubScene;

CB_HomebrewHubScene* CB_HomebrewHubScene_new(void);
