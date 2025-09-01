/**
 * minigb_apu is released under the terms listed within the LICENSE file.
 *
 * minigb_apu emulates the audio processing unit (APU) of the Game Boy. This
 * project is based on MiniGBS by Alex Baines: https://github.com/baines/MiniGBS
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "peanut_gb.h"

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

unsigned audio_get_state_size(void);
void audio_state_save(void* buff);
void audio_state_load(const void* buff);
