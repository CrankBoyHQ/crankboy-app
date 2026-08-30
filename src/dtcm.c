#include "dtcm.h"

#include "utility.h"

#include <stdint.h>

#define __dtcm_ctrl __section__(".text.dtcm_ctrl")

#ifdef DTCM_ALLOC
static uint32_t* dtcm_low_canary_addr = NULL;
// highest dtcm_mempool ever reached; used by dtcm_free to identify DTCM
// pointers even after dtcm_deinit/dtcm_restore moved dtcm_mempool back.
static void* dtcm_mempool_hwm = NULL;
#define DTCM_CANARY 0xDE0DCA94
#endif

bool is_dtcm_init = false;

// high address that's within stack region,
// can allocate global variables from here+
void* dtcm_mempool = NULL;
void* dtcm_mempool_start = NULL;
void* dtcm_probe_lowest = NULL;

struct dtcm_pocket_t dtcm_pockets[DTCM_MAX_POCKETS];
int dtcm_num_pockets = 0;

static void dtcm_pocket_init(struct dtcm_pocket_t* p, void* start, size_t size)
{
    p->start = start;
    p->mempool = start;
    p->end = (void*)((uintptr_t)start + size);
    p->init = true;
}

void* dtcm_pocket_alloc(int pocket_idx, size_t size)
{
    if (pocket_idx < 0 || pocket_idx >= dtcm_num_pockets)
        return NULL;

    struct dtcm_pocket_t* p = &dtcm_pockets[pocket_idx];
    if (!p->init)
        return NULL;

    void* result = p->mempool;
    p->mempool = (void*)(size + (uintptr_t)p->mempool);

    if (p->mempool > p->end)
    {
        p->mempool = result;
        return NULL;
    }

    return result;
}

void* dtcm_pocket_alloc_aligned(int pocket_idx, size_t size, size_t alignment)
{
    if (pocket_idx < 0 || pocket_idx >= dtcm_num_pockets)
        return NULL;

    struct dtcm_pocket_t* p = &dtcm_pockets[pocket_idx];
    if (!p->init)
        return NULL;

    alignment %= 32;
    while ((uintptr_t)p->mempool % 32 != alignment)
        p->mempool = (void*)((uintptr_t)p->mempool + 1);

    void* result = p->mempool;
    p->mempool = (void*)(size + (uintptr_t)p->mempool);

    if (p->mempool > p->end)
    {
        p->mempool = result;
        return NULL;
    }

    return result;
}

bool dtcm_pocket_enabled(int pocket_idx)
{
    if (pocket_idx < 0 || pocket_idx >= dtcm_num_pockets)
        return false;
    return dtcm_pockets[pocket_idx].init;
}

void dtcm_pocket_fill_and_reset(void)
{
    for (int i = 0; i < dtcm_num_pockets; i++)
    {
        size_t used = (uintptr_t)dtcm_pockets[i].mempool - (uintptr_t)dtcm_pockets[i].start;
        if (used > 0)
            memset(dtcm_pockets[i].start, 0xA5, used);
        dtcm_pockets[i].mempool = dtcm_pockets[i].start;
    }
}

#ifdef DTCM_ALLOC
// Hard cap on main-pool size; over it the caller falls back to a pocket or flash.
static bool dtcm_pool_budget(uintptr_t top)
{
    if ((top - (uintptr_t)dtcm_mempool_start) > DTCM_POOL_LIMIT)
    {
        playdate->system->logToConsole(
            "dtcm pool: refuse %uB alloc (limit %uB)", (unsigned)(top - (uintptr_t)dtcm_mempool),
            (unsigned)DTCM_POOL_LIMIT
        );
        return false;
    }
    return true;
}
#endif

__dtcm_ctrl void* dtcm_alloc(size_t size)
{
#ifdef DTCM_ALLOC
    if (is_dtcm_init)
    {
        uintptr_t top = (uintptr_t)dtcm_mempool + size;
        if (!dtcm_pool_budget(top))
            return NULL;
        void* tmp = dtcm_mempool;
        *(uint32_t*)dtcm_mempool = 0;  // remove canary
        dtcm_mempool = (void*)top;
        if (dtcm_mempool > dtcm_mempool_hwm)
            dtcm_mempool_hwm = dtcm_mempool;
        // high canary
        *(uint32_t*)dtcm_mempool = DTCM_CANARY;
        return tmp;
    }
#endif

    // 8-byte header slot preserves cb_malloc's 8-byte alignment guarantee.
    // Original pointer stored in the slot adjacent to the returned pointer,
    // so dtcm_free's ((void**)ptr)[-1] convention still works.
    if (size > SIZE_MAX - 2 * sizeof(void*))
        playdate->system->error("dtcm_alloc: size overflow");
    void* original_ptr = cb_malloc(size + 2 * sizeof(void*));
    ((void**)original_ptr)[1] = original_ptr;
    return (void*)((uintptr_t)original_ptr + 2 * sizeof(void*));
}

__dtcm_ctrl void* dtcm_alloc_aligned(size_t size, size_t alignment)
{
#ifdef DTCM_ALLOC
    if (is_dtcm_init)
    {
        alignment %= 32;
        uintptr_t aligned_start = (uintptr_t)dtcm_mempool;
        while (aligned_start % 32 != alignment)
            aligned_start++;
        uintptr_t top = aligned_start + size;
        if (!dtcm_pool_budget(top))
            return NULL;
        *(uint32_t*)dtcm_mempool = 0;
        dtcm_mempool = (void*)aligned_start;
        void* tmp = dtcm_mempool;
        dtcm_mempool = (void*)top;
        if (dtcm_mempool > dtcm_mempool_hwm)
            dtcm_mempool_hwm = dtcm_mempool;
        *(uint32_t*)dtcm_mempool = DTCM_CANARY;
        return tmp;
    }
#endif

    if (alignment == 0 || size > SIZE_MAX - (alignment - 1) - sizeof(void*))
        playdate->system->error("dtcm_alloc_aligned: size overflow");
    void* original_ptr = cb_malloc(size + alignment - 1 + sizeof(void*));

    void* aligned_ptr =
        (void*)(((uintptr_t)original_ptr + sizeof(void*) + alignment - 1) & ~(alignment - 1));

    ((void**)aligned_ptr)[-1] = original_ptr;

    return aligned_ptr;
}

__dtcm_ctrl void dtcm_pool_release_above(void* ptr_end)
{
#ifdef DTCM_ALLOC
    if (!is_dtcm_init)
        return;

    uintptr_t end = (uintptr_t)ptr_end;
    if (end < (uintptr_t)dtcm_mempool_start)
        end = (uintptr_t)dtcm_mempool_start;
    if (end > (uintptr_t)dtcm_mempool)
        return;
    end = (end + 3u) & ~3u;
    dtcm_mempool = (void*)end;
    *(uint32_t*)dtcm_mempool = DTCM_CANARY;
#endif
}

__dtcm_ctrl void dtcm_init(void)
{
    if (is_dtcm_init)
        return;
    is_dtcm_init = true;

    if (dtcm_mempool_start == NULL)
    {
        is_dtcm_init = false;
        playdate->system->error("Attempt to enable DTCM, but mempool region not set!");
        return;
    }

    dtcm_mempool = dtcm_mempool_start;

#ifdef DTCM_ALLOC
    *(uint32_t*)dtcm_mempool_start = DTCM_CANARY;
    dtcm_low_canary_addr = (uint32_t*)dtcm_alloc(sizeof(uint32_t));
    if (dtcm_low_canary_addr)
        *dtcm_low_canary_addr = DTCM_CANARY;
    playdate->system->logToConsole("DTCM init");
#endif
}

__dtcm_ctrl void dtcm_deinit(void)
{
    is_dtcm_init = 0;
    dtcm_mempool = dtcm_mempool_start;
}

__dtcm_ctrl void dtcm_set_mempool(void* addr)
{
    if (dtcm_mempool_start != NULL)
    {
        playdate->system->error("Cannot set DTCM mempool twice.");
        return;
    }
    dtcm_mempool_start = addr;
#ifdef DTCM_ALLOC
    dtcm_mempool_hwm = addr;
    playdate->system->logToConsole("DTCM mempool: %p\n", dtcm_mempool_start);
#endif
}

__dtcm_ctrl bool dtcm_verify(const char* context)
{
    if (!is_dtcm_init)
        return true;

#ifdef DTCM_ALLOC
    if (dtcm_low_canary_addr)
    {
        if (*dtcm_low_canary_addr != DTCM_CANARY)
        {
            playdate->system->error(
                "ERROR %s: DTCM low canary broken (decrease "
                "PLAYDATE_STACK_SIZE?)",
                context
            );
            return false;
        }
        if (*(uint32_t*)dtcm_mempool != DTCM_CANARY)
        {
            playdate->system->error("ERROR %s: DTCM high canary broken (stack overflow?)", context);
            return false;
        }
    }
#endif
    return true;
}

struct dtcm_store_t
{
    uint32_t* dtcm_low;
    void* dtcm_mempool;
    char data[];
};

struct dtcm_store_t* dtcm_store(void)
{
#ifdef DTCM_ALLOC
    if (!is_dtcm_init)
        return NULL;

    size_t size = (uintptr_t)dtcm_mempool + 4 - (uintptr_t)dtcm_low_canary_addr;

    playdate->system->logToConsole("Storing DTCM (0x%x bytes)", size);
    struct dtcm_store_t* buff = (struct dtcm_store_t*)cb_malloc(sizeof(struct dtcm_store_t) + size);
    buff->dtcm_low = dtcm_low_canary_addr;
    buff->dtcm_mempool = dtcm_mempool;
    memcpy(buff->data, dtcm_low_canary_addr, size);
    return buff;
#else
    return NULL;
#endif
}

void dtcm_restore(struct dtcm_store_t* buff)
{
    if (!buff)
        return;
#ifdef DTCM_ALLOC
    playdate->system->logToConsole("Restoring DTCM");
    dtcm_low_canary_addr = buff->dtcm_low;
    dtcm_mempool = buff->dtcm_mempool;
    size_t size = (uintptr_t)dtcm_mempool + 4 - (uintptr_t)dtcm_low_canary_addr;
    playdate->system->logToConsole("-> restored DTCM is 0x%x bytes", size);
    memcpy(dtcm_low_canary_addr, buff->data, size);
    cb_free(buff);
    playdate->system->logToConsole("Restore complete.");
#endif
}

void dtcm_free(void* ptr)
{
    if (!ptr)
    {
        return;
    }

#ifdef DTCM_ALLOC
    uintptr_t p = (uintptr_t)ptr;
    // check against the high-water mark rather than dtcm_mempool: deinit and
    // restore move dtcm_mempool back, which would misclassify still-live
    // DTCM pointers as heap pointers. DTCM is a bump allocator; no free.
    if (p >= (uintptr_t)dtcm_mempool_start && p < (uintptr_t)dtcm_mempool_hwm)
    {
        return;
    }
    // pockets live BELOW dtcm_mempool_start; bump-allocated, never freed
    for (int i = 0; i < dtcm_num_pockets; i++)
    {
        if (p >= (uintptr_t)dtcm_pockets[i].start && p < (uintptr_t)dtcm_pockets[i].end)
        {
            return;
        }
    }
#endif
    void* original_ptr = ((void**)ptr)[-1];
    cb_free(original_ptr);
}

#define DTCM_PROBE_CANARY 0xD704BEEF
#define DTCM_PROBE_CLEAN 0xA5A5A5A5
#define DTCM_PROBE_STEP 256
#define DTCM_PROBE_MIN_ADDR ((void*)0x20000100)

#ifdef TARGET_SIMULATOR
__attribute__((noinline))
#else
__attribute__((optimize("O0"), noinline))
#endif
void dtcm_probe_lower_bound(void)
{
    if (!is_dtcm_init || !dtcm_mempool_start)
        return;

    playdate->system->logToConsole("DTCM probe: starting downward from %p", dtcm_mempool_start);

    uintptr_t probe = (uintptr_t)dtcm_mempool_start;
    uintptr_t lowest_ok = probe;

    // clean run tracking - top DTCM_MAX_POCKETS runs
    uintptr_t run_starts[DTCM_MAX_POCKETS] = {0};
    unsigned run_sizes[DTCM_MAX_POCKETS] = {0};

    uintptr_t cur_run_start = 0;
    unsigned cur_run = 0;

    unsigned blocks_scanned = 0;
    unsigned blocks_clean = 0;
    unsigned blocks_dirty = 0;

#define INSERT_RUN(start, size)                                    \
    do                                                             \
    {                                                              \
        for (int _i = 0; _i < DTCM_MAX_POCKETS; _i++)              \
        {                                                          \
            if (size > run_sizes[_i])                              \
            {                                                      \
                for (int _j = DTCM_MAX_POCKETS - 1; _j > _i; _j--) \
                {                                                  \
                    run_starts[_j] = run_starts[_j - 1];           \
                    run_sizes[_j] = run_sizes[_j - 1];             \
                }                                                  \
                run_starts[_i] = start;                            \
                run_sizes[_i] = size;                              \
                break;                                             \
            }                                                      \
        }                                                          \
    } while (0)

    while (probe > (uintptr_t)DTCM_PROBE_MIN_ADDR)
    {
        probe -= DTCM_PROBE_STEP;

        volatile uint32_t* addr = (volatile uint32_t*)probe;

        uint32_t prev = *addr;

        *addr = DTCM_PROBE_CANARY;

        volatile uint32_t readback = *addr;
        if (readback != (uint32_t)DTCM_PROBE_CANARY)
        {
            *addr = prev;
            playdate->system->logToConsole(
                "DTCM probe: MISMATCH at %p (wrote %08x, read %08x) - stopping", (void*)probe,
                (unsigned)DTCM_PROBE_CANARY, (unsigned)readback
            );
            probe += DTCM_PROBE_STEP;
            break;
        }

        *addr = prev;
        lowest_ok = probe;

        // Full-block clean scan: every word of the 256-byte block must read
        // DTCM_PROBE_CLEAN. Single-word sampling could claim a block that used
        // data merely crosses into (blind pocket top), or extend a run past a
        // used word that coincidentally reads as DTCM_PROBE_CLEAN.
        bool block_clean = true;
        int w = 0;
        for (; w < DTCM_PROBE_STEP / 4; w++)
        {
            if (addr[w] != (volatile uint32_t)DTCM_PROBE_CLEAN)
            {
                block_clean = false;
                break;
            }
        }

        blocks_scanned++;
        if (block_clean)
            blocks_clean++;
        else
            blocks_dirty++;

#if DTCM_DEBUG
        // Full per-block map, for diagnosing pocket layout changes on new
        // OS/revs. Summary counts above are always logged.
        if (!block_clean)
        {
            playdate->system->logToConsole(
                "DTCM probe: dirty block %p: first dirty word at %p = %08x", (void*)probe,
                (void*)&addr[w], (unsigned)addr[w]
            );
        }
#endif

        if (block_clean)
        {
            if (cur_run == 0)
                cur_run_start = probe;
            cur_run++;
        }
        else
        {
            if (cur_run > 0)
            {
                INSERT_RUN(cur_run_start, cur_run);
                cur_run = 0;
            }
        }
    }

    if (cur_run > 0)
        INSERT_RUN(cur_run_start, cur_run);

#undef INSERT_RUN

    dtcm_probe_lowest = (void*)lowest_ok;

    playdate->system->logToConsole(
        "DTCM probe: scanned %u blocks: %u clean, %u dirty", blocks_scanned, blocks_clean,
        blocks_dirty
    );

    playdate->system->logToConsole(
        "DTCM probe: lowest writable = %p (%u bytes below pool)", (void*)lowest_ok,
        (unsigned)((uintptr_t)dtcm_mempool_start - lowest_ok)
    );

    // init pockets from top clean runs
    dtcm_num_pockets = 0;
    for (int i = 0; i < DTCM_MAX_POCKETS && run_sizes[i] > 0; i++)
    {
        // Guard band: trim one probe step off the pocket top. The pocket
        // bottom stays at the lowest verified-clean word (already exact).
        if (run_sizes[i] < 2)
        {
            playdate->system->logToConsole(
                "DTCM probe: run at %p too small after top guard trim, skipping",
                (void*)run_starts[i]
            );
            continue;
        }
        size_t size = (run_sizes[i] - 1) * DTCM_PROBE_STEP;
        void* low = (void*)(run_starts[i] - (run_sizes[i] - 1) * DTCM_PROBE_STEP);
        dtcm_pocket_init(&dtcm_pockets[i], low, size);
        dtcm_num_pockets = i + 1;
        playdate->system->logToConsole("DTCM pocket[%d] at %p (%u bytes)", i, low, (unsigned)size);
    }
    if (dtcm_num_pockets == 0)
        playdate->system->logToConsole("DTCM probe: no clean pockets found");
}
