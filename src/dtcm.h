#pragma once

#include <stdbool.h>
#include <stdio.h>

extern void* dtcm_mempool;
extern void* dtcm_mempool_start;
extern bool is_dtcm_init;

// The pool shares DTCM with the stack reserve: pool grows up from
// boot_frame - PLAYDATE_STACK_SIZE, stack grows down into the same window.
// DTCM_STACK_GROWTH is the headroom kept for the stack; the pool gets the rest.
#define PLAYDATE_STACK_SIZE 0x2700
#define DTCM_STACK_GROWTH 0x1000
#define DTCM_POOL_LIMIT (PLAYDATE_STACK_SIZE - DTCM_STACK_GROWTH)

void dtcm_set_mempool(void* addr);
void dtcm_init(void);
void dtcm_deinit(void);

// long_call: DTCM_VERIFY* is called from relocated clusters (bl would break).
#ifdef TARGET_SIMULATOR
bool dtcm_verify(const char* context);
#else
__attribute__((long_call)) bool dtcm_verify(const char* context);
#endif

void* dtcm_alloc(size_t size);
void* dtcm_alloc_aligned(size_t size, size_t offset);

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
// use dtcm_pocket_alloc_aligned(pocket_idx, size, alignment) to allocate from one.
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

void* dtcm_pocket_alloc_aligned(int pocket_idx, size_t size, size_t alignment);
bool dtcm_pocket_enabled(int pocket_idx);
void dtcm_pocket_fill_and_reset(void);

// first non-0xA5 byte in a pocket's unused tail, NULL if clean.
const void* dtcm_pockets_unused_dirt(void);

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
