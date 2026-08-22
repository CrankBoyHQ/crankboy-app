/*
 * minigb_apu is released under the terms listed within the LICENSE file.
 * Game Boy APU emulator, based on MiniGBS by Alex Baines:
 * https://github.com/baines/MiniGBS
 */

#pragma once

#include "../../src/app.h"

#include <stdbool.h>
#include <stdint.h>

// Fallback when included before peanut_gb.h (which owns the real typedef).
// Two-level paste: PGB_VERSION must expand before token-pasting.
#ifndef PEANUT_GB_H
#include "../pgb/pgb_version.h"
#define PGB_AD_(X, Y) X##Y
#define PGB_AD(X, Y) PGB_AD_(X, Y)
typedef struct PGB_AD(audio_data_v, PGB_VERSION) audio_data;
#endif

// Calculating VSYNC.
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

/* Read audio register at addr. */
uint8_t audio_read(audio_data* audio, const uint16_t addr);

/* Write val to audio register addr. apu_count is the emulator cycle counter,
 * reset per frame. */
void audio_write(audio_data* audio, const uint16_t addr, const uint8_t val, uint32_t apu_count);

/* Initialise audio driver. */
void audio_init(audio_data* audio);

/* Debug: skip rendering per channel (bit0=CH1,1=CH2,2=CH3,
 * 3=CH4). Freezes channel state while set; measurement only. */
void audio_set_skip_mask(uint8_t m);

/* Advance the DIV-APU frame sequencer one step (512 Hz DIV falling edge;
 * bit 4 DMG/CGB normal speed, bit 5 CGB double speed). */
__shell void audio_div_apu_tick(audio_data* audio);

__shell void __apu_div_tick_detect(audio_data* audio, uint8_t old_div, uint8_t inc, unsigned mask);

/* Playdate audio callback. */
int audio_callback(void* context, int16_t* left, int16_t* right, int len);

// Functions to generate audio for each channel into a buffer.
void audio_update_square(
    audio_data* restrict audio, int16_t* left, int16_t* right, const bool ch2, int len
);
void audio_update_wave(audio_data* restrict audio, int16_t* left, int16_t* right, int len);
void audio_update_noise(audio_data* restrict audio, int16_t* left, int16_t* right, int len);

/* Generate audio with cycle-accurate write replay and 512 Hz
 * frame-sequencer tracking (accurate sound mode). */
__shell void audio_generate_accurate(
    audio_data* restrict audio, int16_t* left, int16_t* right, int len
);

__shell void audio_reset_replay_state(audio_data* audio);

/* Record an emulated frame's end in the write-event stream (accurate mode).
 * Called by the CPU core at the end of gb_run_frame. */
__shell void audio_note_frame_end(audio_data* audio, uint32_t apu_count);

unsigned audio_get_state_size(void);
void audio_state_save(void* buff);
void audio_state_load(const void* buff);
