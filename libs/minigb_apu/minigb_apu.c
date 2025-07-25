/**
 * minigb_apu is released under the terms listed within the LICENSE file.
 *
 * minigb_apu emulates the audio processing unit (APU) of the Game Boy. This
 * project is based on MiniGBS by Alex Baines: https://github.com/baines/MiniGBS
 */

#include "minigb_apu.h"

#include "../peanut_gb.h"
#include "../src/app.h"
#include "../src/dtcm.h"
#include "../src/preferences.h"
#include "../src/scenes/game_scene.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define audio_mem(audio) \
    ((uint8_t*)((void*)audio - offsetof(struct gb_s, audio) + offsetof(struct gb_s, hram) + 0x10))

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
    __attribute__((optimize("Os"))) __attribute__((section(".audio"))) __attribute__((short_call))
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

__audio static void set_note_freq(struct chan* c, const uint32_t freq)
{
    /* Lowest expected value of freq is 64. */
    // Set frequency increment
    c->freq_inc = freq;
}

static void chan_enable(audio_data* audio, const uint_fast8_t i, const bool enable)
{
    uint8_t val;
    struct chan* chans = audio->chans;
    chans[i].enabled = enable;
    val = (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] & 0x80) | (chans[3].enabled << 3) |
          (chans[2].enabled << 2) | (chans[1].enabled << 1) | (chans[0].enabled << 0);

    audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] = val;
}

__audio static void update_env(struct chan* c, int sample_rate)
{
    c->env.counter += c->env.inc;

    while (c->env.counter > sample_rate)
    {
        if (c->env.step)
        {
            c->volume += c->env.up ? 1 : -1;
            if (c->volume == 0 || c->volume == MAX_CHAN_VOLUME)
            {
                c->env.inc = 0;
            }
            c->volume = MAX(0, MIN(MAX_CHAN_VOLUME, c->volume));
        }
        c->env.counter -= sample_rate;
    }
}

// returns sample index at which to stop outputting in channel
__audio static int update_len(audio_data* restrict audio, struct chan* c, int len)
{
    if (!c->enabled)
        return 0;

    if (!c->len_enabled || c->len.inc == 0)
        return len;

    int sample_rate = get_audio_sample_rate();
    int tr = (sample_rate - c->len.counter) / c->len.inc;

    if (tr > len)
    {
        c->len.counter += len * c->len.inc;
        return len;
    }
    else
    {
        c->len.counter = 0;
        chan_enable(audio, c - audio->chans, 0);
        return tr;
    }
}

// This function is only for the "Accurate" mode.
__audio static bool update_freq(struct chan* c, uint32_t* pos, int sample_rate)
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

__audio static void update_sweep(struct chan* c, int sample_rate)
{
    c->sweep.counter += c->sweep.inc;

    while (c->sweep.counter > sample_rate)
    {
        if (c->sweep.shift)
        {
            uint16_t inc = (c->sweep.freq >> c->sweep.shift);
            if (!c->sweep_up)
                inc *= -1;

            c->freq += inc;
            if (c->freq > 2047)
            {
                c->enabled = 0;
            }
            else
            {
                set_note_freq(c, DMG_CLOCK_FREQ_U / ((2048 - c->freq) << 5));
                c->freq_inc *= 8;
            }
        }
        else if (c->sweep.rate)
        {
            c->enabled = 0;
        }
        c->sweep.counter -= sample_rate;
    }
}

__audio static void update_square(
    audio_data* restrict audio, int16_t* buffer, const bool ch2, int len
)
{
    struct chan* c = audio->chans + ch2;

    if (!c->powered || !c->enabled)
        return;

    uint32_t freq = DMG_CLOCK_FREQ_U / ((2048 - c->freq) << 5);
    set_note_freq(c, freq);
    c->freq_inc *= 8;

    if (preferences_sound_mode != 2)
    {
        if (c->freq_inc == 0)
            return;
    }

    len = update_len(audio, c, len);
    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        update_env(c, sample_rate);
        if (!ch2)
            update_sweep(c, sample_rate);

        if (preferences_sound_mode == 2)
        {
            uint32_t pos = 0;
            uint32_t prev_pos = 0;
            int32_t sample = 0;

            while (update_freq(c, &pos, sample_rate))
            {
                c->square.duty_counter = (c->square.duty_counter + 1) & 7;
                sample += ((pos - prev_pos) / c->freq_inc) * c->val;
                c->val = (c->square.duty & (1 << c->square.duty_counter))
                             ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                             : VOL_INIT_MIN / MAX_CHAN_VOLUME;
                prev_pos = pos;
            }

            if (c->muted)
                continue;

            sample += c->val;
            sample *= c->volume;
            sample /= 4;

            int32_t left_contrib = sample * c->on_left * audio->vol_l;
            int32_t right_contrib = sample * c->on_right * audio->vol_r;

            buffer[i] += (left_contrib + right_contrib) / 2;
        }
        else
        {
            c->freq_counter += c->freq_inc;
            while (c->freq_counter >= sample_rate)
            {
                c->freq_counter -= sample_rate;
                c->square.duty_counter = (c->square.duty_counter + 1) & 7;
                c->val = (c->square.duty & (1 << c->square.duty_counter))
                             ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                             : VOL_INIT_MIN / MAX_CHAN_VOLUME;
            }

            if (c->muted)
                continue;

            int32_t sample = c->val;
            sample *= c->volume;
            sample >>= 2;

            int32_t left_contrib = sample * c->on_left * audio->vol_l;
            int32_t right_contrib = sample * c->on_right * audio->vol_r;

            buffer[i] += (left_contrib + right_contrib) / 2;
        }
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

__audio static void update_wave(audio_data* restrict audio, int16_t* buffer, int len)
{
    struct chan* chans = audio->chans;
    struct chan* c = chans + 2;

    if (!c->powered || !c->enabled)
        return;

    uint32_t freq = (DMG_CLOCK_FREQ_U / 64) / (2048 - c->freq);
    set_note_freq(c, freq);
    c->freq_inc *= 32;

    if (preferences_sound_mode != 2)
    {
        if (c->freq_inc == 0)
            return;
    }

    len = update_len(audio, c, len);
    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();

    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        if (preferences_sound_mode == 2)
        {
            uint32_t pos = 0;
            uint32_t prev_pos = 0;
            int32_t sample = 0;

            c->wave.sample = wave_sample(audio, c->val, c->volume);

            while (update_freq(c, &pos, sample_rate))
            {
                c->val = (c->val + 1) & 31;
                sample +=
                    ((pos - prev_pos) / c->freq_inc) * (int32_t)c->wave.sample * (INT16_MAX / 32);
                c->wave.sample = wave_sample(audio, c->val, c->volume);
                prev_pos = pos;
            }

            sample += (int32_t)c->wave.sample * (int)(INT16_MAX / 32);

            if (c->volume == 0 || c->muted)
                continue;

            sample /= 4;

            int32_t left_contrib = sample * c->on_left * audio->vol_l;
            int32_t right_contrib = sample * c->on_right * audio->vol_r;

            buffer[i] += (left_contrib + right_contrib) / 2;
        }
        else
        {
            c->freq_counter += c->freq_inc;
            while (c->freq_counter >= sample_rate)
            {
                c->freq_counter -= sample_rate;
                c->val = (c->val + 1) & 31;
            }

            if (c->muted)
                continue;

            int8_t wave_val = wave_sample(audio, c->val, c->volume);
            int32_t sample = (int32_t)wave_val * (INT16_MAX / 32);
            sample >>= 2;

            int32_t left_contrib = sample * c->on_left * audio->vol_l;
            int32_t right_contrib = sample * c->on_right * audio->vol_r;

            buffer[i] += (left_contrib + right_contrib) / 2;
        }
    }
}

__audio static void update_noise(audio_data* restrict audio, int16_t* buffer, int len)
{
    struct chan* c = audio->chans + 3;

    if (!c->powered)
        return;
    {
        uint32_t freq = precomputed_noise_freqs[c->noise.lfsr_div][c->freq];
        set_note_freq(c, freq);

        // A frequency of 0 would cause a division by zero in accurate sound
        // mode.
        if (c->freq_inc == 0)
            return;
    }

    if (c->freq >= 14)
        c->enabled = 0;

    len = update_len(audio, c, len);

    if (!c->enabled)
        return;

    int sample_replication = get_sample_replication();
    int sample_rate = get_audio_sample_rate();
    for (uint_fast16_t i = 0; i < len; i += sample_replication)
    {
        update_env(c, sample_rate);

        if (preferences_sound_mode == 2)
        {
            uint32_t pos = 0;
            uint32_t prev_pos = 0;
            int32_t sample = 0;

            while (update_freq(c, &pos, sample_rate))
            {
                c->noise.lfsr_reg =
                    (c->noise.lfsr_reg << 1) | (c->val >= VOL_INIT_MAX / MAX_CHAN_VOLUME);

                if (c->lfsr_wide)
                {
                    c->val = !(((c->noise.lfsr_reg >> 14) & 1) ^ ((c->noise.lfsr_reg >> 13) & 1))
                                 ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                                 : VOL_INIT_MIN / MAX_CHAN_VOLUME;
                }
                else
                {
                    c->val = !(((c->noise.lfsr_reg >> 6) & 1) ^ ((c->noise.lfsr_reg >> 5) & 1))
                                 ? VOL_INIT_MAX / MAX_CHAN_VOLUME
                                 : VOL_INIT_MIN / MAX_CHAN_VOLUME;
                }
                sample += ((pos - prev_pos) / c->freq_inc) * c->val;
                prev_pos = pos;
            }

            if (c->muted)
                continue;

            sample += c->val;
            sample *= c->volume;
            sample /= 4;

            int32_t left_contrib = sample * c->on_left * audio->vol_l;
            int32_t right_contrib = sample * c->on_right * audio->vol_r;

            buffer[i] += (left_contrib + right_contrib) / 2;
        }
        else
        {
            c->freq_counter += c->freq_inc;
            while (c->freq_counter >= sample_rate)
            {
                c->freq_counter -= sample_rate;

                uint16_t old_lfsr = c->noise.lfsr_reg;
                c->noise.lfsr_reg <<= 1;

                uint8_t xor_res = (c->lfsr_wide) ? (((old_lfsr >> 14) & 1) ^ ((old_lfsr >> 13) & 1))
                                                 : (((old_lfsr >> 6) & 1) ^ ((old_lfsr >> 5) & 1));

                c->noise.lfsr_reg |= xor_res;
                c->val = !xor_res ? VOL_INIT_MAX / MAX_CHAN_VOLUME : VOL_INIT_MIN / MAX_CHAN_VOLUME;
            }

            if (c->muted)
                continue;

            int32_t sample = c->val;
            sample *= c->volume;
            sample >>= 2;

            int32_t left_contrib = sample * c->on_left * audio->vol_l;
            int32_t right_contrib = sample * c->on_right * audio->vol_r;

            buffer[i] += (left_contrib + right_contrib) / 2;
        }
    }
}

static void chan_trigger(audio_data* restrict audio, uint_fast8_t i)
{
    struct chan* chans = audio->chans;
    struct chan* c = chans + i;

    chan_enable(audio, i, 1);
    c->volume = c->volume_init;

    // volume envelope
    {
        uint8_t val = audio_mem(audio)[(0xFF12 + (i * 5)) - AUDIO_ADDR_COMPENSATION];

        c->env.step = val & 0x07;
        c->env.up = val & 0x08 ? 1 : 0;
        c->env.inc = c->env.step ? 64ul / (uint32_t)c->env.step : 8ul;
        c->env.counter = 0;
    }

    // freq sweep
    if (i == 0)
    {
        uint8_t val = audio_mem(audio)[0xFF10 - AUDIO_ADDR_COMPENSATION];

        c->sweep.freq = c->freq;
        c->sweep.rate = (val >> 4) & 0x07;
        c->sweep_up = !(val & 0x08);
        c->sweep.shift = (val & 0x07);
        c->sweep.inc = c->sweep.rate ? 128 / c->sweep.rate : 0;
        c->sweep.counter = get_audio_sample_rate();
    }

    int len_max = 64;

    if (i == 2)
    {  // wave
        len_max = 256;
        c->val = 0;
    }
    else if (i == 3)
    {  // noise
        c->noise.lfsr_reg = 0xFFFF;
        c->val = VOL_INIT_MIN / MAX_CHAN_VOLUME;
    }

    c->len.inc = (len_max > c->len.load) ? 256 / (len_max - c->len.load) : 0;
    c->len.counter = 0;
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
    struct chan* chans = audio->chans;

    if (addr == 0xFF26)
    {
        audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val & 0x80;
        /* On APU power off, clear all registers apart from wave RAM. */
        if ((val & 0x80) == 0)
        {
            memset(audio_mem(audio), 0x00, 0xFF26 - AUDIO_ADDR_COMPENSATION);
            chans[0].enabled = false;
            chans[1].enabled = false;
            chans[2].enabled = false;
            chans[3].enabled = false;
        }
        return;
    }

    /* Ignore register writes if APU powered off. */
    if (audio_mem(audio)[0xFF26 - AUDIO_ADDR_COMPENSATION] == 0x00)
        return;

    audio_mem(audio)[addr - AUDIO_ADDR_COMPENSATION] = val;

    if (preferences_sound_mode == 2)
    {
        i = (addr - AUDIO_ADDR_COMPENSATION) * 0.2f;
    }
    else
    {
        i = (addr - AUDIO_ADDR_COMPENSATION) / 5;
    }

    switch (addr)
    {
    case 0xFF12:
    case 0xFF17:
    case 0xFF21:
    {
        chans[i].volume_init = val >> 4;
        chans[i].powered = (val >> 3) != 0;

        // "zombie mode" stuff, needed for Prehistorik Man and probably
        // others
        if (chans[i].powered && chans[i].enabled)
        {
            if ((chans[i].env.step == 0 && chans[i].env.inc != 0))
            {
                if (val & 0x08)
                {
                    chans[i].volume++;
                }
                else
                {
                    chans[i].volume += 2;
                }
            }
            else
            {
                chans[i].volume = 16 - chans[i].volume;
            }
            chans[i].volume &= 0x0F;
            chans[i].env.step = val & 0x07;
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
        static const uint8_t duty_lookup[] = {0x10, 0x30, 0x3C, 0xCF};
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
        chans[i].len_enabled = val & 0x40 ? 1 : 0;
        if (val & 0x80)
            chan_trigger(audio, i);
        break;

    case 0xFF22:
        chans[3].freq = val >> 4;
        chans[3].lfsr_wide = !(val & 0x08);
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
    struct chan* chans = audio->chans;

    /* Initialise channels and samples. */
    memset(chans, 0, 4 * sizeof(struct chan));
    chans[0].val = chans[1].val = -1;

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
        uint32_t current_lfsr_div_val = lfsr_selector_idx == 0 ? 8 : lfsr_selector_idx * 16;
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

    if (!gameScene || gameScene->audioLocked)
    {
        return 0;
    }

#ifdef TARGET_SIMULATOR
    pthread_mutex_lock(&audio_mutex);
#endif

    audio_data* audio = &gameScene->context->gb->audio;

    __builtin_prefetch(left, 1);

    int sample_replication = get_sample_replication();
    int max_chunk = ((256 + sample_replication - 1) / sample_replication) * sample_replication;

    while (len > 0)
    {
        int chunksize = len >= max_chunk ? max_chunk : len;

        memset(left, 0, chunksize * sizeof(int16_t));

        update_wave(audio, left, chunksize);
        update_square(audio, left, 0, chunksize);
        update_square(audio, left, 1, chunksize);
        update_noise(audio, left, chunksize);

        // 3. Handle sample replication on the 'left' buffer.
        if (sample_replication > 1)
        {
            for (int i = 0; i < chunksize; i += sample_replication)
            {
                for (int j = 1; j < sample_replication && (i + j) < chunksize; ++j)
                {
                    left[i + j] = left[i];
                }
            }
        }

        len -= chunksize;
        left += chunksize;
        right += chunksize;
    }

#ifdef TARGET_SIMULATOR
    pthread_mutex_unlock(&audio_mutex);
#endif

    DTCM_VERIFY_DEBUG();

    return 1;
}
