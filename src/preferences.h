//
//  preferences.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 18/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef preferences_h
#define preferences_h

#include "utility.h"

#include <stdio.h>

#define DYNAMIC_RATE_OFF 0
#define DYNAMIC_RATE_ON 1
#define DYNAMIC_RATE_AUTO 2

#define CRANK_MODE_START_SELECT 0
#define CRANK_MODE_TURBO_CW 1
#define CRANK_MODE_TURBO_CCW 2
#define CRANK_MODE_OFF 3

#define PREF_BUTTON_NONE 0
#define PREF_BUTTON_START 1
#define PREF_BUTTON_SELECT 2

#define DISPLAY_NAME_MODE_SHORT 0
#define DISPLAY_NAME_MODE_DETAILED 1
#define DISPLAY_NAME_MODE_FILENAME 2

// at least 1 bit for each setting.
// WARNING: don't change this blindly, since these are
// casted down to uintptr_t (potentially 32-bit) for call_with_user_stack
typedef uint32_t preferences_bitfield_t;
typedef int preference_t;

typedef enum preference_index_t
{
#define PREF(x, ...) PREFI_##x,
#include "prefs.x"
    PREFI_COUNT,
} preference_index_t;

typedef enum preference_index_bit_t
{
#define PREF(x, ...) PREFBIT_##x = (1 << (int)PREFI_##x),
#include "prefs.x"
} preference_index_bit_t;

#define PREF(x, ...) extern preference_t preferences_##x;
#include "prefs.x"
extern const int pref_count;

void preferences_init(void);

void preferences_read_from_disk(const char* filename);

void preferences_merge_from_disk(const char* filename);

// returns 0 on failure
int preferences_save_to_disk(const char* filename, preferences_bitfield_t leave_as_is);

// stores the given preferences on the heap. Must be free'd.
void* preferences_store_subset(preferences_bitfield_t subset);
void preferences_restore_subset(void* stored);

// preferences that bundle
extern void* preferences_bundle_default;
extern preferences_bitfield_t preferences_bundle_hidden;

// preferences that script has forced to a particular value
extern preferences_bitfield_t prefs_locked_by_script;

// all the preferences that need the game to restart to apply
#define PREFBITS_REQUIRES_RESTART (PREFBIT_itcm | PREFBIT_script_support)
#define PREFBITS_LIBRARY_ONLY (PREFBIT_ui_sounds | PREFBIT_display_name_mode | PREFBIT_display_article | PREFBIT_display_sort | PREFBIT_library_remember_selection)

#endif /* preferences_h */
