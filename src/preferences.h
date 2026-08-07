//
//  preferences.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 18/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef preferences_h
#define preferences_h

#include <stdint.h>

#define CRANK_MODE_START_SELECT 0
#define CRANK_MODE_TURBO_CW 1
#define CRANK_MODE_TURBO_CCW 2
#define CRANK_MODE_OFF 3

#define PREF_BUTTON_NONE 0
#define PREF_BUTTON_START 1
#define PREF_BUTTON_SELECT 2
#define PREF_BUTTON_START_SELECT 3

// hold+press combos
#define PREF_BUTTON_HP_DEFAULT 0
#define PREF_BUTTON_HP_START 1
#define PREF_BUTTON_HP_SELECT 2
#define PREF_BUTTON_HP_START_SELECT 3
#define PREF_BUTTON_HP_START_A 4
#define PREF_BUTTON_HP_SELECT_A 5
#define PREF_BUTTON_HP_START_SELECT_A 6
#define PREF_BUTTON_HP_START_B 7
#define PREF_BUTTON_HP_SELECT_B 8
#define PREF_BUTTON_HP_START_SELECT_B 9
#define PREF_BUTTON_HP_START_A_B 10
#define PREF_BUTTON_HP_SELECT_A_B 11
#define PREF_BUTTON_HP_START_SELECT_A_B 12

#define PREF_BUTTON_ABR_DEFAULT 0
#define PREF_BUTTON_ABR_X 1
#define PREF_BUTTON_ABR_NONE 2
#define PREF_BUTTON_ABR_START 3
#define PREF_BUTTON_ABR_SELECT 4
#define PREF_BUTTON_ABR_START_SELECT 5
#define PREF_BUTTON_ABR_START_X 6
#define PREF_BUTTON_ABR_SELECT_X 7
#define PREF_BUTTON_ABR_START_SELECT_X 8

#define DISPLAY_NAME_MODE_SHORT 0
#define DISPLAY_NAME_MODE_DETAILED 1
#define DISPLAY_NAME_MODE_FILENAME 2

#define PREF_FADE_NONE 0
#define PREF_FADE_SHORT_BLACK 1
#define PREF_FADE_LONG_BLACK 2
#define PREF_FADE_SHORT_WHITE 3
#define PREF_FADE_LONG_WHITE 4

// at least 1 bit for each setting.
// TODO: if this runs out, we'll need  __attribute__((vector_size (16))),
// plus some work to make bit-shifting work
typedef uint64_t preferences_bitfield_t;
typedef int preference_t;

typedef enum preference_index_t
{
#define PREF(x, ...) PREFI_##x,
#include "prefs.x"
    PREFI_COUNT,
} preference_index_t;

typedef enum preference_index_bit_t
{
#define PREF(x, ...) PREFBIT_##x = (((preferences_bitfield_t)1) << (int)PREFI_##x),
#include "prefs.x"
} preference_index_bit_t;

#define PREF(x, ...) extern preference_t preferences_##x;
#include "prefs.x"

void preferences_init(void);

void preferences_set_defaults(void);

void preferences_read_from_disk(const char* filename);

void preferences_merge_from_disk(const char* filename);

// reads preferences from disk, but leaves the bits in `preserve_mask` unchanged.
// this wraps the common pattern of: store_subset -> read_from_disk -> restore_subset.
void preferences_read_from_disk_preserve(
    const char* filename, preferences_bitfield_t preserve_mask
);

// returns 0 on failure
// always leaves emucore prefs as-they-are
int preferences_save_to_disk(const char* filename, preferences_bitfield_t leave_as_is);

// returns -1 on failure
int prefvar_to_index(preference_t* pref);

// stores the given preferences on the heap. Must be free'd.
void* preferences_store_subset(preferences_bitfield_t subset);
void preferences_restore_subset(void* stored);

// preferences that bundle
extern void* preferences_bundle_default;
extern preferences_bitfield_t preferences_bundle_hidden;

// default values for all preferences
extern int preference_default_value[PREFI_COUNT];

// preferences that script has forced to a particular value
extern preferences_bitfield_t prefs_locked_by_script;

// all the preferences that need the game to restart to apply
#define PREFBITS_REQUIRES_RESTART (PREFBIT_save_slot | PREFBIT_boot_fade)

// these preferences are always saved globally, regardless of if global/per-game selected
#define PREFBITS_ALWAYS_GLOBAL                                                                    \
    (PREFBIT_ui_sounds | PREFBIT_display_name_mode | PREFBIT_display_article |                    \
     PREFBIT_display_sort | PREFBIT_library_remember_selection | PREFBIT_prompt_if_cgb_optional | \
     PREFBIT_library_launch_animation | PREFBIT_show_bundled_games)

// these preferences are always saved per-game, regardless of if global/per-game selected
#define PREFBITS_NEVER_GLOBAL                                                                     \
    (PREFBIT_per_game | PREFBIT_save_state_slot | PREFBIT_save_slot | PREFBIT_script_A |          \
     PREFBIT_script_B | PREFBIT_script_C | PREFBIT_script_support | PREFBIT_script_has_prompted | \
     PREFBIT_recommended_settings_ignored | PREFBIT_cgb_only_has_prompted)

// preferences that are never saved to disk
#define PREFBITS_TRANSIENT (0)

#endif /* preferences_h */
