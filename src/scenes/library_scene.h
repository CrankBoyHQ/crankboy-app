//
//  library_scene.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 15/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#pragma once

#include "../app.h"
#include "../array.h"
#include "../coverflow.h"
#include "../http_safe.h"
#include "../listview.h"
#include "../scene.h"
#include "game_scene.h"

#include <stdio.h>

#define BUILD_BATCH_SIZE 10
#define PRELOAD_HALF 15
#define PRELOAD_BATCH_SIZE 5
#define BG_FILL_BATCH_SIZE 2
#define MAX_COVER_COUNT 200
#define IDLE_THRESHOLD_MS 2000

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
    COVER_DOWNLOAD_IDLE,
    COVER_DOWNLOAD_SEARCHING,
    COVER_DOWNLOAD_DOWNLOADING,
    COVER_DOWNLOAD_FAILED,
    COVER_DOWNLOAD_NO_GAME_IN_DB,
    COVER_DOWNLOAD_COMPLETE
} CoverDownloadState;

typedef enum
{
    kLibraryStateInit,
    kLibraryStateBuildGameList,
    kLibraryStatePreloadCovers,
    kLibraryStateBuildUIList,
    kLibraryStateDone
} CB_LibraryState;

typedef struct CB_Game
{
    char* fullpath;
    char* coverPath;

    const CB_GameName* names;

    char* displayName;
    char* sortName;

    void* cover_compressed_data;
    int cover_compressed_size;
    int cover_width;
    int cover_height;
    int cover_rowbytes;
    bool cover_has_mask;
    uint32_t cover_access_counter;
} CB_Game;

typedef struct CB_LibraryScene
{
    CB_Scene* scene;
    CB_Array* games;
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

CB_Game* CB_Game_new(CB_GameName* cachedName, CB_Array* available_covers);
void CB_Game_free(CB_Game* game);

void CB_cover_compress(
    CB_Game* game, void* lz4_state, size_t* io_cache_bytes, int* io_cached_count
);
void CB_cover_free_compressed(CB_Game* game);

LCDBitmap* CB_decompress_game_cover(CB_Game* game, void** io_buffer, size_t* io_buffer_size);

// returns true if removal succeeded
// does not delete game on disk
bool CB_LibraryScene_removeGame(CB_Game* game);
