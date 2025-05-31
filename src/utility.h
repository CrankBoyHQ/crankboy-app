//
//  utility.h
//  PlayGB
//
//  Created by Matteo D'Ignazio on 14/05/22.
//

#ifndef utility_h
#define utility_h

#include <stdbool.h>
#include <stdio.h>

#include "pd_api.h"

extern PlaydateAPI *playdate;

#define PGB_DEBUG false
#define PGB_DEBUG_UPDATED_ROWS false

#define PGB_LCD_WIDTH 320
#define PGB_LCD_HEIGHT 240
#define PGB_LCD_ROWSIZE 40

#define PGB_LCD_X 32  // multiple of 8
#define PGB_LCD_Y 0

#define PGB_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define PGB_MIN(x, y) (((x) < (y)) ? (x) : (y))

extern const uint8_t PGB_patterns[4][4][4];

extern const char *PGB_savesPath;
extern const char *PGB_gamesPath;

char *string_copy(const char *string);

char *pgb_save_filename(const char *filename, bool isRecovery);
char *pgb_extract_fs_error_code(const char *filename);

float pgb_easeInOutQuad(float x);

void pgb_fillRoundRect(PDRect rect, int radius, LCDColor color);
void pgb_drawRoundRect(PDRect rect, int radius, int lineWidth, LCDColor color);

void *pgb_malloc(size_t size);
void *pgb_realloc(void *ptr, size_t size);
void *pgb_calloc(size_t count, size_t size);
void pgb_free(void *ptr);

#ifdef TARGET_PLAYDATE
    #define __section__(x) __attribute__((section(x)))
#else
    #define __section__(x)
#endif

#define likely(x) (__builtin_expect(!!(x), 1))
#define unlikely(x) (__builtin_expect(!!(x), 0))

#ifdef TARGET_SIMULATOR
#define clalign
#else
#define clalign __attribute__((aligned(32)))
#endif

#endif /* utility_h */
