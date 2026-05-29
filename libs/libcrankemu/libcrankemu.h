#ifndef LIBCRANKEMU_H_
#define LIBCRANKEMU_H_

#define CRANKEMU_VERSION 1

#include <pd_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CE_MODAL_NO_CANCEL 1 /* user cannot press (B) to cancel the modal */

typedef struct ce_frontend
{
    uint32_t version; /* = CRANKEMU_VERSION */

    // Allocate to dtcm area.
    // May be NULL, in which case dtcm allocation is not supported.
    // Not freeable.
    // alignment may be 0.
    void* (*alloc_dtcm)(size_t size, size_t alignment);

    // inform frontend of non-fatal errors
    void (*set_error)(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
    
    // frontend may manipulate button presses
    void (*get_buttons)(PDButtons* o_down, PDButtons* o_pressed, PDButtons* o_released);
    
    // (OPTIONAL) invoke modal callback from frontend.
    //   msg: message to display in modal
    //   options: NULL-terminated list of options (or NULL for no options)
    //   flags: e.g. CE_MODAL_NO_CANCEL
    //   arg: index of selected option, or -1 if cancelled
    void (*blockingModal)(const char* msg, const char* const* options, unsigned flags, void* ud, void (*cb)(void* ud, int arg));
} ce_frontend_t;

typedef struct ce_preference
{
    void* ud;
    int is_category;  // if true, only name is required; represents start of option category
    char* id;         // lower-case machine-readable [a-z_][a-z0-9_]*
    const char* (*name)(struct ce_preference* self);

    /* required if not is_category */
    const char* (*description)(struct ce_preference* self);
    const char* const* values; // NULL-terminated
    int (*get)(struct ce_preference* self); /* returns index of current value (i.e. default value unless previously set) */
    void (*set)(struct ce_preference* self, unsigned value); /* argument will be index of one of the values */
    
    // optional
    bool (*locked)(void);
    bool (*hide)(void);

    // flags
    bool requires_restart;
} ce_preference_t;

// -- symbols for library export --

static inline uint32_t ce_get_version(void) { return CRANKEMU_VERSION; }
/* Optional */ bool ce_is_backward_compatible(uint32_t version); /* Return true if emu core backward-compatible with the given version. */
void ce_set_frontend(const ce_frontend_t*);

const char* ce_core_id(void);       // machine-readable id, [a-z_][a-z0-9_]*
const char* ce_core_name(void);     // human-readable name for core
const char* ce_core_version(void);  // e.g. "v1.0.4"

// return a ;-separated list of system slugs this core can handle e.g. "pm;gb". Optional final semicolon.
// Used for path i.e. /Shared/Emulation/<system>/
const char* ce_get_system_slugs(void);

// maps e.g. "a26" -> "Atari 2600"
// return NULL on failure
/* OPTIONAL */ const char* ce_get_system_name_from_slug(const char* system_slug);

// rom info

// should return a string matching [a-zA-Z0-9 _]*, or NULL
/* OPTIONAL */ const char* get_rom_header_name(const uint8_t* rom, size_t size);

// should return \n-separated lines of the form "prop:\tvalue", e.g.
// "Mapper:\tUNROM\nformat:ines2" etc.
const char* get_rom_info(const uint8_t* rom, size_t size);

// size of save data which must be saved to disk.
// return 0 to indicate no save data
size_t ce_get_rom_save_size(const uint8_t* rom, size_t size);

// logic
bool ce_start_rom(uint8_t* rom, size_t size, const char* system_slug, const char* rom_basename);
void ce_end_rom(void);
void ce_update(void);

// save data (all optional)
bool ce_is_save_dirty(void); // return true if saving would be warranted
void ce_save(uint8_t* buffer, size_t size);
bool ce_load(const uint8_t* buffer, size_t size);  // return false on error

// save-states (all optional)
size_t ce_get_state_size(void);
bool ce_state_save(uint8_t* buffer, size_t size);        // return false on failure
bool ce_state_load(const uint8_t* buffer, size_t size);  // return false on failure

// misc
ce_preference_t** ce_get_preferences(void); // NULL-terminated preference list

// note: eventHandler will receive normal events.
// however, eventHandler should NOT set the playdate update callback.
// in kEventLock, emulator should only set up to 1 menu item, as the others
// may be used by the frontend (e.g. settings, return to library)

#endif /* LIBCRANKEMU_H_ */
