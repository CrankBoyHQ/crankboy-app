// game_scanning_scene.h
#pragma once

#include "../jparse.h"
#include "../scene.h"
#include "../utility.h"

// States for the scanning process
typedef enum
{
    kScanningStateInit,
    kScanningStateScanning,
    kScanningStateDone
} GameScanningState;

typedef struct CB_GameScanningScene
{
    CB_Scene* scene;
    CB_Array* game_filenames;
    CB_Array* new_cache_entries;
    int current_index;
    GameScanningState state;
    json_value crc_cache;
    bool crc_cache_modified;
    int progress_max_width;
} CB_GameScanningScene;

CB_GameScanningScene* CB_GameScanningScene_new(void);
