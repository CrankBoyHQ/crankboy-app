//
//  settings_scene.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef settings_scene_h
#define settings_scene_h

#include "../preferences.h"
#include "../scene.h"
#include "../userstack.h"
#include "emucore_game_scene.h"
#include "game_scene.h"

struct OptionsMenuEntry;
struct SectionDef;
struct PDSynth;
struct CB_LibraryScene;
struct CB_EmucoreGameScene;
struct ce_preference;
typedef struct ce_preference ce_preference_t;

#define CB_SETTINGS_MAX_EXTRA_SECTIONS 4

typedef struct CB_SettingsScene
{
    CB_Scene* scene;
    CB_GameScene* gameScene;
    // NULL unless we're editing settings for an emucore game.
    struct CB_EmucoreGameScene* emucoreGameScene;
    struct CB_LibraryScene* libraryScene;
    char* selected_game_settings_path;

    int cursorIndex;
    int topVisibleIndex;
    int totalMenuItemCount;
    int currentSectionIndex;
    float crankAccumulator;
    bool shouldDismiss : 1;
    bool shouldReturnToLibrary : 1;
    bool wasAudioLocked : 1;
    bool rec_dirty : 1;
    bool needsRebuild : 1;
    int rec_entry_index;

    int scroll_direction;
    int repeatLevel;
    float repeatIncrementTime;
    float repeatTime;

    int initial_sound_mode;
    int initial_sample_rate;
    int initial_headphone_audio;
    int initial_per_game;
    int initial_audio_sync;
    preference_t* immutable_settings;

    // neutrals: neither always-global nor always-local
    void* stored_neutrals;

    LCDBitmap* gradient;

    size_t sections_count;
    struct SectionDef* sections;
    struct OptionsMenuEntry* entries;

    // borrowed from emucore
    ce_preference_t** emu_prefs;

    pdll_t* peek_pdll;
    uint8_t* peek_rom;

    // for options which have special on-hold behaviour
    float option_hold_time;

    // animation for settings header, ranges 0-1
    float header_animation_p;

    uint8_t thumbnail[SAVE_STATE_THUMBNAIL_H * ((SAVE_STATE_THUMBNAIL_W + 7) / 8)];

    char* save_state_desc;
    char* load_state_desc;
    float desc_update_timer;
} CB_SettingsScene;

CB_SettingsScene* CB_SettingsScene_new(
    CB_GameScene* gameScene, CB_EmucoreGameScene* emucoreGameScene,
    struct CB_LibraryScene* libraryScene
);

static inline CB_SettingsScene* CB_SettingsScene_new_userstack(
    CB_GameScene* gameScene, CB_EmucoreGameScene* emucoreGameScene,
    struct CB_LibraryScene* libraryScene
)
{
    return (CB_SettingsScene*)call_with_user_stack_3(
        CB_SettingsScene_new, gameScene, emucoreGameScene, libraryScene
    );
}
#endif /* settings_scene_h */
