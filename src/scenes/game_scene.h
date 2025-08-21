//
//  game_scene.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef game_scene_h
#define game_scene_h

#include "../../libs/peanut_gb.h"
#include "../preferences.h"
#include "../scene.h"

#include <math.h>
#include <stdio.h>

typedef struct CB_GameSceneContext CB_GameSceneContext;
typedef struct CB_GameScene CB_GameScene;

extern CB_GameScene* audioGameScene;

typedef enum
{
    CB_GameSceneStateLoaded,
    CB_GameSceneStateError
} CB_GameSceneState;

typedef enum
{
    CB_GameSceneErrorUndefined,
    CB_GameSceneErrorLoadingRom,
    CB_GameSceneErrorWrongLocation,
    CB_GameSceneErrorFatal
} CB_GameSceneError;

typedef struct
{
    CB_GameSceneState state;
    CB_GameSceneError error;
    int selectorIndex;
    int crank_mode;
    bool empty;
} CB_GameSceneModel;

typedef struct
{
    int width;
    int height;
    int containerWidth;
    int containerHeight;
    int containerX;
    int containerY;
    int x;
    int y;
    int startButtonX;
    int startButtonY;
    int selectButtonX;
    int selectButtonY;
    int numberOfFrames;
    float triggerAngle;
    float deadAngle;
    float index;
    bool startPressed;
    bool selectPressed;
} CB_CrankSelector;

typedef struct CB_GameSceneContext
{
    CB_GameScene* scene;
#ifdef PEANUT_GB_H
    gb_s* gb;
#else
    void* gb;
#endif
    uint8_t wram[WRAM_SIZE];
    uint8_t vram[VRAM_SIZE];
    uint8_t* rom;
    uint8_t* cart_ram;
    clalign uint8_t previous_lcd[LCD_BUFFER_BYTES];
} CB_GameSceneContext;

struct ScriptState;

typedef struct CB_GameScene
{
    CB_Scene* scene;
    char* save_filename;
    char* rom_filename;
    char* base_filename;  // rom filename with extension stripped
    char* settings_filename;
    char* name_short;  // For display in settings menu

    bool audioEnabled;
    bool audioLocked;
    bool cartridge_has_battery;
    bool cartridge_has_rtc;
    bool staticSelectorUIDrawn;
    bool is_stereo;
    bool is_mirroring;
    unsigned int last_save_time;
    bool save_data_loaded_successfully : 1;
    bool save_state_requires_warning : 1;
    unsigned script_available : 1;
    unsigned script_info_available : 1;

    unsigned int rtc_time;
    uint16_t rtc_seconds_to_catch_up;

    CB_GameSceneState state;
    CB_GameSceneContext* context;
    CB_GameSceneModel model;
    CB_GameSceneError error;

    CB_CrankSelector selector;

#if CB_DEBUG && CB_DEBUG_UPDATED_ROWS
    PDRect debug_highlightFrame;
    bool debug_updatedRows[LCD_ROWS];
#endif

    float prev_dt;

    struct ScriptState* script;

    LCDBitmap* menuImage;
    int button_hold_mode;  // 0: Select, 1: None, 2: Start
    int button_hold_frames_remaining;

    float crank_turbo_accumulator;
    bool crank_turbo_a_active;
    bool crank_turbo_b_active;
    bool crank_was_docked;

    // time since started or last save/load state
    unsigned playtime;

    bool isCurrentlySaving;

    int interlace_tendency_counter;
    int interlace_lock_frames_remaining;
    uint8_t previous_joypad_state;
} CB_GameScene;

CB_GameScene* CB_GameScene_new(const char* rom_filename, char* name_short);
void CB_GameScene_apply_settings(CB_GameScene* gameScene, bool audio_settings_changed);
void CB_GameScene_didSelectLibrary(void* userdata);
void CB_reset_audio_sync_state(void);

void reconfigure_audio_source(CB_GameScene* gameScene, int headphones);

unsigned get_save_state_timestamp(CB_GameScene* gameScene, unsigned slot);
bool load_state_thumbnail(CB_GameScene* gameScene, unsigned slot, uint8_t* out);

struct CB_Game;
void show_game_script_info(const char* rompath, const char* name_short);

// horizontal position of game boy screen on playdate screen; must be a multiple of 8
extern unsigned game_picture_x_offset;

// 1 in n rows are squished. Higher value means less vertical compression.
// 0 means 100% vertical scaling
extern unsigned game_picture_scaling;

// [first, last) gameboy rows to render.
extern unsigned game_picture_y_top;
extern unsigned game_picture_y_bottom;
extern LCDColor game_picture_background_color;
extern bool game_menu_button_input_enabled;
extern bool game_hide_indicator;

extern bool gbScreenRequiresFullRefresh;

#endif /* game_scene_h */
