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

#define CB_DEBUG false
#define CB_DEBUG_UPDATED_ROWS false
#define ENABLE_RENDER_PROFILER false

#define CB_LCD_WIDTH 320
#define CB_LCD_HEIGHT 240
#define CB_LCD_ROWSIZE 40

#define CB_LCD_X 40  // multiple of 8
#define CB_LCD_Y 0

#define CB_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define CB_MIN(x, y) (((x) < (y)) ? (x) : (y))

#define CRC_CACHE_FILE "crc_cache.json"

#define LOGO_TEXT_VERTICAL_GAP 20

extern const uint8_t CB_patterns[4][4][4];

extern const char* CB_savesPath;
extern const char* CB_gamesPath;
extern const char* CB_coversPath;
extern const char* CB_statesPath;
extern const char* CB_settingsPath;
extern const char* CB_globalPrefsPath;
extern const char* CB_patchesPath;

typedef struct
{
    char* short_name;
    char* detailed_name;
    uint32_t crc32;
    bool failedToOpenROM;
} CB_FetchedNames;

typedef enum
{
    CB_UISound_Navigate,  // For up/down movement
    CB_UISound_Confirm    // For selection/changing a value
} CB_UISound;

typedef enum
{
    CB_COVER_ART_SUCCESS,
    CB_COVER_ART_ERROR_LOADING,
    CB_COVER_ART_INVALID_IMAGE,
    CB_COVER_ART_FILE_NOT_FOUND
} CB_CoverArtStatus;

typedef struct
{
    LCDBitmap* bitmap;
    int original_width;
    int original_height;
    int scaled_width;
    int scaled_height;
    CB_CoverArtStatus status;
} CB_LoadedCoverArt;

typedef enum
{
    PROGRESS_STYLE_PERCENT,
    PROGRESS_STYLE_FRACTION
} CB_ProgressStyle;

char* cb_strdup(const char* string);

char* cb_basename(const char* filename, bool stripExtension);
char* cb_save_filename(const char* filename, bool isRecovery);
char* cb_extract_fs_error_code(const char* filename);
char* common_article_form(const char* input);

float cb_easeInOutQuad(float x);

// like playdate->file->listfiles, but can filter by file type (pdx/data)
int cb_listfiles(
    const char* path, void (*callback)(const char* filename, void* userdata), void* userdata,
    int showhidden, FileOptions fopts
);

int cb_file_exists(const char* path, FileOptions fopts);

int cb_compare_games_by_display_name(const void* a, const void* b);
int cb_compare_strings(const void* a, const void* b);

int cb_calculate_progress_max_width(LCDFont* font, CB_ProgressStyle style, size_t total_items);

void cb_sanitize_string_for_filename(char* str);
void cb_sort_games_array(CB_Array* games_array);

void cb_draw_logo_screen_and_display(LCDFont* font, const char* message);
void cb_draw_logo_screen_to_buffer(LCDFont* font, const char* message);
void cb_draw_logo_screen_centered_split(
    LCDFont* font, const char* static_text, const char* dynamic_text, int dynamic_text_max_width
);

void cb_fillRoundRect(PDRect rect, int radius, LCDColor color);
void cb_drawRoundRect(PDRect rect, int radius, int lineWidth, LCDColor color);

// result must be user-free'd. returns NULL on error.
char* cb_read_entire_file(const char* path, size_t* o_size, unsigned flags);

// returns false on error
bool cb_write_entire_file(const char* path, const void* data, size_t size);

void* cb_malloc(size_t size);
void* cb_realloc(void* ptr, size_t size);
void* cb_calloc(size_t count, size_t size);
void cb_free(void* ptr);

size_t cb_strlen(const char* s);
char* cb_strrchr(const char* s, int c);
int cb_strcmp(const char* s1, const char* s2);

// returns false on failure
bool cb_calculate_crc32(const char* filepath, FileOptions fopts, uint32_t* crc);

uint32_t crc32_for_buffer(const unsigned char* buf, size_t len);

char* cb_find_cover_art_path_from_list(
    const CB_Array* available_covers, const char* rom_basename_no_ext,
    const char* rom_clean_basename_no_ext
);

CB_FetchedNames cb_get_titles_from_db(const char* fullpath);
CB_FetchedNames cb_get_titles_from_db_by_crc(uint32_t crc);
char* cb_url_encode_for_github_raw(const char* str);

char* cb_game_config_path(const char* rom_filename);

// allocate-print-to-string
char* aprintf(const char* fmt, ...);

// caller-freed
char* en_human_time(unsigned seconds);

bool string_has_descenders(const char* str);

CB_LoadedCoverArt cb_load_and_scale_cover_art_from_path(
    const char* cover_path, int max_target_width, int max_target_height
);

void cb_free_loaded_cover_art_bitmap(CB_LoadedCoverArt* art_result);
void cb_clear_global_cover_cache(void);

void cb_play_ui_sound(CB_UISound sound);

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
#define CB_ASSERT(x) \
    if (!(x))        \
        playdate->system->error("ASSERTION FAILED: %s", #x);
#else
#define CPU_VALIDATE 0
#define CB_ASSERT(x)
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

// malloc array and memset to zero
#define allocza(Type, N) ((Type*)mallocz(sizeof(Type) * (N)));

// non-negative floating-point modulo
float nnfmodf(float a, float b);

void memswap(void* a, void* b, size_t size);

#endif /* utility_h */
