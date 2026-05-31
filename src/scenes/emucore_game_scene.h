#pragma once

#include "../scene.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct emucore_s emucore_t;

typedef bool (*ce_start_rom_fn)(uint8_t* rom, size_t size, const char* system_slug, const char* rom_basename);
typedef void (*ce_update_fn)(void);
typedef void (*ce_end_rom_fn)(void);

typedef struct CB_EmucoreGameScene
{
    CB_Scene* scene;

    emucore_t* core;  // points to CB_App->cores[...]
    uint8_t* rom;
    size_t rom_size;

    char* rom_path;
    char* slug;
    char* name_short;

    ce_start_rom_fn start_rom;
    ce_update_fn update_rom;
    ce_end_rom_fn end_rom;

    bool rom_started;
    bool go_to_library;
} CB_EmucoreGameScene;

CB_EmucoreGameScene* CB_EmucoreGameScene_new(
    const char* rom_path, const char* slug, const char* name_short
);

// return true on success
bool CB_emucore_save_state(CB_EmucoreGameScene* es, unsigned slot);
bool CB_emucore_load_state(CB_EmucoreGameScene* es, unsigned slot);
