//
//  game_scene.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "../gbz.h"
#include "../preferences.h"

#include <pd_api.h>
#include <pd_api/pd_api_gfx.h>
#include <stdbool.h>

unsigned game_picture_x_offset;
unsigned game_picture_y_top;
unsigned game_picture_y_bottom;
unsigned game_picture_scaling;
LCDColor game_picture_background_color;
bool game_hide_indicator;
bool game_invert_indicator;
bool gbScreenRequiresFullRefresh;

// Default screen layout (no script active). Restored on scene init and when
// a script is disabled mid-session, since scripts set these every frame.
static void CB_GameScene_reset_screen_defaults(void);

#define PGB_IMPL
/* clang-format off */
#include "game_scene.h"
/* clang-format on */

#include "../../libs/peanut_gb.h"
#include "../app.h"
#include "../dtcm.h"
#include "../preferences.h"
#include "../script.h"
#include "../softpatch.h"
#include "../userstack.h"
#include "../utility.h"
#include "credits_scene.h"
#include "info_scene.h"
#include "modal.h"
#include "settings_scene.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Definitions for extern globals declared in peanut_gb.h */
uint8_t* pgb_dirty_prev = NULL;
uint16_t* pgb_dirty_flags = NULL;
uint8_t pgb_dirty_skip = 0;

// The maximum Playdate screen lines that can be updated (seems to be 208).
#define PLAYDATE_LINE_COUNT_MAX 208

// Upper bound on the number of audio samples we generate per frame.
#define MAX_AUDIO_SAMPLES_PER_CHUNK ((44100 / 60) * 4)

// --- Adaptive Frame Skip Parameters ---

// Activate 30fps when smoothed FPS drops below this fraction of 60fps target.
#define ADAPTIVE_FS_ACTIVATE_RATIO 0.95f

// Deactivation ratio for probe frames (raw single-frame time vs 60fps).
#define ADAPTIVE_FS_DEACTIVATE_RATIO 0.97f

// Minimum frames 30fps stays on after activation.
#define ADAPTIVE_FS_LOCK_FRAMES 15

// Consecutive slow frames required before activating 30fps.
#define ADAPTIVE_FS_ACTIVATE_FRAMES 5

// Frames between probe attempts when running at 30fps.
#define ADAPTIVE_FS_PROBE_INTERVAL 60

#define MENU_QUICK_PRESS_THRESHOLD_MS 1200

CB_GameScene* audioGameScene = NULL;

volatile int g_audio_resync_requested = 0;

/* Frames to ignore further resync requests after handling one, so a single
 * reset is guaranteed time to refill the ring before another can trigger.
 * Breaks reset -> starve -> reset oscillation (e.g. after sleep/resume). */
static int s_resync_cooldown = 0;

void CB_reset_audio_sync_state(void)
{
    atomic_store(&g_audio_sync_buffer.read_pos, 0);
    atomic_store(&g_audio_sync_buffer.write_pos, 0);
    atomic_store(&g_samples_generated_total, playdate->sound->getCurrentTime());
    memset(g_audio_sync_buffer.left, 0, sizeof(g_audio_sync_buffer.left));
    memset(g_audio_sync_buffer.right, 0, sizeof(g_audio_sync_buffer.right));
    g_audio_resync_requested = 0;
}

/* Render accurate-mode audio straight into the ring buffer at write_pos
 * (wrap-split), avoiding a temp buffer and a copy. The rendered region is
 * unpublished until the final write_pos add, so the audio callback thread
 * never reads it mid-render; lead accounting guarantees we never overwrite
 * unconsumed samples. Callers are accurate-mode only. */
static void generate_audio_chunk(CB_GameScene* gameScene, int samples_to_generate)
{
    if (samples_to_generate <= 0)
        return;

    if (samples_to_generate > AUDIO_RING_BUFFER_SIZE)
    {
        playdate->system->logToConsole(
            "Audio chunk request %d exceeds ring capacity; clamping.", samples_to_generate
        );
        samples_to_generate = AUDIO_RING_BUFFER_SIZE;
    }

    audio_data* audio = &gameScene->context->gb->audio;

    uint32_t write_pos_local = atomic_load(&g_audio_sync_buffer.write_pos);
    uint32_t start = write_pos_local & AUDIO_RING_BUFFER_MASK;
    uint32_t first = AUDIO_RING_BUFFER_SIZE - start;
    if (first > (uint32_t)samples_to_generate)
        first = (uint32_t)samples_to_generate;

    int16_t* dst_left = &g_audio_sync_buffer.left[start];
    int16_t* dst_right = gameScene->is_stereo ? &g_audio_sync_buffer.right[start] : dst_left;

    // Renderers accumulate into the destination: zero the spans first.
    memset(dst_left, 0, first * sizeof(int16_t));
    if (gameScene->is_stereo)
        memset(dst_right, 0, first * sizeof(int16_t));
    audio_generate_accurate(audio, dst_left, dst_right, first);

    uint32_t rest = (uint32_t)samples_to_generate - first;
    if (rest)
    {
        memset(g_audio_sync_buffer.left, 0, rest * sizeof(int16_t));
        if (gameScene->is_stereo)
            memset(g_audio_sync_buffer.right, 0, rest * sizeof(int16_t));
        audio_generate_accurate(
            audio, g_audio_sync_buffer.left,
            gameScene->is_stereo ? g_audio_sync_buffer.right : g_audio_sync_buffer.left, rest
        );
    }

    atomic_fetch_add(&g_audio_sync_buffer.write_pos, samples_to_generate);
}

static void tick_audio_sync(CB_GameScene* gameScene)
{
    if (!gameScene || gameScene->audioLocked)
        return;

    if (!gameScene->audioEnabled || preferences_sound_mode != 2)
    {
        return;
    }

    // Underrun reported by the audio callback: underrun silence counts as
    // played time in the lead accounting, so without a rebaseline the
    // generator believes it is ahead while the ring starves (wedge).
    // Reset positions + baseline, then fall through and regenerate a
    // full lead immediately. Cooldown ensures one reset completes its
    // refill before another can trigger.
    if (g_audio_resync_requested)
    {
        g_audio_resync_requested = 0;
        if (s_resync_cooldown <= 0)
        {
            CB_reset_audio_sync_state();
            s_resync_cooldown = 10;
        }
    }
    if (s_resync_cooldown > 0)
        s_resync_cooldown--;

    uint32_t samples_played = playdate->sound->getCurrentTime();
    uint32_t samples_generated = atomic_load(&g_samples_generated_total);

    // Target having a buffer of ~4 frames of audio (at 60fps)
    uint32_t target_lead_samples = (44100 / 60) * 4;
    uint32_t target_sample_count = samples_played + target_lead_samples;

    int samples_to_generate = 0;
    if (target_sample_count > samples_generated)
    {
        samples_to_generate = target_sample_count - samples_generated;
    }

    int max_gen_this_frame = MAX_AUDIO_SAMPLES_PER_CHUNK;
    if (samples_to_generate > max_gen_this_frame)
    {
        samples_to_generate = max_gen_this_frame;
    }

    if (samples_to_generate > 0)
    {
        uint32_t write_pos = atomic_load(&g_audio_sync_buffer.write_pos);
        uint32_t read_pos = atomic_load(&g_audio_sync_buffer.read_pos);
        uint32_t available_space = AUDIO_RING_BUFFER_SIZE - (write_pos - read_pos);

        if ((uint32_t)samples_to_generate <= available_space)
        {
            generate_audio_chunk(gameScene, samples_to_generate);
            atomic_fetch_add(&g_samples_generated_total, samples_to_generate);
        }
    }
}

static void CB_GameScene_selector_init(CB_GameScene* gameScene);
static void CB_GameScene_update(void* object, uint32_t u32enc_dt);
static void CB_GameScene_menu(void* object);
static void CB_GameScene_generateBitmask(void);
static void CB_GameScene_free(void* object);
static void CB_GameScene_event(void* object, PDSystemEvent event, uint32_t arg);
static bool CB_GameScene_lock(void* object);
static void screen_fade(CB_GameScene* gameScene, int frame_advance);

static uint8_t* read_rom_to_ram(
    const char* filename, CB_GameSceneError* sceneError, size_t* o_rom_size
);

// returns 0 if no pre-existing save data;
// returns 1 if data found and loaded, but not RTC
// returns 2 if data and RTC loaded
// returns -1 on error
static int read_cart_ram_file(const char* save_filename, gb_s* gb, unsigned int* last_save_time);
static void write_cart_ram_file(const char* save_filename, gb_s* gb);

static void gb_error(gb_s* gb, const enum gb_error_e gb_err, const uint16_t val);
static void gb_save_to_disk(gb_s* gb);

static const char* startButtonText = "start";
static const char* selectButtonText = "select";

static int last_scy = -1;
static uint8_t CB_dither_lut_row0[256];
static uint8_t CB_dither_lut_row1[256];
uint8_t cgb_blend_stage = 0;
uint8_t cgb_gray_lum_min = 0;
uint8_t cgb_gray_lum_max = 93;
int8_t cgb_gray_bias = 0;

/* --- Usage histogram (Auto/Contrast gray modes) ---
 * Core render hooks count per-palette BG 4-pixel patterns and OBJ colors;
 * cgb_hist_build() expands them into a 64-bin luminance histogram
 * (bin = lum_sum >> 2). Hooks run while preferences_cgb_bias_auto != 0. */
bool cgb_hist_active;
bool cgb_contrast_active;

/* --- Auto gray mode ---
 * Scores the Dark/Neutral/Bright presets (bias -1/0/+1) against the frame's
 * luminance histogram and applies the best. Score = mid-shade mass: pixels
 * between the black and white thresholds survive as dithered grays, pixels
 * beyond them clip. Guards: flat scenes and naturally posterized (b/w)
 * content force Neutral. Hysteresis + hold-down prevent flicker. */
#define CGB_AUTO_FLAT_SPREAD 16     // P5..P95 lum spread below this -> Neutral
#define CGB_AUTO_BW_MID_PERCENT 15  // mid mass below this for all presets -> Neutral
#define CGB_AUTO_MARGIN_PERCENT 10  // relative mid-mass win required to switch
#define CGB_AUTO_HOLDDOWN 20        // frames between bias switches
static int8_t cgb_auto_bias;
static uint8_t cgb_auto_holddown;
static uint8_t cgb_auto_prev_enabled;
static uint8_t cgb_hist_phase;

/* Contrast mode: gray thresholds blend the frame's luminance percentiles
 * with the linear palette-range mapping (midpoint of P75/P50/P25 and the
 * linear Neutral thresholds): percentiles give detail in clustered scenes,
 * the linear anchor preserves the scene's brightness mood. The stage-2
 * blend pass offsets them by the blended spread/8. Flat or naturally
 * posterized (b/w) scenes fall back to linear Neutral over the palette
 * range (debounced). Per-threshold deadband prevents flicker. */
#define CGB_CONTRAST_FLAT_SPREAD 16  // P5..P95 lum spread below this -> fallback
#define CGB_CONTRAST_BW_POINT 80     // top-2 lum bins >= this % of pixels -> fallback
#define CGB_CONTRAST_DEBOUNCE 5      // consecutive evals required to flip the guard
#define CGB_CONTRAST_DEADBAND 4      // lum units a threshold must move to apply
uint16_t cgb_thresh[3] = {3, 2, 1};
uint16_t cgb_thresh_delta = 1;
static bool cgb_contrast_fallback;
static bool cgb_contrast_init_pending;
static uint8_t cgb_contrast_debounce;

static uint8_t* rom_pool = NULL;
static size_t rom_pool_size = 0;

const uint16_t CB_dither_lut_c0[] = {
    (0b1111 << 0) | (0b0111 << 4) | (0b0001 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),

    // L
    (0b1111 << 0) | (0b0111 << 4) | (0b0101 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),

    // D
    (0b1111 << 0) | (0b0101 << 4) | (0b0001 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),
};

const uint16_t CB_dither_lut_c1[] = {
    (0b1111 << 0) | (0b1101 << 4) | (0b0100 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b1111 << 4) | (0b0000 << 8) | (0b0000 << 12),

    // L
    (0b1111 << 0) | (0b1101 << 4) | (0b1010 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b1111 << 4) | (0b1010 << 8) | (0b0000 << 12),

    // D
    (0b1111 << 0) | (0b1010 << 4) | (0b0100 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b1010 << 4) | (0b0000 << 8) | (0b0000 << 12),
};

__section__(".rare") static void generate_dither_luts(void)
{
    uint32_t dither_lut = CB_dither_lut_c0[preferences_dither_pattern] |
                          ((uint32_t)CB_dither_lut_c1[preferences_dither_pattern] << 16);

    // Loop through all 256 possible values of a 4-pixel Game Boy byte.
    for (int orgpixels_int = 0; orgpixels_int < 256; ++orgpixels_int)
    {
        uint8_t orgpixels = (uint8_t)orgpixels_int;

        // --- Calculate dithered pattern for the first (top) row of pixels ---
        uint8_t pixels_temp_c0 = orgpixels;
        unsigned p0 = 0;
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i)
        {
            p0 <<= 2;
            unsigned c0h = dither_lut >> ((pixels_temp_c0 & 3) * 4);
            unsigned c0 = (c0h >> ((i * 2) % 4)) & 3;
            p0 |= c0;
            pixels_temp_c0 >>= 2;
        }
        CB_dither_lut_row0[orgpixels_int] = p0;

        // --- Calculate dithered pattern for the second (bottom) row of pixels ---
        uint8_t pixels_temp_c1 = orgpixels;
        unsigned p1 = 0;
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i)
        {
            p1 <<= 2;
            unsigned c1h = dither_lut >> (((pixels_temp_c1 & 3) * 4) + 16);
            unsigned c1 = (c1h >> ((i * 2) % 4)) & 3;
            p1 |= c1;
            pixels_temp_c1 >>= 2;
        }
        CB_dither_lut_row1[orgpixels_int] = p1;
    }
}

// forces screen refresh
bool game_menu_button_input_enabled;

__section__(".rare") static void cgb_hist_build(uint32_t hist[64])
{
    // Expand usage counts into a luminance histogram, clearing the counters
    // as they are consumed (replaces the per-frame reset). Palettes the
    // hooks did not touch are skipped via the used-bitmap.
    memset(hist, 0, 64 * sizeof(uint32_t));
    for (int pal = 0; pal < 8; pal++)
    {
        if (cgb_bg_used & (1 << pal))
        {
            uint16_t* u = cgb_bg_usage[pal];
            const uint32_t* pb = cgb_bg_patbin[pal];
            for (int raw = 0; raw < 256; raw++)
            {
                uint16_t n = u[raw];
                u[raw] = 0;
                if (n)
                {
                    uint32_t v = pb[raw];
                    hist[v & 63] += n;
                    hist[(v >> 6) & 63] += n;
                    hist[(v >> 12) & 63] += n;
                    hist[(v >> 18) & 63] += n;
                }
            }
        }
        for (int c = 0; c < 4; c++)
        {
            uint16_t n = cgb_obj_usage[pal][c];
            cgb_obj_usage[pal][c] = 0;
            if (n)
                hist[cgb_obj_lum_sum[pal][c] >> 2] += n;
        }
    }
    cgb_bg_used = 0;
}

// Auto: score each preset (Dark/Neutral/Bright) by how many pixels land in
// the dithered mid shades, then apply the winner with hysteresis.
__section__(".rare") static void cgb_auto_update(void)
{
    uint32_t hist[64];
    cgb_hist_build(hist);

    uint32_t total = 0;
    for (int i = 0; i < 64; i++)
        total += hist[i];
    if (total == 0)
        return;

    // P5..P95 spread, for the flat-scene guard.
    const uint32_t lo_cut = total / 20;
    const uint32_t hi_cut = total - lo_cut;
    uint32_t acc = 0;
    uint8_t lo = 0, hi = 0;
    bool lo_set = false;
    for (int i = 0; i < 64; i++)
    {
        acc += hist[i];
        if (!lo_set && acc >= lo_cut)
        {
            lo = (uint8_t)(i * 4);
            lo_set = true;
        }
        if (acc >= hi_cut)
        {
            hi = (uint8_t)(i * 4);
            break;
        }
    }
    const bool flat = ((uint8_t)(hi - lo) < CGB_AUTO_FLAT_SPREAD);

    // Mid-shade mass per preset: pixels between the stage-1 black threshold
    // (T3) and white threshold (T1) survive as dithered grays; the rest clip.
    const uint16_t base = cgb_gray_lum_min;
    uint16_t range = (uint16_t)cgb_gray_lum_max - base;
    if (range == 0)
        range = 1;
    uint32_t mid[3];
    bool any_usable = false;
    for (int k = 0; k < 3; k++)
    {
        const int b = k - 1;
        const uint16_t t1 = base + ((range * (6 - b)) >> 3);
        const uint16_t t3 = base + ((range * (2 - b)) >> 3);
        uint32_t m = 0;
        for (int i = 0; i < 64; i++)
        {
            const uint16_t lum = (uint16_t)(i * 4);
            if (lum < t1 && lum + 3 >= t3)
                m += hist[i];
        }
        mid[k] = m;
        if (m * 100 >= total * CGB_AUTO_BW_MID_PERCENT)
            any_usable = true;
    }

    const int cur = cgb_auto_bias + 1;
    int want;
    if (flat || !any_usable)
    {
        // Flat or naturally posterized (b/w) content: leave it alone.
        want = 1;  // Neutral
    }
    else
    {
        // Argmax mid mass; ties prefer the current bias, then Neutral.
        int order[3];
        order[0] = cur;
        order[1] = (cur == 1) ? 0 : 1;
        order[2] = (cur == 2) ? 0 : 2;
        want = order[0];
        if (mid[order[1]] > mid[want])
            want = order[1];
        if (mid[order[2]] > mid[want])
            want = order[2];
    }

    if (cgb_auto_holddown)
        cgb_auto_holddown--;

    if (want != cur && cgb_auto_holddown == 0 &&
        mid[want] > mid[cur] + (total * CGB_AUTO_MARGIN_PERCENT) / 100)
    {
        cgb_auto_bias = (int8_t)(want - 1);
        cgb_auto_holddown = CGB_AUTO_HOLDDOWN;
    }
}

// Palette luminance range over all 64 BG+OBJ colors. Reuses the core scan
// (temporarily un-skipping it); safe: lum_min/max are unused in Contrast.
__section__(".rare") static void cgb_palette_lum_range(gb_s* gb, uint8_t* omin, uint8_t* omax)
{
    const bool saved = cgb_contrast_active;
    cgb_contrast_active = false;
    __cgb_scan_luminance_range(gb);
    cgb_contrast_active = saved;
    *omin = cgb_gray_lum_min;
    *omax = cgb_gray_lum_max;
}

__section__(".rare") static void cgb_thresh_apply(
    uint16_t t1, uint16_t t2, uint16_t t3, uint16_t delta
)
{
    cgb_thresh[0] = t1;
    cgb_thresh[1] = t2;
    cgb_thresh[2] = t3;
    cgb_thresh_delta = delta;
    pgb_cgb_lut_dirty = true;
}

// Contrast: thresholds = P75/P50/P25 of the frame's luminance histogram, so
// each shade gets ~25% of the pixels (histogram equalization to 4 shades).
// Flat or naturally posterized content falls back to linear Neutral.
__section__(".rare") static void cgb_auto_contrast_update(gb_s* gb)
{
    uint32_t hist[64];
    cgb_hist_build(hist);

    uint32_t total = 0;
    for (int i = 0; i < 64; i++)
        total += hist[i];
    if (total == 0)
        return;

    // Percentiles P5/P25/P50/P75/P95 in one accumulation pass.
    const uint32_t cut[5] = {
        total * 5 / 100, total * 25 / 100, total * 50 / 100, total * 75 / 100, total * 95 / 100
    };
    uint8_t pct[5] = {0, 0, 0, 0, 0};
    uint32_t acc = 0;
    int ci = 0;
    for (int i = 0; i < 64 && ci < 5; i++)
    {
        acc += hist[i];
        while (ci < 5 && acc >= cut[ci])
            pct[ci++] = (uint8_t)(i * 4);
    }
    const uint16_t spread = (uint16_t)(pct[4] - pct[0]);

    // Fallback reference: linear Neutral thresholds over the palette range.
    uint8_t pal_min, pal_max;
    cgb_palette_lum_range(gb, &pal_min, &pal_max);
    uint16_t pal_range = (uint16_t)pal_max - pal_min;
    if (pal_range == 0)
        pal_range = 1;
    const uint16_t lin_t1 = pal_min + ((pal_range * 6) >> 3);
    const uint16_t lin_t2 = pal_min + ((pal_range * 4) >> 3);
    const uint16_t lin_t3 = pal_min + ((pal_range * 2) >> 3);

    // b/w guard: two-spike (posterized) content. Pure b/w has its pixels in
    // two luminance spikes; equalizing would wash them into two grays. A
    // dark or bright scene with real detail is a broad cluster and passes.
    uint32_t top1 = 0, top2 = 0;
    for (int i = 0; i < 64; i++)
    {
        if (hist[i] >= top1)
        {
            top2 = top1;
            top1 = hist[i];
        }
        else if (hist[i] > top2)
        {
            top2 = hist[i];
        }
    }
    const uint32_t top2_pct = (top1 + top2) * 100 / total;

    // Guard decision is stateless (single point); a debounce counter provides
    // the stability. A transient intrusion (dialog box, flash) can no longer
    // capture the guard the way a hysteresis band could.
    const bool want_fallback =
        (spread < CGB_CONTRAST_FLAT_SPREAD || top2_pct >= CGB_CONTRAST_BW_POINT);

    const bool was_fallback = cgb_contrast_fallback;
    if (cgb_contrast_init_pending)
    {
        // Mode entry: take the guard decision directly.
        cgb_contrast_fallback = want_fallback;
        cgb_contrast_init_pending = false;
        cgb_contrast_debounce = 0;
    }
    else if (want_fallback != cgb_contrast_fallback)
    {
        if (++cgb_contrast_debounce >= CGB_CONTRAST_DEBOUNCE)
        {
            cgb_contrast_fallback = want_fallback;
            cgb_contrast_debounce = 0;
        }
    }
    else
    {
        cgb_contrast_debounce = 0;
    }

    uint16_t t1, t2, t3, delta;
    if (cgb_contrast_fallback)
    {
        t1 = lin_t1;
        t2 = lin_t2;
        t3 = lin_t3;
        delta = pal_range >> 3;
    }
    else
    {
        // Hybrid: midpoint of equalized and linear-Neutral thresholds.
        // Pure equalization normalizes scene mood away (dark caves became
        // bright); pure linear loses detail in clustered scenes. The
        // midpoint keeps the mood while retaining half the detail gain.
        t1 = (uint16_t)((pct[3] + lin_t1) / 2);  // P75
        t2 = (uint16_t)((pct[2] + lin_t2) / 2);  // P50
        t3 = (uint16_t)((pct[1] + lin_t3) / 2);  // P25
        delta = (uint16_t)(((spread + pal_range) / 2) >> 3);
    }

    if (was_fallback != cgb_contrast_fallback)
    {
        // Guard flip or first evaluation: apply immediately, skip deadband.
        cgb_thresh_apply(t1, t2, t3, delta);
        return;
    }

    // Deadband per threshold; any accepted change triggers a LUT rebuild.
    const uint16_t nt[3] = {t1, t2, t3};
    bool changed = false;
    for (int k = 0; k < 3; k++)
    {
        int d = (int)nt[k] - (int)cgb_thresh[k];
        if (d < 0)
            d = -d;
        if (d >= CGB_CONTRAST_DEADBAND)
        {
            cgb_thresh[k] = nt[k];
            changed = true;
        }
    }
    if (changed)
    {
        cgb_thresh_delta = delta;
        pgb_cgb_lut_dirty = true;
    }
}
static uint8_t CB_bitmask[4][4][4];
static bool CB_GameScene_bitmask_done = false;

static PDMenuItem* audioMenuItem;
static PDMenuItem* fpsMenuItem;
static PDMenuItem* frameSkipMenuItem;
static PDMenuItem* buttonMenuItem = NULL;

static const char* buttonMenuOptions[4];

const char* quitGameOptions[3];

void reconfigure_audio_source(CB_GameScene* gameScene)
{
    if (!gameScene)
        return;

    int headphones;
    playdate->sound->getHeadphoneState(&headphones, NULL, CB_headphone_state_changed);

    bool use_stereo = (headphones || gameScene->is_mirroring) ? preferences_headphone_audio : 0;

    static bool sound_source_stereo;
    bool source_unchanged = CB_App->soundSource != NULL && sound_source_stereo == use_stereo;

    gameScene->is_stereo = use_stereo;

    bool was_muted = audio_muted;
    gameScene->audioEnabled = (preferences_sound_mode > 0) && playdate->system->getVolume() > 0.00f;
    audio_muted = !gameScene->audioEnabled;

    if (was_muted && !audio_muted && preferences_sound_mode == 2)
    {
        CB_reset_audio_sync_state();
    }

    playdate->system->logToConsole(
        "Reconfiguring audio. Muted: %s, Headphones: %s, Mirroring: %s, New mode: %s",
        (audio_muted ? "Yes" : "No"), (headphones ? "Yes" : "No"),
        (gameScene->is_mirroring ? "Yes" : "No"), (use_stereo ? "Stereo" : "Mono")
    );

    float volume = 0.0f;
    if (gameScene->audioEnabled)
    {
        volume = use_stereo ? 0.35f : 0.5f;
        if (preferences_sound_mode != 0 && gameScene->context->gb->is_cgb_mode)
            volume *= 1.5f;
    }
    playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), volume);

    audioGameScene = gameScene;

    if (!source_unchanged)
    {
        if (CB_App->soundSource != NULL)
        {
            playdate->sound->removeSource(CB_App->soundSource);
        }

        CB_App->soundSource =
            playdate->sound->addSource(audio_callback, &audioGameScene, use_stereo);
        sound_source_stereo = use_stereo;
    }

    if (headphones)
    {
        playdate->sound->setOutputsActive(1, 0);
    }
    else
    {
        playdate->sound->setOutputsActive(0, 1);
    }
}

#ifdef TARGET_SIMULATOR
volatile int g_trace_frames_remaining = 0;
#endif

// Offset of the relocated draw cluster (0 = run from flash).
// Set by tcm_relocate on RevA; always 0 elsewhere.
intptr_t pgb_draw_reloc_offset = 0;

#if ITCM_CORE
void* core_itcm_reloc = NULL;
intptr_t core_itcm_offset = 0;

// DTCM snapshot of the main pool (gb struct) taken on lock/menu, restored on
// resume so system writes into DTCM can't corrupt emulator state.
static struct dtcm_store_t* s_tcm_store = NULL;

extern char __itcm_dmg_start[];
extern char __itcm_dmg_end[];
extern char __itcm_cgb_start[];
extern char __itcm_cgb_end[];
extern char __draw_dmg_start[];
extern char __draw_dmg_end[];
extern char __draw_cgb_start[];
extern char __draw_cgb_end[];

#define ITCM_CORE_FN(fn) ((void*)((uintptr_t)(void*)fn + core_itcm_offset))

__section__(".rare") void tcm_relocate(bool cgb)
{
    // Restore the main-pool snapshot (gb struct) taken on lock/menu, if any.
    if (s_tcm_store)
    {
        dtcm_restore(s_tcm_store);
        s_tcm_store = NULL;
    }

    void* itcm_start = cgb ? &__itcm_cgb_start : &__itcm_dmg_start;

    void* itcm_end = cgb ? &__itcm_cgb_end : &__itcm_dmg_end;

    uintptr_t core_size = itcm_end - itcm_start;

    // DTCM relocation controlled by the TCM Mode preference (default on).
    // Manual escape hatch if a device/rev misbehaves.
    if (!dtcm_enabled() || preferences_itcm == 0)
    {
        // just use original non-relocated code
        core_itcm_reloc = itcm_start;
        core_itcm_offset = 0;

        playdate->system->logToConsole("itcm[%s]: off - running from flash", cgb ? "cgb" : "dmg");
        return;
    }

    if (core_itcm_reloc == (void*)&__itcm_dmg_start)
        core_itcm_reloc = NULL;

    if (core_itcm_reloc == (void*)&__itcm_cgb_start)
        core_itcm_reloc = NULL;

    if (core_itcm_reloc != NULL)
        return;

    // paranoia
    int MARGIN = 4;
    int DTCM_ALIGN_PAD = 31;

    // probe for clean DTCM pockets (needed for core and/or draw placement)
    dtcm_probe_lower_bound();

    // preference: 0=Off, 1=Both, 2=Core, 3=Draw
    const bool core_on = (preferences_itcm == 1 || preferences_itcm == 2);
    const bool draw_on = (preferences_itcm == 1 || preferences_itcm == 3);
    bool core_in_main_pool = false;
    int best = -1;

    if (core_on)
    {
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
            // No pocket fits: fall back to the main DTCM pool. Validated at
            // post-shrink core sizes; the high canary guards pool overflow.
            core_itcm_reloc = dtcm_alloc_aligned(core_size + MARGIN, (uintptr_t)itcm_start);
            core_in_main_pool = true;
        }

        DTCM_VERIFY();
        memcpy(core_itcm_reloc, (void*)itcm_start, core_size);
        DTCM_VERIFY();
        core_itcm_offset = core_itcm_reloc - itcm_start;
    }
    else
    {
        // Draw-only: core runs from flash.
        core_itcm_reloc = itcm_start;
        core_itcm_offset = 0;
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

    // Relocate the draw cluster as a separate block so the core stays small
    // enough for pockets. Priority: core's pocket slack -> any other pocket
    // -> main pool (only if core is NOT in the main pool) -> flash. This
    // keeps core and draw from ever sharing the main pool.
    if (draw_on)
    {
        void* draw_start = cgb ? (void*)__draw_cgb_start : (void*)__draw_dmg_start;
        void* draw_end = cgb ? (void*)__draw_cgb_end : (void*)__draw_dmg_end;
        size_t draw_size = draw_end - draw_start;
        size_t draw_need = draw_size + MARGIN + DTCM_ALIGN_PAD;

        void* draw_reloc = NULL;
        const char* draw_where = "flash";
        int draw_pocket = -1;

        // core's pocket first (its brk continues after the core block),
        // then any other pocket with room
        for (int i = 0; i < dtcm_num_pockets && !draw_reloc; i++)
        {
            int p = (best >= 0) ? (best + i) % dtcm_num_pockets : i;
            if (!dtcm_pocket_enabled(p))
                continue;
            size_t avail = (uintptr_t)dtcm_pockets[p].end - (uintptr_t)dtcm_pockets[p].mempool;
            if (avail >= draw_need)
            {
                draw_reloc =
                    dtcm_pocket_alloc_aligned(p, draw_size + MARGIN, (uintptr_t)draw_start);
                draw_where = "pocket";
                draw_pocket = p;
            }
        }

        // main pool only when the core is not in it
        if (!draw_reloc && !core_in_main_pool)
        {
            draw_reloc = dtcm_alloc_aligned(draw_size + MARGIN, (uintptr_t)draw_start);
            draw_where = "main pool";
        }

        if (draw_reloc)
        {
            DTCM_VERIFY();
            memcpy(draw_reloc, draw_start, draw_size);
            DTCM_VERIFY();
            pgb_draw_reloc_offset = (char*)draw_reloc - (char*)draw_start;
        }
        else
        {
            pgb_draw_reloc_offset = 0;
        }

        if (draw_pocket >= 0)
            playdate->system->logToConsole(
                "itcm[%s]: draw 0x%X bytes at %p (pocket[%d])", cgb ? "cgb" : "dmg", draw_size,
                draw_reloc, draw_pocket
            );
        else
            playdate->system->logToConsole(
                "itcm[%s]: draw 0x%X bytes at %p (%s)", cgb ? "cgb" : "dmg", draw_size,
                draw_reloc ? draw_reloc : draw_start, draw_where
            );
    }
    else
    {
        void* draw_start = cgb ? (void*)__draw_cgb_start : (void*)__draw_dmg_start;
        void* draw_end = cgb ? (void*)__draw_cgb_end : (void*)__draw_dmg_end;
        playdate->system->logToConsole(
            "itcm[%s]: draw 0x%X bytes at %p (flash)", cgb ? "cgb" : "dmg", draw_end - draw_start,
            draw_start
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
    pgb_draw_reloc_offset = 0;
    core_itcm_reloc = itcm_start;

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
        pgb_draw_reloc_offset = 0;
        core_itcm_reloc = itcm_start;
        dtcm_pocket_fill_and_reset();
        playdate->system->clearICache();
    }
    else
    {
        tcm_relocate(cgb);
    }
}
#else

#define ITCM_CORE_FN(fn) fn

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
#endif

#define REWIND_MAX_MEMORY (4 * 1024 * 1024)
#define REWIND_MAX_STATES 60
#define REWIND_CAPTURE_INTERVAL 10
#define REWIND_ANGLE_STEP 15.0f

static void rewind_init(CB_GameScene* gameScene);
static void rewind_free(CB_GameScene* gameScene);
static void rewind_record_state(CB_GameScene* gameScene);
static void rewind_step_back(CB_GameScene* gameScene);
static void rewind_step_forward(CB_GameScene* gameScene);
static void rewind_enter_scrubbing(CB_GameScene* gameScene);
static void rewind_draw_noise_bands(void);
static void rewind_exit_scrubbing(CB_GameScene* gameScene);
static void rewind_dmg_save(gb_s* gb, uint8_t* out);
static void rewind_dmg_load(gb_s* gb, const uint8_t* in, uint8_t* lcd_target);

static bool CB_GameScene_complete_successful_init(CB_GameScene* gameScene)
{
    CB_GameSceneContext* context = gameScene->context;

    gb_reset(context->gb, context->cgb_mode);

    playdate->system->logToConsole("Initialized gb context.");
    char* save_filename = cb_save_filename(gameScene->rom_filename, false);
    gameScene->save_filename = save_filename;

    gameScene->base_filename = cb_basename(gameScene->rom_filename, true);

    gameScene->cartridge_has_battery = context->gb->cart_battery;
    gameScene->save_state_requires_warning = context->gb->cart_battery;
    playdate->system->logToConsole(
        "Cartridge has battery: %s", gameScene->cartridge_has_battery ? "Yes" : "No"
    );

    gameScene->last_save_time = 0;

    // note: mandatory initialization step before starting emulation!
    // initializes gb_cart_ram
    int ram_load_result =
        read_cart_ram_file(save_filename, context->gb, &gameScene->last_save_time);

    switch (ram_load_result)
    {
    case 0:
        playdate->system->logToConsole("No previous cartridge save data found (%s)", save_filename);
        break;
    case 1:
    case 2:
        playdate->system->logToConsole("Loaded cartridge save data (%s)", save_filename);
        break;
    default:
    {
        if (context->gb && context->gb->gb_cart_ram)
        {
            cb_free(context->gb->gb_cart_ram);
            context->gb->gb_cart_ram = NULL;
        }

        gameScene->error = CB_GameSceneErrorSaveData;
        return false;
    }
    }

    context->cart_ram = context->gb->gb_cart_ram;
    gameScene->save_data_loaded_successfully = true;

    unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);
    gameScene->rtc_time = now;
    gameScene->rtc_seconds_to_catch_up = 0;

    gameScene->cartridge_has_rtc = (context->gb->mbc == 3 && context->gb->cart_battery);
    gameScene->cartridge_has_accelerometer = (context->gb->mbc == 7);

    if (gameScene->cartridge_has_accelerometer)
    {
        playdate->system->setPeripheralsEnabled(kAccelerometer);
    }

    if (gameScene->cartridge_has_rtc)
    {
        playdate->system->logToConsole("Cartridge is MBC3 with battery: RTC Enabled.");

        if (ram_load_result == 2)
        {
            playdate->system->logToConsole("Loaded RTC state and timestamp from save file.");

            if (now > gameScene->last_save_time)
            {
                unsigned int seconds_to_advance = now - gameScene->last_save_time;
                if (seconds_to_advance > 0)
                {
                    playdate->system->logToConsole(
                        "Catching up RTC by %u seconds...", seconds_to_advance
                    );
                    gb_catch_up_rtc_direct(context->gb, seconds_to_advance);
                }
            }
        }
        else
        {
            playdate->system->logToConsole(
                "No valid RTC save data. Initializing clock to system "
                "time."
            );
            time_t time_for_core = gameScene->rtc_time + 946684800;
            struct tm* timeinfo = localtime(&time_for_core);
            if (timeinfo != NULL)
            {
                gb_set_rtc(context->gb, timeinfo);
            }
        }
    }
    return true;
}

// Helper function to generate the config file path for a game
char* cb_game_config_path(const char* rom_filename)
{
    char* basename = cb_basename(rom_filename, true);
    char* path;
    playdate->system->formatString(
        &path, "%s/%s.json", cb_gb_directory_path(CB_settingsPath), basename
    );
    cb_free(basename);
    return path;
}

// one is static-alloc; the other is in the display frame buffer area
// see preferences_tcm_lcd
static uint8_t* lcd_sources[2];

CB_GameScene* CB_GameScene_new(const char* rom_filename, const char* name_short, bool cgb_mode)
{
    // Seed the random number generator to ensure joypad interrupt timing is unpredictable.
    srand(time(NULL));

    clear_last_selected_preference();

    last_scy = -1;

    if (!rom_filename)
    {
        playdate->system->logToConsole("ERROR: NULL rom_filename");
        return NULL;
    }

    playdate->system->logToConsole("ROM: %s", rom_filename);

    if (!DTCM_VERIFY_DEBUG())
        return NULL;

    CB_GameScene_reset_screen_defaults();
    game_menu_button_input_enabled = 1;

    CB_Scene* scene = CB_Scene_new();
    scene->id = "game";

    CB_GameScene* gameScene = allocz(CB_GameScene);
    gameScene->last_loaded_slot = (unsigned)-1;
    gameScene->scene = scene;
    scene->managedObject = gameScene;

    scene->update = CB_GameScene_update;
    scene->menu = CB_GameScene_menu;
    scene->free = CB_GameScene_free;
    scene->event = CB_GameScene_event;
    scene->lock = CB_GameScene_lock;
    scene->use_user_stack = 0;  // user stack is slower

    scene->preferredRefreshRate = 30;

    gameScene->rom_filename = cb_strdup(rom_filename);
    gameScene->name_short = cb_strdup(name_short);
    gameScene->save_filename = NULL;

    gameScene->state = CB_GameSceneStateError;
    gameScene->error = CB_GameSceneErrorUndefined;

    gameScene->model = (CB_GameSceneModel){
        .state = CB_GameSceneStateError,
        .error = CB_GameSceneErrorUndefined,
        .selectorIndex = 0,
        .empty = true
    };

    gameScene->audioEnabled = false;
    gameScene->audioLocked = false;
    gameScene->button_hold_mode = 1;  // None
    gameScene->button_hold_frames_remaining = 0;
    gameScene->prev_joypad = 0xFF;

    gameScene->crank_turbo_accumulator = 0.0f;
    gameScene->crank_turbo_a_active = false;
    gameScene->crank_turbo_b_active = false;
    gameScene->crank_was_docked = playdate->system->isCrankDocked();

    gameScene->adaptive_fs_headroom_counter = 0;
    gameScene->adaptive_fs_lock_frames = 0;
    gameScene->adaptive_fs_perf_allowed = false;

    gameScene->adaptive_fs_probe_cooldown = 0;
    gameScene->adaptive_fs_probe_pending = false;

    gameScene->isCurrentlySaving = false;
    gameScene->quitGameModalConfirmOverride = false;
    gameScene->is_mirroring = false;

    gameScene->menuImage = NULL;

    gameScene->staticSelectorUIDrawn = false;

    gameScene->save_data_loaded_successfully = false;

    gameScene->menu_open_seconds = 0;
    gameScene->menu_open_ms = 0;

    prefs_locked_by_script = 0;

    audio_enabled = 1;

    // Global settings are loaded by default. Check for a game-specific file.
    gameScene->settings_filename = cb_game_config_path(rom_filename);

    if (!CB_App->bundled_rom)
    {
        // Load game-specific preferences, but preserve always-global values
        // (these always come from the global preferences file).
        preferences_per_game = 0;

        void* stored_always_global = preferences_store_subset(PREFBITS_ALWAYS_GLOBAL);
        call_with_user_stack_1(preferences_read_from_disk, gameScene->settings_filename);

        // If the game file doesn't enable per-game scope (or doesn't exist),
        // load the global preferences for the remaining settings,
        // but preserve per-game-only values that came from the game file.
        if (preferences_per_game == 0)
        {
            void* stored_never_global = preferences_store_subset(PREFBITS_NEVER_GLOBAL);
            call_with_user_stack_1(preferences_read_from_disk, CB_globalPrefsPath);
            preferences_restore_subset(stored_never_global);
            cb_free(stored_never_global);
        }

        preferences_restore_subset(stored_always_global);
        cb_free(stored_always_global);
    }
    else
    {
        // Bundled: load game settings, then merge global settings for shared prefs.
        // Always-globals come from global, never-globals come from the game file.
        void* stored_always_global = preferences_store_subset(PREFBITS_ALWAYS_GLOBAL);
        call_with_user_stack_1(preferences_read_from_disk, gameScene->settings_filename);
        void* stored_never_global = preferences_store_subset(PREFBITS_NEVER_GLOBAL);
        call_with_user_stack_1(preferences_read_from_disk, CB_globalPrefsPath);
        preferences_restore_subset(stored_never_global);
        cb_free(stored_never_global);
        preferences_restore_subset(stored_always_global);
        cb_free(stored_always_global);
    }

    CB_GameScene_generateBitmask();

    generate_dither_luts();

    CB_GameScene_selector_init(gameScene);

#if ITCM_CORE
    core_itcm_reloc = NULL;
    pgb_draw_reloc_offset = 0;
    s_tcm_store = NULL;
#endif
    dtcm_deinit();
    dtcm_init();

    DTCM_VERIFY();

    CB_GameSceneContext* context = allocz(CB_GameSceneContext);
    static gb_s
        gb_fallback;  // use this gb struct if dtcm alloc not available. Also during initialization.
    context->gb = &gb_fallback;
    context->cgb_mode = cgb_mode;

    DTCM_VERIFY();
    memset(context->gb, 0, sizeof(gb_s));
    DTCM_VERIFY();

    context->scene = gameScene;
    context->rom = NULL;
    context->cart_ram = NULL;

    gameScene->context = context;

    CB_GameSceneError romError;
    size_t rom_size;
    uint8_t* rom = read_rom_to_ram(rom_filename, &romError, &rom_size);
    DTCM_VERIFY();
    if (rom)
    {
        playdate->system->logToConsole("Opened ROM.");

        // try patches
        SoftPatch* patches = list_patches(rom_filename, NULL);
        if (patches)
        {
            printf("softpatching ROM...\n");
            bool result = call_with_main_stack_3(patch_rom, (void*)&rom, &rom_size, patches);
            // Patching may replace/shrink the ROM allocation (UPS/BPS always
            // allocate an exact-size new buffer). rom returns to rom_pool at
            // teardown, so rom_pool_size must track its actual allocation
            // size, otherwise a later pool reuse could overflow it.
            rom_pool_size = rom_size;
            gameScene->patches_hash = patch_hash(patches);

            free_patches(patches);
        }

        context->rom = rom;
        context->rom_size = rom_size;

        static clalign uint8_t lcd_static[LCD_BUFFER_BYTES];

        lcd_sources[0] = lcd_static;
        lcd_sources[1] = playdate->graphics->getDisplayFrame();
        lcd_sources[1] = (uint8_t*)((((uintptr_t)lcd_sources[1] + 31) / 32) * 32);

        gameScene->cgb_compatible = (gb_get_models_supported(rom) & GB_SUPPORT_CGB);
        gameScene->dmg_compatible = (gb_get_models_supported(rom) & GB_SUPPORT_DMG);

        enum gb_init_error_e gb_ret = gb_init(
            context->gb, context->wram, context->vram, lcd_sources[preferences_tcm_lcd], rom,
            rom_size, gb_error, context, cgb_mode
        );

        CB_ASSERT((((uintptr_t)context->gb->lcd) & 7) == 0);
        CB_ASSERT((((uintptr_t)context->previous_lcd) & 7) == 0);

        if (gb_ret == GB_INIT_NO_ERROR || gb_ret == GB_INIT_NO_ERROR_BUT_REQUIRES_CGB)
        {
            context->gb_initialized = true;
            if (!CB_GameScene_complete_successful_init(gameScene))
            {
                gameScene->state = CB_GameSceneStateError;
            }
            else
            {
                DTCM_VERIFY();

                audio_init(&context->gb->audio);
                CB_GameScene_apply_settings(gameScene);
                CB_reset_audio_sync_state();

                gb_init_lcd(context->gb);
                memset(context->previous_lcd, 0, sizeof(context->previous_lcd));
                memset(context->ghost_state, 0, sizeof(context->ghost_state));
                context->ghost_phase = 0;
                context->ghost_converged = false;
                context->ghost_resnap = false;
                pgb_dirty_prev = context->previous_lcd;
                pgb_dirty_flags = context->line_has_changed;
                gameScene->state = CB_GameSceneStateLoaded;

                playdate->system->logToConsole("gb context initialized.");
            }

            if (dtcm_enabled())
            {
                context->gb = dtcm_alloc(sizeof(gb_s));
                memcpy(context->gb, &gb_fallback, sizeof(gb_s));
            }

            tcm_relocate(context->gb->is_cgb_mode);
        }
        else
        {
            playdate->system->logToConsole("Failed to initialize ROM.");
            gameScene->state = CB_GameSceneStateError;
            gameScene->error = CB_GameSceneErrorFatal;
            return gameScene;
        }
    }
    else
    {
        playdate->system->logToConsole("Failed to open ROM.");
        gameScene->state = CB_GameSceneStateError;
        gameScene->error = romError;
        return gameScene;
    }

    gameScene->script_available = false;
    gameScene->script_info_available = false;
    bool fade_color_override = false;

    ScriptInfo* scriptInfo = script_get_info_by_rom_path(gameScene->rom_filename);
    if (scriptInfo)
    {
        gameScene->script_available = true;
        gameScene->script_info_available = !!scriptInfo->info;
        gameScene->script_toggleable =
            scriptInfo->c_script_info && scriptInfo->c_script_info->toggleable;

        if (scriptInfo->launch_color == ScriptPreferredLaunchColor_White)
        {
            gameScene->fade_white = true;
            fade_color_override = true;
        }
        else if (scriptInfo->launch_color == ScriptPreferredLaunchColor_Black)
        {
            gameScene->fade_white = false;
            fade_color_override = true;
        }
    }

    if (preferences_script_support && gameScene->script_available && scriptInfo)
    {
        playdate->system->logToConsole("ROM name: \"%s\"", scriptInfo->rom_name);
        gameScene->script = script_begin(scriptInfo->rom_name, gameScene);
        gameScene->prev_dt = 0;
        if (!gameScene->script)
        {
            playdate->system->logToConsole("Associated script failed to load or not found.");
        }
    }
    script_info_free(scriptInfo);

    gameScene->fade_frames = cb_boot_fade_initial_frames(preferences_boot_fade);
    if (!fade_color_override)
    {
        gameScene->fade_white = cb_boot_fade_initial_white(preferences_boot_fade);
    }

    // Bundled mode starts out with black screen, so black fade always looks better.
    // To change this, we'd need to have the pdx post-launch image set to white
    if (CB_App->bundled_rom)
        gameScene->fade_white = false;

    if (fade_color_override)
    {
        prefs_locked_by_script |= PREFBIT_boot_fade;
    }

    DTCM_VERIFY();

    CB_ASSERT(gameScene->context == context);
    CB_ASSERT(gameScene->context->scene == gameScene);
    CB_ASSERT(gameScene->context->gb->direct.priv == context);

    return gameScene;
}

// Default screen layout (no script active). Restored on scene init and when
// a script is disabled mid-session, since scripts set these every frame.
static void CB_GameScene_reset_screen_defaults(void)
{
    game_picture_x_offset = CB_LCD_X;
    game_picture_scaling = 3;
    game_picture_y_top = 0;
    game_picture_y_bottom = LCD_HEIGHT;
    game_picture_background_color = kColorBlack;
    game_hide_indicator = false;
    game_invert_indicator = false;
}

// Enable/disable the game script live (settings close). Mirrors the boot
// begin/end; script consumers NULL-guard everywhere.
void CB_GameScene_apply_script_support(CB_GameScene* gameScene)
{
    // Scripts that don't declare .toggleable keep the old behavior: the pref
    // change applies on the next ROM launch.
    if (!gameScene->script_toggleable)
        return;

    if (preferences_script_support && gameScene->script_available && !gameScene->script)
    {
        ScriptInfo* scriptInfo = script_get_info_by_rom_path(gameScene->rom_filename);
        if (scriptInfo)
        {
            playdate->system->logToConsole("ROM name: \"%s\"", scriptInfo->rom_name);
            gameScene->script = script_begin(scriptInfo->rom_name, gameScene);
            gameScene->prev_dt = 0;
            if (!gameScene->script)
                playdate->system->logToConsole("Associated script failed to load or not found.");
            script_info_free(scriptInfo);
        }
    }
    else if (!preferences_script_support && gameScene->script)
    {
        script_end(gameScene->script, gameScene);
        gameScene->script = NULL;

        // Scripts set these every frame; restore the no-script defaults so a
        // mid-game disable reverts scaling/layout (frame loop auto-refreshes).
        CB_GameScene_reset_screen_defaults();

        // Un-force any prefs the script locked, back to the user's values.
        script_pref_restore_originals();
        prefs_locked_by_script = 0;

        // Revert in-memory ROM bytes patched by the script.
        script_patch_restore();

        // Reset the on-screen Start/Select selector to its default position
        // (deterministic; scripts move it via script_selector()).
        CB_GameScene_selector_init(gameScene);
        gameScene->staticSelectorUIDrawn = false;

        // Drop script-added Playdate menu entries now; next menu open
        // rebuilds the full list from scratch.
        playdate->system->removeAllMenuItems();
    }
}

void CB_GameScene_apply_settings(CB_GameScene* gameScene)
{
    CB_GameSceneContext* context = gameScene->context;

    generate_dither_luts();

    if (context->cgb_mode)
    {
        gameScene->cgb_needs_palette_recompute = true;
        pgb_cgb_lut_dirty = true;
    }

    reconfigure_audio_source(gameScene);

    CB_ASSERT(audioGameScene == gameScene);

    context->gb->direct.sound = 1;

    // If the buffered audio sync is NOT the active mode, we MUST ensure
    // its buffer is cleared. This handles the case where a user disables
    // the feature mid-game, preventing stale audio from persisting.
    if (preferences_sound_mode != 2)
    {
        CB_reset_audio_sync_state();
        memset(g_audio_sync_buffer.left, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
        memset(g_audio_sync_buffer.right, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
    }
    else
    {
        // pre_frame snapshot is stale after fast mode (renderer ran in the
        // callback); invalidate so accurate starts from live chans.
        audio_reset_replay_state(&context->gb->audio);
    }

    if (preferences_crank_down_action == 0)
    {
        gameScene->selector.deadAngle = 45;
    }
    else
    {
        gameScene->selector.deadAngle = 20;
    }

    playdate->system->setAutoLockDisabled(preferences_disable_autolock);

    // Apply TCM Mode changes live (skipped at boot, where tcm_relocate runs
    // separately after the gb struct is moved into the DTCM pool).
    if (gameScene->state == CB_GameSceneStateLoaded)
    {
        tcm_apply(context->gb->is_cgb_mode);
        CB_GameScene_apply_script_support(gameScene);
    }
}

static void CB_GameScene_selector_init(CB_GameScene* gameScene)
{
    int startButtonWidth = playdate->graphics->getTextWidth(
        CB_App->labelFont, startButtonText, strlen(startButtonText), kUTF8Encoding, 0
    );
    int selectButtonWidth = playdate->graphics->getTextWidth(
        CB_App->labelFont, selectButtonText, strlen(selectButtonText), kUTF8Encoding, 0
    );

    int width = 18;
    int height = 46;

    int startSpacing = 3;
    int selectSpacing = 6;

    int labelHeight = playdate->graphics->getFontHeight(CB_App->labelFont);

    int containerHeight = labelHeight + startSpacing + height + selectSpacing + labelHeight;

    int containerWidth = width;
    containerWidth = CB_MAX(containerWidth, startButtonWidth);
    containerWidth = CB_MAX(containerWidth, selectButtonWidth);

    const int rightBarX = 40 + 320;
    const int rightBarWidth = 40;

    int containerX = rightBarX + (rightBarWidth - containerWidth) / 2 - 1;
    int containerY = 8;
    int x = containerX + (containerWidth - width) / 2;
    int y = containerY + labelHeight + startSpacing;

    int startButtonX = rightBarX + (rightBarWidth - startButtonWidth) / 2;
    int startButtonY = containerY;

    int selectButtonX = rightBarX + (rightBarWidth - selectButtonWidth) / 2;
    int selectButtonY = containerY + containerHeight - labelHeight;

    gameScene->selector.x = x;
    gameScene->selector.y = y;
    gameScene->selector.width = width;
    gameScene->selector.height = height;
    gameScene->selector.containerX = containerX;
    gameScene->selector.containerY = containerY;
    gameScene->selector.containerWidth = containerWidth;
    gameScene->selector.containerHeight = containerHeight;
    gameScene->selector.startButtonX = startButtonX;
    gameScene->selector.startButtonY = startButtonY;
    gameScene->selector.selectButtonX = selectButtonX;
    gameScene->selector.selectButtonY = selectButtonY;
    gameScene->selector.numberOfFrames = 27;
    gameScene->selector.triggerAngle = 45;
    gameScene->selector.index = 0;
    gameScene->selector.startPressed = false;
    gameScene->selector.selectPressed = false;
}

/**
 * Returns a pointer to the allocated space containing the ROM. Must be freed.
 */
static uint8_t* read_rom_to_ram(
    const char* filename, CB_GameSceneError* sceneError, size_t* o_rom_size
)
{
    *sceneError = CB_GameSceneErrorUndefined;

    SDFile* rom_file = playdate->file->open(filename, kFileReadDataOrPacked);

    if (rom_file == NULL)
    {
        const char* fileError = playdate->file->geterr();
        playdate->system->logToConsole(
            "%s:%i: Can't open rom file %s", __FILE__, __LINE__, filename
        );
        playdate->system->logToConsole("%s:%i: File error %s", __FILE__, __LINE__, fileError);

        *sceneError = CB_GameSceneErrorLoadingRom;

        if (fileError)
        {
            char* fsErrorCode = cb_extract_fs_error_code(fileError);
            if (fsErrorCode)
            {
                if (strcmp(fsErrorCode, "0709") == 0)
                {
                    *sceneError = CB_GameSceneErrorWrongLocation;
                }
            }
        }
        return NULL;
    }

    playdate->file->seek(rom_file, 0, SEEK_END);
    int rom_size = playdate->file->tell(rom_file);
    *o_rom_size = rom_size;
    playdate->file->seek(rom_file, 0, SEEK_SET);

    uint8_t* rom;
    if (rom_pool && rom_pool_size >= (size_t)rom_size)
    {
        rom = rom_pool;
        rom_pool = NULL;
    }
    else
    {
        rom = cb_malloc(rom_size);
        if (rom)
        {
            cb_free(rom_pool);
            rom_pool = NULL;
            rom_pool_size = (size_t)rom_size;
        }
    }

    if (!rom || playdate->file->read(rom_file, rom, rom_size) != rom_size)
    {
        playdate->system->logToConsole(
            "%s:%i: Can't read rom file %s", __FILE__, __LINE__, filename
        );

        cb_free(rom);
        playdate->file->close(rom_file);
        *sceneError = CB_GameSceneErrorLoadingRom;
        return NULL;
    }

    playdate->file->close(rom_file);

    GBZ_Header gbz;
    if (gbz_parse_header(&gbz, rom, rom_size))
    {
        uint8_t* decompressed_rom = cb_malloc(gbz.original_size);
        if (!decompressed_rom)
        {
            playdate->system->logToConsole(
                "%s:%i: Can't decompress %s, out of memory", __FILE__, __LINE__, filename
            );

            cb_free(rom);
            *sceneError = CB_GameSceneErrorLoadingRom;
            return NULL;
        }

        int status = gbz_decompress(rom, rom_size, decompressed_rom, gbz.original_size);
        cb_free(rom);
        if (status != gbz.original_size)
        {
            playdate->system->logToConsole(
                "%s:%i: Failed to decompress %s: %d", __FILE__, __LINE__, filename, status
            );
            cb_free(decompressed_rom);
            *sceneError = CB_GameSceneErrorLoadingRom;
            return NULL;
        }
        else
        {
            playdate->system->logToConsole("Decompressed ROM: %s", filename);
        }

        rom_pool_size = (size_t)gbz.original_size;
        return decompressed_rom;
    }
    else
    {
        return rom;
    }
}

static int read_cart_ram_file(const char* save_filename, gb_s* gb, unsigned int* last_save_time)
{
    *last_save_time = 0;

    const size_t sram_len = gb_get_save_size(gb);

    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    gb->gb_cart_ram = (sram_len > 0) ? cb_malloc(sram_len) : NULL;

    // fill with default
    if (gb->gb_cart_ram)
    {
        // TODO: what is the default fill supposed to be?
        uint8_t fill = 0;
        if (gb->mbc == 7)
            fill = 0xFF;
        memset(gb->gb_cart_ram, fill, sram_len);
    }
    gb->gb_cart_ram_size = sram_len;

    SDFile* f = playdate->file->open(save_filename, kFileReadData);
    if (f == NULL)
    {
        // We assume this only happens if file does not exist
        return 0;
    }

    if (sram_len > 0)
    {
        int read = playdate->file->read(f, gb->gb_cart_ram, (unsigned int)sram_len);
        if (read != sram_len)
        {
            playdate->system->logToConsole("Failed to read save data");
            playdate->file->close(f);
            return -1;
        }
    }

    int code = 1;
    if (gameScene->cartridge_has_battery)
    {
        if (playdate->file->read(f, gb->cart_rtc, sizeof(gb->cart_rtc)) == sizeof(gb->cart_rtc))
        {
            if (playdate->file->read(f, last_save_time, sizeof(unsigned int)) ==
                sizeof(unsigned int))
            {
                code = 2;
            }
        }
    }

    playdate->file->close(f);
    return code;
}

static void write_cart_ram_file(const char* save_filename, gb_s* gb)
{
    // Get the size of the save RAM from the gb context.
    const size_t sram_len = gb_get_save_size(gb);
    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    // If there is no battery, exit.
    if (!gameScene->cartridge_has_battery)
    {
        return;
    }

    // Generate .tmp and .bak filenames
    size_t len = strlen(save_filename);
    char* tmp_filename = cb_malloc(len + 2);
    char* bak_filename = cb_malloc(len + 2);

    if (!tmp_filename || !bak_filename)
    {
        playdate->system->logToConsole("Error: Failed to allocate memory for safe save filenames.");
        goto cleanup;
    }

    strcpy(tmp_filename, save_filename);
    strcpy(bak_filename, save_filename);

    char* ext_tmp = strrchr(tmp_filename, '.');
    if (ext_tmp && strcmp(ext_tmp, ".sav") == 0)
    {
        strcpy(ext_tmp, ".tmp");
    }
    else
    {
        strcat(tmp_filename, ".tmp");
    }

    char* ext_bak = strrchr(bak_filename, '.');
    if (ext_bak && strcmp(ext_bak, ".sav") == 0)
    {
        strcpy(ext_bak, ".bak");
    }
    else
    {
        strcat(bak_filename, ".bak");
    }

    playdate->file->unlink(tmp_filename, false);

    // Write data to the temporary file
    playdate->system->logToConsole("Saving to temporary file: %s", tmp_filename);
    SDFile* f = playdate->file->open(tmp_filename, kFileWrite);
    if (f == NULL)
    {
        playdate->system->logToConsole(
            "Error: Can't open temp save file for writing: %s", tmp_filename
        );
        goto cleanup;
    }

    if (sram_len > 0 && gb->gb_cart_ram != NULL)
    {
        playdate->file->write(f, gb->gb_cart_ram, (unsigned int)sram_len);
    }

    // write rtc
    playdate->file->write(f, gb->cart_rtc, sizeof(gb->cart_rtc));

    // write timestamp
    unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);
    gameScene->last_save_time = now;
    playdate->file->write(f, &now, sizeof(now));

    // write flags
    uint32_t flags = !!gameScene->script;
    playdate->file->write(f, &flags, sizeof(flags));

    // write patch hash
    playdate->file->write(f, &gameScene->patches_hash, sizeof(gameScene->patches_hash));

    // write magic number (must be at end of file)
    uint64_t magic = SRAM_MAGIC_NUMBER;
    playdate->file->write(f, &magic, sizeof(magic));

    playdate->file->close(f);

    // Verify that the temporary file is not zero-bytes
    FileStat stat;
    if (playdate->file->stat(tmp_filename, &stat) != 0)
    {
        playdate->system->logToConsole(
            "Error: Failed to stat temp save file %s. Aborting save.", tmp_filename
        );
        playdate->file->unlink(tmp_filename, false);
        goto cleanup;
    }

    if (stat.size == 0)
    {
        playdate->system->logToConsole(
            "Error: Wrote 0-byte temp save file %s. Aborting and deleting.", tmp_filename
        );
        playdate->file->unlink(tmp_filename, false);
        goto cleanup;
    }

    // Rename files: .sav -> .bak, then .tmp -> .sav
    playdate->system->logToConsole("Save successful, renaming files.");

    playdate->file->unlink(bak_filename, false);
    playdate->file->rename(save_filename, bak_filename);

    if (playdate->file->rename(tmp_filename, save_filename) != 0)
    {
        playdate->system->logToConsole(
            "CRITICAL: Failed to rename temp file to save file. Restoring "
            "backup."
        );
        playdate->file->rename(bak_filename, save_filename);
    }

cleanup:
    if (tmp_filename)
        cb_free(tmp_filename);
    if (bak_filename)
        cb_free(bak_filename);
}

static void gb_save_to_disk_(gb_s* gb)
{
    DTCM_VERIFY_DEBUG();

    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    if (gameScene->isCurrentlySaving)
    {
        playdate->system->logToConsole("Save to disk skipped: another save is in progress.");
        return;
    }

    if (!context->gb->direct.sram_dirty)
    {
        return;
    }

    gameScene->isCurrentlySaving = true;

    if (gameScene->save_filename)
    {
        write_cart_ram_file(gameScene->save_filename, context->gb);
    }
    else
    {
        playdate->system->logToConsole("No save file name specified; can't save.");
    }

    context->gb->direct.sram_dirty = false;

    gameScene->isCurrentlySaving = false;

    DTCM_VERIFY_DEBUG();
}

static void gb_save_to_disk(gb_s* gb)
{
    call_with_main_stack_1(gb_save_to_disk_, gb);
}

/**
 * Handles an error reported by the emulator. The emulator context may be used
 * to better understand why the error given in gb_err was reported.
 */
static void gb_error(gb_s* gb, const enum gb_error_e gb_err, const uint16_t val)
{
    CB_GameSceneContext* context = gb->direct.priv;

    bool is_fatal = false;

    if (gb_err == GB_INVALID_OPCODE)
    {
        is_fatal = true;

        playdate->system->logToConsole(
            "%s:%i: Invalid opcode %#04x at PC: %#06x, SP: %#06x", __FILE__, __LINE__, val,
            gb->cpu_reg.pc - 1, gb->cpu_reg.sp
        );
    }
    else if (gb_err == GB_INVALID_READ)
    {
#if 0
        playdate->system->logToConsole("Invalid read: addr %04x", val);
#endif
    }
    else if (gb_err == GB_INVALID_WRITE)
    {
#if 0
        playdate->system->logToConsole("Invalid write: addr %04x", val);
#endif
    }
    else
    {
        is_fatal = true;
        playdate->system->logToConsole("%s:%i: Unknown error occurred", __FILE__, __LINE__);
    }

    if (is_fatal)
    {
        // save a recovery file
        if (context->scene->save_data_loaded_successfully)
        {
            char* recovery_filename = cb_save_filename(context->scene->rom_filename, true);
            write_cart_ram_file(recovery_filename, context->gb);
            cb_free(recovery_filename);
        }

        // TODO: write recovery savestate

        context->scene->state = CB_GameSceneStateError;
        context->scene->error = CB_GameSceneErrorFatal;

        CB_Scene_refreshMenu(context->scene->scene);
    }

    return;
}

static __section__(".text.tick") void blend_frames(
    uint8_t* restrict frame_a, uint8_t* restrict frame_b_and_dest, uint8_t* restrict prev_lcd,
    uint16_t* restrict dirty_flags
)
{
    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        uint32_t bias_e = (y & 1) ? 0x11111111 : 0;
        uint32_t bias_o = (y & 1) ? 0 : 0x11111111;

        uint32_t* restrict frame_a_32 = (uint32_t*)frame_a;
        uint32_t* restrict frame_b_32 = (uint32_t*)frame_b_and_dest;
        uint32_t* restrict prev_32 = (uint32_t*)prev_lcd;

        uint32_t changed = 0;
        for (int x = 0; x < LCD_WIDTH_PACKED / 4; x++)
        {
            uint32_t a_word = frame_a_32[x];
            uint32_t b_word = frame_b_32[x];

            uint32_t e =
                (((a_word & 0x33333333) + (b_word & 0x33333333) + bias_e) >> 1) & 0x33333333;
            uint32_t o =
                ((((a_word >> 2) & 0x33333333) + ((b_word >> 2) & 0x33333333) + bias_o) >> 1) &
                0x33333333;

            uint32_t blended_word = e | (o << 2);
            frame_b_32[x] = blended_word;

            changed |= blended_word ^ prev_32[x];
            prev_32[x] = blended_word;
        }

        if (changed)
            dirty_flags[y >> 4] |= (1 << (y & 0xF));

        frame_a += LCD_WIDTH_PACKED;
        frame_b_and_dest += LCD_WIDTH_PACKED;
        prev_lcd += LCD_WIDTH_PACKED;
    }
}

static __section__(".text.tick") void blit_tracked(
    const uint8_t* restrict src, uint8_t* restrict dest, uint8_t* restrict prev_lcd,
    uint16_t* restrict dirty_flags
)
{
    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        uint32_t* restrict s = (uint32_t*)src;
        uint32_t* restrict d = (uint32_t*)dest;
        uint32_t* restrict p = (uint32_t*)prev_lcd;
        uint32_t changed = 0;
        for (int x = 0; x < LCD_WIDTH_PACKED / 4; x++)
        {
            uint32_t w = s[x];
            changed |= w ^ p[x];
            d[x] = w;
            p[x] = w;
        }
        if (changed)
            dirty_flags[y >> 4] |= (1 << (y & 0xF));
        src += LCD_WIDTH_PACKED;
        dest += LCD_WIDTH_PACKED;
        prev_lcd += LCD_WIDTH_PACKED;
    }
}

// Dirty-tracking only: frame already sits in the display buffer (rendered
// in place), just sync prev_lcd + dirty flags. Used when blending is
// skipped (identity LUTs).
static __section__(".text.tick") void track_changes(
    const uint8_t* restrict frame, uint8_t* restrict prev_lcd, uint16_t* restrict dirty_flags
)
{
    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        uint32_t* restrict f = (uint32_t*)frame;
        uint32_t* restrict p = (uint32_t*)prev_lcd;
        uint32_t changed = 0;
        for (int x = 0; x < LCD_WIDTH_PACKED / 4; x++)
        {
            uint32_t w = f[x];
            changed |= w ^ p[x];
            p[x] = w;
        }
        if (changed)
            dirty_flags[y >> 4] |= (1 << (y & 0xF));
        frame += LCD_WIDTH_PACKED;
        prev_lcd += LCD_WIDTH_PACKED;
    }
}

/* LCD ghosting: 2-bit slew-rate smear emulating the DMG LCD's slow pixel
 * response. ghost holds one packed 2bpp frame; on step frames each pixel
 * moves one shade toward the current frame. A full white-to-black sweep
 * takes 3 steps: 100 ms in both modes (see cadence logic at the call
 * site).
 */
#define GHOST_STEP_INTERVAL 2

static __section__(".text.tick") void ghost_pass(
    uint8_t* restrict lcd, uint8_t* restrict ghost, uint16_t* restrict dirty_flags, bool do_step,
    bool* converged_inout
)
{
    uint32_t diverged = 0;

    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        uint32_t* restrict lcd32 = (uint32_t*)lcd;
        uint32_t* restrict g32 = (uint32_t*)ghost;
        bool line_changed = false;

        for (int x = 0; x < LCD_WIDTH_PACKED / 4; x++)
        {
            uint32_t s = g32[x];
            uint32_t r = lcd32[x];

            if (s == r)
                continue;

            if (do_step)
            {
                // Step each 2-bit field 1 shade toward r. (a|4)-b never borrows
                // across nibbles, so nibble bit 2 = "a>=b"; XOR gives +-1.
                uint32_t ae = s & 0x33333333;
                uint32_t be = r & 0x33333333;
                uint32_t ge_ab = ((ae | 0x44444444) - be) & 0x44444444;
                uint32_t ge_ba = ((be | 0x44444444) - ae) & 0x44444444;
                ae += ((ge_ab ^ 0x44444444) >> 2) - ((ge_ba ^ 0x44444444) >> 2);

                uint32_t ao = (s >> 2) & 0x33333333;
                uint32_t bo = (r >> 2) & 0x33333333;
                ge_ab = ((ao | 0x44444444) - bo) & 0x44444444;
                ge_ba = ((bo | 0x44444444) - ao) & 0x44444444;
                ao += ((ge_ab ^ 0x44444444) >> 2) - ((ge_ba ^ 0x44444444) >> 2);

                s = ae | (ao << 2);
                g32[x] = s;
            }

            lcd32[x] = s;
            line_changed = true;
            diverged |= s ^ r;
        }

        if (line_changed)
            dirty_flags[y >> 4] |= (1 << (y & 0xF));

        lcd += LCD_WIDTH_PACKED;
        ghost += LCD_WIDTH_PACKED;
    }

    *converged_inout = (diverged == 0);
}

static void save_check(gb_s* gb);

__section__(".text.tick") __space static void crank_update(CB_GameScene* gameScene, float* progress)
{
    CB_GameSceneContext* context = gameScene->context;

    float angle = fmaxf(0, fminf(360, playdate->system->getCrankAngle()));

    if (preferences_crank_mode == CRANK_MODE_START_SELECT && !gameScene->rewind.active)
    {
        gameScene->selector.startPressed = false;
        gameScene->selector.selectPressed = false;

        if (preferences_crank_down_action == 1 && angle > (180 - gameScene->selector.deadAngle) &&
            angle < (180 + gameScene->selector.deadAngle))
        {
            gameScene->selector.startPressed = true;
            gameScene->selector.selectPressed = true;
        }
        else
        {
            if (angle <= 180)
            {
                if (angle >= gameScene->selector.triggerAngle &&
                    angle <= (180 - gameScene->selector.triggerAngle))
                {
                    gameScene->selector.startPressed = true;
                }

                float dist = fminf(angle, 180.0f - angle);
                float adjustedAngle = fminf(dist, gameScene->selector.triggerAngle);
                *progress = 0.5f - (adjustedAngle / gameScene->selector.triggerAngle * 0.5f);
            }
            else
            {
                if (angle >= (180 + gameScene->selector.triggerAngle) &&
                    angle <= (360 - gameScene->selector.triggerAngle))
                {
                    gameScene->selector.selectPressed = true;
                }

                float dist = fminf(360.0f - angle, angle - 180.0f);
                float adjustedAngle = fminf(dist, gameScene->selector.triggerAngle);
                *progress = 0.5f + (adjustedAngle / gameScene->selector.triggerAngle * 0.5f);
            }
        }
    }
    else if (
        (preferences_crank_mode == CRANK_MODE_TURBO_CW ||
         preferences_crank_mode == CRANK_MODE_TURBO_CCW) &&
        !gameScene->rewind.active
    )  // Turbo mode
    {
        float crank_change = playdate->system->getCrankChange();
        gameScene->crank_turbo_accumulator += crank_change;

        // Handle clockwise rotation
        while (gameScene->crank_turbo_accumulator >= 45.0f)
        {
            if (preferences_crank_mode == CRANK_MODE_TURBO_CW)
            {
                gameScene->crank_turbo_a_active = true;
            }
            else
            {
                gameScene->crank_turbo_b_active = true;
            }
            gameScene->crank_turbo_accumulator -= 45.0f;
        }

        // Handle counter-clockwise rotation
        while (gameScene->crank_turbo_accumulator <= -45.0f)
        {
            if (preferences_crank_mode == CRANK_MODE_TURBO_CW)
            {
                gameScene->crank_turbo_b_active = true;
            }
            else
            {
                gameScene->crank_turbo_a_active = true;
            }
            gameScene->crank_turbo_accumulator += 45.0f;
        }
    }
    else if (gameScene->rewind.active)
    {
        PDButtons held;
        playdate->system->getButtonState(&held, NULL, NULL);

        float crank_change = playdate->system->getCrankChange();
        if (held & (kButtonB | kButtonUp))
        {
            gameScene->rewind.scrub_accumulator += crank_change;

            while (gameScene->rewind.scrub_accumulator >= REWIND_ANGLE_STEP)
            {
                rewind_step_forward(gameScene);
                gameScene->rewind.scrub_accumulator -= REWIND_ANGLE_STEP;
            }
            while (gameScene->rewind.scrub_accumulator <= -REWIND_ANGLE_STEP)
            {
                rewind_step_back(gameScene);
                gameScene->rewind.scrub_accumulator += REWIND_ANGLE_STEP;
            }
        }
        else
        {
            gameScene->rewind.scrub_accumulator = 0.0f;
        }
    }

    // playdate extension IO registers
    uint16_t crank16 = (angle / 360.0f) * 0x10000;

    if (context->gb->direct.ext_crank_menu_indexing)
    {
        int16_t crank_diff =
            context->gb->direct.crank_docked ? 0 : (int16_t)(crank16 - context->gb->direct.crank);

        int new_accumulation = (int)context->gb->direct.crank_menu_accumulation + crank_diff;
        if (new_accumulation <= 0x8000 - CRANK_MENU_DELTA_BINANGLE)
        {
            context->gb->direct.crank_menu_delta--;
            context->gb->direct.crank_menu_accumulation = 0x8000;
        }
        else if (new_accumulation >= 0x8000 + CRANK_MENU_DELTA_BINANGLE)
        {
            context->gb->direct.crank_menu_delta++;
            context->gb->direct.crank_menu_accumulation = 0x8000;
        }
        else
        {
            context->gb->direct.crank_menu_accumulation = (uint16_t)new_accumulation;
        }
    }

    context->gb->direct.crank = crank16;
    context->gb->direct.crank_docked = 0;
}

__section__(".text.tick") __space static void CB_GameScene_update(void* object, uint32_t u32enc_dt)
{
    // This prevents flicker when transitioning to the Library Scene.
    if (CB_App->pendingScene)
    {
        return;
    }

    bool force_all_lines_dirty = false;

    setCrankSoundsEnabled(
        !preferences_crank_dock_button && !preferences_crank_undock_button &&
        preferences_crank_mode != CRANK_MODE_START_SELECT
    );

    float dt = UINT32_AS_FLOAT(u32enc_dt);
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;

    if (gameScene->cgb_needs_palette_recompute)
    {
        gb_recompute_cgb_gray_palettes(context->gb);
        gameScene->cgb_needs_palette_recompute = false;
        pgb_cgb_lut_dirty = true;
        // New game / state load: reseed Auto bias from the manual preset.
        cgb_auto_prev_enabled = 0;
    }

    CB_Scene_update(gameScene->scene, dt);

    if (CB_App->mirror_active != gameScene->is_mirroring)
    {
        gameScene->is_mirroring = CB_App->mirror_active;
        reconfigure_audio_source(gameScene);
    }

    // system volume can change without any event reaching us
    bool desiredAudioEnabled =
        (preferences_sound_mode > 0) && playdate->system->getVolume() > 0.00f;
    if (desiredAudioEnabled != gameScene->audioEnabled)
    {
        reconfigure_audio_source(gameScene);
    }

    float progress = 0.5f;

    // --- Adaptive Frame Skip: Frame-Time Decision ---
    // Uses CB_App->avg_dt_raw to detect when frame times are too tight
    // for 60fps, dropping to 30fps to maintain performance.
    if (preferences_frame_skip == 2)
    {
        float fps = 1.0f / CB_App->avg_dt_raw;
        float target_fps = (gameScene->next_frames_elapsed == 2) ? 30.0f : 60.0f;
        float ratio = fps / target_fps;

        if (gameScene->adaptive_fs_lock_frames > 0)
        {
            gameScene->adaptive_fs_lock_frames--;
            gameScene->adaptive_fs_headroom_counter = 0;
        }
        else if (gameScene->adaptive_fs_perf_allowed)
        {
            // Probe-based deactivation: at 30fps the smoothed FPS always
            // looks good because 2 GB frames fit in 33ms. Instead,
            // periodically render a single GB frame and check raw timing.
            if (gameScene->adaptive_fs_probe_pending)
            {
                float probe_fps = 1.0f / CB_App->dt;
                float probe_ratio = probe_fps / 60.0f;
                gameScene->adaptive_fs_probe_pending = false;
                if (probe_ratio > ADAPTIVE_FS_DEACTIVATE_RATIO)
                {
                    gameScene->adaptive_fs_perf_allowed = false;
                    gameScene->adaptive_fs_headroom_counter = 0;
                    gameScene->adaptive_fs_lock_frames = ADAPTIVE_FS_LOCK_FRAMES;
                }
                else
                {
                    gameScene->adaptive_fs_probe_cooldown = ADAPTIVE_FS_PROBE_INTERVAL;
                }
            }
            else if (gameScene->adaptive_fs_probe_cooldown > 0)
            {
                gameScene->adaptive_fs_probe_cooldown--;
            }
            else
            {
                gameScene->adaptive_fs_probe_pending = true;
            }
        }
        else
        {
            // Currently at 60fps. Switch to 30fps if frame times
            // are too tight to sustain 60fps.
            if (ratio < ADAPTIVE_FS_ACTIVATE_RATIO)
            {
                gameScene->adaptive_fs_headroom_counter++;
                if (gameScene->adaptive_fs_headroom_counter >= ADAPTIVE_FS_ACTIVATE_FRAMES)
                {
                    gameScene->adaptive_fs_perf_allowed = true;
                    gameScene->adaptive_fs_headroom_counter = 0;
                    gameScene->adaptive_fs_lock_frames = ADAPTIVE_FS_LOCK_FRAMES;
                }
            }
            else
            {
                gameScene->adaptive_fs_headroom_counter = 0;
            }
        }
    }

    gameScene->selector.startPressed = false;
    gameScene->selector.selectPressed = false;

    gameScene->crank_turbo_a_active = false;
    gameScene->crank_turbo_b_active = false;

    bool crank_docked = playdate->system->isCrankDocked();

    // Undock + B/Up = enter rewind (universal, bypasses scripts/settings)
    if (gameScene->crank_was_docked && !crank_docked && preferences_rewind_enabled &&
        !gameScene->rewind.active)
    {
        PDButtons held;
        playdate->system->getButtonState(&held, NULL, NULL);
        if (held & (kButtonB | kButtonUp))
            rewind_enter_scrubbing(gameScene);
    }

    if (preferences_crank_undock_button && gameScene->crank_was_docked && !crank_docked &&
        !gameScene->rewind.active)
    {
        if (preferences_crank_undock_button == PREF_BUTTON_START)
            gameScene->button_hold_mode = 2;
        else if (preferences_crank_undock_button == PREF_BUTTON_SELECT)
            gameScene->button_hold_mode = 0;
        else if (preferences_crank_undock_button == PREF_BUTTON_START_SELECT)
            gameScene->button_hold_mode = 3;
        gameScene->button_hold_frames_remaining = 10;
    }
    if (preferences_crank_dock_button && !gameScene->crank_was_docked && crank_docked &&
        !gameScene->rewind.active)
    {
        if (preferences_crank_dock_button == PREF_BUTTON_START)
            gameScene->button_hold_mode = 2;
        else if (preferences_crank_dock_button == PREF_BUTTON_SELECT)
            gameScene->button_hold_mode = 0;
        else if (preferences_crank_dock_button == PREF_BUTTON_START_SELECT)
            gameScene->button_hold_mode = 3;
        gameScene->button_hold_frames_remaining = 10;
    }

    bool was_docked = gameScene->crank_was_docked;
    gameScene->crank_was_docked = crank_docked;

    // Dock transition: exit rewind once
    if (!was_docked && crank_docked && gameScene->rewind.active)
    {
        rewind_exit_scrubbing(gameScene);
    }

    if (!crank_docked)
    {
        crank_update(gameScene, &progress);
    }
    else
    {
        context->gb->direct.crank_docked = 1;
        if (preferences_crank_mode == CRANK_MODE_TURBO_CCW ||
            preferences_crank_mode == CRANK_MODE_TURBO_CW)
        {
            gameScene->crank_turbo_accumulator = 0.0f;
        }
        context->gb->direct.crank_menu_delta = 0;
        context->gb->direct.crank_menu_accumulation = 0x8000;
    }

    if (gameScene->button_hold_frames_remaining > 0)
    {
        if (gameScene->button_hold_mode == 2)
        {
            gameScene->selector.startPressed = true;
            gameScene->selector.selectPressed = false;
            progress = 0.0f;
        }
        else if (gameScene->button_hold_mode == 0)
        {
            gameScene->selector.startPressed = false;
            gameScene->selector.selectPressed = true;
            progress = 1.0f;
        }
        else if (gameScene->button_hold_mode == 3)
        {
            gameScene->selector.startPressed = true;
            gameScene->selector.selectPressed = true;
        }

        gameScene->button_hold_frames_remaining--;

        if (gameScene->button_hold_frames_remaining == 0)
        {
            gameScene->button_hold_mode = 1;
        }
    }

    int selectorIndex;

    if (gameScene->selector.startPressed && gameScene->selector.selectPressed)
    {
        selectorIndex = -1;
    }
    else
    {
        selectorIndex = 1 + floorf(progress * (gameScene->selector.numberOfFrames - 2));

        if (progress == 0)
        {
            selectorIndex = 0;
        }
        else if (progress == 1)
        {
            selectorIndex = gameScene->selector.numberOfFrames - 1;
        }
    }

    gameScene->selector.index = selectorIndex;

    gbScreenRequiresFullRefresh = false;
    if (gameScene->model.empty || gameScene->model.state != gameScene->state ||
        gameScene->model.error != gameScene->error || gameScene->scene->forceFullRefresh)
    {
        gbScreenRequiresFullRefresh = true;
        gameScene->scene->forceFullRefresh = false;
    }

    if (gameScene->model.crank_mode != preferences_crank_mode)
    {
        gameScene->staticSelectorUIDrawn = false;
    }

    // check if game picture bounds have changed
    {
        static unsigned prev_game_picture_x_offset, prev_game_picture_scaling,
            prev_game_picture_y_top, prev_game_picture_y_bottom, prev_game_picture_background_color;

        if (prev_game_picture_x_offset != game_picture_x_offset ||
            prev_game_picture_scaling != game_picture_scaling ||
            prev_game_picture_y_top != game_picture_y_top ||
            prev_game_picture_y_bottom != game_picture_y_bottom ||
            prev_game_picture_background_color != game_picture_background_color)
        {
            gbScreenRequiresFullRefresh = 1;
        }

        prev_game_picture_x_offset = game_picture_x_offset;
        prev_game_picture_scaling = game_picture_scaling;
        prev_game_picture_y_top = game_picture_y_top;
        prev_game_picture_y_bottom = game_picture_y_bottom;
        prev_game_picture_background_color = game_picture_background_color;
    }

    if likely (gameScene->state == CB_GameSceneStateLoaded)
    {
        bool shouldDisplayStartSelectUI =
            (!playdate->system->isCrankDocked() &&
             preferences_crank_mode == CRANK_MODE_START_SELECT && !gameScene->rewind.active) ||
            (gameScene->button_hold_frames_remaining > 0);

        static bool wasSelectorVisible = false;
        if (shouldDisplayStartSelectUI != wasSelectorVisible)
        {
            gameScene->staticSelectorUIDrawn = false;
        }
        wasSelectorVisible = shouldDisplayStartSelectUI;

        bool animatedSelectorBitmapNeedsRedraw = false;

        if (gbScreenRequiresFullRefresh || !gameScene->staticSelectorUIDrawn ||
            gameScene->model.selectorIndex != gameScene->selector.index)
        {
            animatedSelectorBitmapNeedsRedraw = true;
        }

        CB_GameSceneContext* context = gameScene->context;

        PDButtons current_pd_buttons = CB_App->buttons_down;

        bool gb_joypad_start_is_active_low = !(gameScene->selector.startPressed);
        bool gb_joypad_select_is_active_low = !(gameScene->selector.selectPressed);

        context->gb->direct.joypad_bits.start = gb_joypad_start_is_active_low;
        context->gb->direct.joypad_bits.select = gb_joypad_select_is_active_low;

        if unlikely (gameScene->lock_button_hold_frames_remaining > 0)
        {
            --gameScene->lock_button_hold_frames_remaining;
            switch (preferences_lock_button)
            {
            case PREF_BUTTON_START:
                context->gb->direct.joypad_bits.start = 0;
                break;
            case PREF_BUTTON_SELECT:
                context->gb->direct.joypad_bits.select = 0;
                break;
            case PREF_BUTTON_START_SELECT:
                context->gb->direct.joypad_bits.start = 0;
                context->gb->direct.joypad_bits.select = 0;
                break;
            default:
                break;
            }
        }

        context->gb->direct.joypad_bits.a =
            !((current_pd_buttons & kButtonA) || gameScene->crank_turbo_a_active);
        context->gb->direct.joypad_bits.b =
            !((current_pd_buttons & kButtonB) || gameScene->crank_turbo_b_active);
        context->gb->direct.joypad_bits.left = !(current_pd_buttons & kButtonLeft);
        context->gb->direct.joypad_bits.up = !(current_pd_buttons & kButtonUp);
        context->gb->direct.joypad_bits.right = !(current_pd_buttons & kButtonRight);
        context->gb->direct.joypad_bits.down = !(current_pd_buttons & kButtonDown);

        {
            uint8_t curr = context->gb->direct.joypad;
            uint8_t changed = gameScene->prev_joypad ^ curr;
            if (changed)
            {
                context->gb->direct.joypad_interrupt_delay = rand() % (int)SCREEN_REFRESH_CYCLES;
            }
            gameScene->prev_joypad = curr;
        }

        if unlikely (preferences_press_a_b)
        {
            if ((CB_App->buttons_pressed & (kButtonA | kButtonB)) == (kButtonA | kButtonB))
            {
                gameScene->press_a_b_hold = true;
            }
        }

        bool ab_combo_was_active =
            gameScene->press_a_b_hold || gameScene->hold_a_press_b || gameScene->hold_b_press_a;

        if likely ((CB_App->buttons_down & (kButtonA | kButtonB)) != (kButtonA | kButtonB))
        {
            gameScene->press_a_b_hold = false;
            gameScene->hold_a_press_b = false;
            gameScene->hold_b_press_a = false;
        }

        static unsigned char holdpress_button_matrix[] = {3, 4,  8,  12, 5,  9, 13,
                                                          6, 10, 14, 7,  11, 15};

        if unlikely (gameScene->press_a_b_hold)
        {
            unsigned buttons = holdpress_button_matrix[preferences_press_a_b];
            if (!(buttons & 1))
                context->gb->direct.joypad_bits.a = 1;
            if (!(buttons & 2))
                context->gb->direct.joypad_bits.b = 1;
            if ((buttons & 4))
                context->gb->direct.joypad_bits.start = 0;
            if ((buttons & 8))
                context->gb->direct.joypad_bits.select = 0;
        }
        else
        {
            if unlikely (preferences_hold_a_press_b)
            {
                if ((CB_App->buttons_down & kButtonA) && (CB_App->buttons_pressed & kButtonB) &&
                    !(CB_App->buttons_pressed & kButtonA))
                {
                    gameScene->hold_a_press_b = true;
                }
            }

            if unlikely (preferences_hold_b_press_a)
            {
                if ((CB_App->buttons_down & kButtonB) && (CB_App->buttons_pressed & kButtonA) &&
                    !(CB_App->buttons_pressed & kButtonB))
                {
                    gameScene->hold_b_press_a = true;
                }
            }
        }

        if unlikely (gameScene->hold_a_press_b)
        {
            unsigned buttons = holdpress_button_matrix[preferences_hold_a_press_b];
            if (!(buttons & 1))
                context->gb->direct.joypad_bits.a = 1;
            if (!(buttons & 2))
                context->gb->direct.joypad_bits.b = 1;
            if ((buttons & 4))
                context->gb->direct.joypad_bits.start = 0;
            if ((buttons & 8))
                context->gb->direct.joypad_bits.select = 0;
        }

        if unlikely (gameScene->hold_b_press_a)
        {
            unsigned buttons = holdpress_button_matrix[preferences_hold_b_press_a];
            if (!(buttons & 1))
                context->gb->direct.joypad_bits.a = 1;
            if (!(buttons & 2))
                context->gb->direct.joypad_bits.b = 1;
            if ((buttons & 4))
                context->gb->direct.joypad_bits.start = 0;
            if ((buttons & 8))
                context->gb->direct.joypad_bits.select = 0;
        }

        if unlikely (gameScene->hold_ab_release)
        {
            PDButtons remaining = (gameScene->hold_ab_release == 1) ? kButtonB : kButtonA;
            if (CB_App->buttons_pressed)
                gameScene->hold_ab_release_frames = 0;
            else if (gameScene->hold_ab_release_frames > 0)
                --gameScene->hold_ab_release_frames;
            if ((CB_App->buttons_down & (kButtonA | kButtonB) & ~remaining) ||
                (CB_App->buttons_pressed & remaining) ||
                (!(CB_App->buttons_down & remaining) && gameScene->hold_ab_release_frames == 0))
            {
                gameScene->hold_ab_release = 0;
            }
        }

        if unlikely (
            !gameScene->hold_ab_release && (CB_App->buttons_released & (kButtonA | kButtonB))
        )
        {
            PDButtons released = CB_App->buttons_released & (kButtonA | kButtonB);
            PDButtons held =
                CB_App->buttons_down & ~CB_App->buttons_pressed & (kButtonA | kButtonB);

            preference_t value = PREF_BUTTON_ABR_DEFAULT;
            uint8_t which = 0;

            if (released == (kButtonA | kButtonB) || (released == kButtonA && held == kButtonB))
            {
                value = preferences_hold_ab_release_a;
                which = 1;
            }
            else if (released == kButtonB && held == kButtonA)
            {
                value = preferences_hold_ab_release_b;
                which = 2;
            }

            if (value == PREF_BUTTON_ABR_DEFAULT && ab_combo_was_active)
            {
                value = PREF_BUTTON_ABR_NONE;
            }

            if (which && value != PREF_BUTTON_ABR_DEFAULT)
            {
                gameScene->hold_ab_release = which;
                gameScene->hold_ab_release_value = value;
                gameScene->hold_ab_release_frames = 4;
            }
        }

        if unlikely (gameScene->hold_ab_release)
        {
            // bit 0: start, bit 1: select, bit 2: hold the unreleased button
            static const unsigned char abrelease_button_matrix[] = {0, 4, 0, 1, 2, 3, 5, 6, 7};
            unsigned buttons = abrelease_button_matrix[gameScene->hold_ab_release_value];
            if (buttons & 1)
                context->gb->direct.joypad_bits.start = 0;
            if (buttons & 2)
                context->gb->direct.joypad_bits.select = 0;
            if (gameScene->hold_ab_release == 1)
                context->gb->direct.joypad_bits.b = !(buttons & 4);
            else
                context->gb->direct.joypad_bits.a = !(buttons & 4);
        }

        /* Overclock capped at x2 while CGB fast mode is active: keeps the
         * combined vblank cycle shift at most >>2, so inst_cycles stays >= 1
         * and the core needs no clamp. Re-evaluated each frame, so runtime
         * speed switches are covered. */
        context->gb->overclock =
            MIN((unsigned)preferences_overclock, context->gb->cgb_fast_mode_active ? 1 : 2);
        context->gb->cgb_speed_permitted = preferences_cgb_speed == 0;
        /* hle_enabled must imply is_cgb_mode: the DMG core compiles HLE out;
         * a warp there rewinds pc with gb_hle unchecked -> poll-loop hang. */
        context->gb->hle_enabled = (preferences_hle == 1) && context->gb->is_cgb_mode;
        context->gb->lcd = lcd_sources[gameScene->rewind.active ? 0 : preferences_tcm_lcd];

        if (gbScreenRequiresFullRefresh)
        {
            playdate->graphics->clear(game_picture_background_color);
        }

        context->gb->direct.sram_updated = 0;

        memset(context->line_has_changed, 0, sizeof(context->line_has_changed));

        bool skip_frame = false;
        if (context->scene->script)
        {
            skip_frame =
                script_tick(context->scene->script, gameScene, gameScene->next_frames_elapsed);
        }
        gameScene->next_frames_elapsed = 0;

        if (gameScene->rewind.active && !preferences_rewind_enabled)
        {
            rewind_exit_scrubbing(gameScene);
        }

        if (gameScene->rewind.states && !preferences_rewind_enabled)
        {
            rewind_free(gameScene);
        }

        if (preferences_rewind_enabled && !gameScene->rewind.states)
        {
            rewind_init(gameScene);
        }

        if (gameScene->rewind.active)
        {
            force_all_lines_dirty = true;
            playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 0.0f);
        }

        if (!skip_frame)
        {
            if (!gameScene->rewind.active)
            {
                CB_ASSERT(context == context->gb->direct.priv);

                gb_s* tmp_gb = context->gb;

#ifdef TARGET_SIMULATOR
                pthread_mutex_lock(&audio_mutex);
#endif

                // Static buffer for the !dtcm_enabled path to prevent stack overflow on the
                // simulator.
                static char stack_gb_data[sizeof(gb_s)];

                if (!dtcm_enabled())
                {
                    gameScene->audioLocked = 1;
                    memcpy(stack_gb_data, tmp_gb, sizeof(gb_s));
                    context->gb = (void*)stack_gb_data;
                    gameScene->audioLocked = 0;
                }

                void* gb_run_frame_ =
                    (context->gb->is_cgb_mode) ? gb_run_frame__cgb : gb_run_frame__dmg;
#ifdef DTCM_ALLOC
                void (*run_frame_function_pointer)(gb_s*) = ITCM_CORE_FN(gb_run_frame_);
#else
                void (*run_frame_function_pointer)(gb_s*) = gb_run_frame_;
#endif

                pgb_dirty_skip = context->gb->is_cgb_mode ||
                                 (preferences_blend_frames && preferences_frame_skip != 0);

                if (context->gb->is_cgb_mode)
                {
                    // --- CGB Dual-Output Blending ---
                    if (preferences_cgb_bias_auto == 2)
                    {
                        // Contrast: per-scene percentile thresholds.
                        cgb_gray_bias = 0;
                        if (cgb_auto_prev_enabled != 2)
                        {
                            // Freshly enabled: start from linear Neutral over
                            // the palette range; the first post-render update
                            // refines from the frame histogram.
                            uint8_t pal_min, pal_max;
                            cgb_palette_lum_range(context->gb, &pal_min, &pal_max);
                            uint16_t pal_range = (uint16_t)pal_max - pal_min;
                            if (pal_range == 0)
                                pal_range = 1;
                            cgb_thresh[0] = pal_min + ((pal_range * 6) >> 3);
                            cgb_thresh[1] = pal_min + ((pal_range * 4) >> 3);
                            cgb_thresh[2] = pal_min + ((pal_range * 2) >> 3);
                            cgb_thresh_delta = pal_range >> 3;
                            // Derive guard state from the first frame
                            // histogram, not a forced value.
                            cgb_contrast_init_pending = true;
                        }
                        cgb_contrast_active = 1;
                    }
                    else if (preferences_cgb_bias_auto == 1)
                    {
                        if (cgb_auto_prev_enabled != 1)
                        {
                            // Freshly enabled: seed from manual so there's no
                            // abrupt bias jump on toggle.
                            cgb_auto_bias = (int8_t)preferences_cgb_blend_bias - 2;
                            if (cgb_auto_bias > 1)
                                cgb_auto_bias = 1;
                            if (cgb_auto_bias < -1)
                                cgb_auto_bias = -1;
                            cgb_auto_holddown = 0;
                        }
                        cgb_gray_bias = cgb_auto_bias;
                        cgb_contrast_active = 0;
                    }
                    else
                    {
                        cgb_gray_bias = (int8_t)preferences_cgb_blend_bias - 2;
                        cgb_contrast_active = 0;
                    }
                    cgb_hist_active = (preferences_cgb_bias_auto != 0);
                    if (preferences_cgb_bias_auto != cgb_auto_prev_enabled)
                    {
                        // Mode toggle: rebuild gray LUTs + histogram tables,
                        // and update immediately on the first frame.
                        pgb_cgb_lut_dirty = true;
                        cgb_hist_phase = 0;
                        if (preferences_cgb_bias_auto != 0 && cgb_auto_prev_enabled == 0)
                        {
                            // Entering from Manual: hooks were off, drop any
                            // stale usage counts.
                            memset(cgb_bg_usage, 0, sizeof(cgb_bg_usage));
                            memset(cgb_obj_usage, 0, sizeof(cgb_obj_usage));
                            cgb_bg_used = 0;
                        }
                    }
                    cgb_auto_prev_enabled = preferences_cgb_bias_auto;
                    if (cgb_gray_bias != gameScene->last_cgb_bias)
                    {
                        gameScene->last_cgb_bias = cgb_gray_bias;
                        pgb_cgb_lut_dirty = true;
                    }
                    static clalign uint8_t cb_frame_buffer[4][LCD_BUFFER_BYTES];

                    uint8_t* original_lcd = context->gb->lcd;

                    if (preferences_frame_skip == 1 && preferences_blend_frames)
                    {
                        // --- CGB 30fps Consecutive-Frame Blending (simple) ---
                        // Frame N (bright)
                        cgb_blend_stage = 1;
                        gb_recompute_cgb_gray_palettes(context->gb);
                        context->gb->lcd = cb_frame_buffer[0];
                        context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                        DTCM_VERIFY_DEBUG();
                        run_frame_function_pointer(context->gb);
                        DTCM_VERIFY_DEBUG();
#else
                        run_frame_function_pointer(context->gb);
#endif
                        ++gameScene->next_frames_elapsed;
                        tick_audio_sync(gameScene);

                        // Frame N+1 (dark, one shade brighter)
                        // Build stage-2 (dark) LUTs into bright slots (offset 0) so
                        // renderer reads them without dual_output. +1 bias = lighter.
                        cgb_blend_stage = 2;
                        __cgb_scan_luminance_range(context->gb);
                        int8_t saved_bias = cgb_gray_bias;
                        cgb_gray_bias += 1;
                        for (int i = 0; i < 8; i++)
                        {
                            __cgb_update_bg_gray_palette(context->gb, i, 0);
                            __cgb_update_obj_gray_palette(
                                context->gb, i, context->gb->cgb_obj_palette_gray
                            );
                        }
                        cgb_gray_bias = saved_bias;
                        cgb_blend_stage = 1;

                        context->gb->lcd = original_lcd;
                        context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                        DTCM_VERIFY_DEBUG();
                        run_frame_function_pointer(context->gb);
                        DTCM_VERIFY_DEBUG();
#else
                        run_frame_function_pointer(context->gb);
#endif
                        ++gameScene->next_frames_elapsed;
                        tick_audio_sync(gameScene);

                        // blend in place: bright N (cb[0]) + dark N+1 (original_lcd)
                        blend_frames(
                            cb_frame_buffer[0], original_lcd, context->previous_lcd,
                            context->line_has_changed
                        );
                    }
                    else
                    {
                        // Gray/blend LUTs only change with palette writes or
                        // bias changes; rebuild them (and the identity check)
                        // only then, not per frame.
                        if (pgb_cgb_lut_dirty)
                        {
                            cgb_blend_stage = 1;
                            gb_recompute_cgb_gray_palettes(context->gb);
                            cgb_blend_stage = 2;
                            gb_recompute_cgb_gray_palettes(context->gb);
                            cgb_blend_stage = 1;

                            // Identity: stage-1 and stage-2 remap outputs
                            // agree for every color -> same-frame blend is a no-op.
                            gameScene->cgb_blend_identity =
                                (memcmp(
                                     context->gb->cgb_bg_palette + 64,
                                     context->gb->cgb_bg_palette + 64 + 8 * 256, 8 * 256
                                 ) == 0) &&
                                (memcmp(
                                     context->gb->cgb_obj_palette_gray,
                                     context->gb->cgb_obj_palette_gray_alt, 8
                                 ) == 0);

                            if (!gameScene->cgb_blend_identity)
                                __cgb_build_blend_luts(context->gb);

                            pgb_cgb_lut_dirty = false;
                        }
                        const bool blend_identity = gameScene->cgb_blend_identity;

                        if (preferences_frame_skip == 2 && preferences_blend_frames)
                        {
                            // --- CGB Adaptive Consecutive-Frame Blending ---
                            if (blend_identity)
                            {
                                // LUTs identical: dual output would render the
                                // same pixels twice - render single-output.
                                // Frame N -> cb[0]
                                context->gb->lcd = cb_frame_buffer[0];
                                context->gb->lcd_alt = NULL;
                                context->gb->direct.frame_skip = 0;
                                context->gb->direct.cgb_dual_output = false;
#ifdef DTCM_ALLOC
                                DTCM_VERIFY_DEBUG();
                                run_frame_function_pointer(context->gb);
                                DTCM_VERIFY_DEBUG();
#else
                                run_frame_function_pointer(context->gb);
#endif
                                ++gameScene->next_frames_elapsed;
                                tick_audio_sync(gameScene);

                                if (gameScene->adaptive_fs_perf_allowed &&
                                    !gameScene->adaptive_fs_probe_pending)
                                {
                                    // Frame N+1 -> original_lcd; temporal blend
                                    // still needed (different frames)
                                    context->gb->lcd = original_lcd;
                                    context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                                    DTCM_VERIFY_DEBUG();
                                    run_frame_function_pointer(context->gb);
                                    DTCM_VERIFY_DEBUG();
#else
                                    run_frame_function_pointer(context->gb);
#endif
                                    ++gameScene->next_frames_elapsed;
                                    tick_audio_sync(gameScene);

                                    blend_frames(
                                        cb_frame_buffer[0], original_lcd, context->previous_lcd,
                                        context->line_has_changed
                                    );
                                }
                                else
                                {
                                    // same-frame blend would be identity
                                    blit_tracked(
                                        cb_frame_buffer[0], original_lcd, context->previous_lcd,
                                        context->line_has_changed
                                    );
                                }
                            }
                            else
                            {
                                // Frame N: dual output -> cb[0]=bright, original_lcd=dark
                                context->gb->lcd = cb_frame_buffer[0];
                                context->gb->lcd_alt = original_lcd;
                                context->gb->direct.frame_skip = 0;
                                context->gb->direct.cgb_dual_output = true;
#ifdef DTCM_ALLOC
                                DTCM_VERIFY_DEBUG();
                                run_frame_function_pointer(context->gb);
                                DTCM_VERIFY_DEBUG();
#else
                                run_frame_function_pointer(context->gb);
#endif
                                context->gb->direct.cgb_dual_output = false;
                                ++gameScene->next_frames_elapsed;
                                tick_audio_sync(gameScene);

                                if (gameScene->adaptive_fs_perf_allowed &&
                                    !gameScene->adaptive_fs_probe_pending)
                                {
                                    // Frame N+1: bright N stays in cb[0]; bright N+1 -> cb[1]
                                    // (unused), dark N+1 -> original_lcd (clobbers dark N,
                                    // which this branch doesn't use)
                                    context->gb->lcd = cb_frame_buffer[1];
                                    context->gb->lcd_alt = original_lcd;
                                    context->gb->direct.frame_skip = 0;
                                    context->gb->direct.cgb_dual_output = true;
#ifdef DTCM_ALLOC
                                    DTCM_VERIFY_DEBUG();
                                    run_frame_function_pointer(context->gb);
                                    DTCM_VERIFY_DEBUG();
#else
                                    run_frame_function_pointer(context->gb);
#endif
                                    context->gb->direct.cgb_dual_output = false;
                                    ++gameScene->next_frames_elapsed;
                                    tick_audio_sync(gameScene);
                                    // blend: bright N (cb[0]) + dark N+1 (original_lcd)
                                }
                                // else blend: bright N (cb[0]) + dark N (original_lcd)

                                // both branches: blend in place, no memcpy
                                blend_frames(
                                    cb_frame_buffer[0], original_lcd, context->previous_lcd,
                                    context->line_has_changed
                                );
                            }
                        }
                        else
                        {
                            // --- Dual-Output Blending (60fps / 30fps / Adaptive) ---
                            if (blend_identity)
                            {
                                // LUTs identical: single-output render directly
                                // into the display buffer; blend is a no-op.
                                context->gb->lcd = original_lcd;
                                context->gb->lcd_alt = NULL;
                                context->gb->direct.frame_skip = 0;
                                context->gb->direct.cgb_dual_output = false;
#ifdef DTCM_ALLOC
                                DTCM_VERIFY_DEBUG();
                                run_frame_function_pointer(context->gb);
                                DTCM_VERIFY_DEBUG();
#else
                                run_frame_function_pointer(context->gb);
#endif
                                track_changes(
                                    original_lcd, context->previous_lcd, context->line_has_changed
                                );
                            }
                            else
                            {
                                // Merged blend: render once with pre-blended
                                // LUTs (rebuilt in the dirty-gate above).
                                // Output is bit-identical to dual render+blend.
                                pgb_blend_merged = true;
                                context->gb->lcd = original_lcd;
                                context->gb->lcd_alt = NULL;
                                context->gb->direct.frame_skip = 0;
                                context->gb->direct.cgb_dual_output = false;

#ifdef DTCM_ALLOC
                                DTCM_VERIFY_DEBUG();
                                run_frame_function_pointer(context->gb);
                                DTCM_VERIFY_DEBUG();
#else
                                run_frame_function_pointer(context->gb);
#endif
                                pgb_blend_merged = false;

                                track_changes(
                                    original_lcd, context->previous_lcd, context->line_has_changed
                                );
                            }

                            ++gameScene->next_frames_elapsed;
                            tick_audio_sync(gameScene);

                            bool run_second_frame = false;
                            if (preferences_frame_skip == 2)
                            {
                                run_second_frame = gameScene->adaptive_fs_perf_allowed &&
                                                   !gameScene->adaptive_fs_probe_pending;
                            }
                            else if (preferences_frame_skip == 1)
                            {
                                run_second_frame = true;
                            }

                            if (run_second_frame)
                            {
                                context->gb->direct.frame_skip = 1;
#ifdef DTCM_ALLOC
                                DTCM_VERIFY_DEBUG();
                                run_frame_function_pointer(context->gb);
                                DTCM_VERIFY_DEBUG();
#else
                                run_frame_function_pointer(context->gb);
#endif
                                ++gameScene->next_frames_elapsed;
                                tick_audio_sync(gameScene);
                            }
                        }

                        context->gb->lcd = original_lcd;
                    }
                }
                else if (preferences_frame_skip == 1 && preferences_blend_frames)
                {
                    // --- 30fps Frame Blending with Double Buffering ---
                    // Two buffers to avoid memcpy - swap lcd pointer instead
                    static clalign uint8_t frame_buffer[2][LCD_BUFFER_BYTES];

                    // Save original lcd pointer
                    uint8_t* original_lcd = context->gb->lcd;

                    // 1. Render Frame A into frame_buffer[0]
                    context->gb->lcd = frame_buffer[0];
                    context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                    DTCM_VERIFY_DEBUG();
                    run_frame_function_pointer(context->gb);
                    DTCM_VERIFY_DEBUG();
#else
                    run_frame_function_pointer(context->gb);
#endif
                    ++gameScene->next_frames_elapsed;
                    tick_audio_sync(gameScene);

                    // 2. Render Frame B directly into the display buffer
                    context->gb->lcd = original_lcd;
                    context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                    DTCM_VERIFY_DEBUG();
                    run_frame_function_pointer(context->gb);
                    DTCM_VERIFY_DEBUG();
#else
                    run_frame_function_pointer(context->gb);
#endif
                    ++gameScene->next_frames_elapsed;
                    tick_audio_sync(gameScene);

                    // 3. Blend in place - no memcpy
                    blend_frames(
                        frame_buffer[0], original_lcd, context->previous_lcd,
                        context->line_has_changed
                    );

                    context->gb->lcd = original_lcd;
                }
                else if (preferences_frame_skip == 2 && preferences_blend_frames)
                {
                    // --- DMG Adaptive Frame Blending ---
                    static clalign uint8_t frame_buffer[2][LCD_BUFFER_BYTES];
                    uint8_t* original_lcd = context->gb->lcd;

                    // Frame N -> buffer[0]
                    context->gb->lcd = frame_buffer[0];
                    context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                    DTCM_VERIFY_DEBUG();
                    run_frame_function_pointer(context->gb);
                    DTCM_VERIFY_DEBUG();
#else
                    run_frame_function_pointer(context->gb);
#endif
                    ++gameScene->next_frames_elapsed;
                    tick_audio_sync(gameScene);

                    if (gameScene->adaptive_fs_perf_allowed &&
                        !gameScene->adaptive_fs_probe_pending)
                    {
                        // Frame N+1 -> directly into the display buffer
                        context->gb->lcd = original_lcd;
                        context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                        DTCM_VERIFY_DEBUG();
                        run_frame_function_pointer(context->gb);
                        DTCM_VERIFY_DEBUG();
#else
                        run_frame_function_pointer(context->gb);
#endif
                        ++gameScene->next_frames_elapsed;
                        tick_audio_sync(gameScene);

                        // blend in place - no memcpy
                        blend_frames(
                            frame_buffer[0], original_lcd, context->previous_lcd,
                            context->line_has_changed
                        );
                    }
                    else
                    {
                        blit_tracked(
                            frame_buffer[0], original_lcd, context->previous_lcd,
                            context->line_has_changed
                        );
                    }

                    context->gb->lcd = original_lcd;
                }
                else
                {
                    // --- Non-blended logic (60fps / 30fps / Adaptive) ---
                    context->gb->direct.frame_skip = 0;
#ifdef DTCM_ALLOC
                    DTCM_VERIFY_DEBUG();
                    run_frame_function_pointer(context->gb);
                    DTCM_VERIFY_DEBUG();
#else
                    run_frame_function_pointer(context->gb);
#endif
                    ++gameScene->next_frames_elapsed;
                    tick_audio_sync(gameScene);

                    bool run_second_frame = false;
                    if (preferences_frame_skip == 2)
                    {
                        run_second_frame = gameScene->adaptive_fs_perf_allowed &&
                                           !gameScene->adaptive_fs_probe_pending;
                    }
                    else if (preferences_frame_skip == 1)
                    {
                        run_second_frame = true;
                    }

                    if (run_second_frame)
                    {
                        context->gb->direct.frame_skip = 1;
#ifdef DTCM_ALLOC
                        DTCM_VERIFY_DEBUG();
                        run_frame_function_pointer(context->gb);
                        DTCM_VERIFY_DEBUG();
#else
                        run_frame_function_pointer(context->gb);
#endif
                        ++gameScene->next_frames_elapsed;
                        tick_audio_sync(gameScene);
                    }
                }

                CB_App->avg_dt_mult =
                    (gameScene->next_frames_elapsed == 2 && preferences_display_fps == 1) ? 0.5f
                                                                                          : 1.0f;
                gameScene->playtime += gameScene->next_frames_elapsed;

                if (preferences_rewind_enabled && !context->gb->is_cgb_mode &&
                    gameScene->rewind.states)
                {
                    gameScene->rewind.frame_counter += gameScene->next_frames_elapsed;
                }

                if (!dtcm_enabled())
                {
                    gameScene->audioLocked = 1;
                    memcpy(tmp_gb, context->gb, sizeof(gb_s));
                    context->gb = tmp_gb;
                    gameScene->audioLocked = 0;
                }

#ifdef TARGET_SIMULATOR
                pthread_mutex_unlock(&audio_mutex);
#endif

                if (gameScene->cartridge_has_battery)
                {
                    save_check(context->gb);
                }
            }  // if (!gameScene->rewind.active)

            // LCD ghosting (DMG only): slew-rate smear, applied to the final
            // presented frame (post-blend). Runs after generation/blending so
            // its dirty bits merge with the core's raw ones, and before
            // update_fb_dirty_lines consumes them.
            if (preferences_ghosting && !context->gb->is_cgb_mode && !gameScene->rewind.active)
            {
                if (context->ghost_resnap)
                {
                    memcpy(context->ghost_state, context->gb->lcd, LCD_BUFFER_BYTES);
                    context->ghost_converged = true;
                    context->ghost_resnap = false;
                    context->ghost_phase = 0;
                }
                else
                {
                    bool do_step = false;
                    if (gameScene->next_frames_elapsed >= 2)
                    {
                        do_step = true;
                        context->ghost_phase = 0;
                    }
                    else if (++context->ghost_phase >= GHOST_STEP_INTERVAL)
                    {
                        context->ghost_phase = 0;
                        do_step = true;
                    }

                    bool has_dirty = false;
                    if (context->ghost_converged)
                    {
                        for (int i = 0; i < LCD_HEIGHT / 16; i++)
                            if (context->line_has_changed[i])
                            {
                                has_dirty = true;
                                break;
                            }
                    }

                    if (!context->ghost_converged || has_dirty)
                    {
                        ghost_pass(
                            context->gb->lcd, context->ghost_state, context->line_has_changed,
                            do_step, &context->ghost_converged
                        );
                    }
                }
            }

            // --- Conditional Screen Update (Drawing) Logic ---
            uint8_t* current_lcd = context->gb->lcd;
            uint8_t* dither_lut0 = CB_dither_lut_row0;
            uint8_t* dither_lut1 = CB_dither_lut_row1;
            int scy = context->gb->gb_reg.SCY;
            bool stable_scaling_enabled = preferences_dither_stable;

            if (context->gb->is_cgb_mode && preferences_cgb_bias_auto != 0)
            {
                cgb_hist_phase ^= 1;
                if (cgb_hist_phase)
                {
                    if (preferences_cgb_bias_auto == 2)
                        cgb_auto_contrast_update(context->gb);
                    else
                        cgb_auto_update();
                }
            }

            const unsigned scaling = game_picture_scaling ? game_picture_scaling : 0x1000;
            if (preferences_dither_stable && scy % scaling != last_scy % scaling)
            {
                force_all_lines_dirty = true;
                last_scy = scy;
            }

            if (gbScreenRequiresFullRefresh || force_all_lines_dirty)
            {
                for (int i = 0; i < LCD_HEIGHT / 16; i++)
                    context->line_has_changed[i] = 0xFFFF;
            }

            update_fb_dirty_lines(
                playdate->graphics->getFrame(), current_lcd, context->line_has_changed,
                playdate->graphics->markUpdatedRows, scy, stable_scaling_enabled, dither_lut0,
                dither_lut1
            );

            if (preferences_rewind_enabled && !context->gb->is_cgb_mode &&
                gameScene->rewind.states && !gameScene->rewind.active &&
                gameScene->rewind.frame_counter >= REWIND_CAPTURE_INTERVAL)
            {
                rewind_record_state(gameScene);
                gameScene->rewind.frame_counter = 0;
            }

            if (gameScene->rewind.active)
            {
                int display_height = playdate->display->getHeight();
                // Scanlines - black line every 3 rows across full width
                for (int y = 0; y < display_height; y += 3)
                    playdate->graphics->fillRect(0, y, 400, 1, kColorBlack);
                // VHS noise bands - drawn once per crank step
                if (gameScene->rewind.noise_pending)
                {
                    rewind_draw_noise_bands();
                    gameScene->rewind.noise_pending = false;
                }
                // Seekbar - unrecorded left, recorded right, fill from current
                if (gameScene->rewind.count > 0)
                {
                    int oldest = gameScene->rewind.buffer_oldest;
                    int read_idx = gameScene->rewind.read_idx;
                    int cap = gameScene->rewind.capacity;
                    int count = gameScene->rewind.count;
                    int pos =
                        (read_idx >= oldest) ? (read_idx - oldest) : (cap - oldest + read_idx);

                    int bar_x = (int)game_picture_x_offset;
                    int bar_w = LCD_COLUMNS - 2 * CB_LCD_X;
                    int bar_y = display_height - 4;

                    // Top border
                    playdate->graphics->fillRect(bar_x, bar_y, bar_w, 1, kColorBlack);

                    // Unrecorded - dithered (left, unreachable past)
                    int unrec_w = (int)((float)(cap - count) / (float)(cap - 1) * (float)bar_w);
                    for (int x = bar_x; x < bar_x + unrec_w; x += 2)
                        playdate->graphics->fillRect(x, bar_y + 1, 1, 3, kColorWhite);

                    // Recorded - white background (right)
                    int rec_x = bar_x + unrec_w;
                    int rec_w = bar_w - unrec_w;
                    if (rec_w > 0)
                        playdate->graphics->fillRect(rec_x, bar_y + 1, rec_w, 3, kColorWhite);

                    // Black fill - from current position to right edge
                    float frac = (count > 1) ? (float)pos / (float)(count - 1) : 0.0f;
                    int fill_x = rec_x + (int)(frac * (float)rec_w);
                    int fill_w = bar_x + bar_w - fill_x;
                    if (fill_w > 0)
                        playdate->graphics->fillRect(fill_x, bar_y + 1, fill_w, 3, kColorBlack);
                }

                if (gameScene->rewind.show_help)
                {
                    const char* text = T(game_rewind_help);
                    int tw = playdate->graphics->getTextWidth(
                        CB_App->labelFont, text, strlen(text), kUTF8Encoding, 0
                    );
                    int fh = playdate->graphics->getFontHeight(CB_App->labelFont);
                    int tx = (int)game_picture_x_offset + LCD_COLUMNS - 2 * CB_LCD_X - tw - 6;
                    int ty = (int)game_picture_y_top + 10;
                    playdate->graphics->fillRect(tx - 2, ty - 2, tw + 4, fh + 4, kColorBlack);
                    playdate->graphics->setFont(CB_App->labelFont);
                    playdate->graphics->setDrawMode(kDrawModeFillWhite);
                    playdate->graphics->drawText(text, strlen(text), kUTF8Encoding, tx, ty + 1);
                    playdate->graphics->setDrawMode(kDrawModeCopy);
                }
            }

            // Always request the update loop to run at 30 FPS.
            // (60 game boy frames per second.)
            // This ensures gb_run_frame() is called at a consistent rate.
            gameScene->scene->preferredRefreshRate =
                (gameScene->next_frames_elapsed == 2) ? 30 : 60;

            if (preferences_uncap_fps)
                gameScene->scene->preferredRefreshRate = -1;

            if (gameScene->cartridge_has_rtc)
            {
                // Check RTC once per second (60 frames at 60fps, 30 frames at 30fps)
                static int rtc_frame_counter = 0;
                int rtc_check_interval = (preferences_frame_skip == 1) ? 30 : 60;

                if (++rtc_frame_counter >= rtc_check_interval)
                {
                    rtc_frame_counter = 0;

                    // Get the current time from the system clock.
                    unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);

                    // Check if time has passed since our last check.
                    if (now > gameScene->rtc_time)
                    {
                        unsigned int seconds_passed = now - gameScene->rtc_time;
                        gameScene->rtc_seconds_to_catch_up += seconds_passed;
                        gameScene->rtc_time = now;
                    }

                    if (gameScene->rtc_seconds_to_catch_up > 0)
                    {
                        gb_catch_up_rtc_direct(context->gb, gameScene->rtc_seconds_to_catch_up);
                        gameScene->rtc_seconds_to_catch_up = 0;
                    }
                }
            }

            if (!game_hide_indicator &&
                (!gameScene->staticSelectorUIDrawn || gbScreenRequiresFullRefresh))
            {
                // Clear the right sidebar area before redrawing any static UI.
                const int rightBarX = 40 + 320;
                const int rightBarWidth = 40;
                playdate->graphics->fillRect(
                    rightBarX, 0, rightBarWidth, playdate->display->getHeight(),
                    game_picture_background_color
                );
            }

            if (context->scene->script)
            {
                script_draw(context->scene->script, gameScene);
            }

            if (!game_hide_indicator &&
                (!gameScene->staticSelectorUIDrawn || gbScreenRequiresFullRefresh))
            {
                // Draw the text labels ("Start/Select") if needed.
                if (shouldDisplayStartSelectUI)
                {
                    playdate->graphics->setFont(CB_App->labelFont);
                    playdate->graphics->setDrawMode(
                        game_invert_indicator ? kDrawModeFillBlack : kDrawModeFillWhite
                    );
                    playdate->graphics->drawText(
                        startButtonText, cb_strlen(startButtonText), kUTF8Encoding,
                        gameScene->selector.startButtonX, gameScene->selector.startButtonY
                    );
                    playdate->graphics->drawText(
                        selectButtonText, cb_strlen(selectButtonText), kUTF8Encoding,
                        gameScene->selector.selectButtonX, gameScene->selector.selectButtonY
                    );
                }

                // Draw the "Turbo" indicator if needed.
                if ((preferences_crank_mode == CRANK_MODE_TURBO_CW ||
                     preferences_crank_mode == CRANK_MODE_TURBO_CCW) &&
                    !gameScene->rewind.active)
                {
                    playdate->graphics->setFont(CB_App->labelFont);
                    playdate->graphics->setDrawMode(
                        game_invert_indicator ? kDrawModeFillBlack : kDrawModeFillWhite
                    );

                    const char* line1 = T(game_turbo);
                    const char* line2 = (preferences_crank_mode == CRANK_MODE_TURBO_CW)
                                            ? T(game_turbo_ab)
                                            : T(game_turbo_ba);

                    int fontHeight = playdate->graphics->getFontHeight(CB_App->labelFont);
                    int lineSpacing = 2;
                    int paddingBottom = 6;

                    int line1Width = playdate->graphics->getTextWidth(
                        CB_App->labelFont, line1, strlen(line1), kUTF8Encoding, 0
                    );
                    int line2Width = playdate->graphics->getTextWidth(
                        CB_App->labelFont, line2, strlen(line2), kUTF8Encoding, 0
                    );

                    const int rightBarX = 40 + 320;
                    const int rightBarWidth = 40;

                    int bottomEdge = playdate->display->getHeight();
                    int y2 = bottomEdge - paddingBottom - fontHeight;
                    int y1 = y2 - fontHeight - lineSpacing;

                    int x1 = rightBarX + (rightBarWidth - line1Width) / 2;
                    int x2 = rightBarX + (rightBarWidth - line2Width) / 2;

                    playdate->graphics->drawText(line1, strlen(line1), kUTF8Encoding, x1, y1);
                    playdate->graphics->drawText(line2, strlen(line2), kUTF8Encoding, x2, y2);

                    playdate->graphics->setDrawMode(kDrawModeCopy);
                }

                playdate->graphics->setDrawMode(
                    game_invert_indicator ? kDrawModeInverted : kDrawModeCopy
                );

                if (shouldDisplayStartSelectUI)
                {
                    LCDBitmap* bitmap;
                    if (gameScene->selector.index < 0)
                    {
                        bitmap = CB_App->startSelectBitmap;
                    }
                    else
                    {
                        bitmap = playdate->graphics->getTableBitmap(
                            CB_App->selectorBitmapTable, gameScene->selector.index
                        );
                    }
                    playdate->graphics->drawBitmap(
                        bitmap, gameScene->selector.x, gameScene->selector.y, kBitmapUnflipped
                    );
                }

                gameScene->staticSelectorUIDrawn = true;
            }
            else if (
                !game_hide_indicator &&
                (animatedSelectorBitmapNeedsRedraw && shouldDisplayStartSelectUI)
            )
            {
                playdate->graphics->setDrawMode(
                    game_invert_indicator ? kDrawModeInverted : kDrawModeCopy
                );
                playdate->graphics->fillRect(
                    gameScene->selector.x, gameScene->selector.y, gameScene->selector.width,
                    gameScene->selector.height, game_picture_background_color
                );

                LCDBitmap* bitmap;
                // Use gameScene->selector.index, which is the most current
                // calculated frame
                if (gameScene->selector.index < 0)
                {
                    bitmap = CB_App->startSelectBitmap;
                }
                else
                {
                    bitmap = playdate->graphics->getTableBitmap(
                        CB_App->selectorBitmapTable, gameScene->selector.index
                    );
                }
                playdate->graphics->drawBitmap(
                    bitmap, gameScene->selector.x, gameScene->selector.y, kBitmapUnflipped
                );

                playdate->graphics->markUpdatedRows(
                    gameScene->selector.y, gameScene->selector.y + gameScene->selector.height - 1
                );
            }

            playdate->graphics->setDrawMode(kDrawModeCopy);

            if (gameScene->fade_frames > 0)
            {
                screen_fade(gameScene, gameScene->next_frames_elapsed);
            }

            if (preferences_display_fps)
            {
                cb_render_fps();
            }
        }
    }
    else if (gameScene->state == CB_GameSceneStateError)
    {
        // Check for pushed A or B button to return to the library
        PDButtons pushed;
        playdate->system->getButtonState(NULL, &pushed, NULL);

        if ((pushed & kButtonA) || (pushed & kButtonB))
        {
            CB_GameScene_didSelectLibrary(gameScene);
            return;
        }

        gameScene->scene->preferredRefreshRate = 30;

        if (gbScreenRequiresFullRefresh)
        {
            const char* errorTitle = T(gameerr_title_default);

            int errorMessagesCount = 1;
            const char* errorMessages[4];

            errorMessages[0] = T(gameerr_generic);

            if (gameScene->error == CB_GameSceneErrorLoadingRom)
            {
                errorMessages[0] = T(gameerr_load_rom);
            }
            else if (gameScene->error == CB_GameSceneErrorWrongLocation)
            {
                errorTitle = T(gameerr_wrong_location_title);
                errorMessagesCount = 2;
                errorMessages[0] = T(gameerr_wrong_location);
                errorMessages[1] = cb_gb_directory_path(CB_gamesPath);
            }
            else if (gameScene->error == CB_GameSceneErrorSaveData)
            {
                errorTitle = T(gameerr_save_data_title);
                errorMessagesCount = 1;
                errorMessages[0] = T(gameerr_save_data);
            }
            else if (gameScene->error == CB_GameSceneErrorFatal)
            {
                errorMessages[0] = T(gameerr_fatal);
            }

            errorMessages[errorMessagesCount++] = "";
            if (CB_App->bundled_rom)
            {
                errorMessages[errorMessagesCount++] = T(gameerr_return_quit);
            }
            else
            {
                errorMessages[errorMessagesCount++] = T(gameerr_return_library);
            }

            playdate->graphics->clear(kColorWhite);

            int titleToMessageSpacing = 6;

            int titleHeight = playdate->graphics->getFontHeight(CB_App->titleFont);
            int lineSpacing = 2;
            int messageHeight = playdate->graphics->getFontHeight(CB_App->bodyFont);
            int messagesHeight =
                messageHeight * errorMessagesCount + lineSpacing * (errorMessagesCount - 1);

            int containerHeight = titleHeight + titleToMessageSpacing + messagesHeight;

            int titleX =
                (float)(playdate->display->getWidth() -
                        playdate->graphics->getTextWidth(
                            CB_App->titleFont, errorTitle, strlen(errorTitle), kUTF8Encoding, 0
                        )) /
                2;
            int titleY = (float)(playdate->display->getHeight() - containerHeight) / 2;

            playdate->graphics->setFont(CB_App->titleFont);
            playdate->graphics->drawText(
                errorTitle, strlen(errorTitle), kUTF8Encoding, titleX, titleY
            );

            int messageY = titleY + titleHeight + titleToMessageSpacing;

            for (int i = 0; i < errorMessagesCount; i++)
            {
                const char* errorMessage = errorMessages[i];
                int messageX = (float)(playdate->display->getWidth() -
                                       playdate->graphics->getTextWidth(
                                           CB_App->bodyFont, errorMessage, strlen(errorMessage),
                                           kUTF8Encoding, 0
                                       )) /
                               2;

                playdate->graphics->setFont(CB_App->bodyFont);
                playdate->graphics->drawText(
                    errorMessage, strlen(errorMessage), kUTF8Encoding, messageX, messageY
                );

                messageY += messageHeight + lineSpacing;
            }

            gameScene->staticSelectorUIDrawn = false;
        }
    }
    gameScene->model.empty = false;
    gameScene->model.state = gameScene->state;
    gameScene->model.error = gameScene->error;
    gameScene->model.selectorIndex = gameScene->selector.index;
    gameScene->model.crank_mode = preferences_crank_mode;
}

__section__(".text.tick") __space static void save_check(gb_s* gb)
{
    static uint32_t frames_since_sram_update;

    gb->direct.sram_dirty |= gb->direct.sram_updated;

    if (gb->direct.sram_updated)
    {
        frames_since_sram_update = 0;
    }
    else
    {
        frames_since_sram_update++;
    }

    if (gb->cart_battery && gb->direct.sram_dirty && !gb->direct.sram_updated)
    {
        // With audio sync enabled, idle-saving can cause audio under-runs.
        // In this case, we rely on saving when the menu is opened or the system is locked.
        if (preferences_sound_mode != 2 && frames_since_sram_update >= CB_IDLE_FRAMES_BEFORE_SAVE)
        {
            playdate->system->logToConsole("Saving (idle detected)");
            gb_save_to_disk(gb);
        }
    }
}

const char* loadStateErrorOptions[3];

__section__(".rare") static void screen_fade(CB_GameScene* gameScene, int frame_advance)
{
    if ((gameScene->context->gb->gb_reg.LCDC & LCDC_ENABLE) || gameScene->fade_frames < 20)
    {
        int nf = (int)gameScene->fade_frames - frame_advance;
        gameScene->fade_frames = nf >= 0 ? nf : 0;
        gameScene->scene->forceFullRefresh = true;
    }

    cb_render_boot_fade(gameScene->fade_frames, gameScene->fade_white);
}

__section__(".rare") static void CB_LoadStateErrorModalCallback(void* userdata, int option)
{
    char* details = (char*)userdata;
    if (option == 1 && details)
    {
        CB_presentModal(CB_Modal_new(details, NULL, NULL, NULL)->scene);
    }
    if (details)
        cb_free(details);
}

void CB_LibraryConfirmModal(void* userdata, int option)
{
    CB_GameScene* gameScene = userdata;

    if (option == 1)
    {
        call_with_user_stack(CB_goToLibrary);
    }
    else
    {
        gameScene->button_hold_frames_remaining = 0;
        gameScene->button_hold_mode = 1;
        gameScene->audioLocked = false;
    }
}

__section__(".rare") void CB_GameScene_didSelectLibrary_(void* userdata)
{
    CB_GameScene* gameScene = userdata;
    gameScene->audioLocked = true;

    // if playing for more than 1 minute, ask confirmation
    if (gameScene->playtime >= 60 * 60 && !gameScene->quitGameModalConfirmOverride)
    {
        quitGameOptions[0] = T(label_no);
        quitGameOptions[1] = T(label_yes);
        quitGameOptions[2] = NULL;
        CB_presentModal(
            CB_Modal_new(T(game_quit_question), quitGameOptions, CB_LibraryConfirmModal, gameScene)
                ->scene
        );
    }
    else
    {
        call_with_user_stack(CB_goToLibrary);
    }
}

__section__(".rare") void CB_GameScene_didSelectLibrary(void* userdata)
{
    DTCM_VERIFY();

    call_with_user_stack_1(CB_GameScene_didSelectLibrary_, userdata);

    DTCM_VERIFY();
}

__section__(".rare") static void CB_GameScene_showSettings(void* userdata)
{
    CB_GameScene* gameScene = userdata;
    CB_SettingsScene* settingsScene = CB_SettingsScene_new_userstack(gameScene, NULL, NULL);
    CB_presentModal(settingsScene->scene);

    if (buttonMenuItem)
    {
        // We need to set this here to None in case the user selected any button.
        // The menu automatically falls back to 0 and the selected button is never
        // pushed.
        playdate->system->setMenuItemValue(buttonMenuItem, 1);
    }
    gameScene->button_hold_mode = 1;
}

__section__(".rare") void CB_GameScene_buttonMenuCallback(void* userdata)
{
    CB_GameScene* gameScene = userdata;
    if (buttonMenuItem)
    {
        int selected_option = playdate->system->getMenuItemValue(buttonMenuItem);

        if (selected_option != 1)
        {
            gameScene->button_hold_mode = selected_option;
            gameScene->button_hold_frames_remaining = 15;
            playdate->system->setMenuItemValue(buttonMenuItem, 1);
        }
    }
}

static void CB_GameScene_menu(void* object)
{
    CB_GameScene* gameScene = object;

    if (gameScene->menuImage != NULL)
    {
        playdate->graphics->freeBitmap(gameScene->menuImage);
        gameScene->menuImage = NULL;
    }

    gameScene->scene->forceFullRefresh = true;

    playdate->system->removeAllMenuItems();

    if (gameScene->state == CB_GameSceneStateError)
    {
        if (!CB_App->bundled_rom)
        {
            playdate->system->addMenuItem(
                T(pdmenu_library), CB_GameScene_didSelectLibrary, gameScene
            );
        }
        return;
    }

    if (!CB_App->bundled_rom)
    {
        playdate->system->addMenuItem(T(pdmenu_library), CB_GameScene_didSelectLibrary, gameScene);
    }
    if (preferences_bundle_hidden != (preferences_bitfield_t)-1)
    {
        // not sure what might happen if some settings are changed in an unusual game scene state.
        // best not find out.
        if (gameScene->state == CB_GameSceneStateLoaded)
        {
            playdate->system->addMenuItem(T(pdmenu_settings), CB_GameScene_showSettings, gameScene);
        }
    }
    else
    {
        playdate->system->addMenuItem(T(pdmenu_about), CB_showCredits, gameScene);
    }

    unsigned script_menu_flags = script_menu(gameScene->script, gameScene);

    if (game_menu_button_input_enabled && gameScene->state == CB_GameSceneStateLoaded &&
        !(script_menu_flags & SCRIPT_MENU_SUPPRESS_BUTTON))
    {
        // order is load-bearing: indexes are persisted in button_hold_mode
        buttonMenuOptions[0] = T(pdmenu_button_select);
        buttonMenuOptions[1] = T(pdmenu_button_none);
        buttonMenuOptions[2] = T(pdmenu_button_start);
        buttonMenuOptions[3] = T(pdmenu_button_both);
        buttonMenuItem = playdate->system->addOptionsMenuItem(
            T(pdmenu_button), buttonMenuOptions, 4, CB_GameScene_buttonMenuCallback, gameScene
        );
        playdate->system->setMenuItemValue(buttonMenuItem, gameScene->button_hold_mode);
    }

    if (!(script_menu_flags & SCRIPT_MENU_SUPPRESS_IMAGE))
    {
        if (gameScene->menuImage == NULL)
        {
            CB_LoadedCoverArt cover_art = {.bitmap = NULL};
            char* actual_cover_path = NULL;

            // --- Get Cover Art ---

            bool has_cover_art = false;
            if (CB_App->coverArtCache.rom_path &&
                strcmp(CB_App->coverArtCache.rom_path, gameScene->rom_filename) == 0 &&
                CB_App->coverArtCache.art.status == CB_COVER_ART_SUCCESS &&
                CB_App->coverArtCache.art.bitmap != NULL)
            {
                has_cover_art = true;
            }

            // --- Get Save Times ---

            unsigned int last_cartridge_save_time = 0;
            if (gameScene->cartridge_has_battery)
            {
                last_cartridge_save_time = gameScene->last_save_time;
            }

            unsigned int last_state_save_time = 0;
            for (int i = 0; i < SAVE_STATE_SLOT_COUNT; ++i)
            {
                last_state_save_time =
                    MAX(last_state_save_time, get_save_state_timestamp(gameScene, i));
            }

            bool show_time_info = false;
            const char* line1_text = NULL;
            unsigned int final_timestamp = 0;

            if (last_state_save_time > last_cartridge_save_time)
            {
                show_time_info = true;
                final_timestamp = last_state_save_time;
                line1_text = T(game_last_save_state);
            }
            else if (last_cartridge_save_time > 0)
            {
                show_time_info = true;
                final_timestamp = last_cartridge_save_time;
                line1_text = T(game_cartridge_data);
            }

            // --- Drawing Logic ---
            if (has_cover_art || show_time_info)
            {
                gameScene->menuImage = playdate->graphics->newBitmap(400, 240, kColorClear);
                if (gameScene->menuImage != NULL)
                {
                    playdate->graphics->pushContext(gameScene->menuImage);
                    playdate->graphics->setDrawMode(kDrawModeCopy);

                    const int content_top = 40;
                    const int content_height = 160;

                    int cover_art_y = 0;
                    int cover_art_height = 0;

                    if (has_cover_art)
                    {
                        playdate->graphics->fillRect(0, 0, 400, 240, kColorBlack);

                        CB_LoadedCoverArt* cached_art = &CB_App->coverArtCache.art;

                        const int max_width = 200;
                        const int max_height = 200;

                        float scale_x = (float)max_width / cached_art->scaled_width;
                        float scale_y = (float)max_height / cached_art->scaled_height;
                        float scale = fminf(scale_x, scale_y);

                        int final_width = (int)(cached_art->scaled_width * scale);
                        int final_height = (int)(cached_art->scaled_height * scale);

                        int art_x = (200 - final_width) / 2;
                        if (!show_time_info)
                        {
                            cover_art_y = content_top + (content_height - final_height) / 2;
                        }

                        playdate->graphics->drawScaledBitmap(
                            cached_art->bitmap, art_x, cover_art_y, scale, scale
                        );

                        cover_art_height = final_height;
                    }
                    else if (show_time_info)
                    {
                        LCDBitmap* ditherOverlay =
                            playdate->graphics->newBitmap(400, 240, kColorWhite);
                        if (ditherOverlay)
                        {
                            int width, height, rowbytes;
                            uint8_t* overlayData;
                            playdate->graphics->getBitmapData(
                                ditherOverlay, &width, &height, &rowbytes, NULL, &overlayData
                            );

                            for (int y = 0; y < height; ++y)
                            {
                                uint8_t pattern_byte = (y % 2 == 0) ? 0xAA : 0x55;
                                uint8_t* row = overlayData + y * rowbytes;
                                memset(row, pattern_byte, rowbytes);
                            }

                            playdate->graphics->setDrawMode(kDrawModeWhiteTransparent);
                            playdate->graphics->drawBitmap(ditherOverlay, 0, 0, kBitmapUnflipped);
                            playdate->graphics->setDrawMode(kDrawModeCopy);
                            playdate->graphics->freeBitmap(ditherOverlay);
                        }
                    }

                    // 2. Draw Save Time if it exists
                    if (show_time_info)
                    {
                        playdate->graphics->setFont(CB_App->labelFont);
                        const char* line1 = line1_text;

                        unsigned current_time = playdate->system->getSecondsSinceEpoch(NULL);

                        const int max_human_time = 60 * 60 * 24 * 10;

                        unsigned use_absolute_time =
                            (current_time < final_timestamp) ||
                            (final_timestamp + max_human_time < current_time);

                        char line2[40];
                        if (use_absolute_time)
                        {
                            unsigned int utc_epoch = final_timestamp;
                            int32_t offset = playdate->system->getTimezoneOffset();
                            unsigned int local_epoch = utc_epoch + offset;

                            struct PDDateTime time_info;
                            playdate->system->convertEpochToDateTime(local_epoch, &time_info);

                            if (playdate->system->shouldDisplay24HourTime())
                            {
                                snprintf(
                                    line2, sizeof(line2), "%02d.%02d.%d - %02d:%02d:%02d",
                                    time_info.day, time_info.month, time_info.year, time_info.hour,
                                    time_info.minute, time_info.second
                                );
                            }
                            else
                            {
                                const char* suffix = (time_info.hour < 12) ? " am" : " pm";
                                int display_hour = time_info.hour;
                                if (display_hour == 0)
                                {
                                    display_hour = 12;
                                }
                                else if (display_hour > 12)
                                {
                                    display_hour -= 12;
                                }
                                snprintf(
                                    line2, sizeof(line2), "%02d.%02d.%d - %d:%02d:%02d%s",
                                    time_info.day, time_info.month, time_info.year, display_hour,
                                    time_info.minute, time_info.second, suffix
                                );
                            }
                        }
                        else
                        {
                            char* human_time = en_human_time(current_time - final_timestamp);
                            snprintf(line2, sizeof(line2), "%s ago", human_time);
                            cb_free(human_time);
                        }

                        int font_height = playdate->graphics->getFontHeight(CB_App->labelFont);
                        int line1_width = playdate->graphics->getTextWidth(
                            CB_App->labelFont, line1, strlen(line1), kUTF8Encoding, 0
                        );
                        int line2_width = playdate->graphics->getTextWidth(
                            CB_App->labelFont, line2, strlen(line2), kUTF8Encoding, 0
                        );
                        int text_spacing = 4;
                        int text_block_height = font_height * 2 + text_spacing;

                        if (has_cover_art)
                        {
                            playdate->graphics->setDrawMode(kDrawModeFillWhite);
                            int text_y = cover_art_y + cover_art_height + 6;
                            playdate->graphics->drawText(
                                line1, strlen(line1), kUTF8Encoding, (200 - line1_width) / 2, text_y
                            );
                            playdate->graphics->drawText(
                                line2, strlen(line2), kUTF8Encoding, (200 - line2_width) / 2,
                                text_y + font_height + text_spacing
                            );
                        }
                        else
                        {
                            int padding_x = 10;
                            int padding_y = 8;
                            int black_border_size = 2;
                            int white_border_size = 1;

                            int box_width = CB_MAX(line1_width, line2_width) + (padding_x * 2);
                            int box_height = text_block_height + (padding_y * 2);

                            int total_border_size = black_border_size + white_border_size;
                            int total_width = box_width + (total_border_size * 2);
                            int total_height = box_height + (total_border_size * 2);

                            int final_box_x = (200 - total_width + 1) / 2;
                            int final_box_y = content_top + (content_height - total_height) / 2;

                            playdate->graphics->fillRect(
                                final_box_x, final_box_y, total_width, total_height, kColorWhite
                            );

                            playdate->graphics->fillRect(
                                final_box_x + white_border_size, final_box_y + white_border_size,
                                box_width + (black_border_size * 2),
                                box_height + (black_border_size * 2), kColorBlack
                            );

                            playdate->graphics->fillRect(
                                final_box_x + total_border_size, final_box_y + total_border_size,
                                box_width, box_height, kColorWhite
                            );

                            playdate->graphics->setDrawMode(kDrawModeFillBlack);

                            int text_y = final_box_y + total_border_size + padding_y;
                            playdate->graphics->drawText(
                                line1, strlen(line1), kUTF8Encoding,
                                final_box_x + total_border_size + (box_width - line1_width) / 2,
                                text_y
                            );
                            playdate->graphics->drawText(
                                line2, strlen(line2), kUTF8Encoding,
                                final_box_x + total_border_size + (box_width - line2_width) / 2,
                                text_y + font_height + text_spacing
                            );
                        }
                    }
                    playdate->graphics->popContext();
                }
            }
        }
        playdate->system->setMenuImage(gameScene->menuImage, 0);
    }
}

static void CB_GameScene_generateBitmask(void)
{
    if (CB_GameScene_bitmask_done)
    {
        return;
    }

    CB_GameScene_bitmask_done = true;

    for (int colour = 0; colour < 4; colour++)
    {
        for (int y = 0; y < 4; y++)
        {
            int x_offset = 0;

            for (int i = 0; i < 4; i++)
            {
                int mask = 0x00;

                for (int x = 0; x < 2; x++)
                {
                    if (CB_patterns[colour][y][x_offset + x] == 1)
                    {
                        int n = i * 2 + x;
                        mask |= (1 << (7 - n));
                    }
                }

                CB_bitmask[colour][i][y] = mask;

                x_offset ^= 2;
            }
        }
    }
}

__section__(".rare") static unsigned get_save_state_timestamp_(
    CB_GameScene* gameScene, unsigned slot
)
{
    char* path;
    playdate->system->formatString(
        &path, "%s/%s.%u.state", cb_gb_directory_path(CB_statesPath), gameScene->base_filename, slot
    );

    SDFile* file = playdate->file->open(path, kFileReadData);

    cb_free(path);

    if (!file)
    {
        return 0;
    }

    struct StateHeader header;
    int read = playdate->file->read(file, &header, sizeof(header));
    playdate->file->close(file);
    if (read < sizeof(header))
    {
        return 0;
    }
    else
    {
        return header.timestamp;
    }
}

__section__(".rare") unsigned get_save_state_timestamp(CB_GameScene* gameScene, unsigned slot)
{
    return (unsigned)call_with_main_stack_2(get_save_state_timestamp_, gameScene, slot);
}

static size_t get_save_state_size_including_script(CB_GameScene* gameScene)
{
    CB_GameSceneContext* context = gameScene->context;
    int save_size = gb_get_state_size(context->gb);
    if (save_size <= 0)
        return 0;

    if (gameScene->script)
    {
        save_size += script_query_savestate_size(gameScene->script);
    }
    return save_size;
}

// returns true if successful
__section__(".rare") static bool save_state_(CB_GameScene* gameScene, unsigned slot)
{
    if (gameScene->isCurrentlySaving)
    {
        playdate->system->logToConsole("Save state failed: another save is in progress.");
        return false;
    }

    gameScene->isCurrentlySaving = true;

    CB_GameSceneContext* context = gameScene->context;

    bool success = false;

    char* path_prefix = NULL;
    char* state_name = NULL;
    char* tmp_name = NULL;
    char* bak_name = NULL;
    char* thumb_name = NULL;
    char* buff = NULL;

    playdate->system->formatString(
        &path_prefix, "%s/%s.%u", cb_gb_directory_path(CB_statesPath), gameScene->base_filename,
        slot
    );

    playdate->system->formatString(&state_name, "%s.state", path_prefix);
    playdate->system->formatString(&tmp_name, "%s.tmp", path_prefix);
    playdate->system->formatString(&thumb_name, "%s.thumb", path_prefix);
    playdate->system->formatString(&bak_name, "%s.bak", path_prefix);

    // Clean up any old temp file
    playdate->file->unlink(tmp_name, false);

    int save_size = gb_get_state_size(context->gb);
    if (save_size <= 0)
    {
        playdate->system->logToConsole("Save state failed: invalid save size.");
        goto cleanup;
    }

    int script_size = 0;
    if (gameScene->script)
    {
        script_size = script_query_savestate_size(gameScene->script);
    }

    buff = cb_malloc(save_size + script_size);
    if (!buff)
    {
        playdate->system->logToConsole("Failed to allocate buffer for save state");
        goto cleanup;
    }

    if (gameScene->script && !script_save_state(gameScene->script, (void*)(buff + save_size)))
    {
        playdate->system->logToConsole("Script error while saving state");
        goto cleanup;
    }
    gb_state_save(context->gb, buff);

    struct StateHeader* header = (struct StateHeader*)buff;
    header->timestamp = playdate->system->getSecondsSinceEpoch(NULL);
    header->script = (context->scene->script != NULL);
    header->cgb = context->gb->is_cgb_mode;
    header->script_save_data_size = script_size;

    // Write the state to the temporary file
    SDFile* file = playdate->file->open(tmp_name, kFileWrite);
    if (!file)
    {
        playdate->system->logToConsole(
            "failed to open temp state file \"%s\": %s", tmp_name, playdate->file->geterr()
        );
    }
    else
    {
        save_size += script_size;

        int written = playdate->file->write(file, buff, save_size);
        playdate->file->close(file);

        // Verify that the temporary file was written correctly
        if (written != save_size)
        {
            playdate->system->logToConsole(
                "Error writing temp state file \"%s\" (wrote %d of %d bytes). "
                "Aborting.",
                tmp_name, written, save_size
            );
            playdate->file->unlink(tmp_name, false);
        }
        else
        {
            // Rename files: .state -> .bak, then .tmp -> .state
            playdate->system->logToConsole("Temp state saved, renaming files.");
            playdate->file->unlink(bak_name, false);
            playdate->file->rename(state_name, bak_name);
            if (playdate->file->rename(tmp_name, state_name) == 0)
            {
                success = true;
            }
            else
            {
                playdate->system->logToConsole(
                    "CRITICAL: Failed to rename temp state file. Restoring "
                    "backup."
                );
                playdate->file->rename(bak_name, state_name);
            }
        }
    }

    // we check playtime nonzero so that LCD has been updated at least once
    // Use previous_lcd (last complete frame) to avoid corruption from
    // double-buffered LCD when TCM is enabled.
    uint8_t* lcd = context->previous_lcd;
    if (success && lcd && gameScene->playtime > 1)
    {
        // save thumbnail, too
        // (inessential, so we don't take safety precautions)
        SDFile* file = playdate->file->open(thumb_name, kFileWrite);

        static const uint8_t dither_pattern[5] = {
            0b00000000 ^ 0xFF, 0b01000100 ^ 0xFF, 0b10101010 ^ 0xFF,
            0b11011101 ^ 0xFF, 0b11111111 ^ 0xFF,
        };

        if (file)
        {
            for (unsigned y = 0; y < SAVE_STATE_THUMBNAIL_H; ++y)
            {
                uint8_t* line0 = lcd + y * LCD_WIDTH_PACKED;

                u8 thumbline[(SAVE_STATE_THUMBNAIL_W + 7) / 8];
                memset(thumbline, 0, sizeof(thumbline));

                for (unsigned x = 0; x < SAVE_STATE_THUMBNAIL_W; ++x)
                {
                    // very bespoke dithering algorithm lol
                    u8 p0, p1;
                    if (context->gb->is_cgb_mode)
                    {
                        p0 = __gb_get_pixel__cgb(line0, x);
                        p1 = __gb_get_pixel__cgb(line0, x ^ 1);
                    }
                    else
                    {
                        p0 = __gb_get_pixel__dmg(line0, x);
                        p1 = __gb_get_pixel__dmg(line0, x ^ 1);
                    }

                    u8 val = p0;
                    if (val >= 2)
                        val++;
                    if (val == 1 && p1 >= 2)
                        ++val;
                    if (val == 3 && p1 < 2)
                        --val;

                    u8 pattern = dither_pattern[val];
                    if (y % 2 == 1)
                    {
                        if (val == 2)
                            pattern = (pattern >> 1) | (pattern << 7);
                        else
                            pattern = (pattern >> 2) | (pattern << 6);
                    }

                    u8 pix = (pattern >> (x % 8)) & 1;

                    thumbline[x / 8] |= pix << (7 - (x % 8));
                }

                playdate->file->write(file, thumbline, sizeof(thumbline));
            }
        }

        playdate->file->close(file);
    }
    else if (success && gameScene->last_loaded_slot != (unsigned)-1)
    {
        // playtime hasn't advanced (likely just loaded a state).
        // Reuse thumbnail from the savegame that was last loaded.
        char* src_thumb_name = NULL;
        playdate->system->formatString(
            &src_thumb_name, "%s/%s.%u.thumb", cb_gb_directory_path(CB_statesPath),
            gameScene->base_filename, gameScene->last_loaded_slot
        );

        SDFile* src = playdate->file->open(src_thumb_name, kFileReadData);
        if (src)
        {
            int thumb_size = SAVE_STATE_THUMBNAIL_H * ((SAVE_STATE_THUMBNAIL_W + 7) / 8);
            uint8_t* thumb_buf = cb_malloc(thumb_size);
            if (thumb_buf)
            {
                int read = playdate->file->read(src, thumb_buf, thumb_size);
                if (read == thumb_size)
                {
                    SDFile* dst = playdate->file->open(thumb_name, kFileWrite);
                    if (dst)
                    {
                        playdate->file->write(dst, thumb_buf, thumb_size);
                        playdate->file->close(dst);
                    }
                }
                cb_free(thumb_buf);
            }
            playdate->file->close(src);
        }
        cb_free(src_thumb_name);
    }

cleanup:
    if (path_prefix)
        cb_free(path_prefix);
    if (state_name)
        cb_free(state_name);
    if (tmp_name)
        cb_free(tmp_name);
    if (bak_name)
        cb_free(bak_name);
    if (thumb_name)
        cb_free(thumb_name);
    if (buff)
        cb_free(buff);

    gameScene->isCurrentlySaving = false;
    return success;
}

// returns true if successful
__section__(".rare") bool save_state(CB_GameScene* gameScene, unsigned slot)
{
    return (bool)call_with_main_stack_2(save_state_, gameScene, slot);
    gameScene->playtime = 0;
}

__section__(".rare") bool load_state_thumbnail_(
    CB_GameScene* gameScene, unsigned slot, uint8_t* out
)
{
    char* path;
    playdate->system->formatString(
        &path, "%s/%s.%u.thumb", cb_gb_directory_path(CB_statesPath), gameScene->base_filename, slot
    );

    SDFile* file = playdate->file->open(path, kFileReadData);

    cb_free(path);

    if (!file)
    {
        return 0;
    }

    int count = SAVE_STATE_THUMBNAIL_H * ((SAVE_STATE_THUMBNAIL_W + 7) / 8);
    int read = playdate->file->read(file, out, count);
    playdate->file->close(file);

    return read == count;
}

// returns true if successful
__section__(".rare") bool load_state_thumbnail(CB_GameScene* gameScene, unsigned slot, uint8_t* out)
{
    return (bool)call_with_main_stack_3(load_state_thumbnail_, gameScene, slot, out);
}

// returns true if successful
__section__(".rare") bool load_state(CB_GameScene* gameScene, unsigned slot)
{
    gameScene->playtime = 0;
    CB_GameSceneContext* context = gameScene->context;
    char* state_name;
    playdate->system->formatString(
        &state_name, "%s/%s.%u.state", cb_gb_directory_path(CB_statesPath),
        gameScene->base_filename, slot
    );
    bool success = false;

    SDFile* file = playdate->file->open(state_name, kFileReadData);
    if (!file)
    {
        playdate->system->logToConsole(
            "failed to open save state file \"%s\": %s", state_name, playdate->file->geterr()
        );
    }
    else
    {
        playdate->file->seek(file, 0, SEEK_END);
        int file_size = playdate->file->tell(file);
        if (file_size > 0)
        {
            if (playdate->file->seek(file, 0, SEEK_SET))
            {
                playdate->system->logToConsole(
                    "Failed to seek to start of state file \"%s\": %s", state_name,
                    playdate->file->geterr()
                );
            }
            else
            {
                success = true;
                int size_remaining = file_size;
                char* buff = cb_malloc(file_size);
                char* buffptr = buff;
                while (size_remaining > 0)
                {
                    int read = playdate->file->read(file, buffptr, size_remaining);
                    if (read == 0)
                    {
                        playdate->system->logToConsole(
                            "Error, read 0 bytes from save file, \"%s\"\n", state_name
                        );
                        success = false;
                        break;
                    }
                    if (read < 0)
                    {
                        playdate->system->logToConsole(
                            "Error reading save file \"%s\": %s\n", state_name,
                            playdate->file->geterr()
                        );
                        success = false;
                        break;
                    }
                    size_remaining -= read;
                    buffptr += read;
                }

                if (success)
                {
                    struct StateHeader* header = (struct StateHeader*)buff;

                    if (header->script_save_data_size > file_size)
                    {
                        success = false;
                        CB_presentModal(
                            CB_Modal_new(T(game_invalid_script_data), NULL, NULL, NULL)->scene
                        );
                    }
                    else
                    {
                        unsigned int loaded_timestamp = header->timestamp;

                        if (loaded_timestamp > 0)
                        {
                            playdate->system->logToConsole(
                                "Save state had been created at: %u", loaded_timestamp
                            );
                        }
                        else
                        {
                            playdate->system->logToConsole(
                                "Save state is from an old version (no "
                                "timestamp)."
                            );
                        }

                        const char* res = gb_state_load(
                            context->gb, buff, file_size - header->script_save_data_size
                        );
                        if (res)
                        {
                            success = false;
                            playdate->system->logToConsole("Error loading state! %s", res);

                            char* details = NULL;
                            playdate->system->formatString(&details, "%s", res);

                            if (details)
                            {
                                loadStateErrorOptions[0] = T(label_ok);
                                loadStateErrorOptions[1] = T(label_details);
                                loadStateErrorOptions[2] = NULL;
                                CB_presentModal(CB_Modal_new(
                                                    T(game_failed_load_state),
                                                    loadStateErrorOptions,
                                                    CB_LoadStateErrorModalCallback, details
                                )
                                                    ->scene);
                            }
                            else
                            {
                                // Fallback: 1-button modal
                                CB_presentModal(
                                    CB_Modal_new(T(game_failed_load_state), NULL, NULL, NULL)->scene
                                );
                            }
                        }
                        else
                        {
                            audio_reset_replay_state(&context->gb->audio);
                            pgb_dirty_prev = context->previous_lcd;
                            pgb_dirty_flags = context->line_has_changed;
                            context->ghost_resnap = true;
                            gameScene->cgb_needs_palette_recompute = true;
                            pgb_cgb_lut_dirty = true;
                            if (gameScene->script)
                            {
                                const char* scriptbuff =
                                    buff + file_size - header->script_save_data_size;
                                if (!script_load_state(
                                        gameScene->script, (void*)scriptbuff,
                                        header->script_save_data_size
                                    ))
                                {
                                    success = false;

                                    CB_presentModal(CB_Modal_new(
                                                        "Failed to load script's custom state.",
                                                        NULL, NULL, NULL
                                    )
                                                        ->scene);
                                }
                            }
                        }
                    }
                }

                if (success)
                {
                    gameScene->last_loaded_slot = slot;
                    rewind_free(gameScene);
                }

                cb_free(buff);
            }
        }
        else
        {
            playdate->system->logToConsole("Failed to determine file size");
        }

        playdate->file->close(file);
    }

    cb_free(state_name);
    return success;
}

__section__(".rare") static bool CB_GameScene_lock(void* object)
{
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;
    gameScene->hold_ab_release_frames = 0;

    if (preferences_lock_button != PREF_BUTTON_NONE)
    {
        gameScene->lock_button_hold_frames_remaining = 8;
        return true;
    }

    return false;
}

__section__(".rare") static void CB_GameScene_event(void* object, PDSystemEvent event, uint32_t arg)
{
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;

    switch (event)
    {
    case kEventLock:
        if (CB_App->hasSystemAccess && preferences_lock_button != PREF_BUTTON_NONE)
            return;
        // fallthrough
    case kEventPause:
        audioGameScene = NULL;

        // System may write into TCM pockets while locked/menu open; clear
        // code to flash. gb struct (main pool) stays; re-placed on resume.
        tcm_clear(context->gb->is_cgb_mode, (char*)context->gb + sizeof(gb_s));

        // Re-enable auto-lock when the system menu is open.
        playdate->system->setAutoLockDisabled(0);

        gameScene->lock_button_hold_frames_remaining = 0;
        gameScene->hold_ab_release_frames = 0;

        gameScene->scene->forceFullRefresh = true;

        gameScene->menu_open_seconds =
            playdate->system->getSecondsSinceEpoch(&gameScene->menu_open_ms);

        // fall-through
    case kEventTerminate:
        DTCM_VERIFY();
        if (context->gb->direct.sram_dirty && gameScene->save_data_loaded_successfully)
        {
            playdate->system->logToConsole("saving (system event)");
            gb_save_to_disk(context->gb);
        }
        DTCM_VERIFY();
        break;
    case kEventUnlock:
    case kEventResume:
        // Re-probe pockets and relocate core/draw back into TCM (no-op if
        // never cleared or TCM off). Must run before audio reconfig/frames.
        tcm_relocate(context->gb->is_cgb_mode);

        // Re-apply the user's auto-lock preference on resume.
        playdate->system->setAutoLockDisabled(preferences_disable_autolock);
        reconfigure_audio_source(gameScene);
        if (gameScene->menu_open_seconds > 0)
        {
            unsigned now_ms;
            unsigned now_s = playdate->system->getSecondsSinceEpoch(&now_ms);
            unsigned elapsed = (now_s - gameScene->menu_open_seconds) * 1000 + (int)now_ms -
                               (int)gameScene->menu_open_ms;

            if (elapsed < MENU_QUICK_PRESS_THRESHOLD_MS && preferences_menu_button > 0)
            {
                if (preferences_menu_button == PREF_BUTTON_START)
                {
                    gameScene->button_hold_mode = 2;
                    gameScene->button_hold_frames_remaining = 15;
                }
                else if (preferences_menu_button == PREF_BUTTON_SELECT)
                {
                    gameScene->button_hold_mode = 0;
                    gameScene->button_hold_frames_remaining = 15;
                }
                else if (preferences_menu_button == PREF_BUTTON_START_SELECT)
                {
                    gameScene->button_hold_mode = 3;
                    gameScene->button_hold_frames_remaining = 15;
                }
            }
        }
        if (gameScene->audioEnabled)
        {
            // If the buffered audio sync is the active mode upon leaving the settings,
            // we MUST reset our timing baseline. This recalibrates our sample counter
            // against the hardware clock, closing the "time gap" that was created
            // while the device was locked or the system menu was open.
            if (preferences_sound_mode == 2)
            {
                // Stabilize while the callback is still silenced: reset (now
                // also clears the ring of stale samples), pre-fill a full
                // lead of fresh audio, then re-enable output. Prevents the
                // reset -> starve -> resync storm that caused seconds of
                // muffled/distorted audio after sleep or the system menu.
                audioGameScene = NULL;
                CB_reset_audio_sync_state();
                audio_reset_replay_state(&context->gb->audio);
                generate_audio_chunk(gameScene, MAX_AUDIO_SAMPLES_PER_CHUNK);
                s_resync_cooldown = 10;
                audioGameScene = gameScene;
            }
        }
        break;
    case kEventLowPower:
        if (context->gb->direct.sram_dirty && gameScene->save_data_loaded_successfully)
        {
            // save a recovery file
            char* recovery_filename = cb_save_filename(context->scene->rom_filename, true);
            write_cart_ram_file(recovery_filename, context->gb);
            cb_free(recovery_filename);
        }
        break;
    case kEventKeyPressed:
        playdate->system->logToConsole("Key pressed: %x\n", (unsigned)arg);

        switch (arg)
        {
        case 0x35:  // 5
            if (save_state(gameScene, 0))
            {
                playdate->system->logToConsole("Save state %d successful", 0);
            }
            else
            {
                playdate->system->logToConsole("Save state %d failed", 0);
            }
            break;
        case 0x37:  // 7
            if (load_state(gameScene, 0))
            {
                playdate->system->logToConsole("Load state %d successful", 0);
            }
            else
            {
                playdate->system->logToConsole("Load state %d failed", 0);
            }
            break;
        case 0x76:  // V
        {
            __gb_dump_vram(context->gb);
        }
        break;
#ifdef TARGET_SIMULATOR
        case 0x74:  // t (trace one frame of instructions)
            g_trace_frames_remaining = 1;
            playdate->system->logToConsole("Trace armed: next frame will be logged.");
            break;
#endif
        }
    default:
        break;
    }
}

static void rewind_init(CB_GameScene* gameScene)
{
    CB_GameSceneContext* context = gameScene->context;

    if (context->gb->is_cgb_mode)
        return;

    size_t state_size =
        sizeof(gb_s) + WRAM_SIZE + VRAM_SIZE + LCD_BUFFER_BYTES + context->gb->gb_cart_ram_size;

    int capacity = REWIND_MAX_STATES;
    size_t total_memory = (size_t)capacity * state_size;
    if (total_memory > REWIND_MAX_MEMORY)
    {
        capacity = (int)(REWIND_MAX_MEMORY / state_size);
        if (capacity < 2)
            return;
    }

    gameScene->rewind.states = cb_malloc((size_t)capacity * sizeof(uint8_t*));
    if (!gameScene->rewind.states)
        return;

    memset(gameScene->rewind.states, 0, (size_t)capacity * sizeof(uint8_t*));

    for (int i = 0; i < capacity; i++)
    {
        gameScene->rewind.states[i] = cb_malloc(state_size);
        if (!gameScene->rewind.states[i])
        {
            rewind_free(gameScene);
            return;
        }
    }

    gameScene->rewind.state_size = state_size;
    gameScene->rewind.capacity = capacity;
    gameScene->rewind.write_idx = 0;
    gameScene->rewind.read_idx = -1;
    gameScene->rewind.buffer_oldest = 0;
    gameScene->rewind.count = 0;
    gameScene->rewind.frame_counter = 0;
    gameScene->rewind.scrub_accumulator = 0.0f;
    gameScene->rewind.active = false;
    gameScene->rewind.noise_pending = false;
    gameScene->rewind.show_help = true;
    gameScene->rewind.help_step_count = 0;
}

static void rewind_free(CB_GameScene* gameScene)
{
    if (gameScene->rewind.states)
    {
        for (int i = 0; i < gameScene->rewind.capacity; i++)
        {
            cb_free(gameScene->rewind.states[i]);
        }
        cb_free(gameScene->rewind.states);
        gameScene->rewind.states = NULL;
    }
    gameScene->rewind.capacity = 0;
    gameScene->rewind.count = 0;
    gameScene->rewind.write_idx = 0;
    gameScene->rewind.read_idx = -1;
    gameScene->rewind.buffer_oldest = 0;
    gameScene->rewind.frame_counter = 0;
    gameScene->rewind.scrub_accumulator = 0.0f;
    gameScene->rewind.active = false;
    gameScene->rewind.noise_pending = false;
    gameScene->rewind.state_size = 0;
}

static void rewind_record_state(CB_GameScene* gameScene)
{
    if (!gameScene->rewind.states || gameScene->rewind.capacity == 0)
        return;

    CB_GameSceneContext* context = gameScene->context;
    int idx = gameScene->rewind.write_idx;

    rewind_dmg_save(context->gb, gameScene->rewind.states[idx]);

    gameScene->rewind.write_idx = (idx + 1) % gameScene->rewind.capacity;
    if (gameScene->rewind.count < gameScene->rewind.capacity)
        gameScene->rewind.count++;
    else
        gameScene->rewind.buffer_oldest = gameScene->rewind.write_idx;
}

static void rewind_dmg_save(gb_s* gb, uint8_t* out)
{
    uint8_t* p = out;
    memcpy(p, gb, sizeof(gb_s));
    p += sizeof(gb_s);
    memcpy(p, gb->wram, WRAM_SIZE);
    p += WRAM_SIZE;
    memcpy(p, gb->vram, VRAM_SIZE);
    p += VRAM_SIZE;
    if (gb->gb_cart_ram_size > 0)
    {
        memcpy(p, gb->gb_cart_ram, gb->gb_cart_ram_size);
        p += gb->gb_cart_ram_size;
    }
    memcpy(p, gb->lcd, LCD_BUFFER_BYTES);
}

static void rewind_dmg_load(gb_s* gb, const uint8_t* in, uint8_t* lcd_target)
{
    const uint8_t* p = in;
    memcpy(gb, p, sizeof(gb_s));
    p += sizeof(gb_s);
    memcpy(gb->wram, p, WRAM_SIZE);
    p += WRAM_SIZE;
    memcpy(gb->vram, p, VRAM_SIZE);
    p += VRAM_SIZE;
    if (gb->gb_cart_ram_size > 0)
    {
        memcpy(gb->gb_cart_ram, p, gb->gb_cart_ram_size);
        p += gb->gb_cart_ram_size;
    }
    memcpy(lcd_target, p, LCD_BUFFER_BYTES);
    gb->lcd = lcd_target;
}

static void rewind_enter_scrubbing(CB_GameScene* gameScene)
{
    if (!gameScene->rewind.states || gameScene->rewind.count == 0)
        return;
    if (gameScene->rewind.active)
        return;

    CB_GameSceneContext* context = gameScene->context;

    int newest =
        (gameScene->rewind.write_idx - 1 + gameScene->rewind.capacity) % gameScene->rewind.capacity;
    gameScene->rewind.read_idx = newest;
    gameScene->rewind.active = true;
    gameScene->rewind.scrub_accumulator = 0.0f;

    uint8_t* buf = gameScene->rewind.states[newest];
    rewind_dmg_load(context->gb, buf, lcd_sources[0]);
}

static void rewind_draw_noise_bands(void)
{
    int display_h = playdate->display->getHeight();
    int gb_x = (int)game_picture_x_offset;
    int gb_w = LCD_COLUMNS - 2 * CB_LCD_X;
    int num_bands = 3 + rand() % 5;
    for (int i = 0; i < num_bands; i++)
    {
        int y = rand() % display_h;
        int band_h = 1 + rand() % 2;
        for (int x = gb_x; x < gb_x + gb_w; x += 4)
            playdate->graphics->fillRect(x, y, 3, band_h, (rand() & 1) ? kColorBlack : kColorWhite);
    }
}

static void rewind_step_back(CB_GameScene* gameScene)
{
    if (!gameScene->rewind.states || gameScene->rewind.count == 0)
        return;
    if (!gameScene->rewind.active)
        return;

    CB_GameSceneContext* context = gameScene->context;

    if (gameScene->rewind.read_idx == gameScene->rewind.buffer_oldest)
        return;

    gameScene->rewind.read_idx =
        (gameScene->rewind.read_idx - 1 + gameScene->rewind.capacity) % gameScene->rewind.capacity;

    uint8_t* buf = gameScene->rewind.states[gameScene->rewind.read_idx];
    rewind_dmg_load(context->gb, buf, lcd_sources[0]);
    gameScene->rewind.noise_pending = true;

    if (gameScene->rewind.show_help && ++gameScene->rewind.help_step_count >= 3)
        gameScene->rewind.show_help = false;
}

static void rewind_step_forward(CB_GameScene* gameScene)
{
    if (!gameScene->rewind.states || gameScene->rewind.count == 0)
        return;

    if (!gameScene->rewind.active)
        return;

    CB_GameSceneContext* context = gameScene->context;

    int newest =
        (gameScene->rewind.write_idx - 1 + gameScene->rewind.capacity) % gameScene->rewind.capacity;
    if (gameScene->rewind.read_idx == newest)
        return;

    gameScene->rewind.read_idx = (gameScene->rewind.read_idx + 1) % gameScene->rewind.capacity;

    uint8_t* buf = gameScene->rewind.states[gameScene->rewind.read_idx];
    rewind_dmg_load(context->gb, buf, lcd_sources[0]);
    gameScene->rewind.noise_pending = true;
}

static void rewind_exit_scrubbing(CB_GameScene* gameScene)
{
    if (!gameScene->rewind.active)
        return;

    gameScene->rewind.active = false;
    gameScene->rewind.scrub_accumulator = 0.0f;
    gameScene->rewind.noise_pending = false;
    gameScene->rewind.help_step_count = 0;
    gameScene->context->ghost_resnap = true;

    if (preferences_sound_mode == 2)
        CB_reset_audio_sync_state();

    playdate->graphics->clear(game_picture_background_color);

    float volume = 0.0f;
    if (gameScene->audioEnabled)
    {
        volume = gameScene->is_stereo ? 0.35f : 0.5f;
        if (preferences_sound_mode != 0 && gameScene->context->gb->is_cgb_mode)
            volume *= 1.5f;
    }
    playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), volume);

    if (gameScene->rewind.states && gameScene->rewind.count > 0)
    {
        int read_idx = gameScene->rewind.read_idx;
        int capacity = gameScene->rewind.capacity;
        int oldest = gameScene->rewind.buffer_oldest;

        int kept;
        if (read_idx >= oldest)
            kept = read_idx - oldest + 1;
        else
            kept = capacity - oldest + read_idx + 1;

        gameScene->rewind.count = kept;
        gameScene->rewind.write_idx = (read_idx + 1) % capacity;
    }

    gbScreenRequiresFullRefresh = true;
    gameScene->scene->forceFullRefresh = true;
}

static void CB_GameScene_free(void* object)
{
    DTCM_VERIFY();
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;

    playdate->system->setAutoLockDisabled(0);
    playdate->system->setPeripheralsEnabled(kNone);

    if (audioGameScene == gameScene)
    {
        if (CB_App->soundSource != NULL)
        {
            playdate->sound->removeSource(CB_App->soundSource);
            CB_App->soundSource = NULL;
        }

        if (preferences_sound_mode == 2)
        {
            CB_reset_audio_sync_state();
            memset(g_audio_sync_buffer.left, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
            memset(g_audio_sync_buffer.right, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
        }

        audioGameScene = NULL;
        audio_enabled = 0;
    }

    // Ensure UI/library sounds are audible after leaving the game, even if game audio was off.
    playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 1.0f);

    prefs_locked_by_script = 0;
    preferences_merge_from_disk(CB_globalPrefsPath);
    preferences_per_game = 0;
    preferences_save_state_slot = 0;
    if (context && gameScene->state == CB_GameSceneStateLoaded)
    {
        gb_save_to_disk(context->gb);
    }
    preferences_save_slot = 0;

    if (gameScene->menuImage)
    {
        playdate->graphics->freeBitmap(gameScene->menuImage);
    }

    playdate->system->setMenuImage(NULL, 0);

    CB_Scene_free(gameScene->scene);

    if (context && context->gb_initialized)
    {
        gb_reset(context->gb, context->cgb_mode);
    }

    cb_free(gameScene->rom_filename);
    cb_free(gameScene->save_filename);
    cb_free(gameScene->base_filename);
    cb_free(gameScene->settings_filename);
    cb_free(gameScene->name_short);

    if (context && context->rom)
    {
        cb_free(rom_pool);
        rom_pool = context->rom;
    }

    if (context && context->cart_ram)
    {
        cb_free(context->cart_ram);
    }

    if (gameScene->script)
    {
        script_end(gameScene->script, gameScene);
        gameScene->script = NULL;
    }

    rewind_free(gameScene);

    if (context)
    {
        cb_free(context->gb->cgb_bg_palette);
        cb_free(context->gb->cgb_obj_palette);
    }

    cb_free(context);
    cb_free(gameScene);

#if ITCM_CORE
    core_itcm_reloc = NULL;
    // Free any outstanding lock snapshot (pool is about to be deinited).
    cb_free(s_tcm_store);
    s_tcm_store = NULL;
#endif
    dtcm_pocket_fill_and_reset();
    dtcm_deinit();

    DTCM_VERIFY();
}

__section__(".rare") void __gb_dump_vram(gb_s* gb)
{
    playdate->system->logToConsole("dumping vram to vram.bin");

    // reverse byte order of appropriate bytes
    for (int pass = 0; pass <= 1; ++pass)
    {
        for (int b = 0; b <= gb->is_cgb_mode; ++b)
        {
            for (int i = 0; i < 0x1800; ++i)
            {
                gb->vram[i | (0x2000 * b)] = reverse_bits_u8(gb->vram[i | (0x2000 * b)]);
            }
        }

        if (pass == 0)
            call_with_main_stack_3(
                cb_write_entire_file, "vram.bin", gb->vram,
                (gb->is_cgb_mode) ? VRAM_SIZE_CGB : VRAM_SIZE
            );
    }
}

__section__(".rare") void __gb_on_breakpoint(gb_s* gb, int breakpoint_number)
{
    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    CB_ASSERT(gameScene->context == context);
    CB_ASSERT(gameScene->context->scene == gameScene);
    CB_ASSERT(gameScene->context->gb->direct.priv == context);
    CB_ASSERT(gameScene->context->gb == gb);

    if (gameScene->script)
    {
        call_with_user_stack_2(script_on_breakpoint, gameScene, breakpoint_number);
    }
}

void show_game_script_info(const char* rompath, const char* name_short)
{
    ScriptInfo* info = script_get_info_by_rom_path(rompath);
    if (!info)
        return;

    if (!info->info)
    {
        script_info_free(info);
        return;
    }

    char* text = NULL;

    // Check if name_short was provided and is not an empty string
    if (name_short && name_short[0] != '\0')
    {
        text = aprintf(T(game_script_info), info->info);
    }
    else
    {
        // Fallback to just the rom_name if name_short is not available
        text = aprintf(T(game_script_info), info->info);
    }

    script_info_free(info);
    if (!text)
        return;

    CB_InfoScene* infoScene = CB_InfoScene_new(name_short, text);

    cb_free(text);

    CB_presentModal(infoScene->scene);
}

static LCDBitmap* numbers_bmp = NULL;
static uint8_t fps_draw_timer;

static const unsigned init_fade_frames[] = {0, 30, 60, 29, 59};
static const unsigned init_fade_color[] = {
    kColorWhite, kColorBlack, kColorBlack, kColorWhite, kColorWhite
};

unsigned cb_boot_fade_initial_frames(int boot_fade_pref)
{
    if (boot_fade_pref < 0 || boot_fade_pref >= (int)CB_ARRAY_SIZE(init_fade_frames))
        return 0;
    return init_fade_frames[boot_fade_pref];
}

bool cb_boot_fade_initial_white(int boot_fade_pref)
{
    if (boot_fade_pref < 0 || boot_fade_pref >= (int)CB_ARRAY_SIZE(init_fade_color))
        return false;
    return init_fade_color[boot_fade_pref] == kColorWhite;
}

__section__(".text.tick") void cb_render_fps(void)
{
    if (!numbers_bmp)
    {
        numbers_bmp = playdate->graphics->loadBitmap(CB_get_forwarded_path("fonts/numbers"), NULL);
        if (!numbers_bmp)
            return;
    }

    static char buff[5] = "00.0";
    if ((++fps_draw_timer & 3) == 0)
    {
        float fps;
        if (CB_App->avg_dt <= 1.0f / 98.5f)
        {
            fps = 99.9f;
        }
        else
        {
            fps = 1.0f / CB_App->avg_dt;
        }

        // for rounding
        fps += 0.004f;

        int fps_multiplied = (int)(fps * 10.0f);

        if (fps_multiplied > 999)
        {
            fps_multiplied = 999;
        }

        buff[0] = (fps_multiplied / 100) + '0';
        buff[1] = ((fps_multiplied / 10) % 10) + '0';
        buff[2] = '.';
        buff[3] = (fps_multiplied % 10) + '0';
        buff[4] = '\0';
    }

    uint8_t* lcd = playdate->graphics->getFrame();

    uint8_t* data;
    int width, height, rowbytes;
    playdate->graphics->getBitmapData(numbers_bmp, &width, &height, &rowbytes, NULL, &data);

    if (!data || !lcd)
        return;

    playdate->graphics->setFont(CB_App->labelFont);

    for (int y = 0; y < height; ++y)
    {
        uint32_t out = 0;
        unsigned x = 0;
        uint8_t* rowdata = data + y * rowbytes;
        for (int i = 0; i < sizeof(buff); ++i)
        {
            char c = buff[i];
            int cidx = 11, advance = 0;
            if (c == '.')
            {
                cidx = 10;
                advance = 3;
            }
            else if (c >= '0' && c <= '9')
            {
                cidx = c - '0';
                advance = 7;
            }

            unsigned cdata = (~rowdata[cidx]) & reverse_bits_u8((1 << (advance + 1)) - 1);
            out |= cdata << (32 - x - 8);
            x += advance;
        }

        uint32_t mask = ((1 << (30 - x)) - 1);

        for (int i = 0; i < 4; ++i)
        {
            lcd[y * LCD_ROWSIZE + i] &= (mask >> ((3 - i) * 8));
            lcd[y * LCD_ROWSIZE + i] |= (out >> ((3 - i) * 8));
        }
    }

    playdate->graphics->markUpdatedRows(0, height - 1);
}

static int fade_matrix[] = {
    3, 6, 16, 13, 18, 7, 0, 9, 11, 15, 17, 14, 1, 2, 8, 6, 5, 4, 10, 12,
};

__section__(".rare") void cb_render_boot_fade(unsigned fade_frames, bool fade_white)
{
    if (fade_frames >= 20)
    {
        int n = sizeof(fade_matrix) / sizeof(fade_matrix[0]);
        for (int i = n - 1; i > 0; i--)
        {
            int j = rand() % (i + 1);
            int t = fade_matrix[i];
            fade_matrix[i] = fade_matrix[j];
            fade_matrix[j] = t;
        }
    }

    uint8_t base_mask[8];
    for (int y = 0; y < 8; y++)
    {
        uint8_t mask_byte = 0;
        for (int x = 0; x < 8; x++)
        {
            int idx = (y % 5) * 4 + (x % 4);
            if (fade_matrix[idx] < (int)fade_frames)
            {
                mask_byte |= (1 << (7 - x));
            }
        }
        base_mask[y] = mask_byte;
    }

    uint8_t fill_byte = fade_white ? 0xFF : 0x00;

    int n_bands = (LCD_ROWS + 7) / 8;
    for (int band = 0; band < n_bands; band++)
    {
        int shift = (band * 2) & 7;
        LCDPattern pattern;
        for (int y = 0; y < 8; y++)
        {
            uint8_t m = base_mask[y];
            uint8_t shifted = shift ? (uint8_t)((m << shift) | (m >> (8 - shift))) : m;
            pattern[y] = fill_byte;
            pattern[8 + y] = shifted;
        }
        playdate->graphics->fillRect(0, band * 8, LCD_COLUMNS, 8, (LCDColor)(uintptr_t)pattern);
    }
    playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}
