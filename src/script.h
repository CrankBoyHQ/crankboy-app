#pragma once

#include "app.h"
#include "preferences.h"

#include <stdbool.h>
#include <stdint.h>

/*

C scripts are .c files which must be included in the makefile
at build time, and they must contain a C_SCRIPT { ... } declaration.

Scripts can be toggled on/off mid-session from the settings menu, but only
if they declare `.toggleable = true` in their C_SCRIPT{...} declaration.
Default (unset) is false: the pref change then applies on the next ROM
launch. toggleable scripts must keep on_begin/on_end re-runnable:

- on_begin may start mid-game: don't assume boot-time state; ROM patches
  are safe to run more than once (poke_verify tolerates already-patched bytes).
- on_end should undo what the script changed. The framework auto-resets the
  screen layout, restores force_pref'd prefs, reverts ROM patches, re-inits
  the Start/Select selector (move it via script_selector()), and clears
  script-added Playdate menu entries + custom settings; the script only needs
  to clean up any other gameScene fields it changed.
- userdata is fresh per enable; state does not survive a disable/enable.

*/

struct CB_GameScene;
#ifndef PEANUT_GB_H
typedef void gb_s;
#endif

#define SCRIPT_MENU_SUPPRESS_BUTTON 1
#define SCRIPT_MENU_SUPPRESS_IMAGE 2

// returns user-data; return value of NULL indicates an error.
typedef void* (*CS_OnBegin)(gb_s* gb, const char* rom_header_name);

typedef void (*CS_OnTick)(gb_s* gb, void* userdata, int frames_elapsed);

typedef void (*CS_OnDraw)(gb_s* gb, void* userdata);

// returns flags SCRIPT_MENU_*
typedef unsigned (*CS_OnMenu)(gb_s* gb, void* userdata);

typedef void (*CS_OnSettings)(void* userdata);

// should free userdata
typedef void (*CS_OnEnd)(gb_s* gb, void* userdata);

typedef void (*CS_OnBreakpoint)(gb_s* gb, uint16_t addr, int breakpoint_idx, void* userdata);

typedef size_t (*CS_QuerySerialSize)(void* userdata);
typedef bool (*CS_Serialize)(char* out, void* userdata);
typedef bool (*CS_Deserialize)(const char* in, size_t size, void* userdata);

enum ScriptPreferredLaunchSystem
{
    ScriptPreferredLaunchSystem_None,
    ScriptPreferredLaunchSystem_DMG,
    ScriptPreferredLaunchSystem_CGB,
    // CGB iff the rom header supports it, else DMG (for scripts whose title
    // matches both DMG and CGB variants, e.g. ZELDA / Link's Awakening DX).
    ScriptPreferredLaunchSystem_Auto,
};

enum ScriptPreferredLaunchColor
{
    ScriptPreferredLaunchColor_None,
    ScriptPreferredLaunchColor_Black,
    ScriptPreferredLaunchColor_White,
};

struct ScriptRecommendedSetting
{
    preferences_bitfield_t bit;
    int value;
};

#define RECOMMENDED_SETTINGS_END {0, 0}

struct ScriptRecommendedSettings
{
    char* message;
    const struct ScriptRecommendedSetting* settings;
    const struct ScriptRecommendedSetting* settings_A;
    const struct ScriptRecommendedSetting* settings_B;
};

struct CScriptInfo
{
    // must match what's in the header
    const char* rom_name;
    const char* description;
    bool experimental;
    // can be toggled on/off mid-session from settings; default false means
    // the pref change applies on the next ROM launch (see doc block above).
    bool toggleable;
    enum ScriptPreferredLaunchSystem launch_system;
    enum ScriptPreferredLaunchColor launch_color;
    CS_OnBegin on_begin;
    CS_OnTick on_tick;
    CS_OnDraw on_draw;
    CS_OnMenu on_menu;
    CS_OnSettings on_settings;
    CS_OnEnd on_end;

    CS_QuerySerialSize query_serial_size;
    CS_Serialize serialize;
    CS_Deserialize deserialize;

    const struct ScriptRecommendedSettings* recommended_settings;
};

typedef struct ScriptInfo
{
    char rom_name[17];
    bool experimental;
    enum ScriptPreferredLaunchSystem launch_system;
    enum ScriptPreferredLaunchColor launch_color;
    char* info;  // human-readable description

    const struct CScriptInfo* c_script_info;
} ScriptInfo;

typedef struct ScriptState
{
    const struct CScriptInfo* c;

    // C script state
    void* ud;

    CS_OnBreakpoint* cbp;
} ScriptState;

ScriptState* script_begin(const char* game_name, struct CB_GameScene* game_scene);
void script_end(ScriptState* state, struct CB_GameScene* game_scene);

// Restore prefs forced by the script (via force_pref) to their pre-script
// values. Called when a script is disabled mid-session.
void script_pref_restore_originals(void);

// ROM patches are tracked by rom_poke so a mid-session script disable can
// revert the in-memory ROM to its pre-patch bytes.
void script_patch_record_reset(void);
void script_patch_restore(void);

bool script_tick(ScriptState* state, struct CB_GameScene* game_scene, int frames_elapsed);
void script_draw(ScriptState* state, struct CB_GameScene* game_scene);

// returns flags SCRIPT_MENU_*
unsigned script_menu(ScriptState* state, struct CB_GameScene* game_scene);
void script_add_settings(ScriptState* state);
void script_on_breakpoint(struct CB_GameScene* game_scene, int index);
size_t script_query_savestate_size(ScriptState* state);
bool script_save_state(ScriptState* state, uint8_t* out);
bool script_load_state(ScriptState* state, const uint8_t* in, size_t size);
void script_quit(void);

void register_c_script(const struct CScriptInfo* info);
void cb_register_all_c_scripts(void);

// for C scripts.
// Returns negative on failure; breakpoint index otherwise.
int c_script_add_hw_breakpoint(gb_s* gb, uint32_t addr, CS_OnBreakpoint callback);

// script info
void script_info_free(ScriptInfo* info);
ScriptInfo* script_get_info_by_rom_path(const char* game_path);

ScriptInfo* get_script_info(const char* game_name);

// o_rom_name must point to a buffer at least length 17
ScriptInfo* script_get_info_by_rom_path_and_get_header_info(
    const char* game_path, char* o_rom_name, enum cgb_support_e* o_cgb, unsigned* o_battery,
    int* o_is_gbz, uint32_t* o_gbz_checksum
);
bool script_exists(const char* game_path);

bool script_check_recommended_settings(
    const struct ScriptRecommendedSettings* settings, const char* game_settings_path
);
void script_apply_recommended_settings(
    const struct ScriptRecommendedSettings* settings, const char* game_settings_path
);
bool script_check_recommended_current(const struct ScriptRecommendedSettings* settings);
void script_apply_recommended_current(const struct ScriptRecommendedSettings* settings);
const struct ScriptRecommendedSettings* script_get_recommended_for_game(const char* rom_name);
