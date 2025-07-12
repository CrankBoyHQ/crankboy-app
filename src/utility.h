//
//  utility.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef utility_h
#define utility_h

#include "array.h"
#include "pd_api.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern PlaydateAPI* playdate;

#define PGB_DEBUG false
#define PGB_DEBUG_UPDATED_ROWS false
#define ENABLE_RENDER_PROFILER false

#define PGB_LCD_WIDTH 320
#define PGB_LCD_HEIGHT 240
#define PGB_LCD_ROWSIZE 40

#define PGB_LCD_X 40  // multiple of 8
#define PGB_LCD_Y 0

#define PGB_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define PGB_MIN(x, y) (((x) < (y)) ? (x) : (y))

#define CRC_CACHE_FILE "crc_cache.json"

extern const uint8_t PGB_patterns[4][4][4];

extern const char* PGB_savesPath;
extern const char* PGB_gamesPath;
extern const char* PGB_coversPath;
extern const char* PGB_statesPath;
extern const char* PGB_settingsPath;
extern const char* PGB_globalPrefsPath;

typedef struct
{
    char* short_name;
    char* detailed_name;
    uint32_t crc32;
    bool failedToOpenROM;
} PGB_FetchedNames;

typedef enum
{
    PGB_UISound_Navigate,  // For up/down movement
    PGB_UISound_Confirm    // For selection/changing a value
} PGB_UISound;

typedef enum
{
    PGB_COVER_ART_SUCCESS,
    PGB_COVER_ART_ERROR_LOADING,
    PGB_COVER_ART_INVALID_IMAGE,
    PGB_COVER_ART_FILE_NOT_FOUND
} PGB_CoverArtStatus;

typedef struct
{
    LCDBitmap* bitmap;
    int original_width;
    int original_height;
    int scaled_width;
    int scaled_height;
    PGB_CoverArtStatus status;
} PGB_LoadedCoverArt;

char* string_copy(const char* string);

char* pgb_basename(const char* filename, bool stripExtension);
char* pgb_save_filename(const char* filename, bool isRecovery);
char* pgb_extract_fs_error_code(const char* filename);
char* common_article_form(const char* input);

float pgb_easeInOutQuad(float x);

// like playdate->file->listfiles, but can filter by file type (pdx/data)
int pgb_listfiles(
    const char* path, void (*callback)(const char* filename, void* userdata), void* userdata,
    int showhidden, FileOptions fopts
);

int pgb_file_exists(const char* path, FileOptions fopts);

int pgb_compare_games_by_display_name(const void* a, const void* b);
int pgb_compare_strings(const void* a, const void* b);

void pgb_sanitize_string_for_filename(char* str);
void pgb_sort_games_array(PGB_Array* games_array);
void pgb_draw_logo_screen_and_display(const char* message);
void pgb_draw_logo_screen_to_buffer(const char* message);
void pgb_fillRoundRect(PDRect rect, int radius, LCDColor color);
void pgb_drawRoundRect(PDRect rect, int radius, int lineWidth, LCDColor color);

// result must be user-free'd. returns NULL on error.
char* pgb_read_entire_file(const char* path, size_t* o_size, unsigned flags);

// returns false on error
bool pgb_write_entire_file(const char* path, const void* data, size_t size);

void* pgb_malloc(size_t size);
void* pgb_realloc(void* ptr, size_t size);
void* pgb_calloc(size_t count, size_t size);
void pgb_free(void* ptr);

size_t pgb_strlen(const char* s);
char* pgb_strrchr(const char* s, int c);
int pgb_strcmp(const char* s1, const char* s2);

// returns false on failure
bool pgb_calculate_crc32(const char* filepath, FileOptions fopts, uint32_t* crc);

char* pgb_find_cover_art_path_from_list(
    const PGB_Array* available_covers, const char* rom_basename_no_ext,
    const char* rom_clean_basename_no_ext
);

PGB_FetchedNames pgb_get_titles_from_db(const char* fullpath);
PGB_FetchedNames pgb_get_titles_from_db_by_crc(uint32_t crc);
char* pgb_url_encode_for_github_raw(const char* str);

char* pgb_game_config_path(const char* rom_filename);

// allocate-print-to-string
char* aprintf(const char* fmt, ...);

// caller-freed
char* en_human_time(unsigned seconds);

PGB_LoadedCoverArt pgb_load_and_scale_cover_art_from_path(
    const char* cover_path, int max_target_width, int max_target_height
);

void pgb_free_loaded_cover_art_bitmap(PGB_LoadedCoverArt* art_result);
void pgb_clear_global_cover_cache(void);

void pgb_play_ui_sound(PGB_UISound sound);

char* strltrim(const char* str);

static inline float toward(float x, float dst, float step)
{
    if (dst > x)
    {
        x += step;
        if (x > dst)
            x = dst;
    }
    else
    {
        x -= step;
        if (x < dst)
            x = dst;
    }
    return x;
};

#define TOWARD(x, dst, step)        \
    do                              \
    {                               \
        float* a = &(x);            \
        *a = toward(*a, dst, step); \
    } while (0)

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

#ifdef TARGET_SIMULATOR
#define CPU_VALIDATE 1
#define PGB_ASSERT(x) \
    if (!(x))         \
        playdate->system->error("ASSERTION FAILED: %s", #x);
#else
#define CPU_VALIDATE 0
#define PGB_ASSERT(x)
#endif

// compute the next highest power of 2 of 32-bit v,
// or v if v is a power of 2
// https://stackoverflow.com/a/466242
static inline unsigned next_pow2(unsigned v)
{
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++v;
}

#define LAMBDA(_RESULT_TYPE_, _ARGS_, _BODY_) \
    ({                                        \
        _RESULT_TYPE_ _fn_ _ARGS_             \
        {                                     \
            _BODY_;                           \
        };                                    \
        _fn_;                                 \
    })

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define FLOAT_AS_UINT32(_f) \
    (((union {              \
         float f;           \
         uint32_t u;        \
     }){.f = (_f)})         \
         .u)

#define UINT32_AS_FLOAT(_u) \
    (((union {              \
         float f;           \
         uint32_t u;        \
     }){.u = (_u)})         \
         .f)

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

bool startswith(const char* str, const char* prefix);
bool startswithi(const char* str, const char* prefix);
bool endswith(const char* str, const char* suffix);
bool endswithi(const char* str, const char* suffix);

void setCrankSoundsEnabled(bool enabled);

// queue an error to show the user later
void spoolError(const char* fmt, ...);

size_t getSpooledErrors(void);
const char* getSpooledErrorMessage(void);
void freeSpool(void);

// malloc and memset to zero
void* mallocz(size_t size);

#define allocz(Type) ((Type*)mallocz(sizeof(Type)))

// non-negative floating-point modulo
float nnfmodf(float a, float b);

#endif /* utility_h */
