//
//  settings_scene.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef settings_scene_h
#define settings_scene_h

#include "../scene.h"
#include "game_scene.h"

#include <stdio.h>

struct OptionsMenuEntry;
struct PDSynth;
struct CB_LibraryScene;

typedef struct CB_SettingsScene
{
    CB_Scene* scene;
    CB_GameScene* gameScene;
    struct CB_LibraryScene* libraryScene;
    char* selected_game_settings_path;

    int cursorIndex;
    int topVisibleIndex;
    int totalMenuItemCount;
    float crankAccumulator;
    bool shouldDismiss : 1;
    bool wasAudioLocked : 1;

    int scroll_direction;
    int repeatLevel;
    float repeatIncrementTime;
    float repeatTime;

    int initial_sound_mode;
    int initial_sample_rate;
    int initial_headphone_audio;
    int initial_per_game;
    int initial_audio_sync;
    int initial_ppu_timing;
    preference_t* immutable_settings;

    struct OptionsMenuEntry* entries;

    // for options which have special on-hold behaviour
    float option_hold_time;

    // animation for settings header, ranges 0-1
    float header_animation_p;

    uint8_t thumbnail[SAVE_STATE_THUMBNAIL_H * ((SAVE_STATE_THUMBNAIL_W + 7) / 8)];
} CB_SettingsScene;

CB_SettingsScene* CB_SettingsScene_new(
    CB_GameScene* gameScene, struct CB_LibraryScene* libraryScene
);

#endif /* settings_scene_h */
