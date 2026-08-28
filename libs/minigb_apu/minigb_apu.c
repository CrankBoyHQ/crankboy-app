/*
 * minigb_apu is released under the terms listed within the LICENSE file.
 * Game Boy APU emulator, based on MiniGBS by Alex Baines:
 * https://github.com/baines/MiniGBS
 */

#include "minigb_apu.h"

#include "../../src/app.h"
#include "../../src/dtcm.h"
#include "../../src/preferences.h"
#include "../../src/scenes/game_scene.h"
#include "../peanut_gb.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define audio_mem(audio) \
    ((uint8_t*)((void*)audio - offsetof(gb_s, audio) + offsetof(gb_s, hram) + 0x10))

#define DMG_CLOCK_FREQ_U ((unsigned)DMG_CLOCK_FREQ)

/* Per-frame APU register write events for cycle-accurate replay.
 * File-static in main RAM (gb_s is DTCM-resident); transient, never
 * serialized. Sized for ~7 frames at the CPU-max write rate
 * (LDH every 12 dots = 5852/frame); the pause-throttle engages long
 * before this fills. */
#define APU_EVENT_CAP 32768
typedef struct
{
    uint32_t apu_count;
    uint16_t addr;
    uint8_t val;
    /* NR52 power-on only: frame-sequencer phase captured at emulation time
     * (replay would re-derive it from post-frame DIV). bits 0-2 = div_apu_step,
     * bit 6 = valid, bit 7 = skip_next_apu_tick. */
    uint8_t div_step;
} apu_event_t;
#define APU_EVENT_PHASE_MASK 0x07u
#define APU_EVENT_PHASE_VALID 0x40u
#define APU_EVENT_PHASE_SKIP 0x80u
static apu_event_t s_apu_events[APU_EVENT_CAP];
static uint16_t s_apu_event_count;

/* Debug: skip rendering per channel (bit0=CH1,1=CH2,2=CH3,
 * 3=CH4). Freezes channel state while set; measurement only. */
static uint8_t s_audio_skip_mask;

void audio_set_skip_mask(uint8_t m)
{
    s_audio_skip_mask = m;
}

/* Sentinel addr marking an emulated frame's end in the event stream (never
 * passed to audio_write): explicit boundary, also for write-less frames. */
#define APU_FRAME_END_ADDR 0xFFFF

/* 512 Hz frame-sequencer schedule phase. Persistent across replay spans and
 * generate calls: restarting per call drops the fractional interval
 * (sequencer runs at 478 Hz instead of 512 Hz). Transient, never serialized. */
static uint16_t s_apu_tick_left;       // samples until next tick; 0 = uninitialized
static uint16_t s_apu_tick_rem_accum;  // fractional interval, 512ths of a sample

/* CH3 cycle-domain sample cursor for DMG wave-RAM write timing in accurate
 * mode (the renderer's c->val advances at sample granularity, too coarse for
 * the access quirk). Transient, replay-only; never serialized. */
static bool s_ch3_cursor_valid;
static uint8_t s_ch3_cursor_pos;    // sample index 0-31
static uint32_t s_ch3_step_period;  // apu_count cycles per step: 2 * (2048 - freq)
static int64_t s_ch3_last_step;     // apu_count of most recent step (may precede frame start)

/* Partial-frame resume state: when a generate call's sample budget is smaller
 * than the pending event span, we stop applying events at the budget and hold
 * the rest for the next call (instead of applying-then-dropping their samples). */
static int s_apu_resume_event = -1;  // index of first un-applied event, -1 = none
static int s_apu_resume_offset = 0;  // samples already rendered of the pending span

/* Replay overflow + backlog state. If the event batch hits APU_EVENT_CAP,
 * dropped events/sentinels leave holes - replaying would corrupt audio - so
 * the batch is dropped and the ring rebaselined (audio_take_replay_overflow).
 * s_apu_pending_frames tracks frames recorded but not yet consumed by
 * replay; used for drain override and pause-throttling. */
static bool s_apu_replay_overflow;
static int s_apu_pending_frames;

/* Replay-domain wave RAM image. Replay renders CH3 behind emulation, but
 * wave RAM writes land in live audio_mem immediately (CPU-visible). At
 * stream boundaries (sample end -> wave RAM zeroed/reloaded) replay would
 * read future data in span-start windows and frame tails = silence gaps.
 * Renderers (and wave writes during replay) use this image instead; it is
 * synced from live state whenever the batch drains. */
static uint8_t s_replay_wave[16];
static uint8_t* s_wave_render_src;  // NULL = live audio_mem

static inline __attribute__((always_inline)) int get_audio_sample_rate(void);

static inline uint32_t ch3_step_period(const chan* c)
{
    return 2u * (2048u - c->freq);
}

/* Anchor cursor to current channel state (frame start, or NR30/NR33/NR34/NR52
 * writes: freq change alters step period, trigger restarts step phase). */
__shell static void ch3_cursor_reanchor(const audio_data* audio, uint32_t apu_count)
{
    const chan* c = &audio->chans[2];
    s_ch3_cursor_pos = c->val & 31;
    s_ch3_step_period = ch3_step_period(c);
    // freq_counter holds fractional progress toward the next step;
    // backdate last_step by it.
    uint32_t frac = (uint32_t)(((uint64_t)c->freq_counter * s_ch3_step_period +
                                (uint64_t)get_audio_sample_rate() / 2) /
                               (uint64_t)get_audio_sample_rate());
    s_ch3_last_step = (int64_t)apu_count - (int64_t)frac;
}

__shell static void ch3_cursor_reset(const audio_data* audio, uint32_t base_count)
{
    ch3_cursor_reanchor(audio, base_count);
    s_ch3_cursor_valid = true;
}

__shell static void ch3_cursor_advance(const audio_data* audio, uint32_t apu_count)
{
    const chan* c = &audio->chans[2];
    if (!(c->powered && c->enabled))
        return;
    int64_t dist = (int64_t)apu_count - s_ch3_last_step;
    if (dist < (int64_t)s_ch3_step_period)
        return;  // mid-frame restart (dist < 0) or no full step yet
    // div/mod instead of per-step loop: PDM carriers step every 2 cycles.
    uint32_t steps = (uint32_t)(dist / s_ch3_step_period);
    s_ch3_cursor_pos = (s_ch3_cursor_pos + steps) & 31;
    s_ch3_last_step += (int64_t)steps * s_ch3_step_period;
}

/* DMG write window: the write's 4-dot bus transaction must overlap CH3's byte
 * read (writes are timestamped at their end). */
static inline bool ch3_cursor_just_read(uint32_t apu_count)
{
    int64_t last_byte_read =
        s_ch3_last_step - ((s_ch3_cursor_pos & 1) ? (int64_t)s_ch3_step_period : 0);
    int64_t delta = (int64_t)apu_count - last_byte_read;
    return delta >= 0 && delta <= 4;
}

/* Fractional CH3 gain for NR32 PDM (Pokemon Yellow cries): per-sample
 * time-weighted volume instead of integer-grid edge snapping (jitter).
 * Transient, replay-only; never serialized. */
#define APU_GAIN_CAP 3072  // >= MAX_AUDIO_SAMPLES_PER_CHUNK (2940)
static uint16_t s_ch3_gain_q8[APU_GAIN_CAP];

/* Same for CH1/CH2 NRx2 PCM (Cannon Fodder vocals): vol*16 (0-240, exact /16).
 * Index 0 = CH1 (0xFF12), 1 = CH2 (0xFF17). */
static uint16_t s_sq_gain_q4[2][APU_GAIN_CAP];

/* Volume -> gain Q8, matching wave_sample's shift: 0=mute, 1=100%, 2=50%, 3=25%. */
static inline uint16_t ch3_gain_q8(unsigned volume)
{
    static const uint16_t map[4] = {0, 256, 128, 64};
    return map[volume & 3];
}

/* Accumulate gain g over Q16 sample interval [a_q16, b_q16) into per-sample
 * weighted sums, clamped to bound. */
static void apu_gain_fill(uint16_t* gain, uint32_t a_q16, uint32_t b_q16, uint16_t g, int bound)
{
    uint32_t pos = a_q16;
    while (pos < b_q16)
    {
        uint32_t s = pos >> 16;
        if ((int)s >= bound)
            break;
        uint32_t next = (s + 1) << 16;
        if (next > b_q16)
            next = b_q16;
        gain[s] += (uint16_t)(((uint32_t)g * (next - pos)) >> 16);
        pos = next;
    }
}
#define AUDIO_MEM_SIZE (0xFF40 - 0xFF10)
#define AUDIO_ADDR_COMPENSATION 0xFF10

#ifndef MAX
#define MAX(a, b) (a > b ? a : b)
#endif

#ifndef MIN
#define MIN(a, b) (a <= b ? a : b)
#endif

#define VOL_INIT_MAX (INT16_MAX / 8)
#define VOL_INIT_MIN (INT16_MIN / 8)

/* Fixed timing reference, independent of output sample rate. */
#define FREQ_INC_REF 44100

#define MAX_CHAN_VOLUME 15

#if TARGET_PLAYDATE
/* Q1.15 fixed-point equivalents of the factors below. */
static const int16_t get_charge_factors_q15[2][3] = {{32637, 32507, 32378}, {29632, 26797, 24233}};
#else

/* Factors: base^(DMG_CLOCK_FREQ / sample_rate); DMG base 0.999958, CGB 0.998943. */
static const float get_charge_factors[2][3] = {{0.996f, 0.992f, 0.988f}, {0.904f, 0.818f, 0.739f}};

static inline int16_t clamp16(float s)
{
    if (s < -32768.0f)
        return -32768;
    if (s > 32767.0f)
        return 32767;
    return (int16_t)s;
}
#endif

static inline __attribute__((always_inline)) int get_effective_sample_rate(void)
{
    return (preferences_sound_mode == 2) ? 0 : preferences_sample_rate;
}

static inline __attribute__((always_inline)) int get_sample_replication(void)
{
    return get_effective_sample_rate() + 1;
}

static inline __attribute__((always_inline)) int get_audio_sample_rate(void)
{
    return FREQ_INC_REF / get_sample_replication();
}

static uint32_t precomputed_noise_freqs[8][16];

static inline __attribute__((always_inline)) bool calculate_new_sweep_freq(
    audio_data* audio, chan* c, int i
);

static inline __attribute__((always_inline)) void set_note_freq(chan* c, const uint32_t freq)
{
    c->freq_inc = freq;  // freq >= 64
}

static inline __attribute__((always_inline)) void chan_enable(
    audio_data* audio, const uint_fast8_t i, const bool enable
)
{
    uint8_t val;
    chan* chans = audio->chans;
    chans[i].enabled = enable;
    val = (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] & 0x80) | (chans[3].enabled << 3) |
          (chans[2].enabled << 2) | (chans[1].enabled << 1) | (chans[0].enabled << 0);

    audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] = val;
}

__shell void audio_div_apu_tick(audio_data* audio);

__shell void __apu_div_tick_detect(audio_data* audio, uint8_t old_div, uint8_t inc, unsigned mask)
{
    while (inc--)
    {
        old_div++;
        if ((old_div & mask) == 0 && ((old_div - 1) & mask) != 0)
            audio_div_apu_tick(audio);
    }
}

__shell void audio_div_apu_tick(audio_data* audio)
{
    if (audio->skip_next_apu_tick)
    {
        audio->skip_next_apu_tick = false;
        return;
    }

    uint8_t step = (audio->div_apu_step + 1) & 7;
    audio->div_apu_step = step;

    chan* chans = audio->chans;

    // Length timer: 256 Hz (steps 0, 2, 4, 6).
    if ((step & 1) == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            chan* c = chans + i;
            if (!c->enabled || !c->len_enabled)
                continue;
            uint16_t remaining = (uint16_t)(c->len.inc >> 16);
            if (remaining == 0)
                continue;
            remaining--;
            c->len.inc = (c->len.inc & 0xFFFF) | ((uint32_t)remaining << 16);
            if (remaining == 0)
                chan_enable(audio, i, 0);
        }
    }

    // Process pending envelope ticks from previous step-7 events.
    // Hardware delays the volume change by half a DIV-APU cycle: the
    // clock rises at divider 0, volume changes when it falls next tick.
    {
        for (int i = 0; i < 4; i++)
        {
            if (i == 2)
                continue;
            chan* c = chans + i;
            if (!c->enabled || !c->env_pending)
                continue;
            c->env_pending = false;

            if (!c->env.clock)
                continue;

            c->env.clock = false;

            if (c->env.locked)
            {
                c->env.locked = c->env.should_lock;
                continue;
            }

            c->volume += c->env.up ? 1 : -1;
            if (c->volume == 0 || c->volume == MAX_CHAN_VOLUME)
                c->env.locked = true;
#if TARGET_PLAYDATE
            int vol = c->volume;
            asm volatile("usat %0, #4, %0" : "+r"(vol));
            c->volume = vol;
#else
            c->volume = MAX(0, MIN(MAX_CHAN_VOLUME, c->volume));
#endif
        }
    }

    // Envelope: 64 Hz (step 7). Decrement divider; when zero,
    // raise the envelope clock (volume change deferred to next tick).
    if (step == 7)
    {
        for (int i = 0; i < 4; i++)
        {
            if (i == 2)
                continue;
            chan* c = chans + i;
            if (!c->enabled || c->env.step == 0)
                continue;
            if (c->env.clock)
                continue;
            if (c->env.locked)
                continue;

            c->env_divider--;
            if (c->env_divider == 0)
            {
                c->env_divider = c->env.step;
                c->env.clock = true;
                c->env.should_lock =
                    (c->volume == MAX_CHAN_VOLUME && c->env.up) || (c->volume == 0 && !c->env.up);
                c->env_pending = true;
            }
        }
    }

    // Sweep: 128 Hz (steps 2, 6). Decrement divider; when zero, recalculate.
    if (step == 2 || step == 6)
    {
        chan* c0 = chans + 0;
        if (c0->enabled && c0->sweep.enabled && c0->sweep.rate > 0)
        {
            c0->sweep.divider--;
            if (c0->sweep.divider == 0)
            {
                c0->sweep.divider = c0->sweep.rate;

                if (calculate_new_sweep_freq(audio, c0, 0))
                    goto sweep_done;

                if (c0->sweep.shift > 0)
                {
                    uint16_t second;
                    second = c0->sweep.freq >> c0->sweep.shift;
                    if (!c0->sweep_up)
                    {
                        second = c0->sweep.freq - second;
                        c0->sweep.did_subtract = true;
                    }
                    else
                    {
                        second = c0->sweep.freq + second;
                    }
                    if (second > 2047)
                        chan_enable(audio, 0, 0);
                }
            }
        }
    sweep_done:;
    }
}

static inline __attribute__((always_inline)) void _nrx2_glitch(
    chan* chans, int i, uint8_t new_val, uint8_t old_val
)
{
    bool env_running = (chans[i].env.step != 0);
    if (env_running)
        chans[i].env.inc = (new_val & 7) ? 64ul / (uint32_t)(new_val & 7) : 8ul;

    bool should_invert = (new_val & 8) ^ (old_val & 8);
    bool should_tick = (new_val & 7) && !(old_val & 7) && !chans[i].env.locked;

    // Edge case: both old and new are vol=0, dir=up, step=0, not locked
    if ((new_val & 0xF) == 8 && (old_val & 0xF) == 8 && !chans[i].env.locked)
        should_tick = true;

    if (should_invert)
    {
        if (new_val & 8)
        {
            if (!(old_val & 7) && !chans[i].env.locked)
                chans[i].volume ^= 0xF;
            else
            {
                chans[i].volume = 0xE - chans[i].volume;
                chans[i].volume &= 0xF;
            }
            should_tick = false;
        }
        else
        {
            chans[i].volume = 0x10 - chans[i].volume;
            chans[i].volume &= 0xF;
        }
    }

    if (should_tick)
    {
        if (new_val & 8)
            chans[i].volume++;
        else
            chans[i].volume--;
        chans[i].volume &= 0xF;
    }
}

/* NRx2 write side effects: zombie volume glitch + envelope fields.
 * Shared by audio_write and the square-PCM gain pre-pass (shadow chan). */
static inline __attribute__((always_inline)) void apply_nrx2(
    chan* chans, int i, uint8_t val, uint8_t old_val, bool is_cgb
)
{
    // SameBoy _nrx2_glitch: adjust volume on NRx2 write while channel active.
    // DMG: 2-pass via 0xFF intermediate. CGB: single pass.
    if (chans[i].powered && chans[i].enabled)
    {
        if (!is_cgb)
        {
            _nrx2_glitch(chans, i, 0xFF, old_val);
            _nrx2_glitch(chans, i, val, 0xFF);
        }
        else
        {
            _nrx2_glitch(chans, i, val, old_val);
        }
    }

    chans[i].volume_init = val >> 4;
    chans[i].powered = (val >> 3) != 0;
    chans[i].env.up = (val & 0x08) != 0;
    chans[i].env.step = val & 0x07;
    chans[i].env.inc = chans[i].env.step ? 64ul / (uint32_t)chans[i].env.step : 8ul;
    chans[i].env.counter = 0;

    // Reload divider and recompute lock state when NRx2 is written while
    // the envelope clock is high (the glitch may have changed volume).
    if (chans[i].enabled && chans[i].env.clock && chans[i].env.step > 0)
    {
        chans[i].env_divider = chans[i].env.step;
        chans[i].env.should_lock = (chans[i].volume == MAX_CHAN_VOLUME && chans[i].env.up) ||
                                   (chans[i].volume == 0 && !chans[i].env.up);
    }
}

static inline __attribute__((always_inline)) bool calculate_new_sweep_freq(
    audio_data* audio, chan* c, int i
)
{
    uint16_t new_freq;
    new_freq = c->sweep.freq >> c->sweep.shift;

    if (!c->sweep_up)
    {
        new_freq = c->sweep.freq - new_freq;
        if (c->sweep.shift > 0)
            c->sweep.did_subtract = true;
    }
    else
    {
        new_freq = c->sweep.freq + new_freq;
    }

    if (new_freq > 2047)
    {
        chan_enable(audio, i, 0);
        return true;
    }

    if (c->sweep.shift > 0)
    {
        c->freq = new_freq;
        c->sweep.freq = new_freq;
    }

    return false;  // Channel still active
}

/* Returns the sample index at which the length counter expires (<= len). */
static inline __attribute__((always_inline)) int update_len(
    audio_data* restrict audio, chan* c, int len
)
{
    if (!c->enabled)
        return 0;

    if (!c->len_enabled || c->len.inc == 0)
        return len;

    int remaining = (int)(c->len.inc >> 16);
    if (remaining == 0)
        return 0;
    int sample_rate = get_audio_sample_rate();
    int max_len = (remaining * sample_rate + 255) / 256;
    if (max_len < len)
        return max_len;
    return len;
}

__attribute__((always_inline)) static inline bool update_freq(
    chan* c, uint32_t* pos, int sample_rate
)
{
    uint32_t inc = c->freq_inc - *pos;
    c->freq_counter += inc;

    if (c->freq_counter > sample_rate)
    {
        *pos = c->freq_inc - (c->freq_counter - sample_rate);
        c->freq_counter = 0;
        return true;
    }
    else
    {
        *pos = c->freq_inc;
        return false;
    }
}

/* gain_q4: optional per-sample volume array (vol*16, replay fractional NRx2
 * PCM). When non-NULL it carries the write-driven volume: envelope_smooth is
 * bypassed and the above-Nyquist early-out is lifted (PCM players park the
 * carrier at max frequency and stay audible via the averaged DC mean). */
__apu_sample_gen static void update_square(
    audio_data* restrict audio, int16_t* left, int16_t* right, const bool ch2, int len,
    const uint16_t* gain_q4
)
{
    chan* c = audio->chans + ch2;

    if (s_audio_skip_mask & (1 << ch2))
        return;

    if (!c->powered)
        return;

    if (!c->enabled)
        return;

    uint32_t freq = DMG_CLOCK_FREQ_U / ((2048 - c->freq) << 5);
    set_note_freq(c, freq);
    c->freq_inc *= 8;

    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

    // gain_q4 implies accurate (replay-only); fast mode takes the cheap path.
    const bool accurate = gain_q4 || (preferences_sound_mode == 2);

    if (!gain_q4 && c->freq_inc / 8 > (uint32_t)sample_rate / 2)
        return;

    len = update_len(audio, c, len);

#if TARGET_PLAYDATE
    int16_t final_vol_l = c->on_left * audio->vol_l;
    int16_t final_vol_r = c->on_right * audio->vol_r;
    uint32_t packed_vols;

    asm volatile("pkhbt %0, %1, %2, lsl #16"
                 : "=r"(packed_vols)
                 : "r"(final_vol_l), "r"(final_vol_r));
#endif

    uint16_t last_freq = 0;
    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        if (!ch2)
        {
            if (c->freq != last_freq)
            {
                last_freq = c->freq;
                uint32_t new_freq = DMG_CLOCK_FREQ_U / ((2048 - last_freq) << 5);
                set_note_freq(c, new_freq);
                c->freq_inc *= 8;
            }
        }

        int32_t sample_out;

        if (!accurate)
        {
            // --- FAST MODE: plain step counting, no per-sample divide ---
            c->freq_counter += c->freq_inc;
            int step_count = 0;
            int32_t step_sum = 0;
            while (c->freq_counter >= (uint32_t)sample_rate)
            {
                c->freq_counter -= sample_rate;
                c->square.duty_counter = (c->square.duty_counter + 1) & 7;
                c->val = (c->square.duty & (1 << c->square.duty_counter))
                             ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                             : VOL_INIT_MIN / MAX_CHAN_VOLUME;
                step_count++;
                step_sum += c->val;
            }

            if (step_count >= 3)
                sample_out = (step_sum / step_count) * c->volume;
            else
                sample_out = c->val * c->volume;

            // Keep the smoothing ramp synced for a click-free switch to accurate.
            c->envelope_smooth = (int32_t)c->volume << 8;
        }
        else
        {
            // --- ACCURATE MODE (incl. gain replay) ---
            uint32_t pos = 0;
            uint32_t prev_pos = 0;
            int32_t weighted_sum = 0;

            while (update_freq(c, &pos, sample_rate))
            {
                c->square.duty_counter = (c->square.duty_counter + 1) & 7;
                weighted_sum += (int32_t)(pos - prev_pos) * c->val;
                c->val = (c->square.duty & (1 << c->square.duty_counter))
                             ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                             : VOL_INIT_MIN / MAX_CHAN_VOLUME;
                prev_pos = pos;
            }

            weighted_sum += (int32_t)(c->freq_inc - prev_pos) * c->val;

            int32_t effective_volume;
            if (gain_q4)
            {
                // Gain array carries the volume; skip the smoothing ramp.
                effective_volume = ((int32_t)gain_q4[i] + 8) >> 4;
            }
            else
            {
                int32_t target_vol = (int32_t)c->volume << 8;
                c->envelope_smooth += (target_vol - c->envelope_smooth) >> 3;
                effective_volume = (c->envelope_smooth + 128) >> 8;
            }

            if (c->freq_inc > 0)
                sample_out = (weighted_sum / (int32_t)c->freq_inc) * effective_volume;
            else
                sample_out = c->val * effective_volume;

            if (c->sample_surpressed)
            {
                sample_out = INT16_MIN / 8;
                c->sample_surpressed = false;
            }
        }

        if (c->muted)
            continue;

#if TARGET_PLAYDATE
        // --- Hardware ---
        int16_t sample16 = sample_out / 4;
        uint32_t packed_sample = (uint32_t)((uint16_t)sample16) | ((uint32_t)sample16 << 16);
        if (left == right)  // MONO
        {
            int32_t stereo_sum;
            asm volatile("smuad %0, %1, %2"
                         : "=r"(stereo_sum)
                         : "r"(packed_sample), "r"(packed_vols));
            left[i] += (int16_t)(stereo_sum >> 1);
        }
        else  // STEREO
        {
            int32_t left_contrib, right_contrib;
            asm volatile("smulbb %0, %1, %2"
                         : "=r"(left_contrib)
                         : "r"(packed_sample), "r"(packed_vols));
            asm volatile("smultt %0, %1, %2"
                         : "=r"(right_contrib)
                         : "r"(packed_sample), "r"(packed_vols));
            left[i] += (int16_t)left_contrib;
            right[i] += (int16_t)right_contrib;
        }
#else
        // --- Simulator Path ---
        sample_out /= 4;
        if (left == right)  // MONO
        {
            int32_t left_contrib = sample_out * c->on_left * audio->vol_l;
            int32_t right_contrib = sample_out * c->on_right * audio->vol_r;
            left[i] += (left_contrib + right_contrib) >> 1;
        }
        else  // STEREO
        {
            left[i] += sample_out * c->on_left * audio->vol_l;
            right[i] += sample_out * c->on_right * audio->vol_r;
        }
#endif
    }
}

static inline __attribute__((always_inline)) uint8_t* wave_render_ram(audio_data* audio)
{
    return s_wave_render_src ? s_wave_render_src
                             : audio_mem(audio) + (0xFF30 - AUDIO_ADDR_COMPENSATION);
}

static inline __attribute__((always_inline)) int8_t
wave_sample(audio_data* audio, const unsigned int pos, const unsigned int volume)
{
    uint8_t sample;

    sample = wave_render_ram(audio)[pos / 2];
    if (pos & 1)
    {
        sample &= 0xF;
    }
    else
    {
        sample >>= 4;
    }
    int8_t signed_sample = (int8_t)sample - 8;

    return volume ? (signed_sample >> (volume - 1)) : 0;
}

/* Sum of wave_sample over all 32 wave positions with the given volume.
 * Used by the extreme-frequency fast path in update_wave. */
__apu_sample_gen static int16_t wave_cycle_sum(audio_data* audio, const chan* c, unsigned volume)
{
    int16_t sum = 0;
    for (int p = 0; p < 32; p++)
        sum += wave_sample(audio, p, volume);
    return sum;
}

/* gain_q8: optional per-sample gain array (Q8, replay fractional NR32 PDM).
 * When non-NULL, volume is treated as 1 internally and each output sample is
 * scaled by gain_q8[i]; the muted early-out is bypassed (gain 0 handles
 * silence while the carrier keeps advancing). Accurate mode only:
 * sample_replication is 1, so gain_q8[i] aligns with output index i. */
__apu_sample_gen static void update_wave(
    audio_data* restrict audio, int16_t* left, int16_t* right, int len, const uint16_t* gain_q8
)
{
    chan* c = audio->chans + 2;

    if (s_audio_skip_mask & 4)
        return;

    if (!c->powered)
        return;

    if (!c->enabled)
    {
        if (c->powered && c->wave.pulsed)
        {
            gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
            uint8_t* wave_ram = wave_render_ram(audio);
            uint8_t corrupt_byte = wave_ram[gb->cpu_reg.pc & 0xF];
            int8_t nibble = (int8_t)(corrupt_byte >> 4) - 8;
            c->wave.sample = c->volume ? (nibble >> (c->volume - 1)) : 0;
        }
        return;
    }

    uint32_t freq = (DMG_CLOCK_FREQ_U / 64) / (2048 - c->freq);
    set_note_freq(c, freq);
    c->freq_inc *= 32;

    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

    len = update_len(audio, c, len);

    // Effective internal volume: 1 when a gain array carries the envelope.
    const unsigned volume = gain_q8 ? 1 : c->volume;
    c->wave.sample = wave_sample(audio, c->val, volume);

    /* CH3 muted (NR32 volume 0): no output, but keep position and frequency
     * counter advancing (wave RAM access timing, cursor anchor). O(1) per
     * chunk. Idle state of PDM-via-NR32 players (Yellow parks CH3 at max
     * frequency, volume 0). */
    if (!gain_q8 && c->volume == 0)
    {
        uint32_t total = c->freq_inc * (uint32_t)len + c->freq_counter;
        uint32_t steps = total / (uint32_t)sample_rate;
        c->val = (c->val + steps) & 31;
        c->freq_counter = total % (uint32_t)sample_rate;
        if (steps)
            c->wave.just_read = true;
        return;
    }

#if TARGET_PLAYDATE
    int16_t final_vol_l = c->on_left * audio->vol_l;
    int16_t final_vol_r = c->on_right * audio->vol_r;
    uint32_t packed_vols;

    asm volatile("pkhbt %0, %1, %2, lsl #16"
                 : "=r"(packed_vols)
                 : "r"(final_vol_l), "r"(final_vol_r));
#endif

    /* Extreme frequency (a full 32-step wave cycle per output sample, e.g.
     * period 2047 = 65.6 kHz NR32 PDM carrier): collapse whole cycles
     * analytically via the cached nibble sum; remainder (< 32 steps) runs
     * through the normal loop. The weighted average yields the correct
     * band-limited mean, like hardware + speaker low-pass. */
    const bool extreme_freq = c->freq_inc >= 32u * (uint32_t)sample_rate;
    if (extreme_freq && (!c->wave.sum_valid || c->wave.sum_volume != volume))
    {
        c->wave.sum = wave_cycle_sum(audio, c, volume);
        c->wave.sum_volume = volume;
        c->wave.sum_valid = true;
    }

    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        int32_t sample_out;

        uint32_t pos = 0;
        uint32_t prev_pos = 0;
        int32_t weighted_sum = 0;

        if (extreme_freq)
        {
            // First step: consume freq_counter leftover from previous sample.
            if (update_freq(c, &pos, sample_rate))
            {
                weighted_sum += (int32_t)(pos - prev_pos) * c->wave.sample;
                c->val = (c->val + 1) & 31;
                c->wave.sample = wave_sample(audio, c->val, volume);
                c->wave.just_read = true;
                prev_pos = pos;
            }

            // Skip full 32-step wave cycles (c->val unchanged by 32 steps).
            uint32_t cycle_span = 32u * (uint32_t)sample_rate;
            uint32_t full = (c->freq_inc - prev_pos) / cycle_span;
            if (full)
            {
                weighted_sum += (int32_t)(full * (uint32_t)sample_rate) * (int32_t)c->wave.sum;
                prev_pos += full * cycle_span;
                pos = prev_pos;  // update_freq derives the next step from *pos
                c->wave.just_read = true;
            }
        }

        while (update_freq(c, &pos, sample_rate))
        {
            weighted_sum += (int32_t)(pos - prev_pos) * c->wave.sample;
            c->val = (c->val + 1) & 31;
            c->wave.sample = wave_sample(audio, c->val, volume);
            c->wave.just_read = true;
            prev_pos = pos;
        }

        weighted_sum += (int32_t)(c->freq_inc - prev_pos) * c->wave.sample;
        int32_t avg = (c->freq_inc > 0) ? (weighted_sum / (int32_t)c->freq_inc) : c->wave.sample;
        sample_out = avg * (INT16_MAX / 32);

        if (gain_q8)
            sample_out = (sample_out * (int32_t)gain_q8[i]) >> 8;

        if (c->muted)
            sample_out = 0;

        int32_t mono_sample = sample_out;
#if TARGET_PLAYDATE
        int16_t sample16 = mono_sample / 4;
        uint32_t packed_sample = (uint32_t)((uint16_t)sample16) | ((uint32_t)sample16 << 16);

        if (left == right)
        {
            int32_t stereo_sum;
            asm volatile("smuad %0, %1, %2"
                         : "=r"(stereo_sum)
                         : "r"(packed_sample), "r"(packed_vols));
            left[i] += (int16_t)(stereo_sum >> 1);
        }
        else
        {
            int32_t left_contrib, right_contrib;
            asm volatile("smulbb %0, %1, %2"
                         : "=r"(left_contrib)
                         : "r"(packed_sample), "r"(packed_vols));
            asm volatile("smultt %0, %1, %2"
                         : "=r"(right_contrib)
                         : "r"(packed_sample), "r"(packed_vols));
            left[i] += (int16_t)left_contrib;
            right[i] += (int16_t)right_contrib;
        }
#else
        // --- Simulator Path ---
        mono_sample /= 4;
        if (left == right)
        {
            int32_t left_contrib = mono_sample * c->on_left * audio->vol_l;
            int32_t right_contrib = mono_sample * c->on_right * audio->vol_r;
            left[i] += (left_contrib + right_contrib) / 2;
        }
        else
        {
            left[i] += mono_sample * c->on_left * audio->vol_l;
            right[i] += mono_sample * c->on_right * audio->vol_r;
        }
#endif
    }
}

__apu_sample_gen static void update_noise(
    audio_data* restrict audio, int16_t* left, int16_t* right, int len
)
{
    chan* c = audio->chans + 3;

    if (s_audio_skip_mask & 8)
        return;

    if (!c->powered)
        return;

    if (!c->enabled)
        return;

    uint32_t freq = precomputed_noise_freqs[c->noise.lfsr_div][c->freq];
    set_note_freq(c, freq);

    len = update_len(audio, c, len);
    if (len == 0)
        return;

    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

    const bool accurate = (preferences_sound_mode == 2);

#if TARGET_PLAYDATE
    int16_t final_vol_l = c->on_left * audio->vol_l;
    int16_t final_vol_r = c->on_right * audio->vol_r;
    uint32_t packed_vols;
    asm volatile("pkhbt %0, %1, %2, lsl #16"
                 : "=r"(packed_vols)
                 : "r"(final_vol_l), "r"(final_vol_r));
#endif

    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        int32_t mono_sample;

        if (!accurate)
        {
            // --- FAST MODE: step counting, no envelope ramp, no per-sample divide ---
            c->freq_counter += c->freq_inc;
            int step_count = 0;
            int32_t step_sum = 0;
            if (c->freq >= 14)
            {
                // LFSR frozen; cap counter to avoid wasted loop spins.
                c->freq_counter %= sample_rate;
            }
            else
            {
                while (c->freq_counter >= (uint32_t)sample_rate)
                {
                    c->freq_counter -= sample_rate;

                    uint8_t xor_res =
                        ((c->noise.lfsr_reg >> 0) & 1) == ((c->noise.lfsr_reg >> 1) & 1);
                    c->noise.lfsr_reg >>= 1;
                    c->noise.lfsr_reg |= (xor_res << 14);
                    if (c->lfsr_narrow)
                        c->noise.lfsr_reg = (c->noise.lfsr_reg & ~(1 << 6)) | (xor_res << 6);
                    c->val = (c->noise.lfsr_reg & 1) ? (VOL_INIT_MAX / MAX_CHAN_VOLUME)
                                                     : (VOL_INIT_MIN / MAX_CHAN_VOLUME);
                    step_count++;
                    step_sum += c->val;
                }
            }

            if (c->muted)
                continue;

            if (step_count > 0)
                mono_sample = (step_sum / step_count) * c->volume;
            else
                mono_sample = c->val * c->volume;

            // Keep the smoothing ramp synced for a click-free switch to accurate.
            c->envelope_smooth = (int32_t)c->volume << 8;
        }
        else
        {
            // --- ACCURATE MODE ---
            uint32_t fc = c->freq_counter;
            c->freq_counter += c->freq_inc;
            uint32_t total = c->freq_inc;
            int32_t weighted_sum = 0;

            if (c->freq >= 14)
            {
                weighted_sum = (int32_t)total * c->val;
                c->freq_counter %= sample_rate;
            }
            else
            {
                uint32_t first = sample_rate - fc;
                if (first > total)
                    first = total;
                weighted_sum += (int32_t)first * c->val;
                uint32_t done = first;

                while (done < total)
                {
                    uint8_t xor_res =
                        ((c->noise.lfsr_reg >> 0) & 1) == ((c->noise.lfsr_reg >> 1) & 1);
                    c->noise.lfsr_reg >>= 1;
                    c->noise.lfsr_reg |= (xor_res << 14);
                    if (c->lfsr_narrow)
                        c->noise.lfsr_reg = (c->noise.lfsr_reg & ~(1 << 6)) | (xor_res << 6);
                    c->val = (c->noise.lfsr_reg & 1) ? (VOL_INIT_MAX / MAX_CHAN_VOLUME)
                                                     : (VOL_INIT_MIN / MAX_CHAN_VOLUME);

                    uint32_t seg = sample_rate;
                    if (total - done < seg)
                        seg = total - done;
                    weighted_sum += (int32_t)seg * c->val;
                    done += seg;
                }

                c->freq_counter %= sample_rate;
            }

            if (c->muted)
                continue;

            int32_t target_vol = (int32_t)c->volume << 8;
            c->envelope_smooth += (target_vol - c->envelope_smooth) >> 3;
            int32_t effective_volume = (c->envelope_smooth + 128) >> 8;

            if (total > 0)
                mono_sample = (weighted_sum / (int32_t)total) * effective_volume;
            else
                mono_sample = c->val * effective_volume;
        }
#if TARGET_PLAYDATE
        int16_t sample16 = mono_sample / 4;

        uint32_t packed_sample = (uint32_t)((uint16_t)sample16) | ((uint32_t)sample16 << 16);

        if (left == right)  // MONO
        {
            int32_t stereo_sum;
            asm volatile("smuad %0, %1, %2"
                         : "=r"(stereo_sum)
                         : "r"(packed_sample), "r"(packed_vols));

            left[i] += (int16_t)(stereo_sum >> 1);
        }
        else  // STEREO
        {
            int32_t left_contrib, right_contrib;
            asm volatile("smulbb %0, %1, %2"
                         : "=r"(left_contrib)
                         : "r"(packed_sample), "r"(packed_vols));
            asm volatile("smultt %0, %1, %2"
                         : "=r"(right_contrib)
                         : "r"(packed_sample), "r"(packed_vols));
            left[i] += (int16_t)left_contrib;
            right[i] += (int16_t)right_contrib;
        }
#else
        // --- Simulator Path ---
        mono_sample /= 4;
        if (left == right)
        {
            int32_t left_contrib = mono_sample * c->on_left * audio->vol_l;
            int32_t right_contrib = mono_sample * c->on_right * audio->vol_r;
            left[i] += (left_contrib + right_contrib) / 2;
        }
        else
        {
            left[i] += mono_sample * c->on_left * audio->vol_l;
            right[i] += mono_sample * c->on_right * audio->vol_r;
        }
#endif
    }
}

__apu_write static void chan_trigger(audio_data* restrict audio, uint_fast8_t i)
{
    chan* chans = audio->chans;
    chan* c = chans + i;

    // DMG wave RAM corruption on retrigger: must capture before chan_enable.
    bool wave_was_active = (i == 2) && c->enabled;
    bool was_enabled = c->enabled;

    // Digital-zero surpression on first start only (not re-trigger).
    // Must check c->enabled before chan_enable sets it to 1. Accurate mode only.
    if ((i == 0 || i == 1) && preferences_sound_mode == 2)
        c->sample_surpressed = !c->enabled;

    chan_enable(audio, i, 1);
    c->volume = c->volume_init;

    // volume envelope
    {
        uint8_t val = audio_mem(audio)[(0xFF12 + (i * 5)) - AUDIO_ADDR_COMPENSATION];

        c->env.step = val & 0x07;
        c->env.up = val & 0x08 ? 1 : 0;
        c->env.inc = c->env.step ? 64ul / (uint32_t)c->env.step : 8ul;
        c->env.locked = false;
        c->env.should_lock = false;
        c->env.clock = false;
        c->env_pending = false;
        // Accurate: CH4 keeps its shift divider phase across trigger.
        if (preferences_sound_mode == 2)
        {
            if (i != 3)
                c->freq_counter = 0;
        }
        else
        {
            c->freq_counter = 0;
        }

        if (c->env.step > 0)
        {
            c->env_divider = c->env.step;
            if (preferences_sound_mode == 2)
            {
                uint8_t next_step = (audio->div_apu_step + 1) & 7;
                if (next_step == 7)
                    c->env_divider++;
            }
        }
        else
        {
            c->env.counter = 0;
        }
    }

    // freq sweep
    if (i == 0)
    {
        uint8_t val = audio_mem(audio)[0xFF10 - AUDIO_ADDR_COMPENSATION];

        c->sweep.freq = c->freq;
        c->sweep.rate = (val >> 4) & 0x07;
        c->sweep_up = !(val & 0x08);
        c->sweep.shift = (val & 0x07);

        uint8_t period = c->sweep.rate;
        if (period == 0)
        {
            period = 8;
        }

        c->sweep.inc = (128 / period);

        c->sweep.enabled = (c->sweep.rate != 0) || (c->sweep.shift != 0);
        c->sweep.divider = c->sweep.rate ? c->sweep.rate : 8;
        if (preferences_sound_mode == 2 && c->sweep.rate > 0)
        {
            uint8_t next_step = (audio->div_apu_step + 1) & 7;
            if (next_step == 2 || next_step == 6)
                c->sweep.divider++;
        }

        if (c->sweep.shift > 0)
        {
            if (calculate_new_sweep_freq(audio, c, i))
            {
                return;
            }
        }

        c->sweep.counter = 0;
        c->sweep.did_subtract = false;
    }

    int len_max = 64;

    if (i == 2)
    {  // wave
        len_max = 256;
        // DMG wave RAM corruption on re-trigger when channel is about to read a sample.
        if (wave_was_active && c->freq_inc > 0 &&
            c->freq_counter + c->freq_inc >= (uint32_t)get_audio_sample_rate())
        {
            gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
            if (!gb->is_cgb_mode)
            {
                uint8_t* wave_ram = audio_mem(audio) + (0xFF30 - AUDIO_ADDR_COMPENSATION);
                int byte_idx = ((c->val + 1) >> 1) & 0xF;
                if (byte_idx < 4)
                {
                    wave_ram[0] = wave_ram[byte_idx];
                }
                else
                {
                    int aligned = byte_idx & ~3;
                    wave_ram[0] = wave_ram[aligned + 0];
                    wave_ram[1] = wave_ram[aligned + 1];
                    wave_ram[2] = wave_ram[aligned + 2];
                    wave_ram[3] = wave_ram[aligned + 3];
                }
            }
        }
        c->val = 0;
        c->wave.pulsed = true;
    }
    else if (i == 3)
    {  // noise
        // Inverted-LFSR (XNOR) convention: 0 is the dual of hardware's
        // all-ones load. 0x7FFF would latch the register (constant DC).
        c->noise.lfsr_reg = 0x0000;
        c->val = VOL_INIT_MIN / MAX_CHAN_VOLUME;
    }

    c->envelope_smooth = (int32_t)c->volume << 8;

    // Always reload length on trigger (hardware restarts the timer).
    int load = len_max - c->len.load;

    // Accurate mode also applies the obscure max-1 reload (63 vs 64) when
    // the next DIV-APU step doesn't clock the length counter.
    if (preferences_sound_mode == 2)
    {
        uint8_t div_apu_next = (audio->div_apu_step + 1) & 7;
        bool next_doesnt_clock_len = (div_apu_next & 1) != 0;

        if (next_doesnt_clock_len && c->len_enabled && load == len_max)
            load = len_max - 1;
    }

    c->len.inc = 256 | ((uint32_t)load << 16);
    c->len.counter = 0;
}

/* Read audio register (0xFF10 <= addr <= 0xFF3F, unchecked). */
uint8_t audio_read(audio_data* audio, const uint16_t addr)
{ /* clang-format off */
    static const uint8_t ortab[] =
    {
        0x80, 0x3f, 0x00, 0xff, 0xbf,
        0xff, 0x3f, 0x00, 0xff, 0xbf,
        0x7f, 0xff, 0x9f, 0xff, 0xbf,
        0xff, 0xff, 0x00, 0x00, 0xbf,
        0x00, 0x00, 0x70,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    /* clang-format on */

    /* Wave RAM reads during CH3 playback return the byte currently being read.
     * DMG: 0xFF unless the access coincides with CH3's read (just_read). */
    if (addr >= 0xFF30 && addr <= 0xFF3F)
    {
        chan* c = &audio->chans[2];
        if (c->powered && c->enabled)
        {
            gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
            uint8_t wave_idx = c->val >> 1;

            if (!gb->is_cgb_mode)
            {
                // DMG: CPU can only read wave RAM during same cycle CH3 reads.
                if (!c->wave.just_read)
                    return 0xFF;
            }
            c->wave.just_read = false;
            return audio_mem(audio)[0xFF30 + wave_idx - AUDIO_ADDR_COMPENSATION];
        }
    }

    /* PCM12: CH2 (high nibble) + CH1 (low). PCM34: CH4 (high) + CH3 (low).
     * CGB-only; DMG returns open bus (0xFF). */
    if (addr == 0xFF76 || addr == 0xFF77)
    {
        gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
        if (gb->is_cgb_mode)
        {
            int base_ch = (addr == 0xFF76) ? 0 : 2;
            uint8_t lo = 0, hi = 0;

            for (int ch = base_ch; ch < base_ch + 2; ch++)
            {
                chan* c = &audio->chans[ch];
                uint8_t val = 0;
                if (c->powered && c->enabled)
                {
                    if (ch < 2)
                    {
                        val = (c->square.duty & (1 << c->square.duty_counter)) ? c->volume : 0;
                    }
                    else if (ch == 2)
                    {
                        int idx = c->val >> 1;
                        uint8_t byte = audio_mem(audio)[0xFF30 + idx - AUDIO_ADDR_COMPENSATION];
                        uint8_t nibble = (c->val & 1) ? (byte & 0x0F) : (byte >> 4);
                        val = nibble >> (c->volume & 3);
                    }
                    else
                    {
                        val = (c->noise.lfsr_reg & 1) ? c->volume : 0;
                    }
                }
                if (ch == base_ch)
                    lo = val;
                else
                    hi = val;
            }
            return (hi << 4) | lo;
        }
    }

    return audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] | ortab[addr - AUDIO_ADDR_COMPENSATION];
}

/* Write audio register (0xFF10 <= addr <= 0xFF3F, unchecked). */
__apu_write void audio_write(
    audio_data* restrict audio, const uint16_t addr, const uint8_t val, uint32_t apu_count
)
{
    /* No recording under turbo (uncap fps): generation can never keep up
     * with unthrottled emulation; the batch would only overflow. Live state
     * application continues, and generation falls through to live render. */
    if (audio->pre_frame_valid && preferences_sound_mode == 2 && preferences_uncap_fps == 0)
    {
        if (s_apu_event_count < APU_EVENT_CAP)
        {
            s_apu_events[s_apu_event_count].apu_count = apu_count;
            s_apu_events[s_apu_event_count].addr = addr;
            s_apu_events[s_apu_event_count].val = val;
            s_apu_events[s_apu_event_count].div_step = 0;
            s_apu_event_count++;
        }
        else
        {
            s_apu_replay_overflow = true;
        }
    }
    uint_fast8_t i;
    chan* chans = audio->chans;

    if (addr == 0xFF26)
    {
        bool was_on = (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] & 0x80) != 0;
        audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val & 0x80;
        // On APU power off, clear all registers apart from wave RAM.
        if ((val & 0x80) == 0)
        {
            for (int r = 0; r < 0xFF26 - AUDIO_ADDR_COMPENSATION; r++)
                audio_mem(audio)[r] = 0x00;
            chans[0].enabled = false;
            chans[1].enabled = false;
            chans[2].enabled = false;
            chans[2].wave.sample = 0;
            chans[2].wave.pulsed = false;
            chans[3].enabled = false;
            audio->div_apu_step = 0;
            audio->skip_next_apu_tick = false;
        }
        else if (!was_on)
        {
            gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
            uint8_t mask = gb->cgb_fast_mode_active ? 0x20 : 0x10;
            if (gb->gb_reg.DIV & mask)
                audio->skip_next_apu_tick = true;

            /* CGB fast mode: DIV runs at 2x with a 7-bit sub-count, so the
             * frame sequencer (512 Hz) sits on DIV bits 4-6 instead of 3-5. */
            uint32_t reg_div16 = ((uint32_t)gb->gb_reg.DIV << (gb->cgb_fast_mode_active ? 7 : 8)) |
                                 gb->counter.div_count;
            int S_pon = (reg_div16 >> 11) & 7;
            int a = ((reg_div16 & 0x7FF) >= 1023) ? 1 : 0;
            audio->div_apu_step = (S_pon + a - 1) & 7;

            /* Capture the phase for replay (replay re-runs audio_write with
             * post-frame DIV). Store it in the just-logged event. */
            if (audio->pre_frame_valid && preferences_sound_mode == 2 && s_apu_event_count > 0 &&
                s_apu_events[s_apu_event_count - 1].addr == 0xFF26)
                s_apu_events[s_apu_event_count - 1].div_step =
                    (audio->div_apu_step & APU_EVENT_PHASE_MASK) | APU_EVENT_PHASE_VALID |
                    (audio->skip_next_apu_tick ? APU_EVENT_PHASE_SKIP : 0);
        }
        return;
    }

    /* Ignore register writes if APU powered off, except wave RAM (always
     * accessible; CH3 can't be active, so no redirect needed). */
    if (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] == 0x00)
    {
        if (addr >= 0xFF30 && addr <= 0xFF3F)
        {
            audio->chans[2].wave.sum_valid = false;
            audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val;
            if (s_wave_render_src)
                s_wave_render_src[addr - 0xFF30] = val;
        }
        return;
    }

    /* Wave RAM writes during CH3 playback redirect to the byte CH3 is reading.
     * DMG: ignored unless coincident with CH3's read. Accurate mode uses the
     * cycle cursor for position and window; the fallback (just_read) stays
     * loose by design (renderer has no cycle timing). */
    if (addr >= 0xFF30 && addr <= 0xFF3F)
    {
        chan* c = &audio->chans[2];
        if (c->powered && c->enabled)
        {
            gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
            if (!gb->is_cgb_mode)
            {
                bool can_access =
                    s_ch3_cursor_valid ? ch3_cursor_just_read(apu_count) : c->wave.just_read;
                if (!can_access)
                    return;
            }
            uint8_t wave_idx = (s_ch3_cursor_valid ? s_ch3_cursor_pos : (uint8_t)c->val) >> 1;
            // CGB: on the exact fetch cycle the channel's fetch wins and the
            // access targets the next byte (vba-m 912cc37).
            if (s_ch3_cursor_valid && apu_count == s_ch3_last_step && !(s_ch3_cursor_pos & 1))
                wave_idx = (wave_idx + 1) & 0xF;
            c->wave.sum_valid = false;
            audio_mem(audio)[0xFF30 + wave_idx - AUDIO_ADDR_COMPENSATION] = val;
            if (s_wave_render_src)
                s_wave_render_src[wave_idx] = val;
            return;
        }
        c->wave.sum_valid = false;
    }

    audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val;
    if (s_wave_render_src && addr >= 0xFF30 && addr <= 0xFF3F)
        s_wave_render_src[addr - 0xFF30] = val;

    i = (addr - AUDIO_ADDR_COMPENSATION) / 5;

    switch (addr)
    {
    case 0xFF10:
    {
        bool old_sweep_up = chans[0].sweep_up;

        uint8_t new_rate = (val >> 4) & 0x07;
        chans[0].sweep_up = !(val & 0x08);
        chans[0].sweep.shift = val & 0x07;

        if (new_rate == 0)
        {
            chans[0].sweep.rate = 0;
            chans[0].sweep.inc = 0;
            chans[0].sweep.enabled = (chans[0].sweep.shift != 0);
            chans[0].sweep.divider = 0;
        }
        else
        {
            bool was_zero = (chans[0].sweep.rate == 0);
            chans[0].sweep.rate = new_rate;
            chans[0].sweep.inc = 128 / new_rate;
            chans[0].sweep.enabled = true;
            if (was_zero)
                chans[0].sweep.divider = new_rate;
            // else: divider not reset; hardware loads new pace on
            // sweep iteration completion (when divider hits 0).
        }

        if (chans[0].sweep_up && chans[0].sweep.shift > 0)
        {
            uint16_t check = chans[0].sweep.freq + (chans[0].sweep.freq >> chans[0].sweep.shift);
            if (check > 2047)
                chan_enable(audio, 0, false);
        }

        if (old_sweep_up == false && chans[0].sweep_up == true && chans[0].sweep.did_subtract)
        {
            chan_enable(audio, 0, false);
        }
    }
    break;

    case 0xFF12:
    case 0xFF17:
    case 0xFF21:
    {
        gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
        uint8_t old_val = audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION];
        apply_nrx2(chans, i, val, old_val, gb->is_cgb_mode);
    }
    break;

    case 0xFF1C:
        chans[i].volume = chans[i].volume_init = (val >> 5) & 0x03;
        if (chans[i].powered && chans[i].enabled)
        {
            chans[i].wave.sample = wave_sample(audio, chans[i].val, chans[i].volume);
        }
        break;

    case 0xFF11:
    case 0xFF16:
    case 0xFF20:
    {
        static const uint8_t duty_lookup[] = {0x80, 0x81, 0xE1, 0x7E};
        chans[i].len.load = val & 0x3f;
        if (i < 2)
        {  // Only for square channels
            chans[i].square.duty = duty_lookup[val >> 6];
        }
        break;
    }

    case 0xFF1B:
        chans[i].len.load = val;
        break;

    case 0xFF13:
    case 0xFF18:
    case 0xFF1D:
        chans[i].freq &= 0xFF00;
        chans[i].freq |= val;
        break;

    case 0xFF1A:
    {
        bool dac_was_on = chans[i].powered;
        chans[i].powered = (val & 0x80) != 0;

        if (!chans[i].powered && dac_was_on && chans[i].enabled)
        {
            chans[i].wave.pulsed = false;
            bool about_to_read =
                (chans[i].freq_inc > 0 &&
                 chans[i].freq_counter + chans[i].freq_inc >= (uint32_t)get_audio_sample_rate());

            uint8_t* wave_ram = audio_mem(audio) + (0xFF30 - AUDIO_ADDR_COMPENSATION);
            uint8_t corrupt_byte;
            bool do_corrupt = false;

            if (about_to_read)
            {
                gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
                corrupt_byte = wave_ram[gb->cpu_reg.pc & 0xF];
                do_corrupt = true;
            }
            else if (chans[i].wave.just_read)
            {
                corrupt_byte = wave_ram[0xFF1A & 0xF];
                do_corrupt = true;
            }

            if (do_corrupt)
            {
                int8_t nibble = (int8_t)(corrupt_byte >> 4) - 8;
                chans[i].wave.sample = chans[i].volume ? (nibble >> (chans[i].volume - 1)) : 0;
            }
        }

        chan_enable(audio, i, val & 0x80);
        break;
    }

    case 0xFF14:
    case 0xFF19:
    case 0xFF1E:
        chans[i].freq &= 0x00FF;
        chans[i].freq |= ((val & 0x07) << 8);
        // Intentional fall-through.
    case 0xFF23:
    {
        bool old_len_enabled = chans[i].len_enabled;
        chans[i].len_enabled = val & 0x40 ? 1 : 0;
        bool trigger = val & 0x80;

        // Accurate mode: obscure length clock on enable when the next
        // DIV-APU step doesn't clock the length counter.
        if (preferences_sound_mode == 2)
        {
            uint8_t div_apu_next = (audio->div_apu_step + 1) & 7;
            bool next_doesnt_clock_len = (div_apu_next & 1) != 0;

            if (next_doesnt_clock_len && !old_len_enabled && chans[i].len_enabled)
            {
                int remaining = (int)(chans[i].len.inc >> 16);
                if (remaining > 0)
                {
                    chans[i].len.inc -= (1 << 16);
                    if (((chans[i].len.inc >> 16) == 0) && !trigger)
                        chan_enable(audio, i, 0);
                }
            }
        }

        if (trigger)
            chan_trigger(audio, i);
    }
    break;

    case 0xFF22:
        chans[3].freq = val >> 4;
        chans[3].lfsr_narrow = (val & 0x08) != 0;
        chans[3].noise.lfsr_div = val & 0x07;
        break;

    case 0xFF24:
        audio->vol_l = ((val >> 4) & 0x07);
        audio->vol_r = (val & 0x07);
        break;

    case 0xFF25:
        for (uint_fast8_t j = 0; j < 4; j++)
        {
            chans[j].on_left = (val >> (4 + j)) & 1;
            chans[j].on_right = (val >> j) & 1;
        }
        break;
    }
}

void audio_reset(audio_data* audio)
{
    chan* chans = audio->chans;

    memset(chans, 0, 4 * sizeof(chan));
    chans[0].val = chans[1].val = -1;
    chans[2].wave.sample = 0;
    chans[0].square.duty_counter = 2;
    chans[0].freq_counter = 0x7F9;
    chans[0].enabled = 1;
    chans[0].powered = 1;
    chans[3].noise.lfsr_reg = 0x7FFF;

#if TARGET_PLAYDATE
    audio->capacitor_l = 0;
    audio->capacitor_r = 0;
#else
    audio->capacitor_l = 0.0f;
    audio->capacitor_r = 0.0f;
#endif
    audio->skip_next_apu_tick = false;
    s_apu_event_count = 0;
    s_ch3_cursor_valid = false;
    s_apu_tick_left = 0;
    s_apu_tick_rem_accum = 0;
    s_apu_resume_event = -1;
    s_apu_resume_offset = 0;
    s_apu_replay_overflow = false;
    s_apu_pending_frames = 0;
    audio->pre_frame_valid = false;

    // Zero the audio registers so the core's writes
    // (incl. NR52 power-on) are deterministic across launches.
    memset(audio_mem(audio), 0, AUDIO_MEM_SIZE);
    memset(s_replay_wave, 0, sizeof(s_replay_wave));
    s_wave_render_src = NULL;

    for (uint8_t lfsr_selector_idx = 0; lfsr_selector_idx < 8; ++lfsr_selector_idx)
    {
        uint32_t current_lfsr_div_val = lfsr_selector_idx == 0 ? 4 : lfsr_selector_idx * 8;
        for (uint8_t c_freq_shift_val = 0; c_freq_shift_val < 16; ++c_freq_shift_val)
        {
            uint32_t divisor_term = current_lfsr_div_val << c_freq_shift_val;

            if (divisor_term == 0)
            {
                // unreachable with current values
                precomputed_noise_freqs[lfsr_selector_idx][c_freq_shift_val] = 0;
            }
            else
            {
                precomputed_noise_freqs[lfsr_selector_idx][c_freq_shift_val] =
                    DMG_CLOCK_FREQ_U / divisor_term;
            }
        }
    }
}

void audio_reset_snapshot(audio_data* audio)
{
    memcpy(audio->pre_frame_chans, audio->chans, sizeof(audio->pre_frame_chans));
    audio->pre_frame_div_apu_step = audio->div_apu_step;
    audio->pre_frame_skip_apu_tick = audio->skip_next_apu_tick;
    audio->pre_frame_valid = true;
}

void audio_reset_replay_state(audio_data* audio)
{
    s_apu_event_count = 0;
    s_ch3_cursor_valid = false;
    s_apu_tick_left = 0;
    s_apu_tick_rem_accum = 0;
    s_apu_resume_event = -1;
    s_apu_resume_offset = 0;
    s_apu_replay_overflow = false;
    s_apu_pending_frames = 0;
    s_wave_render_src = NULL;
    memcpy(
        s_replay_wave, audio_mem(audio) + (0xFF30 - AUDIO_ADDR_COMPENSATION), sizeof(s_replay_wave)
    );
    audio->pre_frame_valid = false;
}

// completely disables audio subsystem, important for multithreading reasons
int audio_enabled;

// cpu-visible audio state still updated, but most of it is skipped
int audio_muted;

// see minigb_apu.h
volatile int audio_inflight = 0;

#if TARGET_PLAYDATE
__attribute__((always_inline)) static inline void replicate_samples_interpolated(
    int16_t* left_ptr, int16_t* right_ptr, int chunksize, int sample_replication, bool is_stereo
)
{
    if (sample_replication <= 1)
        return;

    for (int i = 0; i < chunksize; i += sample_replication)
    {
        int16_t a_l = left_ptr[i];
        int16_t b_l = (i + sample_replication < chunksize) ? left_ptr[i + sample_replication] : a_l;
        int step_l = ((int)b_l - (int)a_l) / sample_replication;

        int step_r = step_l;
        int16_t a_r = a_l;
        if (is_stereo)
        {
            a_r = right_ptr[i];
            int16_t b_r =
                (i + sample_replication < chunksize) ? right_ptr[i + sample_replication] : a_r;
            step_r = ((int)b_r - (int)a_r) / sample_replication;
        }

        int16_t val_l = a_l;
        int16_t val_r = a_r;
        for (int k = 1; k < sample_replication && (i + k) < chunksize; k++)
        {
            val_l += step_l;
            left_ptr[i + k] = val_l;
            if (is_stereo)
            {
                val_r += step_r;
                right_ptr[i + k] = val_r;
            }
        }
    }
}

__attribute__((always_inline)) static inline void high_pass_filter_fixed_asm(
    int16_t* left, int16_t* right, int len, audio_data* audio, int16_t charge_factor_q15
)
{
    int32_t cap_l = audio->capacitor_l;
    int32_t cap_r = audio->capacitor_r;

    for (int i = 0; i < len; i++)
    {
        uint32_t in_lr, out_lr;
        int32_t next_cap_l, next_cap_r;

        if (left == right)
        {
            uint16_t sample = left[i];
            in_lr = ((uint32_t)sample) | ((uint32_t)sample << 16);
        }
        else
        {
            in_lr = (uint32_t)((uint16_t)left[i]) | ((uint32_t)right[i] << 16);
        }

        asm volatile(
            "asr r0, %[cap_l_in], #15\n\t"
            "asr r1, %[cap_r_in], #15\n\t"
            "pkhbt r0, r0, r1, lsl #16\n\t"

            "qsub16 %[out_lr_out], %[in_lr_in], r0\n\t"

            "smulbb r1, %[out_lr_out], %[charge_in]\n\t"
            "lsl r0, %[in_l_in], #15\n\t"
            "sub %[cap_l_out], r0, r1\n\t"

            "smultb r1, %[out_lr_out], %[charge_in]\n\t"
            "lsl r0, %[in_r_in], #15\n\t"
            "sub %[cap_r_out], r0, r1\n\t"

            :
            [out_lr_out] "=&r"(out_lr), [cap_l_out] "=&r"(next_cap_l), [cap_r_out] "=&r"(next_cap_r)
            : [in_lr_in] "r"(in_lr), [in_l_in] "r"((int16_t)in_lr),
              [in_r_in] "r"((int16_t)(in_lr >> 16)), [cap_l_in] "r"(cap_l), [cap_r_in] "r"(cap_r),
              [charge_in] "r"(charge_factor_q15)
            : "r0", "r1", "cc"
        );

        cap_l = next_cap_l;
        cap_r = next_cap_r;
        left[i] = (int16_t)out_lr;
        if (left != right)
        {
            right[i] = (int16_t)(out_lr >> 16);
        }
    }
    audio->capacitor_l = cap_l;
    audio->capacitor_r = cap_r;
}

/* Optimized memset for audio buffers: 8 samples per iteration via STM. */
__attribute__((always_inline)) static inline void audio_buffer_clear_optimized(
    int16_t* buf, int len
)
{
    int batch_count = len / 8;
    int remaining = len % 8;

    asm volatile(
        "mov r0, #0 \n\t"
        "mov r1, #0 \n\t"
        "mov r2, #0 \n\t"
        "mov r3, #0 \n\t"
        "1: \n\t"
        "cmp %[count], #0 \n\t"
        "beq 2f \n\t"
        "stmia %[buf]!, {r0-r3} \n\t"
        "subs %[count], %[count], #1 \n\t"
        "bne 1b \n\t"
        "2: \n\t"
        : [buf] "+r"(buf), [count] "+r"(batch_count)
        :
        : "r0", "r1", "r2", "r3", "memory", "cc"
    );

    // Clear remaining samples
    for (int i = 0; i < remaining; i++)
    {
        buf[i] = 0;
    }
}
#endif

/* Clear both audio buffers (mono or stereo). */
#if TARGET_PLAYDATE
__attribute__((always_inline)) static inline void clear_audio_buffers(
    int16_t* left, int16_t* right, int len
)
{
    audio_buffer_clear_optimized(left, len);
    if (left != right)
    {
        audio_buffer_clear_optimized(right, len);
    }
}
#else
__attribute__((always_inline)) static inline void clear_audio_buffers(
    int16_t* left, int16_t* right, int len
)
{
    memset(left, 0, len * sizeof(int16_t));
    if (left != right)
    {
        memset(right, 0, len * sizeof(int16_t));
    }
}
#endif

/* Engine body in TCM; entered only while a scene is active (wipe-safe). */
__apu_sample_gen static int audio_callback_render(
    CB_GameScene* gameScene, int16_t* left, int16_t* right, int len
)
{
    DTCM_VERIFY_DEBUG();

    audio_data* audio = &gameScene->context->gb->audio;

    if (gameScene->audioLocked)
    {
        clear_audio_buffers(left, right, len);
#if TARGET_PLAYDATE
        audio->capacitor_l = 0;
        audio->capacitor_r = 0;
#else
        audio->capacitor_l = 0.0f;
        audio->capacitor_r = 0.0f;
#endif
        return 1;
    }

#ifdef TARGET_SIMULATOR
    pthread_mutex_lock(&audio_mutex);
#endif

    if (preferences_sound_mode == 2)
    {
        uint32_t read_pos = atomic_load(&g_audio_sync_buffer.read_pos);
        uint32_t write_pos = atomic_load(&g_audio_sync_buffer.write_pos);
        uint32_t samples_available = write_pos - read_pos;

        if (samples_available < len)
        {
            /* Underrun: do NOT advance read_pos past unwritten samples.
             * Request a main-thread rebaseline (underrun silence counts as
             * played time and would wedge the lead accounting). Rate-limit
             * the log: console spam makes the underrun worse. */
            static unsigned s_underrun_count = 0;
            s_underrun_count++;
            if (s_underrun_count == 1 || s_underrun_count % 60 == 0)
            {
                playdate->system->logToConsole(
                    "AUDIO UNDERRUN #%u! available: %d, needed: %d", s_underrun_count,
                    samples_available, len
                );
            }
            g_audio_resync_requested = 1;
            clear_audio_buffers(left, right, len);
        }
        else
        {
            for (int i = 0; i < len; ++i)
            {
                uint32_t current_pos = (read_pos + i) & AUDIO_RING_BUFFER_MASK;
                left[i] = g_audio_sync_buffer.left[current_pos];
                if (left != right)
                {
                    right[i] = g_audio_sync_buffer.right[current_pos];
                }
            }

            atomic_store(&g_audio_sync_buffer.read_pos, read_pos + len);
        }
    }
    else
    {
        __builtin_prefetch(left, 1);
        int sample_replication = get_sample_replication();
        int max_chunk = ((256 + sample_replication - 1) / sample_replication) * sample_replication;

        int16_t* left_ptr = left;
        int16_t* right_ptr = gameScene->is_stereo ? right : left;
        int remaining_len = len;

        while (remaining_len > 0)
        {
            int chunksize = remaining_len >= max_chunk ? max_chunk : remaining_len;

#if TARGET_PLAYDATE
            audio_buffer_clear_optimized(left_ptr, chunksize);
            if (gameScene->is_stereo)
                audio_buffer_clear_optimized(right_ptr, chunksize);
#else
            memset(left_ptr, 0, chunksize * sizeof(int16_t));
            if (gameScene->is_stereo)
                memset(right_ptr, 0, chunksize * sizeof(int16_t));
#endif

            update_wave(audio, left_ptr, right_ptr, chunksize, NULL);
            update_square(audio, left_ptr, right_ptr, 0, chunksize, NULL);
            update_square(audio, left_ptr, right_ptr, 1, chunksize, NULL);
            update_noise(audio, left_ptr, right_ptr, chunksize);

#if TARGET_PLAYDATE
            replicate_samples_interpolated(
                left_ptr, right_ptr, chunksize, sample_replication, gameScene->is_stereo
            );
#else

            if (sample_replication > 1)
            {
                for (int i = 0; i < chunksize; i += sample_replication)
                {
                    int16_t a_l = left_ptr[i];
                    int16_t b_l = (i + sample_replication < chunksize)
                                      ? left_ptr[i + sample_replication]
                                      : a_l;
                    int step_l = ((int)b_l - (int)a_l) / sample_replication;
                    int step_r = step_l;
                    int16_t a_r = a_l;
                    if (gameScene->is_stereo)
                    {
                        a_r = right_ptr[i];
                        int16_t b_r = (i + sample_replication < chunksize)
                                          ? right_ptr[i + sample_replication]
                                          : a_r;
                        step_r = ((int)b_r - (int)a_r) / sample_replication;
                    }
                    int16_t val_l = a_l;
                    int16_t val_r = a_r;
                    for (int j = 1; j < sample_replication && (i + j) < chunksize; ++j)
                    {
                        val_l += step_l;
                        left_ptr[i + j] = val_l;
                        if (gameScene->is_stereo)
                        {
                            val_r += step_r;
                            right_ptr[i + j] = val_r;
                        }
                    }
                }
            }
#endif
            remaining_len -= chunksize;
            left_ptr += chunksize;
            if (gameScene->is_stereo)
                right_ptr += chunksize;
        }
    }

    // --- High-Pass Filter ---
    if (preferences_high_pass_filter)
    {
        bool dacs_enabled = audio->chans[0].powered || audio->chans[1].powered ||
                            audio->chans[2].powered || audio->chans[3].powered;

        if (dacs_enabled)
        {
#if TARGET_PLAYDATE
            int cgb_idx = gameScene->context->gb->is_cgb_mode;
            int16_t charge_factor = get_charge_factors_q15[cgb_idx][get_effective_sample_rate()];
            high_pass_filter_fixed_asm(left, right, len, audio, charge_factor);
#else
            int cgb_idx = gameScene->context->gb->is_cgb_mode;
            float charge_factor = get_charge_factors[cgb_idx][get_effective_sample_rate()];
            for (int i = 0; i < len; i++)
            {
                float in_l = left[i];
                float out_l = in_l - audio->capacitor_l;
                audio->capacitor_l = in_l - out_l * charge_factor;
                left[i] = clamp16(out_l);

                if (left != right)
                {
                    float in_r = right[i];
                    float out_r = in_r - audio->capacitor_r;
                    audio->capacitor_r = in_r - out_r * charge_factor;
                    right[i] = clamp16(out_r);
                }
            }
#endif
        }
        /* All DACs off: capacitor keeps its charge. */
    }
    else
    {
        /* Filter disabled by preference: keep capacitors discharged so
         * re-enabling the filter doesn't cause a click. */
#if TARGET_PLAYDATE
        audio->capacitor_l = 0;
        audio->capacitor_r = 0;
#else
        audio->capacitor_l = 0.0f;
        audio->capacitor_r = 0.0f;
#endif
    }

#ifdef TARGET_SIMULATOR
    pthread_mutex_unlock(&audio_mutex);
#endif

    DTCM_VERIFY_DEBUG();

    return 1;
}
/* Playdate audio callback. */
int audio_callback(void* context, int16_t* left, int16_t* right, int len)
{
    if (!audio_enabled || audio_muted)
        return 0;

    CB_GameScene* gameScene = *(CB_GameScene**)context;
    if (!gameScene)
    {
        clear_audio_buffers(left, right, len);
        return 1;
    }

    /* Increment first, then re-check the scene pointer: the wipe side sets
     * it NULL, then waits for audio_inflight == 0 (see minigb_apu.h). */
    __sync_add_and_fetch(&audio_inflight, 1);
    gameScene = *(CB_GameScene**)context;

    int result;
    if (!gameScene)
    {
        clear_audio_buffers(left, right, len);
        result = 1;
    }
    else
    {
        result = APU_GEN_CALL_CB(audio_callback_render, gameScene, left, right, len);
    }

    __sync_sub_and_fetch(&audio_inflight, 1);
    return result;
}

void audio_update_square(
    audio_data* restrict audio, int16_t* left, int16_t* right, const bool ch2, int len
)
{
    update_square(audio, left, right, ch2, len, NULL);
}

void audio_update_wave(audio_data* restrict audio, int16_t* left, int16_t* right, int len)
{
    update_wave(audio, left, right, len, NULL);
}

void audio_update_noise(audio_data* restrict audio, int16_t* left, int16_t* right, int len)
{
    update_noise(audio, left, right, len);
}

/* Advance the persistent 512 Hz DIV-APU schedule by rendered samples. Ticks
 * must fire at 512 Hz of output progress regardless of segmentation:
 * event-dense frames (PDM cries) cut segments to 1-2 samples, and
 * chunk-relative ticking starved the sequencer (hanging notes). Phase
 * persists across spans and calls — a per-call restart slows it to 478 Hz. */
__shell static void apu_div_tick_sched_advance(audio_data* audio, int samples, int sample_rate)
{
    if (s_apu_tick_left == 0)
        s_apu_tick_left = (uint16_t)(sample_rate / 512);  // first tick one interval in
    while (samples >= s_apu_tick_left)
    {
        samples -= s_apu_tick_left;
        audio_div_apu_tick(audio);
        s_apu_tick_rem_accum += (uint16_t)(sample_rate % 512);
        s_apu_tick_left = (uint16_t)(sample_rate / 512);
        if (s_apu_tick_rem_accum >= 512)
        {
            s_apu_tick_rem_accum -= 512;
            s_apu_tick_left++;
        }
    }
    s_apu_tick_left -= (uint16_t)samples;
}

__shell bool audio_take_replay_overflow(void)
{
    bool v = s_apu_replay_overflow;
    s_apu_replay_overflow = false;
    return v;
}

__shell int audio_replay_pending_frames(void)
{
    return s_apu_pending_frames;
}

/* Record an emulated frame's end as a sentinel event. Called by the CPU
 * core at the end of gb_run_frame with the frame's final apu_count. */
__shell void audio_note_frame_end(audio_data* audio, uint32_t apu_count)
{
    /* No recording under turbo (uncap fps): see audio_write. */
    if (audio->pre_frame_valid && preferences_sound_mode == 2 && preferences_uncap_fps == 0)
    {
        if (s_apu_event_count < APU_EVENT_CAP)
        {
            s_apu_events[s_apu_event_count].apu_count = apu_count;
            s_apu_events[s_apu_event_count].addr = APU_FRAME_END_ADDR;
            s_apu_events[s_apu_event_count].val = 0;
            s_apu_events[s_apu_event_count].div_step = 0;
            s_apu_event_count++;
            s_apu_pending_frames++;
        }
        else
        {
            s_apu_replay_overflow = true;
        }
    }
}

/* Render one emulated frame's event span [span_start, span_end) plus the
 * tail after its last event, emitting up to frame_samples samples, covering
 * frame positions [start_offset, start_offset + frame_samples).
 * Stops before the first event beyond the budget and returns its index
 * (span_end if the whole span was consumed); the caller holds the remainder
 * for the next call via s_apu_resume_event / s_apu_resume_offset. */
__shell static int replay_event_span(
    audio_data* restrict audio, int16_t* left, int16_t* right, int span_start, int span_end,
    int frame_samples, int sample_rate, int start_offset
)
{
    int samples_per_tick = sample_rate / 512;
    int samples_per_tick_rem = sample_rate % 512;
    int tick_accum = 0;
    int offset = 0;

    const uint32_t start_q16 = (uint32_t)start_offset << 16;

    /* Fractional CH3 gain pre-pass: sweep NR32 (CH3 volume) writes into
     * per-sample weighted gains (Q16 sub-sample positions) instead of
     * cutting render segments at integer samples. */
    const uint16_t* ch3_gain = NULL;
    if (frame_samples > 0 && frame_samples <= APU_GAIN_CAP)
    {
        bool has_nr32 = false;
        for (int i = span_start; i < span_end; i++)
            if (s_apu_events[i].addr == 0xFF1C)
            {
                has_nr32 = true;
                break;
            }
        if (has_nr32)
        {
            const uint32_t span_end_q16 = (uint32_t)frame_samples << 16;
            uint32_t cursor_q16 = 0;
            uint16_t cur = ch3_gain_q8(audio->chans[2].volume);

            memset(s_ch3_gain_q8, 0, (size_t)frame_samples * sizeof(uint16_t));
            for (int i = span_start; i < span_end; i++)
            {
                if (s_apu_events[i].addr != 0xFF1C)
                    continue;
                uint32_t pos_q16 = (uint32_t)(((uint64_t)s_apu_events[i].apu_count *
                                               (uint64_t)sample_rate * 65536u) /
                                              DMG_CLOCK_FREQ_U);
                if (pos_q16 < start_q16)
                    continue;  // already applied in a prior partial call
                pos_q16 -= start_q16;
                if (pos_q16 > span_end_q16)
                    break;  // beyond budget: defer to next call
                if (pos_q16 < cursor_q16)
                    pos_q16 = cursor_q16;
                apu_gain_fill(s_ch3_gain_q8, cursor_q16, pos_q16, cur, frame_samples);
                cursor_q16 = pos_q16;
                cur = ch3_gain_q8((unsigned)(s_apu_events[i].val >> 5) & 3);
            }
            apu_gain_fill(s_ch3_gain_q8, cursor_q16, span_end_q16, cur, frame_samples);
            ch3_gain = s_ch3_gain_q8;
        }
    }

    /* Fractional CH1/CH2 gain pre-pass (16 kHz NRx2 PCM voice): volume
     * timeline reconstructed on a shadow chan via apply_nrx2 (+ trigger
     * reload), identical to the live write path. Requires envelope off for
     * the whole span, else the envelope owns volume: fall back to snapping. */
    const uint16_t* sq_gain[2] = {NULL, NULL};
    if (frame_samples > 0 && frame_samples <= APU_GAIN_CAP)
    {
        gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
        const uint32_t span_end_q16 = (uint32_t)frame_samples << 16;

        for (int ch = 0; ch < 2; ch++)
        {
            const uint16_t nrx2 = (uint16_t)(0xFF12 + ch * 5);
            const uint16_t nrx4 = (uint16_t)(0xFF14 + ch * 5);

            bool has_nrx2 = false;
            for (int i = span_start; i < span_end; i++)
                if (s_apu_events[i].addr == nrx2)
                {
                    has_nrx2 = true;
                    break;
                }
            if (!has_nrx2 || audio->chans[ch].env.step != 0)
                continue;

            chan shadow;
            memcpy(&shadow, &audio->chans[ch], sizeof(chan));

            uint32_t cursor_q16 = 0;
            unsigned rec_vol = (unsigned)shadow.volume;
            uint8_t last_nrx2 = audio_mem(audio)[nrx2 - AUDIO_ADDR_COMPENSATION];
            bool envelope_set = false;

            memset(s_sq_gain_q4[ch], 0, (size_t)frame_samples * sizeof(uint16_t));
            for (int i = span_start; i < span_end && !envelope_set; i++)
            {
                uint16_t a = s_apu_events[i].addr;
                if (a != nrx2 && a != nrx4)
                    continue;
                if (a == nrx2 && (s_apu_events[i].val & 7) != 0)
                {
                    // Envelope period set: envelope owns volume; bail to snapping.
                    envelope_set = true;
                    break;
                }

                uint32_t pos_q16 = (uint32_t)(((uint64_t)s_apu_events[i].apu_count *
                                               (uint64_t)sample_rate * 65536u) /
                                              DMG_CLOCK_FREQ_U);
                if (pos_q16 < start_q16)
                    continue;  // already applied in a prior partial call
                pos_q16 -= start_q16;
                if (pos_q16 > span_end_q16)
                    break;  // beyond budget: defer to next call
                if (pos_q16 < cursor_q16)
                    pos_q16 = cursor_q16;

                if (a == nrx2)
                {
                    apply_nrx2(&shadow, 0, s_apu_events[i].val, last_nrx2, gb->is_cgb_mode);
                    last_nrx2 = s_apu_events[i].val;
                }
                else if (s_apu_events[i].val & 0x80)
                {
                    shadow.volume = shadow.volume_init;  // trigger reload
                }

                if ((unsigned)shadow.volume != rec_vol)
                {
                    apu_gain_fill(
                        s_sq_gain_q4[ch], cursor_q16, pos_q16, (uint16_t)(rec_vol * 16),
                        frame_samples
                    );
                    cursor_q16 = pos_q16;
                    rec_vol = (unsigned)shadow.volume;
                }
            }
            if (envelope_set)
                continue;

            apu_gain_fill(
                s_sq_gain_q4[ch], cursor_q16, span_end_q16, (uint16_t)(rec_vol * 16), frame_samples
            );
            sq_gain[ch] = s_sq_gain_q4[ch];
        }
    }

    int resume_idx = span_end;  // first un-applied event, or span_end if all consumed
    for (int i = span_start; i < span_end; i++)
    {
        // Absolute sample position: avoids cumulative floor drift.
        int seg = (int)((uint64_t)s_apu_events[i].apu_count * sample_rate / DMG_CLOCK_FREQ_U) -
                  start_offset - offset;
        if (seg > frame_samples - offset)
        {
            // Event beyond this call's budget: defer it (and the rest).
            resume_idx = i;
            break;
        }
        if (seg < 0)
            seg = 0;

        // Gain-carried volume changes need no segment cut.
        if ((ch3_gain && s_apu_events[i].addr == 0xFF1C) ||
            (sq_gain[0] && s_apu_events[i].addr == 0xFF12) ||
            (sq_gain[1] && s_apu_events[i].addr == 0xFF17))
            seg = 0;

        if (seg > 0)
        {
            int generated = 0;
            while (generated < seg)
            {
                int chunk = samples_per_tick;
                tick_accum += samples_per_tick_rem;
                if (tick_accum >= 512)
                {
                    tick_accum -= 512;
                    chunk++;
                }
                if (generated + chunk > seg)
                    chunk = seg - generated;

                APU_GEN_WV(
                    update_wave, audio, left + offset + generated, right + offset + generated,
                    chunk, ch3_gain ? ch3_gain + offset + generated : NULL
                );
                APU_GEN_SQ(
                    update_square, audio, left + offset + generated, right + offset + generated, 0,
                    chunk, sq_gain[0] ? sq_gain[0] + offset + generated : NULL
                );
                APU_GEN_SQ(
                    update_square, audio, left + offset + generated, right + offset + generated, 1,
                    chunk, sq_gain[1] ? sq_gain[1] + offset + generated : NULL
                );
                APU_GEN_NZ(
                    update_noise, audio, left + offset + generated, right + offset + generated,
                    chunk
                );

                generated += chunk;

                apu_div_tick_sched_advance(audio, chunk, sample_rate);
            }
            offset += seg;
        }

        if (s_ch3_cursor_valid)
            ch3_cursor_advance(audio, s_apu_events[i].apu_count);

        audio_write(audio, s_apu_events[i].addr, s_apu_events[i].val, s_apu_events[i].apu_count);

        // NR52 power-on: restore the phase captured at emulation time (audio_write
        // re-derived it from post-frame DIV).
        if (s_apu_events[i].addr == 0xFF26 && (s_apu_events[i].val & 0x80) &&
            (s_apu_events[i].div_step & APU_EVENT_PHASE_VALID))
        {
            audio->div_apu_step = s_apu_events[i].div_step & APU_EVENT_PHASE_MASK;
            audio->skip_next_apu_tick = (s_apu_events[i].div_step & APU_EVENT_PHASE_SKIP) != 0;
        }

        // Re-anchor cursor on CH3-affecting writes (NR30/NR33/NR34/NR52).
        if (s_ch3_cursor_valid)
        {
            uint16_t a = s_apu_events[i].addr;
            if (a == 0xFF1A || a == 0xFF1D || a == 0xFF1E || a == 0xFF26)
                ch3_cursor_reanchor(audio, s_apu_events[i].apu_count);
        }
    }

    int rem = frame_samples - offset;
    if (rem > 0)
    {
        int generated = 0;
        while (generated < rem)
        {
            int chunk = samples_per_tick;
            tick_accum += samples_per_tick_rem;
            if (tick_accum >= 512)
            {
                tick_accum -= 512;
                chunk++;
            }
            if (generated + chunk > rem)
                chunk = rem - generated;

            APU_GEN_WV(
                update_wave, audio, left + offset + generated, right + offset + generated, chunk,
                ch3_gain ? ch3_gain + offset + generated : NULL
            );
            APU_GEN_SQ(
                update_square, audio, left + offset + generated, right + offset + generated, 0,
                chunk, sq_gain[0] ? sq_gain[0] + offset + generated : NULL
            );
            APU_GEN_SQ(
                update_square, audio, left + offset + generated, right + offset + generated, 1,
                chunk, sq_gain[1] ? sq_gain[1] + offset + generated : NULL
            );
            APU_GEN_NZ(
                update_noise, audio, left + offset + generated, right + offset + generated, chunk
            );

            generated += chunk;

            apu_div_tick_sched_advance(audio, chunk, sample_rate);
        }
        offset += rem;
    }

    // Re-sync the smoothing ramp for the next non-gain span.
    for (int ch = 0; ch < 2; ch++)
        if (sq_gain[ch])
            audio->chans[ch].envelope_smooth = (int32_t)audio->chans[ch].volume << 8;

    return resume_idx;
}

__shell void audio_generate_accurate(
    audio_data* restrict audio, int16_t* left, int16_t* right, int len
)
{
    if (s_apu_event_count)
    {
        int sample_rate = get_audio_sample_rate();

        if (audio->pre_frame_valid)
        {
            memcpy(audio->chans, audio->pre_frame_chans, sizeof(audio->chans));
            audio->div_apu_step = audio->pre_frame_div_apu_step;
            audio->skip_next_apu_tick = audio->pre_frame_skip_apu_tick;
        }

        audio->pre_frame_valid = false;

        /* Split the batch at frame boundaries: the CPU core appends a sentinel
         * (APU_FRAME_END_ADDR) at every frame's end. Multi-frame batches occur
         * on skipped generation ticks or frame-skip; without splitting, later
         * frames' events clamp to one sample point (audible flutter). Sentinels
         * also mark write-less frames, undetectable by apu_count decrease. */
        int span_start = (s_apu_resume_event >= 0) ? s_apu_resume_event : 0;
        int start_offset = (s_apu_resume_event >= 0) ? s_apu_resume_offset : 0;
        int offset = 0;

        /* Render CH3 (and apply wave writes) against the replay-domain wave
         * image for the whole replay block. */
        s_wave_render_src = s_replay_wave;
        while (span_start < s_apu_event_count)
        {
            // Span = run of write events up to the next sentinel or batch end.
            int span_end = span_start;
            while (span_end < s_apu_event_count &&
                   s_apu_events[span_end].addr != APU_FRAME_END_ADDR)
                span_end++;

            const bool frame_complete = span_end < s_apu_event_count;

            uint32_t end_count = 0;
            for (int i = span_start; i < span_end; i++)
                if (s_apu_events[i].apu_count > end_count)
                    end_count = s_apu_events[i].apu_count;

            int full_frame_samples;
            if (frame_complete)
            {
                // Full frame: render to the sentinel's apu_count, not the
                // last write — audio continues past the final event.
                full_frame_samples = (int)((uint64_t)s_apu_events[span_end].apu_count *
                                           sample_rate / DMG_CLOCK_FREQ_U);
            }
            else
            {
                full_frame_samples = (int)((uint64_t)end_count * sample_rate / DMG_CLOCK_FREQ_U);
            }

            // Remaining samples of this span, clamped to this call's budget.
            int frame_samples = full_frame_samples - start_offset;
            if (frame_samples > len - offset)
                frame_samples = len - offset;
            if (frame_samples < 0)
                frame_samples = 0;

            if (frame_samples <= 0)
            {
                // No budget left for this span: hold it for the next call.
                s_apu_resume_event = span_start;
                s_apu_resume_offset = start_offset;
                break;
            }

            // Anchor cursor for fresh spans only; resume keeps the live cursor.
            if (start_offset == 0)
                ch3_cursor_reset(audio, s_apu_events[span_start].apu_count);

            int resume = replay_event_span(
                audio, left + offset, right + offset, span_start, span_end, frame_samples,
                sample_rate, start_offset
            );
            offset += frame_samples;

            if (resume < span_end)
            {
                // Partial span: hold the un-applied events for the next call.
                s_apu_resume_event = resume;
                s_apu_resume_offset = start_offset + frame_samples;
                break;
            }

            if (frame_samples < full_frame_samples - start_offset)
            {
                if (frame_complete)
                {
                    /* Tail truncated (all events fit, trailing samples
                     * didn't): resume at the sentinel as an empty span so
                     * the tail renders as live samples. Dropping it would
                     * advance CH3 position and the 512 Hz schedule by fewer
                     * samples on small chunks than large ones, drifting the
                     * wave position against the absolute event timeline. */
                    s_apu_resume_event = span_end;
                    s_apu_resume_offset = start_offset + frame_samples;
                }
                else
                {
                    /* Incomplete span (batch end without sentinel => event
                     * cap overflow). Never resume: the next events start a
                     * new frame at apu_count 0, and the stale start_offset
                     * would skip their gain-timeline entries. Drop the tail;
                     * overflow handling drops the batch but keeps the ring
                     * (its queued samples are already valid). */
                    s_apu_resume_event = -1;
                    s_apu_resume_offset = 0;
                }
                break;
            }

            // Span fully consumed; any following span starts fresh.
            s_apu_resume_event = -1;
            s_apu_resume_offset = 0;
            if (frame_complete)
                s_apu_pending_frames--;
            start_offset = 0;
            span_start = frame_complete ? span_end + 1 : span_end;
        }

        s_wave_render_src = NULL;

        left += offset;
        right += offset;
        len -= offset;

        if (s_apu_resume_event < 0)
        {
            /* All spans consumed: drop the batch, reset the cursor, and
             * re-sync the wave image (live and replay positions coincide). */
            s_apu_event_count = 0;
            s_ch3_cursor_valid = false;
            memcpy(
                s_replay_wave, audio_mem(audio) + (0xFF30 - AUDIO_ADDR_COMPENSATION),
                sizeof(s_replay_wave)
            );
        }
        else if (s_apu_resume_event > 0)
        {
            /* Partial consumption: purge the consumed prefix so the array
             * tracks pending frames instead of growing to the cap. The
             * resume offset counts samples within the now-first span, so it
             * carries over unchanged. */
            s_apu_event_count -= (uint16_t)s_apu_resume_event;
            memmove(
                s_apu_events, s_apu_events + s_apu_resume_event,
                (size_t)s_apu_event_count * sizeof(s_apu_events[0])
            );
            s_apu_resume_event = 0;
        }
    }

    int sample_rate = get_audio_sample_rate();
    int samples_per_tick = sample_rate / 512;
    int samples_per_tick_rem = sample_rate % 512;
    int tick_accum = 0;
    int generated = 0;

    while (generated < len)
    {
        int chunk = samples_per_tick;
        tick_accum += samples_per_tick_rem;
        if (tick_accum >= 512)
        {
            tick_accum -= 512;
            chunk += 1;
        }
        if (generated + chunk > len)
            chunk = len - generated;

        APU_GEN_WV(update_wave, audio, left + generated, right + generated, chunk, NULL);
        APU_GEN_SQ(update_square, audio, left + generated, right + generated, 0, chunk, NULL);
        APU_GEN_SQ(update_square, audio, left + generated, right + generated, 1, chunk, NULL);
        APU_GEN_NZ(update_noise, audio, left + generated, right + generated, chunk);

        generated += chunk;

        apu_div_tick_sched_advance(audio, chunk, sample_rate);
    }

    memcpy(audio->pre_frame_chans, audio->chans, sizeof(audio->pre_frame_chans));
    audio->pre_frame_div_apu_step = audio->div_apu_step;
    audio->pre_frame_skip_apu_tick = audio->skip_next_apu_tick;
    audio->pre_frame_valid = true;
}
