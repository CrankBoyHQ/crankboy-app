//
//  library_scene.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 15/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#pragma once

#include "../array.h"
#include "../coverflow.h"
#include "../http_safe.h"
#include "../listview.h"
#include "../romcategory.h"
#include "../scene.h"
#include "cover_download.h"

#include <stdio.h>

#define BUILD_BATCH_SIZE 10
#define BUNDLED_REFRESH_BATCH_SIZE 2
#define PRELOAD_HALF 15
#define PRELOAD_BATCH_SIZE 5
#define BG_FILL_BATCH_SIZE 2
#define MAX_COVER_COUNT 200
#define IDLE_THRESHOLD_MS 2000
#define CB_LIBRARY_FILTER_BAR_H 20

typedef enum
{
    CB_LibrarySceneTabList,
    CB_LibrarySceneTabEmpty
} CB_LibrarySceneTab;

typedef struct
{
    bool empty;
    CB_LibrarySceneTab tab;
} CB_LibrarySceneModel;

typedef enum
{
    kLibraryStateInit,
    kLibraryStateBuildGameList,
    kLibraryStatePreloadCovers,
    kLibraryStateBuildUIList,
    kLibraryStateDone
} CB_LibraryState;

// live update after the bundled-games setting changed (catalog builds)
typedef enum
{
    CB_BundledRefreshNone,
    CB_BundledRefreshHide,
    CB_BundledRefreshShowBuild
} CB_BundledRefreshState;

typedef struct CB_LibraryScene
{
    CB_Scene* scene;
    CB_Array* games;

    // active category identity: kind + stable key (id for user categories,
    // raw DB genre for genres); ROMCAT_ALL = the All view
    enum RomCategoryType active_category_type;
    char active_category_id[CATEGORY_ID_LEN];
    char active_category_name[MAX_CATEGORY_NAME];

    bool filter_options_cached;
    bool categories_editor_open;

    CB_BundledRefreshState bundled_refresh_state;
    int bundled_refresh_index;
    char* bundled_refresh_selection;  // fullpath of the selection to restore
    json_value bundled_refresh_crc_cache;
    bool bundled_refresh_crc_cache_valid;
    bool bundled_games_baseline;

    CB_LibrarySceneModel model;
    CB_ListView* listView;
    CB_CoverFlow* coverFlow;
    bool last_view_flow;
    CB_LibrarySceneTab tab;

    CB_LibraryState state;
    int build_index;

    bool firstLoad;
    bool initialLoadComplete;
    int lastSelectedItem;
    int last_display_name_mode;

    LCDBitmap* missingCoverIcon;

    CoverDownloadState coverDownloadState;
    char* coverDownloadMessage;
    HTTPSafe* activeCoverDownloadConnection;

    int last_filter_bar_width;

    bool isReloading;
    int progress_max_width;
    bool update_modal_shown;
    bool migration_modal_shown;

    void* decompression_buffer;
    size_t decompression_buffer_size;

    int launchAnimShiftLeft;
    int launchAnimShiftRight;
    int launchAnimSideBarWidth;
    bool launchAnimWhiteGap;

    CB_Array* available_covers;
    int build_game_index;

    void* lz4_state;
    int preload_cover_index;
    int preload_cover_total;
    size_t cover_cache_bytes;
    int cover_cached_count;
    uint32_t cover_global_access_counter;
    uint32_t last_user_input_time_ms;
    int last_selection_for_idle;
    int bg_fill_center;
    int bg_fill_dist;
    int bg_fill_dir;
} CB_LibraryScene;

CB_LibraryScene* CB_LibraryScene_new(void);

// returns true if removal succeeded
// does not delete game on disk
bool CB_LibraryScene_removeGame(CB_Game* game);
// drop the cover-flow slot cache so deleted covers disappear immediately
void CB_LibraryScene_invalidateFlow(void);
