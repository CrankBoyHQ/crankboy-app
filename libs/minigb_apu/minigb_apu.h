/**
 * minigb_apu is released under the terms listed within the LICENSE file.
 *
 * minigb_apu emulates the audio processing unit (APU) of the Game Boy. This
 * project is based on MiniGBS by Alex Baines: https://github.com/baines/MiniGBS
 */

#pragma once

#include <stdint.h>

/* Calculating VSYNC. */
#ifndef DMG_CLOCK_FREQ
#define DMG_CLOCK_FREQ 4194304.0f
#endif

#ifndef SCREEN_REFRESH_CYCLES
#define SCREEN_REFRESH_CYCLES 70224.0f
#endif

#define VERTICAL_SYNC (DMG_CLOCK_FREQ / SCREEN_REFRESH_CYCLES)

// master audio control
extern int audio_enabled;

struct chan_len_ctr
{
    uint8_t load;
    uint32_t counter;
    uint32_t inc;
};

struct chan_vol_env
{
    uint8_t step : 3;
    unsigned up : 1;
    uint32_t counter;
    uint32_t inc;
};

struct chan_freq_sweep
{
    uint16_t freq;
    uint8_t rate;
    uint8_t shift;
    uint32_t counter;
    uint32_t inc;
};

struct chan
{
    unsigned enabled : 1;
    unsigned powered : 1;
    unsigned on_left : 1;
    unsigned on_right : 1;
    unsigned muted : 1;
    uint8_t lfsr_wide : 1;
    unsigned sweep_up : 1;
    unsigned len_enabled : 1;

    uint8_t volume : 4;
    uint8_t volume_init : 4;
    uint16_t freq;
    uint32_t freq_counter;
    uint32_t freq_inc;

    int_fast16_t val;

    struct chan_len_ctr len;
    struct chan_vol_env env;
    struct chan_freq_sweep sweep;

    union
    {
        struct
        {
            uint8_t duty;
            uint8_t duty_counter;
        } square;
        struct
        {
            uint16_t lfsr_reg;
            uint8_t lfsr_div;
        } noise;
        struct
        {
            int8_t sample;
        } wave;
    };
};

typedef struct audio_data
{
    int vol_l : 4;
    int vol_r : 4;
    uint8_t* audio_mem;
    struct chan chans[4];
} audio_data;

/**
 * Read audio register at given address "addr".
 */
uint8_t audio_read(struct audio_data* audio, const uint16_t addr);

/**
 * Write "val" to audio register at given address "addr".
 */
void audio_write(struct audio_data* audio, const uint16_t addr, const uint8_t val);

/**
 * Initialise audio driver.
 */
void audio_init(audio_data* audio);

/**
 * Playdate audio callback function.
 */
int audio_callback(void* context, int16_t* left, int16_t* right, int len);

unsigned audio_get_state_size(void);
void audio_state_save(void* buff);
void audio_state_load(const void* buff);
