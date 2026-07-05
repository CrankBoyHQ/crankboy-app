/**
 * minigb_apu is released under the terms listed within the LICENSE file.
 *
 * minigb_apu emulates the audio processing unit (APU) of the Game Boy. This
 * project is based on MiniGBS by Alex Baines: https://github.com/baines/MiniGBS
 */

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

/* Handles time keeping for sound generation.
 * This is a fixed reference to ensure timing calculations are consistent
 * regardless of the output sample rate. */
#define FREQ_INC_REF 44100

#define MAX_CHAN_VOLUME 15

#ifdef TARGET_SIMULATOR
#define __audio
#else
#define __audio \
    __attribute__((optimize("O3"))) __attribute__((section(".audio"))) __attribute__((short_call))
#endif

#if TARGET_PLAYDATE
/* Q1.15 fixed-point equivalents of the factors below.
 * Formula: (int16_t)((base ^ (DMG_CLOCK_FREQ / sample_rate)) * 32768)
 *   DMG  base: 0.999958  -> 32637, 32507, 32378
 *   CGB  base: 0.998943  -> 29632, 26797, 24233
 */
static const int16_t get_charge_factors_q15[2][3] = {{32637, 32507, 32378}, {29632, 26797, 24233}};
#else

/* The factors are calculated using the formula:
 * base^(DMG_CLOCK_FREQ / sample_rate)
 * DMG base: 0.999958, CGB base: 0.998943
 */
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

static inline int get_sample_replication(void)
{
    // preferences_sample_rate: 0 -> 1 (44.1kHz), 1 -> 2 (22.05kHz)
    return preferences_sample_rate + 1;
}

static inline int get_audio_sample_rate(void)
{
    return FREQ_INC_REF / get_sample_replication();
}

/**
 * Memory holding audio registers between 0xFF10 and 0xFF3F inclusive.
 */
static uint32_t precomputed_noise_freqs[8][16];

__audio static void set_note_freq(chan* c, const uint32_t freq)
{
    /* Lowest expected value of freq is 64. */
    // Set frequency increment
    c->freq_inc = freq;
}

static void chan_enable(audio_data* audio, const uint_fast8_t i, const bool enable)
{
    uint8_t val;
    chan* chans = audio->chans;
    chans[i].enabled = enable;
    val = (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] & 0x80) | (chans[3].enabled << 3) |
          (chans[2].enabled << 2) | (chans[1].enabled << 1) | (chans[0].enabled << 0);

    audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] = val;
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

    if (preferences_sound_mode != 2)
        return;

    chan* chans = audio->chans;

    /* Length timer: 256 Hz (steps 0, 2, 4, 6). */
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

    /* Sweep: 128 Hz (steps 2, 6). Channel 1 only.
     * Timer runs when enabled flag is set (pace != 0 || shift != 0).
     * Period 0 is treated as 8 per hardware. */
    if (step == 2 || step == 6)
    {
        chan* c = chans + 0;
        if (c->enabled && (c->sweep.rate != 0 || c->sweep.shift != 0))
        {
            c->sweep_divider--;
            if (c->sweep_divider == 0)
            {
                uint8_t sweep_period = c->sweep.rate ? c->sweep.rate : 8;
                c->sweep_divider = sweep_period;

                if (c->sweep.rate != 0 && c->sweep.shift > 0)
                {
                    uint16_t new_freq = c->sweep.freq >> c->sweep.shift;
                    if (!c->sweep_up)
                    {
                        new_freq = c->sweep.freq - new_freq + 1;
                        c->sweep.did_subtract = true;
                    }
                    else
                        new_freq = c->sweep.freq + new_freq;

                    if (new_freq > 2047)
                    {
                        c->enabled = 0;
                        goto sweep_done;
                    }

                    c->freq = new_freq;
                    c->sweep.freq = new_freq;

                    /* Pandocs: double calculation for overflow check. */
                    uint16_t second = c->sweep.freq >> c->sweep.shift;
                    if (!c->sweep_up)
                    {
                        second = c->sweep.freq - second;
                        c->sweep.did_subtract = true;
                    }
                    else
                        second = c->sweep.freq + second;

                    if (second > 2047)
                        c->enabled = 0;
                }
            }
        }
    sweep_done:;
    }

    // Process pending envelope ticks from previous step-7 events.
    // Hardware delays volume change by ~1/2 DIV-APU cycle via
    // the secondary event (rising edge) after countdown hits 0.
    {
        for (int i = 0; i < 4; i++)
        {
            if (i == 2)
                continue;
            chan* c = chans + i;
            if (!c->enabled || !c->env_pending)
                continue;
            c->env_pending = false;

            if (c->env.locked)
                continue;

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
    // set pending (volume changes on next tick).
    if (step == 7)
    {
        for (int i = 0; i < 4; i++)
        {
            if (i == 2)
                continue;
            chan* c = chans + i;
            if (!c->enabled || c->env.step == 0 || c->env.locked)
                continue;

            c->env_divider--;
            if (c->env_divider == 0)
            {
                c->env_divider = c->env.step;
                c->env_pending = true;
            }
        }
    }
}

/* No-op: frame sequencer ticks are processed on the CPU thread
 * via audio_div_apu_tick(). Audio thread only reads channel state. */
static void flush_apu_ticks(audio_data* audio)
{
    (void)audio;
}

__audio static void update_env(chan* c, int sample_rate)
{
    if (preferences_sound_mode == 2)
        return;

    c->env.counter += c->env.inc;

    while (c->env.counter > sample_rate)
    {
        if (c->env.step)
        {
            c->volume += c->env.up ? 1 : -1;
            if (c->volume == 0 || c->volume == MAX_CHAN_VOLUME)
            {
                c->env.inc = 0;
                c->env.locked = true;
            }
#if TARGET_PLAYDATE
            int volume = c->volume;
            asm volatile("usat %0, #4, %0" : "+r"(volume));
            c->volume = volume;
#else
            c->volume = MAX(0, MIN(MAX_CHAN_VOLUME, c->volume));
#endif
        }
        c->env.counter -= sample_rate;
    }
}

// SameBoy _nrx2_glitch: zombie volume adjustment on NRx2 write while channel active.
// Handles direction-change inversion and old-step=0->new-step!=0 tick.
__audio static void _nrx2_glitch(chan* chans, int i, uint8_t new_val, uint8_t old_val)
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

__audio static bool calculate_new_sweep_freq(chan* c)
{
    uint16_t new_freq;
    new_freq = c->sweep.freq >> c->sweep.shift;

    if (!c->sweep_up)
    {
        new_freq = c->sweep.freq - new_freq + 1;
        if (c->sweep.shift > 0)
            c->sweep.did_subtract = true;
    }
    else
    {
        new_freq = c->sweep.freq + new_freq;
    }

    if (new_freq > 2047)
    {
        c->enabled = 0;
        return true;
    }

    if (c->sweep.shift > 0)
    {
        c->freq = new_freq;
        c->sweep.freq = new_freq;
    }

    return false;  // Channel still active
}

// returns sample index at which to stop outputting in channel
__audio static int update_len(audio_data* restrict audio, chan* c, int len)
{
    if (!c->enabled)
        return 0;

    if (!c->len_enabled || c->len.inc == 0)
        return len;

    if (preferences_sound_mode == 2)
    {
        int remaining = (int)(c->len.inc >> 16);
        if (remaining == 0)
            return 0;
        int sample_rate = get_audio_sample_rate();
        int max_len = (remaining * sample_rate + 255) / 256;
        if (max_len < len)
            return max_len;
        return len;
    }

    int tick_rate = (int)(c->len.inc & 0xFFFF);
    int ticks_needed = (int)(c->len.inc >> 16);

    if (ticks_needed <= 0)
        return len;

    int sample_rate = get_audio_sample_rate();
    uint32_t counter_before = c->len.counter;
    uint32_t counter_after = counter_before + (uint32_t)len * (uint32_t)tick_rate;

    uint32_t ticks_total = counter_after / (uint32_t)sample_rate;

    if (ticks_total < (uint32_t)ticks_needed)
    {
        c->len.counter = counter_after;
        return len;
    }

    uint32_t target = (uint32_t)ticks_needed * (uint32_t)sample_rate;
    int tr;
    if (counter_before >= target)
    {
        tr = 0;
    }
    else
    {
        uint32_t needed = target - counter_before;
        tr = (int)((needed + (uint32_t)tick_rate - 1) / (uint32_t)tick_rate);
    }

    if (tr > len)
        tr = len;

    c->len.counter = 0;
    chan_enable(audio, (int)(c - audio->chans), 0);
    return tr;
}

// This function is only for the "Accurate" mode.
__audio static bool update_freq(chan* c, uint32_t* pos, int sample_rate)
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

__audio static void update_sweep(chan* c, int sample_rate)
{
    if (c->sweep.rate == 0)
    {
        return;
    }

    if (preferences_sound_mode == 2)
        return;

    c->sweep.counter += c->sweep.inc;

    while (c->sweep.counter > sample_rate)
    {
        c->sweep.counter -= sample_rate;

        uint16_t new_freq = c->sweep.freq >> c->sweep.shift;
        if (!c->sweep_up)
        {
            new_freq = c->sweep.freq - new_freq + 1;
            if (c->sweep.shift > 0)
                c->sweep.did_subtract = true;
        }
        else
        {
            new_freq = c->sweep.freq + new_freq;
        }

        if (new_freq > 2047)
        {
            c->enabled = 0;
            return;
        }

        if (c->sweep.shift > 0)
        {
            c->freq = new_freq;
            c->sweep.freq = new_freq;

            uint16_t second_new_freq = c->sweep.freq >> c->sweep.shift;
            if (!c->sweep_up)
            {
                second_new_freq = c->sweep.freq - second_new_freq;
                if (c->sweep.shift > 0)
                    c->sweep.did_subtract = true;
            }
            else
            {
                second_new_freq = c->sweep.freq + second_new_freq;
            }

            if (second_new_freq > 2047)
            {
                c->enabled = 0;
                return;
            }
        }
    }
}

/* Output constant DC offset for a disabled channel with DAC on.
 * Hardware: disabled generator outputs digital 0 = analog +1. */
__audio static void output_disabled_dc(
    audio_data* restrict audio, chan* c, int16_t* left, int16_t* right, int len
)
{
    int32_t dc = VOL_INIT_MAX / MAX_CHAN_VOLUME;  // digital 0
    int sr = get_sample_replication();
#if TARGET_PLAYDATE
    int16_t final_vol_l = c->on_left * audio->vol_l;
    int16_t final_vol_r = c->on_right * audio->vol_r;
    int16_t sample16 = (int16_t)((dc * MAX_CHAN_VOLUME) / 4);
    uint32_t packed_sample = (uint32_t)((uint16_t)sample16) | ((uint32_t)sample16 << 16);
    uint32_t packed_vols;
    asm volatile("pkhbt %0, %1, %2, lsl #16"
                 : "=r"(packed_vols)
                 : "r"(final_vol_l), "r"(final_vol_r));
    for (uint_fast16_t i = 0; i < len; i += sr)
    {
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
    }
#else
    int32_t dc_scaled = dc * MAX_CHAN_VOLUME / 4;
    for (uint_fast16_t i = 0; i < len; i += sr)
    {
        if (left == right)
        {
            int32_t left_contrib = dc_scaled * c->on_left * audio->vol_l;
            int32_t right_contrib = dc_scaled * c->on_right * audio->vol_r;
            left[i] += (left_contrib + right_contrib) / 2;
        }
        else
        {
            left[i] += dc_scaled * c->on_left * audio->vol_l;
            right[i] += dc_scaled * c->on_right * audio->vol_r;
        }
    }
#endif
}

__audio static void update_square(
    audio_data* restrict audio, int16_t* left, int16_t* right, const bool ch2, int len
)
{
    chan* c = audio->chans + ch2;

    if (!c->powered)
        return;

    if (!c->enabled)
    {
        if (preferences_sound_mode == 2)
        {
            output_disabled_dc(audio, c, left, right, len);
        }
        return;
    }

    uint32_t freq = DMG_CLOCK_FREQ_U / ((2048 - c->freq) << 5);
    set_note_freq(c, freq);
    c->freq_inc *= 8;

    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

    if (preferences_sound_mode != 2)
    {
        if (c->freq_inc == 0)
            return;
    }

    if (c->freq_inc / 8 > (uint32_t)sample_rate / 2)
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

    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        update_env(c, sample_rate);
        if (!ch2)
            update_sweep(c, sample_rate);

        int32_t sample_out;

        if (preferences_sound_mode == 2)
        {
            // --- ACCURATE MODE ---
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

            int32_t target_vol = (int32_t)c->volume << 8;
            c->envelope_smooth += (target_vol - c->envelope_smooth) >> 3;
            int32_t effective_volume = (c->envelope_smooth + 128) >> 8;

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
        else
        {
            // --- FAST MODE ---
            c->freq_counter += c->freq_inc;
            int step_count = 0;
            int32_t step_sum = 0;
            while (c->freq_counter >= sample_rate)
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

__audio static int8_t wave_sample(
    audio_data* audio, const unsigned int pos, const unsigned int volume
)
{
    uint8_t sample;

    sample = audio_mem(audio)[(0xFF30 + pos / 2) - AUDIO_ADDR_COMPENSATION];
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

__audio static void update_wave(audio_data* restrict audio, int16_t* left, int16_t* right, int len)
{
    chan* c = audio->chans + 2;

    if (!c->powered)
        return;

    if (!c->enabled)
    {
        if (preferences_sound_mode == 2)
        {
            output_disabled_dc(audio, c, left, right, len);
        }
        return;
    }

    uint32_t freq = (DMG_CLOCK_FREQ_U / 64) / (2048 - c->freq);
    set_note_freq(c, freq);
    c->freq_inc *= 32;

    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

    if (c->freq_inc == 0 && preferences_sound_mode != 2)
        return;

    if (c->freq_inc / 32 > (uint32_t)sample_rate / 2)
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

    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        int32_t sample_out;

        if (preferences_sound_mode == 2)
        {
            // --- ACCURATE MODE ---
            uint32_t pos = 0;
            uint32_t prev_pos = 0;
            int32_t weighted_sum = 0;

            while (update_freq(c, &pos, sample_rate))
            {
                weighted_sum += (int32_t)(pos - prev_pos) * c->wave.sample;
                c->val = (c->val + 1) & 31;
                c->wave.sample = wave_sample(audio, c->val, c->volume);
                c->wave.just_read = true;
                prev_pos = pos;
            }

            weighted_sum += (int32_t)(c->freq_inc - prev_pos) * c->wave.sample;
            int32_t avg =
                (c->freq_inc > 0) ? (weighted_sum / (int32_t)c->freq_inc) : c->wave.sample;
            sample_out = avg * (INT16_MAX / 32);
        }
        else
        {
            // --- FAST MODE ---
            c->freq_counter += c->freq_inc;
            while (c->freq_counter >= sample_rate)
            {
                c->freq_counter -= sample_rate;
                c->val = (c->val + 1) & 31;
                c->wave.sample = wave_sample(audio, c->val, c->volume);
                c->wave.just_read = true;
            }
        }

        if (preferences_sound_mode != 2)
            sample_out = (int32_t)c->wave.sample * (INT16_MAX / 32);

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

__audio static void update_noise(audio_data* restrict audio, int16_t* left, int16_t* right, int len)
{
    chan* c = audio->chans + 3;

    if (!c->powered)
        return;

    if (!c->enabled)
    {
        if (preferences_sound_mode == 2)
        {
            output_disabled_dc(audio, c, left, right, len);
        }
        return;
    }

    uint32_t freq = precomputed_noise_freqs[c->noise.lfsr_div][c->freq];
    set_note_freq(c, freq);

    len = update_len(audio, c, len);
    if (len == 0)
        return;

    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

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
        update_env(c, sample_rate);

        c->freq_counter += c->freq_inc;
        int step_count = 0;
        int32_t step_sum = 0;
        if (c->freq >= 14)
        {
            /* LFSR frozen; cap counter to avoid wasted loop spins. */
            c->freq_counter %= sample_rate;
        }
        else
        {
            while (c->freq_counter >= sample_rate)
            {
                c->freq_counter -= sample_rate;

                uint8_t xor_res = ((c->noise.lfsr_reg >> 0) & 1) == ((c->noise.lfsr_reg >> 1) & 1);

                c->noise.lfsr_reg >>= 1;
                c->noise.lfsr_reg |= (xor_res << 14);

                if (c->lfsr_narrow)
                {
                    c->noise.lfsr_reg = (c->noise.lfsr_reg & ~(1 << 6)) | (xor_res << 6);
                }

                c->val = (c->noise.lfsr_reg & 1) ? (VOL_INIT_MAX / MAX_CHAN_VOLUME)
                                                 : (VOL_INIT_MIN / MAX_CHAN_VOLUME);
                step_count++;
                step_sum += c->val;
            }
        }

        if (c->muted)
            continue;

        int32_t mono_sample;
        int32_t effective_volume;
        if (preferences_sound_mode == 2)
        {
            int32_t target_vol = (int32_t)c->volume << 8;
            c->envelope_smooth += (target_vol - c->envelope_smooth) >> 3;
            effective_volume = (c->envelope_smooth + 128) >> 8;
        }
        else
        {
            effective_volume = c->volume;
        }

        if (step_count > 0)
            mono_sample = (step_sum / step_count) * effective_volume;
        else
            mono_sample = c->val * effective_volume;
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

static void chan_trigger(audio_data* restrict audio, uint_fast8_t i)
{
    chan* chans = audio->chans;
    chan* c = chans + i;

    // DMG wave RAM corruption on retrigger: must capture before chan_enable.
    bool wave_was_active = (i == 2) && c->enabled;
    bool was_enabled = c->enabled;

    // Digital-zero surpression on first start only (not re-trigger).
    // Must check c->enabled before chan_enable sets it to 1.
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
        c->env_pending = false;
        c->freq_counter = 0;

        if (preferences_sound_mode == 2 && c->env.step > 0)
        {
            c->env_divider = c->env.step;
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

        if (c->sweep.shift > 0)
        {
            if (calculate_new_sweep_freq(c))
            {
                return;
            }
        }

        c->sweep.counter = 0;
        c->sweep.did_subtract = false;

        if (preferences_sound_mode == 2 && (c->sweep.rate > 0 || c->sweep.shift > 0))
        {
            c->sweep_divider = c->sweep.rate ? c->sweep.rate : 8;
        }
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
                int byte_idx = c->val >> 1;
                if (byte_idx < 4)
                {
                    wave_ram[0] = wave_ram[byte_idx];
                }
                else
                {
                    int aligned = byte_idx & ~3;
                    memcpy(wave_ram, wave_ram + aligned, 4);
                }
            }
        }
        c->val = 0;
    }
    else if (i == 3)
    {  // noise
        c->noise.lfsr_reg = 0x0000;
        c->val = VOL_INIT_MIN / MAX_CHAN_VOLUME;
    }

    // Accurate-mode envelope smoothing: init per-channel running average
    if (preferences_sound_mode == 2)
        c->envelope_smooth = (int32_t)c->volume << 8;

    // Accurate mode: only reload length if the counter has already hit zero.
    // Hardware: trigger resets the length timer only when it expired.
    // The obscure length clocking in NRx4 writes can decrement the counter
    // to 0 without disabling the channel (when trigger is also set), so
    // check the remaining count directly, not just the enabled flag.
    // Fast mode: always reload length for simplicity.
    bool len_expired = ((c->len.inc >> 16) == 0);
    if (i == 3 || preferences_sound_mode != 2 || !was_enabled || len_expired)
    {
        int load = len_max - c->len.load;

        if (preferences_sound_mode == 2)
        {
            uint8_t div_apu_next = (audio->div_apu_step + 1) & 7;
            bool next_doesnt_clock_len = (div_apu_next & 1) != 0;

            // Obscure: length reload 63 vs 64 (255 vs 256 for wave)
            if (next_doesnt_clock_len && c->len_enabled && load == len_max)
                load = len_max - 1;
        }

        c->len.inc = 256 | ((uint32_t)load << 16);
        c->len.counter = 0;
    }
}

/**
 * Read audio register.
 * \param addr  Address of audio register. Must be 0xFF10 <= addr <= 0xFF3F.
 *              This is not checked in this function.
 * \return      Byte at address.
 */
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
     * On DMG, returns 0xFF unless CPU access coincides with CH3 read cycle (just_read).
     */
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

    /* PCM12: CH2 (high nibble) + CH1 (low nibble). PCM34: CH4 (high) + CH3 (low).
     * CGB-only registers; DMG returns open bus (0xFF)).
     */
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

/**
 * Write audio register.
 * \param addr  Address of audio register. Must be 0xFF10 <= addr <= 0xFF3F.
 *              This is not checked in this function.
 * \param val   Byte to write at address.
 */
void audio_write(audio_data* restrict audio, const uint16_t addr, const uint8_t val)
{
    /* Find sound channel corresponding to register address. */
    uint_fast8_t i;
    chan* chans = audio->chans;

    if (addr == 0xFF26)
    {
        bool was_on = (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] & 0x80) != 0;
        audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val & 0x80;
        /* On APU power off, clear all registers apart from wave RAM. */
        if ((val & 0x80) == 0)
        {
            memset(audio_mem(audio), 0x00, 0xFF26 - AUDIO_ADDR_COMPENSATION);
            chans[0].enabled = false;
            chans[1].enabled = false;
            chans[2].enabled = false;
            chans[2].wave.sample = 0;
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
        }
        return;
    }

    /* Ignore register writes if APU powered off, except wave RAM (always accessible). */
    if (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] == 0x00)
    {
        /* Wave RAM is writable even with APU off (Pandocs). CH3 can't be active
         * when APU is off, so no redirect needed.
         */
        if (addr >= 0xFF30 && addr <= 0xFF3F)
        {
            audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val;
        }
        return;
    }

    /* Wave RAM writes during CH3 playback redirect to the byte CH3 is currently
     * reading. On DMG, writes are ignored unless coincident with CH3 read cycle.
     */
    if (addr >= 0xFF30 && addr <= 0xFF3F)
    {
        chan* c = &audio->chans[2];
        if (c->powered && c->enabled)
        {
            gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
            if (!gb->is_cgb_mode)
            {
                if (!c->wave.just_read)
                    return;
            }
            uint8_t wave_idx = c->val >> 1;
            audio_mem(audio)[0xFF30 + wave_idx - AUDIO_ADDR_COMPENSATION] = val;
            return;
        }
    }

    audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val;

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
        }
        else
        {
            chans[0].sweep.rate = new_rate;
            chans[0].sweep.inc = preferences_sound_mode == 2 ? 0 : 128 / new_rate;
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

        /* Reload sweep divider when NR10 is written mid-playback.
         * Hardware: timer period 0 is treated as 8. */
        if (preferences_sound_mode == 2 && chans[0].enabled)
        {
            uint8_t pace = chans[0].sweep.rate;
            if (pace != 0 || chans[0].sweep.shift != 0)
                chans[0].sweep_divider = pace ? pace : 8;
        }
    }
    break;

    case 0xFF12:
    case 0xFF17:
    case 0xFF21:
    {
        // SameBoy _nrx2_glitch: adjust volume on NRx2 write while channel active.
        // DMG: 2-pass via 0xFF intermediate. CGB: single pass.
        if (chans[i].powered && chans[i].enabled)
        {
            gb_s* gb = (gb_s*)((uint8_t*)audio - offsetof(gb_s, audio));
            uint8_t old_val = audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION];

            if (!gb->is_cgb_mode)
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

        // Reload envelope divider when NRx2 is written mid-playback,
        // but only while the envelope clock is active (env_pending set).
        if (preferences_sound_mode == 2 && chans[i].enabled && chans[i].env_pending &&
            chans[i].env.step > 0)
        {
            chans[i].env_divider = chans[i].env.step;
        }
    }
    break;

    case 0xFF1C:
        chans[i].volume = chans[i].volume_init = (val >> 5) & 0x03;
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
        chans[i].powered = (val & 0x80) != 0;
        chan_enable(audio, i, val & 0x80);
        break;

    case 0xFF14:
    case 0xFF19:
    case 0xFF1E:
        chans[i].freq &= 0x00FF;
        chans[i].freq |= ((val & 0x07) << 8);
        /* Intentional fall-through. */
    case 0xFF23:
    {
        bool old_len_enabled = chans[i].len_enabled;
        chans[i].len_enabled = val & 0x40 ? 1 : 0;
        bool trigger = val & 0x80;

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

void audio_init(audio_data* audio)
{
    chan* chans = audio->chans;

    /* Initialise channels and samples. */
    memset(chans, 0, 4 * sizeof(chan));
    chans[0].val = chans[1].val = -1;
    chans[2].wave.sample = 0;

#if TARGET_PLAYDATE
    audio->capacitor_l = 0;
    audio->capacitor_r = 0;
#else
    audio->capacitor_l = 0.0f;
    audio->capacitor_r = 0.0f;
#endif
    audio->div_apu_step = 0;
    audio->skip_next_apu_tick = false;

    // NRx4 registers ($FF14/$FF19/$FF1E/$FF23) are set to $3F instead of the Pan Docs
    // post-boot-rom value $BF. The difference is bit 7 (channel trigger): the real boot
    // ROM sets this because it played "ba-ding" before handing off. With skip-BIOS we
    // must keep it clear so the game triggers channels when it intends to and avoid a
    // "ba-ding" when the game first launches.

    /* Initialise IO registers. */
    { /* clang-format off */
        static const uint8_t regs_init[] = {
            0x80, 0xBF, 0xF3, 0xFF, 0x3F,
            0xFF, 0x3F, 0x00, 0xFF, 0x3F,
            0x7F, 0xFF, 0x9F, 0xFF, 0x3F,
            0xFF, 0xFF, 0x00, 0x00, 0x3F,
            0x77, 0xF3, 0xF1
        };
        /* clang-format on */

        for (uint_fast8_t i = 0; i < sizeof(regs_init); ++i)
            audio_write(audio, 0xFF10 + i, regs_init[i]);
    }

    /* Initialise Wave Pattern RAM. */
    { /* clang-format off */
        static const uint8_t wave_init[] = {
            0xac, 0xdd, 0xda, 0x48,
            0x36, 0x02, 0xcf, 0x16,
            0x2c, 0x04, 0xe5, 0x2c,
            0xac, 0xdd, 0xda, 0x48
        };
        /* clang-format on */

        for (uint_fast8_t i = 0; i < sizeof(wave_init); ++i)
            audio_write(audio, 0xFF30 + i, wave_init[i]);
    }

    for (uint8_t lfsr_selector_idx = 0; lfsr_selector_idx < 8; ++lfsr_selector_idx)
    {
        uint32_t current_lfsr_div_val = lfsr_selector_idx == 0 ? 4 : lfsr_selector_idx * 8;
        for (uint8_t c_freq_shift_val = 0; c_freq_shift_val < 16; ++c_freq_shift_val)
        {
            uint32_t divisor_term = current_lfsr_div_val << c_freq_shift_val;

            if (divisor_term == 0)
            {
                // This should ideally not happen with current_lfsr_div_val and
                // 0-15 shift
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

int audio_enabled;

#if TARGET_PLAYDATE
__attribute__((always_inline)) static inline void replicate_samples_optimized(
    int16_t* left_ptr, int16_t* right_ptr, int chunksize, int sample_replication, bool is_stereo
)
{
    if (sample_replication <= 1)
        return;

    for (int i = 0; i < chunksize; i += sample_replication)
    {
        int samples_to_replicate = sample_replication - 1;
        if (i + samples_to_replicate >= chunksize)
        {
            samples_to_replicate = chunksize - i - 1;
        }
        if (samples_to_replicate <= 0)
            continue;

        if (is_stereo)
        {
            int16_t sample_l = left_ptr[i];
            int16_t sample_r = right_ptr[i];
            int16_t* dest_l = &left_ptr[i + 1];
            int16_t* dest_r = &right_ptr[i + 1];

            asm volatile(
                "mov r0, %[sl]\n\t"
                "mov r1, r0\n\t"
                "mov r2, r0\n\t"
                "mov r3, r0\n\t"
                "mov r4, %[sr]\n\t"
                "mov r5, r4\n\t"
                "mov r6, r4\n\t"
                "mov r7, r4\n\t"

                "1:\n\t"
                "cmp %[count], #4\n\t"
                "blt 2f\n\t"
                "stmia %[dl]!, {r0-r3}\n\t"
                "stmia %[dr]!, {r4-r7}\n\t"
                "sub %[count], #4\n\t"
                "b 1b\n\t"
                "2:\n\t"

                : [dl] "+r"(dest_l), [dr] "+r"(dest_r), [count] "+r"(samples_to_replicate)
                : [sl] "r"(sample_l), [sr] "r"(sample_r)
                : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "memory", "cc"
            );

            if (samples_to_replicate > 0)
            {
                uint32_t packed_l = (uint32_t)((uint16_t)sample_l) | ((uint32_t)sample_l << 16);
                uint32_t packed_r = (uint32_t)((uint16_t)sample_r) | ((uint32_t)sample_r << 16);
                if (samples_to_replicate >= 2)
                {
                    *((uint32_t*)dest_l) = packed_l;
                    *((uint32_t*)dest_r) = packed_r;
                    dest_l += 2;
                    dest_r += 2;
                    samples_to_replicate -= 2;
                }
                if (samples_to_replicate > 0)
                {
                    *dest_l = sample_l;
                    *dest_r = sample_r;
                }
            }
        }
        else
        {
            int16_t sample = left_ptr[i];
            int16_t* dest = &left_ptr[i + 1];

            asm volatile(
                "mov r0, %[s]\n\t"
                "mov r1, r0\n\t"
                "mov r2, r0\n\t"
                "mov r3, r0\n\t"
                "1:\n\t"
                "cmp %[count], #4\n\t"
                "blt 2f\n\t"
                "stmia %[d]!, {r0-r3}\n\t"
                "sub %[count], #4\n\t"
                "b 1b\n\t"
                "2:\n\t"
                : [d] "+r"(dest), [count] "+r"(samples_to_replicate)
                : [s] "r"(sample)
                : "r0", "r1", "r2", "r3", "memory", "cc"
            );

            if (samples_to_replicate > 0)
            {
                uint32_t packed = (uint32_t)((uint16_t)sample) | ((uint32_t)sample << 16);
                if (samples_to_replicate >= 2)
                {
                    *((uint32_t*)dest) = packed;
                    dest += 2;
                    samples_to_replicate -= 2;
                }
                if (samples_to_replicate > 0)
                {
                    *dest = sample;
                }
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
            "asr r0, %[cap_l_in], #16\n\t"
            "asr r1, %[cap_r_in], #16\n\t"
            "pkhbt r0, r0, r1, lsl #16\n\t"

            "qsub16 %[out_lr_out], %[in_lr_in], r0\n\t"

            "smulbb r1, %[out_lr_out], %[charge_in]\n\t"
            "lsl r1, r1, #1\n\t"
            "lsl r0, %[in_l_in], #16\n\t"
            "sub %[cap_l_out], r0, r1\n\t"

            "smultb r1, %[out_lr_out], %[charge_in]\n\t"
            "lsl r1, r1, #1\n\t"
            "lsl r0, %[in_r_in], #16\n\t"
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

/**
 * Optimized memset for audio buffers using SIMD stores.
 * Clears buffer 8 samples at a time using STM.
 */
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

/**
 * Helper to clear both audio buffers (mono or stereo).
 */
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

/**
 * Playdate audio callback function.
 */
__audio int audio_callback(void* context, int16_t* left, int16_t* right, int len)
{
    if (!audio_enabled)
        return 0;

    DTCM_VERIFY_DEBUG();

    CB_GameScene** gameScene_ptr = context;
    CB_GameScene* gameScene = *gameScene_ptr;

    if (!gameScene)
    {
        clear_audio_buffers(left, right, len);
        return 1;
    }

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

    if (preferences_audio_sync == 1)
    {
        uint32_t read_pos = atomic_load(&g_audio_sync_buffer.read_pos);
        uint32_t write_pos = atomic_load(&g_audio_sync_buffer.write_pos);
        uint32_t samples_available = write_pos - read_pos;

        if (samples_available < len)
        {
            playdate->system->logToConsole(
                "AUDIO UNDERRUN! available: %d, needed: %d", samples_available, len
            );
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
        }

        atomic_store(&g_audio_sync_buffer.read_pos, read_pos + len);
    }
    else
    {
        __builtin_prefetch(left, 1);
        int sample_replication = get_sample_replication();
        int max_chunk = ((256 + sample_replication - 1) / sample_replication) * sample_replication;

        int16_t* left_ptr = left;
        int16_t* right_ptr = gameScene->is_stereo ? right : left;
        int remaining_len = len;

        flush_apu_ticks(audio);

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

            update_wave(audio, left_ptr, right_ptr, chunksize);
            update_square(audio, left_ptr, right_ptr, 0, chunksize);
            update_square(audio, left_ptr, right_ptr, 1, chunksize);
            update_noise(audio, left_ptr, right_ptr, chunksize);

#if TARGET_PLAYDATE
            replicate_samples_optimized(
                left_ptr, right_ptr, chunksize, sample_replication, gameScene->is_stereo
            );
#else

            if (sample_replication > 1)
            {
                for (int i = 0; i < chunksize; i += sample_replication)
                {
                    for (int j = 1; j < sample_replication && (i + j) < chunksize; ++j)
                    {
                        left_ptr[i + j] = left_ptr[i];
                        if (gameScene->is_stereo)
                        {
                            right_ptr[i + j] = right_ptr[i];
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
    if (preferences_sound_mode == 2)
    {
        bool dacs_enabled = audio->chans[0].powered || audio->chans[1].powered ||
                            audio->chans[2].powered || audio->chans[3].powered;

        if (dacs_enabled)
        {
#if TARGET_PLAYDATE
            int cgb_idx = gameScene->context->gb->is_cgb_mode;
            int16_t charge_factor = get_charge_factors_q15[cgb_idx][preferences_sample_rate];
            high_pass_filter_fixed_asm(left, right, len, audio, charge_factor);
#else
            int cgb_idx = gameScene->context->gb->is_cgb_mode;
            float charge_factor = get_charge_factors[cgb_idx][preferences_sample_rate];
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
        else
        {
#if TARGET_PLAYDATE
            audio->capacitor_l = 0;
            audio->capacitor_r = 0;
#else
            audio->capacitor_l = 0.0f;
            audio->capacitor_r = 0.0f;
#endif
        }
    }

#ifdef TARGET_SIMULATOR
    pthread_mutex_unlock(&audio_mutex);
#endif

    DTCM_VERIFY_DEBUG();

    return 1;
}

void audio_update_square(
    audio_data* restrict audio, int16_t* left, int16_t* right, const bool ch2, int len
)
{
    update_square(audio, left, right, ch2, len);
}

void audio_update_wave(audio_data* restrict audio, int16_t* left, int16_t* right, int len)
{
    update_wave(audio, left, right, len);
}

void audio_update_noise(audio_data* restrict audio, int16_t* left, int16_t* right, int len)
{
    update_noise(audio, left, right, len);
}
