#ifndef LIBCRANKEMU_H_
#define LIBCRANKEMU_H_

#define CRANKEMU_VERSION 1

#include "pd_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ce_frontend
{
    uint32_t version; /* = CRANKEMU_VERSION */
    
    // Allocate to dtcm area.
    // May be NULL, in which case dtcm allocation is not supported.
    // Not freeable.
    // alignment may be 0.
    void* (*alloc_dtcm)(size_t size, size_t alignment);
    
    // inform frontend of non-fatal errors
    void (*set_error)(const char* fmt, ...);
    
    // frontend may manipulate button presses
    void (*get_buttons)(PDButtons* o_down, PDButtons* o_pressed, PDButtons* o_released);
} ce_frontend_t;

typedef struct ce_preference
{
    void* ud;
    int is_category; // if true, only name is required; represents start of option category
    char* id; // lower-case machine-readable [a-z_][a-z0-9_]*
    const char* (*name)(struct ce_preference* self);
    
    /* required if not is_category */
    const char* (*description)(struct ce_preference* self);
    const char* const* values; // null-terminated
    int (*get)(struct ce_preference* self); /* returns index of current value (i.e. default value unless previously set) */
    void (*set)(struct ce_preference* self, unsigned value); /* argument will be index of one of the values */
    
    // optional
    bool (*locked)(void);
    bool (*hide)(void);
    
    // flags
    bool requires_restart;
} ce_preference_t;

// -- symbols for library export --

uint32_t ce_get_version(void); // returns CRANKEMU_VERSION
void ce_set_frontend(const ce_frontend_t*);

const char* ce_core_id(void); // machine-readable id, [a-z_][a-z0-9_]*
const char* ce_core_name(void); // human-readable name for core
const char* ce_core_version(void); // e.g. "v1.0.4"

// return a NULL-terminated list of systems this core can handle e.g. {"pm", "gb", NULL}
const char* const* ce_get_system_directories(void);
bool ce_start_rom(uint8_t* rom, size_t size, const char* system_directory, const char* rom_basename);
void ce_end_rom(void);
void ce_update(void);

// save data
bool ce_is_save_dirty(void); // return true if saving would be warranted
size_t ce_get_save_size(void);
void ce_save(uint8_t* buffer, size_t size);
bool ce_load(const uint8_t* buffer, size_t size); // return false on error

// save-states
size_t ce_get_state_size(void);
bool ce_state_save(uint8_t* buffer, size_t size); // return false on failure
bool ce_state_load(const uint8_t* buffer, size_t size); // return false on failure

// misc
ce_preference_t** ce_get_preferences(void);

// should return \n-separated lines of the form "prop:\tvalue", e.g.
// "Mapper:\tUNROM\nformat:ines2" etc.
const char* get_rom_info(const uint8_t* rom, size_t size);

// note: eventHandler will receive normal events.
// however, eventHandler should NOT set the playdate update callback.
// in kEventLock, emulator should only set up to 1 menu item, as the others
// may be used by the frontend (e.g. settings, return to library)

#endif /* LIBCRANKEMU_H_ */