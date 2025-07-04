//
//  settings_scene.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef settings_scene_h
#define settings_scene_h

#include "game_scene.h"
#include "scene.h"

#include <stdio.h>

struct OptionsMenuEntry;
struct PDSynth;

typedef struct PGB_SettingsScene
{
    PGB_Scene* scene;
    PGB_GameScene* gameScene;

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
    int initial_per_game;
    preference_t* immutable_settings;

    struct OptionsMenuEntry* entries;

    // for options which have special on-hold behaviour
    float option_hold_time;

    // animation for settings header, ranges 0-1
    float header_animation_p;

    uint8_t thumbnail[SAVE_STATE_THUMBNAIL_H * ((SAVE_STATE_THUMBNAIL_W + 7) / 8)];
} PGB_SettingsScene;

PGB_SettingsScene* PGB_SettingsScene_new(PGB_GameScene* gameScene);

#endif /* settings_scene_h */
