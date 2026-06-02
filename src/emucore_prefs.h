#pragma once

#include "app.h"

#include <stdbool.h>
#include <stddef.h>

void cb_emucore_prefs_init(void);

int cb_emucore_prefs_read_from_disk(const char* filename, bool is_global);
int cb_emucore_prefs_save_to_disk(const char* filename, bool is_global);

// returns false if key isn't present.
bool cb_emucore_prefs_get_global(const char* key, unsigned* out);
bool cb_emucore_prefs_get_local(const char* key, unsigned* out);

void cb_emucore_prefs_set_global(const char* key, unsigned value);
void cb_emucore_prefs_set_local(const char* key, unsigned value);

void cb_apply_persisted_emucore_prefs(emucore_t* core, const char* system_slug);
