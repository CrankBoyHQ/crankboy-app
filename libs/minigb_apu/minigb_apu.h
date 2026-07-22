/**
 * minigb_apu is released under the terms listed within the LICENSE file.
 *
 * minigb_apu emulates the audio processing unit (APU) of the Game Boy. This
 * project is based on MiniGBS by Alex Baines: https://github.com/baines/MiniGBS
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Note: This header is included by peanut_gb.h when PGB_IMPL is defined.
 * It should not be included directly - always include peanut_gb.h instead.
 * The audio_data type is defined in peanut_gb.h.
 */

/* If peanut_gb.h hasn't been included yet, declare audio_data as opaque */
#ifndef PEANUT_GB_H
typedef struct audio_data audio_data;
#endif

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
extern int audio_muted;

/**
 * Read audio register at given address "addr".
 */
uint8_t audio_read(audio_data* audio, const uint16_t addr);

/**
 * Write "val" to audio register at given address "addr".
 */
void audio_write(audio_data* audio, const uint16_t addr, const uint8_t val);

/**
 * Initialise audio driver.
 */
void audio_init(audio_data* audio);

/**
 * Advance the DIV-APU frame sequencer by one step. Called by the CPU when the
 * DIV register's relevant bit falls (bit 4 on DMG / CGB normal-speed, bit 5
 * on CGB double-speed), which occurs at 512 Hz under normal operation.
 */
__shell void audio_div_apu_tick(audio_data* audio);

/**
 * Playdate audio callback function.
 */
int audio_callback(void* context, int16_t* left, int16_t* right, int len);

/*
 * Functions to generate audio for each channel into a buffer.
 */
void audio_update_square(
    audio_data* restrict audio, int16_t* left, int16_t* right, const bool ch2, int len
);
void audio_update_wave(audio_data* restrict audio, int16_t* left, int16_t* right, int len);
void audio_update_noise(audio_data* restrict audio, int16_t* left, int16_t* right, int len);

/**
 * Generate audio samples with accurate frame-sequencer tracking.
 * Breaks the batch into sub-chunks aligned to the DIV-APU tick rate
 * (512 Hz) and advances the frame sequencer between sub-chunks so
 * envelope, sweep, and length counters update at hardware-correct
 * rates even when generating large batches for ring-buffer sync.
 */
__shell void audio_generate_accurate(
    audio_data* restrict audio, int16_t* left, int16_t* right, int len
);

/**
 * Advance envelope and sweep state by one frame's worth of samples
 * (audio_sample_rate / 60). No-op in Accurate mode (sound_mode == 2)
 * where the CPU thread handles this via audio_div_apu_tick().
 */
void audio_tick_env_fast(audio_data* audio);

unsigned audio_get_state_size(void);
void audio_state_save(void* buff);
void audio_state_load(const void* buff);
