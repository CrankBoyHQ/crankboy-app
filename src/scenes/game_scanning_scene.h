// game_scanning_scene.h
#pragma once

#include "../array.h"
#include "../scene.h"

#include <stddef.h>
#include <stdint.h>

typedef struct CB_GameName CB_GameName;

// States for the scanning process
typedef enum
{
    kScanningStateInit,
    kScanningStateListSource,
    kScanningStateScanning,
    kScanningStateDone
} GameScanningState;

typedef struct
{
    char* games_dir;
    char* slug;
    int emucore_index;  // (-1 for built-in GB)
} CB_ScanSource;

typedef size_t (*ce_rom_save_size_fn)(const uint8_t*, size_t);

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

    CB_Array* sources;  // CB_ScanSource*
    int source_index;
    void* emucore_pdll;
    ce_rom_save_size_fn save_size_fn;
    char* progress_title;
} CB_GameScanningScene;

CB_GameScanningScene* CB_GameScanningScene_new(void);

#ifdef CRANKBOY_OFFICIAL_CATALOG
CB_GameName* cb_build_packed_game_name(const char* filename, const json_value* crc_cache);
void cb_rescan_packed_filenames(void);
#endif
