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
#include "../scene.h"

#include <stdio.h>

typedef struct CB_GameSceneContext CB_GameSceneContext;
typedef struct CB_GameScene CB_GameScene;

extern CB_GameScene* audioGameScene;

typedef enum
{
    CB_GameSceneStateLoaded,
    CB_GameSceneStateError,
} CB_GameSceneState;

typedef enum
{
    CB_GameSceneErrorUndefined,
    CB_GameSceneErrorLoadingRom,
    CB_GameSceneErrorWrongLocation,
    CB_GameSceneErrorFatal,
    CB_GameSceneErrorSaveData
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
    uint8_t wram[WRAM_SIZE_CGB];
    uint8_t vram[VRAM_SIZE_CGB];
    uint8_t* rom;
    size_t rom_size;
    bool cgb_mode;
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
    bool cartridge_has_accelerometer;
    bool staticSelectorUIDrawn;
    bool is_stereo;
    bool is_mirroring;
    unsigned int last_save_time;
    bool save_data_loaded_successfully : 1;
    bool save_state_requires_warning : 1;
    unsigned script_available : 1;
    unsigned script_info_available : 1;

    // from ROM header
    bool dmg_compatible : 1;
    bool cgb_compatible : 1;

    unsigned int rtc_time;
    uint16_t rtc_seconds_to_catch_up;

    CB_GameSceneState state;
    CB_GameSceneContext* context;
    CB_GameSceneModel model;
    CB_GameSceneError error;

    CB_CrankSelector selector;

    float prev_dt;

    int next_frames_elapsed;

    struct ScriptState* script;

    LCDBitmap* menuImage;
    int button_hold_mode;  // 0: Select, 1: None, 2: Start
    int button_hold_frames_remaining;

    int lock_button_hold_frames_remaining;
    uint8_t prev_joypad;

    float crank_turbo_accumulator;
    bool crank_turbo_a_active;
    bool crank_turbo_b_active;
    bool crank_was_docked;

    // set to true when simultaneously pressing a+b
    bool press_a_b_hold;
    bool hold_a_press_b;
    bool hold_b_press_a;

    int16_t* audio_temp_left;
    int16_t* audio_temp_right;
    size_t audio_temp_capacity;

    // time since started or last save/load state
    unsigned playtime;
    // epoch seconds when system menu was opened, 0 if not open
    unsigned menu_open_seconds;
    // millisecond fraction of menu_open_seconds, 0-999
    unsigned menu_open_ms;
    // slot last loaded via load_state()
    unsigned last_loaded_slot;
    bool quitGameModalConfirmOverride : 1;

    bool isCurrentlySaving : 1;
    bool cgb_needs_palette_recompute : 1;

    // Adaptive frame_skip (preferences_frame_skip == 2)
    int adaptive_fs_headroom_counter;
    int adaptive_fs_lock_frames;
    bool adaptive_fs_perf_allowed;

    // Probe-based deactivation: when under mitigation, periodically render
    // one unmitigated frame to measure real performance.
    bool adaptive_fs_probe_pending;
    int adaptive_fs_probe_cooldown;

    uint32_t patches_hash;

    unsigned fade_frames;
    bool fade_white;

    // Rewind system (DMG only)
    struct
    {
        uint8_t** states;
        size_t state_size;
        int capacity;
        int write_idx;
        int read_idx;
        int buffer_oldest;
        int count;
        int frame_counter;
        float scrub_accumulator;
        bool active;
        bool noise_pending;
    } rewind;
} CB_GameScene;

CB_GameScene* CB_GameScene_new(const char* rom_filename, char* name_short, bool cgb_mode);
void CB_GameScene_apply_settings(CB_GameScene* gameScene, bool audio_settings_changed);
void CB_GameScene_didSelectLibrary(void* userdata);
void CB_reset_audio_sync_state(void);

void reconfigure_audio_source(CB_GameScene* gameScene, int headphones);

unsigned get_save_state_timestamp(CB_GameScene* gameScene, unsigned slot);
bool load_state_thumbnail(CB_GameScene* gameScene, unsigned slot, uint8_t* out);

struct CB_Game;
void show_game_script_info(const char* rompath, const char* name_short);

void cb_render_fps(void);

void cb_render_boot_fade(unsigned fade_frames, bool fade_white);

unsigned cb_boot_fade_initial_frames(int boot_fade_pref);
bool cb_boot_fade_initial_white(int boot_fade_pref);

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
extern bool game_invert_indicator;

extern bool gbScreenRequiresFullRefresh;

#endif /* game_scene_h */
