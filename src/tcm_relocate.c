//
//  tcm_relocate.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "tcm_relocate.h"

#include "dtcm.h"         // IWYU pragma: keep
#include "preferences.h"  // IWYU pragma: keep
#include "utility.h"      // IWYU pragma: keep

#include <pd_api.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Offset of the relocated draw cluster (0 = run from flash).
// Set by tcm_relocate on RevA; always 0 elsewhere.
intptr_t pgb_draw_reloc_offset = 0;
// Same for the rare and HLE clusters.
intptr_t pgb_rare_reloc_offset = 0;
intptr_t pgb_hle_reloc_offset = 0;
// APU cluster relocation offsets (untemplated; 0 = flash copy, also correct).
intptr_t pgb_apu_write_reloc_offset = 0;
intptr_t pgb_apu_sample_gen_reloc_offset = 0;

#if ITCM_CORE
// Re-entry guards; 0 = running from flash. core_itcm_offset/_batch are the
// runtime deltas consumed by the emulator core (see peanut_gb.h).
static void* core_itcm_reloc = NULL;
static void* core_itcm_reloc_batch = NULL;
intptr_t core_itcm_offset = 0;
intptr_t core_itcm_offset_batch = 0;

// DTCM snapshot of the main pool (gb struct) taken on lock/menu, restored on
// resume so system writes into DTCM can't corrupt emulator state.
static struct dtcm_store_t* s_tcm_store = NULL;

extern char __itcm_dmg_start[];
extern char __itcm_dmg_end[];
extern char __itcm_dmg_a_end[];
extern char __itcm_dmg_batch_start[];
extern char __itcm_dmg_batch_end[];
extern char __itcm_cgb_start[];
extern char __itcm_cgb_end[];
extern char __itcm_cgb_a_end[];
extern char __itcm_cgb_batch_start[];
extern char __itcm_cgb_batch_end[];
extern char __draw_dmg_start[];
extern char __draw_dmg_end[];
extern char __draw_cgb_start[];
extern char __draw_cgb_end[];
extern char __rare_dmg_start[];
extern char __rare_dmg_end[];
extern char __rare_cgb_start[];
extern char __rare_cgb_end[];
extern char __hle_cgb_start[];
extern char __hle_cgb_end[];
extern char __apu_write_start[];
extern char __apu_write_end[];
extern char __apu_sample_gen_start[];
extern char __apu_sample_gen_end[];

#define MARGIN 4
#define DTCM_ALIGN_PAD 31
#define TCM_NCLUSTERS 5

typedef struct
{
    const char* name;
    char* start;
    char* end;
    intptr_t* offset;
    bool cgb_only;
    bool needs_hle_pref;
    bool is_draw;
} tcm_cluster_t;

// Simulate an aligned pocket allocation (mirrors dtcm_pocket_alloc_aligned).
// Returns the post-allocation brk, or 0 if it does not fit.
static uintptr_t tcm_pocket_fit(uintptr_t sim, int p, uintptr_t align, size_t size)
{
    uintptr_t m = sim;
    align %= 32;
    while (m % 32 != align)
        m++;
    m += size;
    return (m > (uintptr_t)dtcm_pockets[p].end) ? 0 : m;
}

// Backtracking search for an all-pocket assignment: clusters in priority
// order, pockets in ascending-remaining-slack order (best-fit first). Found
// only when the greedy pass left something in pool/flash; never reorders
// clusters, so priority is preserved.
static bool tcm_pack_dfs(
    const tcm_cluster_t* clusters, int n, int c, bool cgb, uintptr_t* sim, int* decisions
)
{
    if (c == n)
        return true;

    const tcm_cluster_t* e = &clusters[c];
    if ((e->cgb_only && !cgb) || (e->needs_hle_pref && preferences_hle != 1) || e->end == e->start)
    {
        return tcm_pack_dfs(clusters, n, c + 1, cgb, sim, decisions);
    }

    const size_t size = (size_t)(e->end - e->start);
    const size_t need = size + MARGIN;

    int order[DTCM_MAX_POCKETS];
    int cnt = 0;
    for (int i = 0; i < dtcm_num_pockets; i++)
        if (dtcm_pocket_enabled(i))
            order[cnt++] = i;

    // insertion sort by ascending remaining slack (best-fit first)
    for (int a = 1; a < cnt; a++)
        for (int b = a; b > 0; b--)
        {
            const uintptr_t sa = (uintptr_t)dtcm_pockets[order[b]].end - sim[order[b]];
            const uintptr_t sb = (uintptr_t)dtcm_pockets[order[b - 1]].end - sim[order[b - 1]];
            if (sa < sb)
            {
                int t = order[b];
                order[b] = order[b - 1];
                order[b - 1] = t;
            }
            else
                break;
        }

    for (int oi = 0; oi < cnt; oi++)
    {
        const int p = order[oi];
        const uintptr_t brk = tcm_pocket_fit(sim[p], p, (uintptr_t)e->start, need);
        if (!brk)
            continue;
        const uintptr_t save = sim[p];
        sim[p] = brk;
        decisions[c] = p;
        if (tcm_pack_dfs(clusters, n, c + 1, cgb, sim, decisions))
            return true;
        sim[p] = save;  // backtrack: undo cluster c from pocket p
    }
    decisions[c] = -1;
    return false;
}

__section__(".rare") void tcm_relocate(bool cgb)
{
    // Restore the main-pool snapshot (gb struct) taken on lock/menu, if any.
    if (s_tcm_store)
    {
        dtcm_restore(s_tcm_store);
        s_tcm_store = NULL;
    }

    void* itcm_start = cgb ? &__itcm_cgb_start : &__itcm_dmg_start;
    void* itcm_a_end = cgb ? &__itcm_cgb_a_end : &__itcm_dmg_a_end;
    void* itcm_batch_start = cgb ? &__itcm_cgb_batch_start : &__itcm_dmg_batch_start;
    void* itcm_batch_end = cgb ? &__itcm_cgb_batch_end : &__itcm_dmg_batch_end;

    // Core is split into two independently-relocatable blocks:
    // A (hot: read/write helpers + cb + micro) and batch (batch-level:
    // step_cpu/run_frame). Splitting lets the blocks fit smaller pockets
    // and allows batch to fall back to flash when space runs out.
    uintptr_t core_size = itcm_a_end - itcm_start;
    uintptr_t core_batch_size = itcm_batch_end - itcm_batch_start;

    // DTCM relocation controlled by the TCM Mode preference (default on).
    // Manual escape hatch if a device/rev misbehaves.
    if (!dtcm_enabled() || preferences_itcm == 0)
    {
        // just use original non-relocated code
        core_itcm_reloc = itcm_start;
        core_itcm_offset = 0;
        core_itcm_reloc_batch = itcm_batch_start;
        core_itcm_offset_batch = 0;

        playdate->system->logToConsole("itcm[%s]: off - running from flash", cgb ? "cgb" : "dmg");
        return;
    }

    if (core_itcm_reloc == (void*)&__itcm_dmg_start)
    {
        core_itcm_reloc = NULL;
        core_itcm_reloc_batch = NULL;
    }

    if (core_itcm_reloc == (void*)&__itcm_cgb_start)
    {
        core_itcm_reloc = NULL;
        core_itcm_reloc_batch = NULL;
    }

    if (core_itcm_reloc != NULL)
        return;

    // probe for clean DTCM pockets (needed for core and/or draw placement)
    dtcm_probe_lower_bound();

    bool core_in_main_pool = false;
    int best = -1;
    int best_batch = -1;
    const char* batch_where = "flash";

    // choose the pocket with the most slack
    size_t best_slack = 0;

    for (int i = 0; i < dtcm_num_pockets; i++)
    {
        if (!dtcm_pocket_enabled(i))
            continue;
        size_t avail = (uintptr_t)dtcm_pockets[i].end - (uintptr_t)dtcm_pockets[i].start;
        if (avail >= core_size + MARGIN + DTCM_ALIGN_PAD)
        {
            size_t slack = avail - (core_size + MARGIN + DTCM_ALIGN_PAD);
            if (best == -1 || slack > best_slack)
            {
                best = i;
                best_slack = slack;
            }
        }
    }

    if (best >= 0)
        core_itcm_reloc =
            dtcm_pocket_alloc_aligned(best, core_size + MARGIN, (uintptr_t)itcm_start);
    else
    {
        // No pocket fits: main pool (budget-enforced); if even that
        // refuses, core runs from flash (slow but safe).
        core_itcm_reloc = dtcm_alloc_aligned(core_size + MARGIN, (uintptr_t)itcm_start);
        core_in_main_pool = (core_itcm_reloc != NULL);
    }

    if (core_itcm_reloc)
    {
        DTCM_VERIFY();
        memcpy(core_itcm_reloc, (void*)itcm_start, core_size);
        DTCM_VERIFY();
        core_itcm_offset = core_itcm_reloc - itcm_start;
    }
    else
    {
        core_itcm_reloc = (void*)itcm_start;
        core_itcm_offset = 0;
        playdate->system->logToConsole("dtcm pool full: core runs from flash");
    }

    // B block: A's pocket slack first, then any pocket with room, then
    // the main pool, else flash (offset 0, correct).
    void* batch_reloc = NULL;
    const size_t b_need = core_batch_size + MARGIN + DTCM_ALIGN_PAD;
    for (int i = 0; i < dtcm_num_pockets && !batch_reloc; i++)
    {
        int p = (best >= 0) ? (best + i) % dtcm_num_pockets : i;
        if (!dtcm_pocket_enabled(p))
            continue;
        size_t avail = (uintptr_t)dtcm_pockets[p].end - (uintptr_t)dtcm_pockets[p].mempool;
        if (avail >= b_need)
        {
            batch_reloc =
                dtcm_pocket_alloc_aligned(p, core_batch_size + MARGIN, (uintptr_t)itcm_batch_start);
            batch_where = "pocket";
            best_batch = p;
        }
    }
    if (!batch_reloc)
    {
        batch_reloc = dtcm_alloc_aligned(core_batch_size + MARGIN, (uintptr_t)itcm_batch_start);
        if (batch_reloc)
            batch_where = "main pool";
    }
    if (batch_reloc)
    {
        DTCM_VERIFY();
        memcpy(batch_reloc, itcm_batch_start, core_batch_size);
        DTCM_VERIFY();
        core_itcm_reloc_batch = batch_reloc;
        core_itcm_offset_batch = (char*)batch_reloc - (char*)itcm_batch_start;
    }
    else
    {
        // Tolerated: B is per-batch code, flash wait states cost ~1-2%.
        core_itcm_reloc_batch = itcm_batch_start;
        core_itcm_offset_batch = 0;
    }

    // Unified placement log: itcm[<mode>]: <cluster> <size> at <addr> (<where>)
    if (best >= 0)
        playdate->system->logToConsole(
            "itcm[%s]: core 0x%X bytes at %p (pocket[%d])", cgb ? "cgb" : "dmg", core_size,
            core_itcm_reloc, best
        );
    else
        playdate->system->logToConsole(
            "itcm[%s]: core 0x%X bytes at %p (%s)", cgb ? "cgb" : "dmg", core_size, core_itcm_reloc,
            core_in_main_pool ? "main pool" : "flash"
        );
    if (best_batch >= 0)
        playdate->system->logToConsole(
            "itcm[%s]: batch 0x%X bytes at %p (pocket[%d])", cgb ? "cgb" : "dmg", core_batch_size,
            core_itcm_reloc_batch, best_batch
        );
    else
        playdate->system->logToConsole(
            "itcm[%s]: batch 0x%X bytes at %p (%s)", cgb ? "cgb" : "dmg", core_batch_size,
            core_itcm_reloc_batch, batch_where
        );

    // Placement priority: core > batch > hle > apu_write > draw > rare >
    // apu_sample_gen. Each: pockets (share used pockets' slack, then best-fit
    // fresh) -> main pool (budget-bounded, multi-claim) -> flash. If greedy
    // leaves a cluster in pool/flash but pockets can hold everything, a DFS
    // repack finds the all-pocket arrangement (priority order preserved).
    // hle: CGB only (self-skips on DMG) and needs the HLE pref on.
    // apu_write outranks draw/rare: PCM voice streaming hammers the write
    // path, and draw is cheaper than the HLE/APU write clusters in practice.
    pgb_rare_reloc_offset = 0;
    pgb_hle_reloc_offset = 0;
    pgb_apu_write_reloc_offset = 0;
    pgb_apu_sample_gen_reloc_offset = 0;
    pgb_draw_reloc_offset = 0;

    int draw_pocket = -1;

    const tcm_cluster_t clusters[] = {
        {"hle", __hle_cgb_start, __hle_cgb_end, &pgb_hle_reloc_offset, true, true, false},
        {"apu_write", __apu_write_start, __apu_write_end, &pgb_apu_write_reloc_offset, false, false,
         false},
        {"draw", cgb ? (char*)__draw_cgb_start : (char*)__draw_dmg_start,
         cgb ? (char*)__draw_cgb_end : (char*)__draw_dmg_end, &pgb_draw_reloc_offset, false, false,
         true},
        {"rare", cgb ? __rare_cgb_start : __rare_dmg_start, cgb ? __rare_cgb_end : __rare_dmg_end,
         &pgb_rare_reloc_offset, false, false, false},
        {"apu_sample_gen", __apu_sample_gen_start, __apu_sample_gen_end,
         &pgb_apu_sample_gen_reloc_offset, false, false, false},
    };

    // Snapshot pocket brks: the search phases mutate only sim, the real
    // pockets stay untouched until the final apply.
    void* saved_brk[DTCM_MAX_POCKETS];
    for (int i = 0; i < dtcm_num_pockets; i++)
        saved_brk[i] = dtcm_pockets[i].mempool;

    uintptr_t sim[DTCM_MAX_POCKETS];
    for (int i = 0; i < dtcm_num_pockets; i++)
        sim[i] = (uintptr_t)dtcm_pockets[i].mempool;

    int decisions[TCM_NCLUSTERS];
    bool all_pockets = true;

    // Greedy pass: priority order, share used pockets then best-fit.
    for (int c = 0; c < TCM_NCLUSTERS; c++)
    {
        decisions[c] = -1;
        const tcm_cluster_t* e = &clusters[c];
        if ((e->cgb_only && !cgb) || (e->needs_hle_pref && preferences_hle != 1) ||
            e->end == e->start)
            continue;

        const size_t size = (size_t)(e->end - e->start);
        int pick = -1;

        // Pass 1: share an already-used pocket (core's, then draw's).
        const int shared[2] = {best, draw_pocket};
        for (int s = 0; s < 2 && pick < 0; s++)
        {
            const int p = shared[s];
            if (p < 0 || (s == 1 && p == shared[0]) || !dtcm_pocket_enabled(p))
                continue;
            if (tcm_pocket_fit(sim[p], p, (uintptr_t)e->start, size + MARGIN))
                pick = p;
        }

        // Pass 2: best-fit over the remaining pockets.
        if (pick < 0)
        {
            size_t best_fit = SIZE_MAX;
            for (int i = 0; i < dtcm_num_pockets; i++)
            {
                if (i == best || i == draw_pocket || !dtcm_pocket_enabled(i))
                    continue;
                const uintptr_t brk = tcm_pocket_fit(sim[i], i, (uintptr_t)e->start, size + MARGIN);
                if (!brk)
                    continue;
                const size_t slack = (uintptr_t)dtcm_pockets[i].end - brk;
                if (slack < best_fit)
                {
                    best_fit = slack;
                    pick = i;
                }
            }
        }

        if (pick >= 0)
        {
            sim[pick] = tcm_pocket_fit(sim[pick], pick, (uintptr_t)e->start, size + MARGIN);
            decisions[c] = pick;
            if (e->is_draw)
                draw_pocket = pick;
        }
        else
        {
            all_pockets = false;
        }
    }

    // Repack pass: only when greedy fell back, try for an all-pocket layout.
    if (!all_pockets)
    {
        uintptr_t repack_sim[DTCM_MAX_POCKETS];
        for (int i = 0; i < dtcm_num_pockets; i++)
            repack_sim[i] = (uintptr_t)saved_brk[i];
        int repack_decisions[TCM_NCLUSTERS];
        for (int c = 0; c < TCM_NCLUSTERS; c++)
            repack_decisions[c] = -1;

        if (tcm_pack_dfs(clusters, TCM_NCLUSTERS, 0, cgb, repack_sim, repack_decisions))
        {
            for (int c = 0; c < TCM_NCLUSTERS; c++)
                decisions[c] = repack_decisions[c];
            playdate->system->logToConsole("itcm: repacked clusters into pockets");
        }
    }

    // Apply the chosen decisions.
    for (int c = 0; c < TCM_NCLUSTERS; c++)
    {
        const tcm_cluster_t* e = &clusters[c];
        if ((e->cgb_only && !cgb) || (e->needs_hle_pref && preferences_hle != 1) ||
            e->end == e->start)
            continue;

        const size_t size = (size_t)(e->end - e->start);
        const int pick = decisions[c];
        void* reloc = NULL;
        const char* where = "flash";
        if (pick >= 0)
        {
            reloc = dtcm_pocket_alloc_aligned(pick, size + MARGIN, (uintptr_t)e->start);
            where = "pocket";
        }
        else
        {
            reloc = dtcm_alloc_aligned(size + MARGIN, (uintptr_t)e->start);
            if (reloc)
                where = "main pool";
        }

        if (reloc)
        {
            DTCM_VERIFY();
            memcpy(reloc, e->start, size);
            DTCM_VERIFY();
            *e->offset = (char*)reloc - e->start;
        }

        playdate->system->logToConsole(
            "itcm[%s]: %s 0x%X bytes at %p (%s%d)", cgb ? "cgb" : "dmg", e->name, (unsigned)size,
            reloc ? reloc : (void*)e->start, where, pick >= 0 ? pick : -1
        );
    }

    {
        unsigned used = (unsigned)((uintptr_t)dtcm_mempool - (uintptr_t)dtcm_mempool_start);
        playdate->system->logToConsole(
            "DTCM pool: used %uB, free %uB (limit %uB)", used, (unsigned)DTCM_POOL_LIMIT - used,
            (unsigned)DTCM_POOL_LIMIT
        );
    }

    playdate->system->clearICache();
}

// Clear TCM on lock/menu: system may write into pockets while locked, so
// switch code back to flash and return pockets to boot-idle 0xA5.
// tcm_relocate() re-probes and re-places on unlock/resume (re-entry guard
// re-armed by setting core_itcm_reloc to the flash start below).
//
// pool_keep_end: lowest main-pool address that must survive (the gb struct);
// space above it that held fallback core/draw copies is released so repeated
// lock/unlock cycles don't leak the pool toward the stack canary.
__section__(".rare") void tcm_clear(bool cgb, void* pool_keep_end)
{
    if (!dtcm_enabled() || preferences_itcm == 0)
        return;

    void* itcm_start = cgb ? (void*)&__itcm_cgb_start : (void*)&__itcm_dmg_start;

    core_itcm_offset = 0;
    core_itcm_offset_batch = 0;
    pgb_draw_reloc_offset = 0;
    pgb_rare_reloc_offset = 0;
    pgb_hle_reloc_offset = 0;
    pgb_apu_write_reloc_offset = 0;
    pgb_apu_sample_gen_reloc_offset = 0;
    core_itcm_reloc = itcm_start;
    core_itcm_reloc_batch = cgb ? (void*)&__itcm_cgb_batch_start : (void*)&__itcm_dmg_batch_start;

    dtcm_pocket_fill_and_reset();

    if (pool_keep_end)
        dtcm_pool_release_above(pool_keep_end);

    // Snapshot the main pool (gb struct) so system writes into DTCM while
    // locked/menu open can't corrupt emulator state; tcm_relocate restores.
    if (!s_tcm_store)
        s_tcm_store = dtcm_store();

    playdate->system->clearICache();
}

// Apply the current TCM Mode preference live (settings close): relocate core/
// draw into TCM if enabled, else switch back to flash and empty pockets.
__section__(".rare") void tcm_apply(bool cgb)
{
    if (!dtcm_enabled())
        return;

    if (preferences_itcm == 0)
    {
        void* itcm_start = cgb ? (void*)&__itcm_cgb_start : (void*)&__itcm_dmg_start;
        core_itcm_offset = 0;
        core_itcm_offset_batch = 0;
        pgb_draw_reloc_offset = 0;
        pgb_rare_reloc_offset = 0;
        pgb_hle_reloc_offset = 0;
        pgb_apu_write_reloc_offset = 0;
        pgb_apu_sample_gen_reloc_offset = 0;
        core_itcm_reloc = itcm_start;
        core_itcm_reloc_batch =
            cgb ? (void*)&__itcm_cgb_batch_start : (void*)&__itcm_dmg_batch_start;
        dtcm_pocket_fill_and_reset();
        playdate->system->clearICache();
    }
    else
    {
        tcm_relocate(cgb);
    }
}

// Reset relocation state on scene init.
void tcm_reset(void)
{
    core_itcm_reloc = NULL;
    core_itcm_reloc_batch = NULL;
    core_itcm_offset = 0;
    core_itcm_offset_batch = 0;
    pgb_draw_reloc_offset = 0;
    pgb_rare_reloc_offset = 0;
    pgb_hle_reloc_offset = 0;
    pgb_apu_write_reloc_offset = 0;
    pgb_apu_sample_gen_reloc_offset = 0;
    s_tcm_store = NULL;
}

// Reset relocation state on scene deinit, freeing any outstanding snapshot.
void tcm_deinit(void)
{
    core_itcm_reloc = NULL;
    core_itcm_reloc_batch = NULL;
    core_itcm_offset = 0;
    core_itcm_offset_batch = 0;
    pgb_draw_reloc_offset = 0;
    pgb_rare_reloc_offset = 0;
    pgb_hle_reloc_offset = 0;
    pgb_apu_write_reloc_offset = 0;
    pgb_apu_sample_gen_reloc_offset = 0;
    // Free any outstanding lock snapshot (pool is about to be deinited).
    cb_free(s_tcm_store);
    s_tcm_store = NULL;
}

#else

void tcm_relocate(bool cgb)
{
}

void tcm_clear(bool cgb, void* pool_keep_end)
{
    (void)cgb;
    (void)pool_keep_end;
}

void tcm_apply(bool cgb)
{
    (void)cgb;
}

void tcm_reset(void)
{
}

void tcm_deinit(void)
{
}
#endif
