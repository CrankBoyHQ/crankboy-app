//
//  tcm_relocate.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef tcm_relocate_h
#define tcm_relocate_h

#include <stdbool.h>
#include <stdint.h>

#if ITCM_CORE
extern intptr_t core_itcm_offset;
extern intptr_t core_itcm_offset_batch;
#endif

// Resolve a core A/B block function pointer through its relocation offset
// (0 = flash copy, correct). Plain passthrough when ITCM_CORE is disabled.
#if ITCM_CORE
static inline void* tcm_core_fn(void* fn)
{
    return (void*)((uintptr_t)fn + core_itcm_offset);
}

static inline void* tcm_core_fn_batch(void* fn)
{
    return (void*)((uintptr_t)fn + core_itcm_offset_batch);
}
#else
static inline void* tcm_core_fn(void* fn)
{
    return fn;
}

static inline void* tcm_core_fn_batch(void* fn)
{
    return fn;
}
#endif

// Relocate core + satellite clusters into TCM (called on boot and resume).
void tcm_relocate(bool cgb);

// Clear TCM on lock/menu: return code to flash and release pockets.
void tcm_clear(bool cgb, void* pool_keep_end);

// Apply the current TCM Mode preference live (settings close).
void tcm_apply(bool cgb);

// Reset relocation state on scene init/deinit.
void tcm_reset(void);
void tcm_deinit(void);

#endif /* tcm_relocate_h */
