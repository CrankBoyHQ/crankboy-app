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
#include "../listview.h"
#include "../scene.h"

#include <stdio.h>

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
} CB_Game;

typedef struct CB_LibraryScene
{
    CB_Scene* scene;
    CB_Array* games;
    CB_LibrarySceneModel model;
    CB_ListView* listView;
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
    HTTPConnection* activeCoverDownloadConnection;

    bool showCrc;
    bool isReloading;
    int progress_max_width;
    float bButtonHoldTimer;
    bool deleteCoverModalShown;
} CB_LibraryScene;

CB_LibraryScene* CB_LibraryScene_new(void);

CB_Game* CB_Game_new(CB_GameName* cachedName, CB_Array* available_covers);
void CB_Game_free(CB_Game* game);
