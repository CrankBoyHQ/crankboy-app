#pragma once

#include <stdbool.h>
#include <stdio.h>

extern void* dtcm_mempool;
extern void* dtcm_mempool_start;
extern bool is_dtcm_init;

void dtcm_set_mempool(void* addr);
void dtcm_init(void);
void dtcm_deinit(void);

// Stack low-water mark (lowest address the main stack has reached) and free
// pool bytes up to it. The main pool shares DTCM with the stack reserve; these
// let placement size its claims instead of bump-allocating blindly. NULL/0 on
// the simulator.
void* dtcm_stack_hwm(void);
size_t dtcm_pool_free(void);
// Minimum stack headroom kept below the measured low-water mark.
#define DTCM_POOL_STACK_GUARD 0x400

#ifdef TARGET_SIMULATOR
bool dtcm_verify(const char* context);
#else
__attribute__((long_call)) bool dtcm_verify(const char* context);
#endif

// dtcm_free: no-op for pointers into the DTCM main region or pockets
// (bump-allocated, never individually freed); frees heap-fallback
// allocations. Pockets are released only via dtcm_pocket_fill_and_reset().

void* dtcm_alloc(size_t size);
void* dtcm_alloc_aligned(size_t size, size_t offset);
void dtcm_free(void* ptr);

// Release main-pool space above ptr_end (lock/unlock cycle cleanup).
void dtcm_pool_release_above(void* ptr_end);

struct dtcm_store_t;

// copies dtcm region to a buffer outside of dtcm.
// use this before an operation which might destroy dtcm.
struct dtcm_store_t* dtcm_store(void);

// restores from above, and invalidates the store
void dtcm_restore(struct dtcm_store_t*);

// probe downward from dtcm_mempool_start to find lowest accessible DTCM address.
void dtcm_probe_lower_bound(void);

// lowest accessible DTCM address found by dtcm_probe_lower_bound().
// NULL until probe runs.
extern void* dtcm_probe_lowest;

// clean DTCM pockets found by the probe, for data that doesn't need dtcm_store/restore.
// use dtcm_pocket_alloc(pocket_idx, size) to allocate from one.
// probe verifies every word of each 256-byte block; pocket tops carry a one-step guard band.
#define DTCM_MAX_POCKETS 5

struct dtcm_pocket_t
{
    void* start;
    void* mempool;
    void* end;
    bool init;
};

extern struct dtcm_pocket_t dtcm_pockets[DTCM_MAX_POCKETS];
extern int dtcm_num_pockets;

void* dtcm_pocket_alloc(int pocket_idx, size_t size);
void* dtcm_pocket_alloc_aligned(int pocket_idx, size_t size, size_t alignment);
bool dtcm_pocket_enabled(int pocket_idx);
void dtcm_pocket_fill_and_reset(void);

#define DTCM_VERIFY__(f, l) dtcm_verify(f ":" #l)
#define DTCM_VERIFY_(f, l) DTCM_VERIFY__(f, l)
#define DTCM_VERIFY() DTCM_VERIFY_(__FILE__, __LINE__)
#if DTCM_DEBUG
#define DTCM_VERIFY_DEBUG() DTCM_VERIFY()
#else
#define DTCM_VERIFY_DEBUG() 1
#endif

// true if dtcm_init called and DTCM_ALLOC enabled
static inline bool dtcm_enabled(void)
{
#ifndef DTCM_ALLOC
    return false;
#endif
    return is_dtcm_init;
}
