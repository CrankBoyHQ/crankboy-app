//
//  game_scene.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef game_scene_h
#define game_scene_h

#include "../peanut_gb/peanut_gb.h"
#include "preferences.h"
#include "scene.h"

#include <math.h>
#include <stdio.h>

typedef struct PGB_GameSceneContext PGB_GameSceneContext;
typedef struct PGB_GameScene PGB_GameScene;

extern PGB_GameScene* audioGameScene;

typedef enum
{
    PGB_GameSceneStateLoaded,
    PGB_GameSceneStateError
} PGB_GameSceneState;

typedef enum
{
    PGB_GameSceneErrorUndefined,
    PGB_GameSceneErrorLoadingRom,
    PGB_GameSceneErrorWrongLocation,
    PGB_GameSceneErrorFatal
} PGB_GameSceneError;

typedef struct
{
    PGB_GameSceneState state;
    PGB_GameSceneError error;
    int selectorIndex;
    int crank_mode;
    bool empty;
} PGB_GameSceneModel;

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
} PGB_CrankSelector;

struct gb_s;

typedef struct PGB_GameSceneContext
{
    PGB_GameScene* scene;
    struct gb_s* gb;
    uint8_t wram[WRAM_SIZE];
    uint8_t vram[VRAM_SIZE];
    uint8_t* rom;
    uint8_t* cart_ram;
    uint8_t previous_lcd[LCD_HEIGHT * LCD_WIDTH_PACKED];  // Buffer for the previous frame's LCD
} PGB_GameSceneContext;

struct ScriptState;

typedef struct PGB_GameScene
{
    PGB_Scene* scene;
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
    unsigned int last_save_time;
    bool save_data_loaded_successfully;

    // clang-format off
    // [7700] We disable save states for carts with battery-backed ram
    // because one could easily lose their save data by mistake.
    //
    // !! IF YOU BYPASS THIS, YOU ARE TAKING RESPONSIBILITY FOR YOUR OWN SAVE DATA !!
    // !!  ~AND YOU ARE AIMING A LOADED GUN DIRECTLY AT YOUR FOOT WITH NO SAFETY~  !!
    // !!                       >> You have been warned. <<                        !!
    //
    // If you'd like to help enable save states on all ROMs, please give users a
    // BIG WARNING MESSAGE before they save state on a battery-backed ROM so that
    // they accept responsibility for what misery may ensue when mixing save types.
                                bool save_states_supported;
    // clang-format off

    unsigned int rtc_time;
    uint16_t rtc_seconds_to_catch_up;

    PGB_GameSceneState state;
    PGB_GameSceneContext *context;
    PGB_GameSceneModel model;
    PGB_GameSceneError error;

    PGB_CrankSelector selector;

#if PGB_DEBUG && PGB_DEBUG_UPDATED_ROWS
    PDRect debug_highlightFrame;
    bool debug_updatedRows[LCD_ROWS];
#endif

    float prev_dt;

    struct ScriptState* script;

    LCDBitmap *menuImage;
    int button_hold_mode; // 0: Select, 1: None, 2: Start
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
    int previous_scale_line_index;
    unsigned script_available : 1;
    unsigned script_info_available : 1;
} PGB_GameScene;

PGB_GameScene *PGB_GameScene_new(const char *rom_filename, char* name_short);
void PGB_GameScene_apply_settings(PGB_GameScene *gameScene, bool audio_settings_changed);
void PGB_GameScene_didSelectLibrary(void* userdata);

unsigned get_save_state_timestamp(PGB_GameScene *gameScene, unsigned slot);
bool load_state_thumbnail(PGB_GameScene *gameScene, unsigned slot, uint8_t* out);

struct PGB_Game;
void show_game_script_info(const char* rompath);

// horizontal position of game boy screen on playdate screen; must be a multiple of 8
extern unsigned game_picture_x_offset;

// 1 in n rows are squished. Higher value means less vertical compression.
// 0 means 100% vertical scaling
extern unsigned game_picture_scaling;

// [first, last) gameboy rows to render.
extern unsigned game_picture_y_top;
extern unsigned game_picture_y_bottom;
extern LCDColor game_picture_background_color;

extern bool gbScreenRequiresFullRefresh;

#endif /* game_scene_h */
