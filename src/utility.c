//
//  utility.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "utility.h"

#include "app.h"
#include "jparse.h"
#include "preferences.h"
#include "scenes/library_scene.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

PlaydateAPI* playdate;

const char* CB_savesPath = "saves";
const char* CB_gamesPath = "games";
const char* CB_coversPath = "covers";
const char* CB_statesPath = "states";
const char* CB_settingsPath = "settings";
const char* CB_globalPrefsPath = "preferences.json";
const char* CB_patchesPath = "patches";

/* clang-format off */
const clalign uint8_t CB_patterns[4][4][4] = {
    {
        {1, 1, 1, 1},
        {1, 1, 1, 1},
        {1, 1, 1, 1},
        {1, 1, 1, 1}
    },
    {
        {0, 1, 1, 1},
        {1, 1, 0, 1},
        {0, 1, 1, 1},
        {1, 1, 0, 1}
    },
    {
        {0, 0, 0, 1},
        {0, 1, 0, 0},
        {0, 0, 0, 1},
        {0, 1, 0, 0}
    },
    {
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}
    }
};
/* clang-format on */

char* cb_strdup(const char* string)
{
    if (!string)
        return NULL;

    char* copied = cb_malloc(strlen(string) + 1);
    strcpy(copied, string);
    return copied;
}

size_t cb_strlen(const char* s)
{
    return strlen(s);
}

char* cb_strrchr(const char* s, int c)
{
    return strrchr(s, c);
}

int cb_strcmp(const char* s1, const char* s2)
{
    return strcmp(s1, s2);
}

static const char* en_plural(int n)
{
    return (n == 1 || n == -1) ? "" : "s";
}

static const char* en_pluraly(int n)
{
    return (n == 1 || n == -1) ? "y" : "ies";
}

char* en_human_time(unsigned secondsAgo)
{
    char* tr;
    if (secondsAgo < 60)
    {
        playdate->system->formatString(&tr, "%d  second%s", secondsAgo, en_plural(secondsAgo));
        return tr;
    }
    int minutesAgo = (secondsAgo / 60);
    if (minutesAgo < 60)
    {
        playdate->system->formatString(&tr, "%d  minute%s", minutesAgo, en_plural(minutesAgo));
        return tr;
    }
    int hoursAgo = (minutesAgo / 60);
    if (hoursAgo < 24)
    {
        playdate->system->formatString(&tr, "%d  hour%s", hoursAgo, en_plural(hoursAgo));
        return tr;
    }
    int daysAgo = (hoursAgo / 24);
    int weeksAgo = (daysAgo / 7);

    // approximate, but good enough
    int monthsAgo = (daysAgo / 30);
    int yearsAgo = (daysAgo / 365);
    int decadesAgo = (yearsAgo / 10);
    int centuriesAgo = (yearsAgo / 100);
    if (centuriesAgo)
    {
        // sure
        playdate->system->formatString(&tr, "%d  centur%s", centuriesAgo, en_pluraly(centuriesAgo));
        return tr;
    }
    if (decadesAgo)
    {
        playdate->system->formatString(&tr, "%d  decade%s", decadesAgo, en_plural(decadesAgo));
        return tr;
    }
    if (yearsAgo)
    {
        playdate->system->formatString(&tr, "%d  year%s", yearsAgo, en_plural(yearsAgo));
        return tr;
    }
    if (monthsAgo)
    {
        playdate->system->formatString(&tr, "%d  month%s", monthsAgo, en_plural(monthsAgo));
        return tr;
    }
    if (weeksAgo)
    {
        playdate->system->formatString(&tr, "%d  week%s", weeksAgo, en_plural(weeksAgo));
        return tr;
    }

    playdate->system->formatString(&tr, "%d  day%s", daysAgo, en_plural(daysAgo));
    return tr;
}

// CRC32 implementation
static uint32_t crc32_table[256];
static int crc32_table_generated = 0;

static void generate_crc32_table(void)
{
    uint32_t c;
    for (int n = 0; n < 256; n++)
    {
        c = (uint32_t)n;
        for (int k = 0; k < 8; k++)
        {
            if (c & 1)
            {
                c = 0xedb88320L ^ (c >> 1);
            }
            else
            {
                c = c >> 1;
            }
        }
        crc32_table[n] = c;
    }
    crc32_table_generated = 1;
}

static uint32_t update_crc32(uint32_t crc, const unsigned char* buf, size_t len)
{
    if (!crc32_table_generated)
    {
        generate_crc32_table();
    }
    for (size_t i = 0; i < len; i++)
    {
        crc = crc32_table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
    }
    return crc;
}

uint32_t crc32_for_buffer(const unsigned char* buf, size_t len)
{
    if (!crc32_table_generated)
    {
        generate_crc32_table();
    }
    uint32_t crc = 0xffffffffL;
    crc = update_crc32(crc, buf, len);
    return crc ^ 0xffffffffL;
}

bool cb_calculate_crc32(const char* filepath, FileOptions fopts, uint32_t* o_crc)
{
    if (!crc32_table_generated)
    {
        generate_crc32_table();
    }

    SDFile* file = playdate->file->open(filepath, fopts);
    if (!file)
    {
        playdate->system->logToConsole(
            "CRC Error: Could not open file '%s'. Error: %s", filepath, playdate->file->geterr()
        );
        return false;
    }

    uint32_t crc = 0xffffffffL;
    const int buffer_size = 4096;
    unsigned char* buffer = cb_malloc(buffer_size);
    if (!buffer)
    {
        playdate->system->logToConsole("CRC Error: Failed to allocate buffer.");
        playdate->file->close(file);
        return false;
    }

    int bytes_read;
    while ((bytes_read = playdate->file->read(file, buffer, buffer_size)) > 0)
    {
        crc = update_crc32(crc, buffer, bytes_read);
    }

    cb_free(buffer);
    playdate->file->close(file);

    *o_crc = crc ^ 0xffffffffL;
    return true;
}

char* cb_basename(const char* filename, bool stripExtension)
{
    if (filename == NULL)
    {
        return NULL;
    }

    const char* last_slash = strrchr(filename, '/');
    const char* last_backslash = strrchr(filename, '\\');
    const char* start = filename;

    if (last_slash != NULL || last_backslash != NULL)
    {
        if (last_slash != NULL && last_backslash != NULL)
        {
            start = (last_slash > last_backslash) ? last_slash + 1 : last_backslash + 1;
        }
        else if (last_slash != NULL)
        {
            start = last_slash + 1;
        }
        else
        {
            start = last_backslash + 1;
        }
    }

    if (*start == '\0')
    {
        return cb_strdup(filename);
    }

    const char* end = start + strlen(start);

    if (stripExtension)
    {
        const char* last_dot = strrchr(start, '.');
        if (last_dot != NULL && last_dot != start)
        {
            end = last_dot;
        }
    }

    size_t len = end - start;

    char* result = cb_malloc(len + 1);
    if (result == NULL)
    {
        return NULL;
    }

    strncpy(result, start, len);
    result[len] = '\0';

    return result;
}

char* cb_save_filename(const char* path, bool isRecovery)
{

    char* filename;

    char* slash = strrchr(path, '/');
    if (!slash)
    {
        filename = (char*)path;
    }
    else
    {
        filename = slash + 1;
    }

    size_t len;

    char* dot = strrchr(filename, '.');
    if (!dot || dot == filename)
    {
        len = strlen(filename);
    }
    else
    {
        len = strlen(filename) - strlen(dot);
    }

    char* filenameNoExt = cb_malloc(len + 1);
    strcpy(filenameNoExt, "");
    strncat(filenameNoExt, filename, len);

    char* suffix = "";
    if (isRecovery)
    {
        suffix = " (recovery)";
    }

    char* buffer;
    playdate->system->formatString(&buffer, "%s/%s%s.sav", CB_savesPath, filenameNoExt, suffix);

    cb_free(filenameNoExt);

    return buffer;
}

char* cb_extract_fs_error_code(const char* fileError)
{
    char* findStr = "uC-FS error: ";
    char* fsErrorPtr = strstr(fileError, findStr);
    if (fsErrorPtr)
    {
        return fsErrorPtr + strlen(findStr);
    }
    return NULL;
}

float cb_easeInOutQuad(float x)
{
    return (x < 0.5f) ? 2 * x * x : 1 - powf(-2 * x + 2, 2) * 0.5f;
}

int cb_compare_games_by_sort_name(const void* a, const void* b)
{
    CB_Game* gameA = *(CB_Game**)a;
    CB_Game* gameB = *(CB_Game**)b;

    return strcasecmp(gameA->sortName, gameB->sortName);
}

int cb_compare_strings(const void* a, const void* b)
{
    const char* str_a = *(const char**)a;
    const char* str_b = *(const char**)b;
    return strcmp(str_a, str_b);
}

void cb_sanitize_string_for_filename(char* str)
{
    if (str == NULL)
    {
        return;
    }
    char* p = str;
    while (*p)
    {
        if (*p == ' ' || *p == '(' || *p == ')' || *p == '[' || *p == ']' || *p == '{' ||
            *p == '}' || *p == '!' || *p == '?' || *p == ':' || *p == ';' || *p == ',' ||
            *p == '&' || *p == '\'')
        {
            *p = '_';
        }
        p++;
    }
}

void cb_sort_games_array(CB_Array* games_array)
{
    if (games_array != NULL && games_array->length > 1)
    {
        qsort(
            games_array->items, games_array->length, sizeof(CB_Game*), cb_compare_games_by_sort_name
        );
    }
}

char* cb_find_cover_art_path_from_list(
    const CB_Array* available_covers, const char* rom_basename_no_ext,
    const char* rom_clean_basename_no_ext
)
{
    for (int i = 0; i < available_covers->length; ++i)
    {
        const char* cover_basename = available_covers->items[i];

        if (strcmp(cover_basename, rom_clean_basename_no_ext) == 0 ||
            strcmp(cover_basename, rom_basename_no_ext) == 0)
        {
            char* found_path = NULL;
            playdate->system->formatString(&found_path, "%s/%s.pdi", CB_coversPath, cover_basename);
            return found_path;
        }
    }

    return NULL;
}

CB_LoadedCoverArt cb_load_and_scale_cover_art_from_path(
    const char* cover_path, int max_target_width, int max_target_height
)
{
    CB_LoadedCoverArt result = {
        .bitmap = NULL,
        .original_width = 0,
        .original_height = 0,
        .scaled_width = 0,
        .scaled_height = 0,
        .status = CB_COVER_ART_FILE_NOT_FOUND
    };

    if (!cover_path)
    {
        result.status = CB_COVER_ART_FILE_NOT_FOUND;
        return result;
    }

    FileStat fileStatCheck;
    if (playdate->file->stat(cover_path, &fileStatCheck) != 0)
    {
        result.status = CB_COVER_ART_FILE_NOT_FOUND;
        return result;
    }

    const char* error_str = NULL;
    LCDBitmap* original_image = playdate->graphics->loadBitmap(cover_path, &error_str);

    if (error_str)
    {
        playdate->system->logToConsole(
            "Error string from loadBitmap for %s: %s", cover_path, error_str
        );
    }

    if (original_image == NULL)
    {
        result.status = CB_COVER_ART_ERROR_LOADING;
        playdate->system->logToConsole("Failed to load bitmap: %s", cover_path);
        return result;
    }

    playdate->graphics->getBitmapData(
        original_image, &result.original_width, &result.original_height, NULL, NULL, NULL
    );

    if (result.original_width <= 0 || result.original_height <= 0)
    {
        playdate->graphics->freeBitmap(original_image);
        result.status = CB_COVER_ART_INVALID_IMAGE;
        playdate->system->logToConsole(
            "Invalid image dimensions (%dx%d) for: %s", result.original_width,
            result.original_height, cover_path
        );
        return result;
    }

    float scale;
    float scaleX = (float)max_target_width / result.original_width;
    float scaleY = (float)max_target_height / result.original_height;
    scale = (scaleX < scaleY) ? scaleX : scaleY;

    result.scaled_width = (int)roundf(result.original_width * scale);
    result.scaled_height = (int)roundf(result.original_height * scale);

    if (result.scaled_width < 1 && result.original_width > 0)
        result.scaled_width = 1;
    if (result.scaled_height < 1 && result.original_height > 0)
        result.scaled_height = 1;

    bool perform_scaling_operation = false;
    if (result.scaled_width != result.original_width ||
        result.scaled_height != result.original_height)
    {
        perform_scaling_operation = true;
    }

    if (perform_scaling_operation)
    {
        if (result.scaled_width <= 0 || result.scaled_height <= 0)
        {
            playdate->system->logToConsole(
                "Error: Calculated scaled dimensions are zero or negative "
                "(%dx%d) for %s. Original: %dx%d, Scale: %f",
                result.scaled_width, result.scaled_height, cover_path, result.original_width,
                result.original_height, (double)scale
            );
            playdate->graphics->freeBitmap(original_image);
            result.status = CB_COVER_ART_INVALID_IMAGE;
            return result;
        }

        LCDBitmap* scaled_bitmap =
            playdate->graphics->newBitmap(result.scaled_width, result.scaled_height, kColorClear);
        if (scaled_bitmap == NULL)
        {
            playdate->graphics->freeBitmap(original_image);
            result.status = CB_COVER_ART_ERROR_LOADING;
            playdate->system->logToConsole(
                "Failed to create new scaled bitmap (%dx%d) for: %s", result.scaled_width,
                result.scaled_height, cover_path
            );
            return result;
        }

        playdate->graphics->pushContext(scaled_bitmap);
        playdate->graphics->setDrawMode(kDrawModeCopy);
        playdate->graphics->drawScaledBitmap(original_image, 0, 0, scale, scale);
        playdate->graphics->popContext();

        playdate->graphics->freeBitmap(original_image);
        result.bitmap = scaled_bitmap;
    }
    else
    {
        result.bitmap = original_image;
    }

    result.status = CB_COVER_ART_SUCCESS;
    return result;
}

void cb_free_loaded_cover_art_bitmap(CB_LoadedCoverArt* art_result)
{
    if (art_result && art_result->bitmap)
    {
        playdate->graphics->freeBitmap(art_result->bitmap);
        art_result->bitmap = NULL;
    }
}

void cb_clear_global_cover_cache(void)
{
    if (CB_App->coverArtCache.rom_path)
    {
        cb_free(CB_App->coverArtCache.rom_path);
        CB_App->coverArtCache.rom_path = NULL;
    }
    cb_free_loaded_cover_art_bitmap(&CB_App->coverArtCache.art);

    CB_App->coverArtCache.art.status = CB_COVER_ART_FILE_NOT_FOUND;
}

void cb_fillRoundRect(PDRect rect, int radius, LCDColor color)
{
    int r2 = radius * 2;

    playdate->graphics->fillRect(rect.x, rect.y + radius, radius, rect.height - r2, color);
    playdate->graphics->fillRect(rect.x + radius, rect.y, rect.width - r2, rect.height, color);
    playdate->graphics->fillRect(
        rect.x + rect.width - radius, rect.y + radius, radius, rect.height - r2, color
    );

    playdate->graphics->fillEllipse(rect.x, rect.y, r2, r2, -90, 0, color);
    playdate->graphics->fillEllipse(rect.x + rect.width - r2, rect.y, r2, r2, 0, 90, color);
    playdate->graphics->fillEllipse(
        rect.x + rect.width - r2, rect.y + rect.height - r2, r2, r2, 90, 180, color
    );
    playdate->graphics->fillEllipse(rect.x, rect.y + rect.height - r2, r2, r2, -180, -90, color);
}

void cb_drawRoundRect(PDRect rect, int radius, int lineWidth, LCDColor color)
{
    int r2 = radius * 2;

    playdate->graphics->fillRect(rect.x, rect.y + radius, lineWidth, rect.height - r2, color);
    playdate->graphics->fillRect(rect.x + radius, rect.y, rect.width - r2, lineWidth, color);
    playdate->graphics->fillRect(
        rect.x + rect.width - lineWidth, rect.y + radius, lineWidth, rect.height - r2, color
    );
    playdate->graphics->fillRect(
        rect.x + radius, rect.y + rect.height - lineWidth, rect.width - r2, lineWidth, color
    );

    playdate->graphics->drawEllipse(rect.x, rect.y, r2, r2, lineWidth, -90, 0, color);
    playdate->graphics->drawEllipse(
        rect.x + rect.width - r2, rect.y, r2, r2, lineWidth, 0, 90, color
    );
    playdate->graphics->drawEllipse(
        rect.x + rect.width - r2, rect.y + rect.height - r2, r2, r2, lineWidth, 90, 180, color
    );
    playdate->graphics->drawEllipse(
        rect.x, rect.y + rect.height - r2, r2, r2, lineWidth, -180, -90, color
    );
}

/**
 * @brief Calculates the maximum pixel width for a progress indicator string.
 * @param font The font to use for the width measurement.
 * @param style The format of the indicator (percentage or fraction).
 * @param total_items The total number of items (only used for fraction style).
 * @return The calculated maximum pixel width.
 */

int cb_calculate_progress_max_width(LCDFont* font, CB_ProgressStyle style, size_t total_items)
{
    if (!font)
        return 0;

    char buffer[40];
    const char* text_to_measure = NULL;

    switch (style)
    {
    case PROGRESS_STYLE_PERCENT:
        text_to_measure = "100%";
        break;

    case PROGRESS_STYLE_FRACTION:
        snprintf(buffer, sizeof(buffer), "%zu/%zu", total_items, total_items);
        text_to_measure = buffer;
        break;

    default:
        return 0;
    }

    if (!text_to_measure)
        return 0;

    return playdate->graphics->getTextWidth(
        font, text_to_measure, strlen(text_to_measure), kUTF8Encoding, 0
    );
}

/**
 * @brief Draws the logo screen to the graphics buffer without updating the display.
 * Use this inside the main game loop; the app's central update will handle the display call.
 * @param font The font to use for the message text.
 * @param message The text to display below the logo.
 */
void cb_draw_logo_screen_to_buffer(LCDFont* font, const char* message)
{
    if (!font)
        return;

    LCDBitmap* logoBitmap = CB_App->logoBitmap;

    playdate->graphics->clear(kColorWhite);

    if (logoBitmap)
    {
        int screenWidth = LCD_COLUMNS;
        int screenHeight = LCD_ROWS;

        playdate->graphics->setFont(font);

        int logoWidth, logoHeight;
        playdate->graphics->getBitmapData(logoBitmap, &logoWidth, &logoHeight, NULL, NULL, NULL);

        int textWidth =
            playdate->graphics->getTextWidth(font, message, strlen(message), kUTF8Encoding, 0);
        int textHeight = playdate->graphics->getFontHeight(font);

        int lineSpacing = LOGO_TEXT_VERTICAL_GAP;
        int totalBlockHeight = logoHeight + lineSpacing + textHeight;
        int blockY_start = (screenHeight - totalBlockHeight) / 2;

        int logoX = (screenWidth - logoWidth) / 2;
        int logoY = blockY_start;

        int textX = (screenWidth - textWidth) / 2;
        int textY = logoY + logoHeight + lineSpacing;

        playdate->graphics->drawBitmap(logoBitmap, logoX, logoY, kBitmapUnflipped);
        playdate->graphics->drawText(message, strlen(message), kUTF8Encoding, textX, textY);
    }
    else
    {
        int textWidth =
            playdate->graphics->getTextWidth(font, message, strlen(message), kUTF8Encoding, 0);
        playdate->graphics->drawText(
            message, strlen(message), kUTF8Encoding, LCD_COLUMNS / 2 - textWidth / 2, LCD_ROWS / 2
        );
    }
}

/**
 * @brief Draws the logo screen and forces an immediate display update.
 * Use for instant feedback outside the main game loop (e.g., during initialization or file loads).
 * @param font The font to use for the message text.
 * @param message The text to display below the logo.
 */
void cb_draw_logo_screen_and_display(LCDFont* font, const char* message)
{
    cb_draw_logo_screen_to_buffer(font, message);
    playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);
    playdate->graphics->display();
}

/**
 * @brief Draws a centered message with a stable static part and a right-aligned dynamic part.
 * @param font The font to use for the message text.
 * @param static_text The static text part (e.g., "Scanning... ").
 * @param dynamic_text The dynamic text part (e.g., "(10/150)").
 * @param dynamic_text_max_width The pre-calculated maximum pixel width of the dynamic_text.
 */
void cb_draw_logo_screen_centered_split(
    LCDFont* font, const char* static_text, const char* dynamic_text, int dynamic_text_max_width
)
{
    LCDBitmap* logoBitmap = CB_App->logoBitmap;
    playdate->graphics->clear(kColorWhite);

    if (logoBitmap)
    {
        int screenWidth = LCD_COLUMNS;
        int screenHeight = LCD_ROWS;

        playdate->graphics->setFont(font);

        int logoWidth, logoHeight;
        playdate->graphics->getBitmapData(logoBitmap, &logoWidth, &logoHeight, NULL, NULL, NULL);

        int textHeight = playdate->graphics->getFontHeight(font);
        int lineSpacing = LOGO_TEXT_VERTICAL_GAP;
        int totalBlockHeight = logoHeight + lineSpacing + textHeight;
        int blockY_start = (screenHeight - totalBlockHeight) / 2;

        int logoX = (screenWidth - logoWidth) / 2;
        int logoY = blockY_start;
        int textY = logoY + logoHeight + lineSpacing;

        playdate->graphics->drawBitmap(logoBitmap, logoX, logoY, kBitmapUnflipped);

        int static_text_width = playdate->graphics->getTextWidth(
            font, static_text, strlen(static_text), kUTF8Encoding, 0
        );
        int total_width = static_text_width + dynamic_text_max_width;
        int block_start_x = (screenWidth - total_width) / 2;

        playdate->graphics->drawText(
            static_text, strlen(static_text), kUTF8Encoding, block_start_x, textY
        );

        int dynamic_text_width = playdate->graphics->getTextWidth(
            font, dynamic_text, strlen(dynamic_text), kUTF8Encoding, 0
        );
        int dynamic_text_x =
            block_start_x + static_text_width + (dynamic_text_max_width - dynamic_text_width);

        playdate->graphics->drawText(
            dynamic_text, strlen(dynamic_text), kUTF8Encoding, dynamic_text_x, textY
        );
    }
    else
    {
        char* full_message = aprintf("%s%s", static_text, dynamic_text ? dynamic_text : "");
        int textWidth = playdate->graphics->getTextWidth(
            font, full_message, strlen(full_message), kUTF8Encoding, 0
        );
        playdate->graphics->drawText(
            full_message, strlen(full_message), kUTF8Encoding, LCD_COLUMNS / 2 - textWidth / 2,
            LCD_ROWS / 2
        );
        cb_free(full_message);
    }
}

void* cb_malloc(size_t size)
{
    return playdate->system->realloc(NULL, size);
}

void* cb_realloc(void* ptr, size_t size)
{
    return playdate->system->realloc(ptr, size);
}

void* cb_calloc(size_t count, size_t size)
{
    return memset(cb_malloc(count * size), 0, count * size);
}

void cb_free(void* ptr)
{
    if (ptr)
    {
        playdate->system->realloc(ptr, 0);
    }
}

void setCrankSoundsEnabled(bool enabled)
{
    static int was_enabled = -1;
    if (was_enabled == enabled)
        return;

    playdate->system->setCrankSoundsDisabled(!enabled);

    was_enabled = enabled;
}

char* aprintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char* out;
    playdate->system->vaFormatString(&out, fmt, args);

    va_end(args);
    return out;
}

void cb_play_ui_sound(CB_UISound sound)
{
    if (!preferences_ui_sounds || !CB_App->clickSynth)
    {
        return;
    }

    switch (sound)
    {
    case CB_UISound_Navigate:
        playdate->sound->synth->playNote(
            CB_App->clickSynth, 1480.0f - (rand() % 32), 0.13f, 0.07f, 0
        );
        break;

    case CB_UISound_Confirm:
        playdate->sound->synth->playNote(
            CB_App->clickSynth, 880.0f + (rand() % 32), 0.18f, 0.1f, 0
        );
        break;
    }
}

char* cb_read_entire_file(const char* path, size_t* o_size, unsigned flags)
{
    SDFile* file = playdate->file->open(path, flags);
    char* dat;
    char* out;
    int size;
    if (!file)
        return NULL;

    if (playdate->file->seek(file, 0, SEEK_END) < 0)
        goto fail;

    size = playdate->file->tell(file);
    if (size < 0)
        goto fail;

    if (o_size)
        *o_size = size;

    if (playdate->file->seek(file, 0, SEEK_SET))
        goto fail;

    dat = cb_malloc(size + 1);
    if (!dat)
        goto fail;

    out = dat;
    while (size > 0)
    {
        int read = playdate->file->read(file, out, size);
        if (read <= 0)
            goto fail_free_dat;

        size -= read;
        out += read;
    }

    // ensure terminal 0
    *out = 0;

    playdate->file->close(file);
    return dat;

fail_free_dat:
    cb_free(dat);

fail:
    playdate->file->close(file);
    return NULL;
}

void memswap(void* a, void* b, size_t size)
{
    unsigned char temp;
    for (size_t i = 0; i < size; i++)
    {
        temp = ((unsigned char*)a)[i];
        ((unsigned char*)a)[i] = ((unsigned char*)b)[i];
        ((unsigned char*)b)[i] = temp;
    }
}

bool cb_write_entire_file(const char* path, const void* data, size_t size)
{
    SDFile* file = playdate->file->open(path, kFileWrite);
    if (!file)
        return false;

    while (size > 0)
    {
        int written = playdate->file->write(file, data, size);
        if (written <= 0)
            goto fail;

        size -= written;
        data += written;
    }

    playdate->file->close(file);
    return true;

fail:
    playdate->file->close(file);
    return false;
}

bool startswith(const char* str, const char* prefix)
{
    if (!str || !prefix)
        return false;

    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);

    if (prefix_len > str_len)
        return false;

    return strncmp(str, prefix, prefix_len) == 0;
}

bool endswith(const char* str, const char* suffix)
{
    if (!str || !suffix)
        return false;

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len)
        return false;

    return strncmp(str + (str_len - suffix_len), suffix, suffix_len) == 0;
}

bool startswithi(const char* str, const char* prefix)
{
    if (!str || !prefix)
        return false;

    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);

    if (prefix_len > str_len)
        return false;

    return strncasecmp(str, prefix, prefix_len) == 0;
}

bool endswithi(const char* str, const char* suffix)
{
    if (!str || !suffix)
        return false;

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len)
        return false;

    return strncasecmp(str + (str_len - suffix_len), suffix, suffix_len) == 0;
}

int cb_file_exists(const char* path, FileOptions fopts)
{
    SDFile* file = playdate->file->open(path, fopts);

    if (!file)
        return false;

    playdate->file->close(file);
    return true;
}

struct listfiles_ud
{
    void* ud;
    void (*callback)(const char* filename, void* userdata);
    const char* path;
    FileOptions opts;
};

static void process_file(const char* path, void* ud)
{
    struct listfiles_ud* lud = ud;

    // TODO: strip `/` from lud->path
    char* fullpath = aprintf("%s/%s", lud->path, path);

    SDFile* file = playdate->file->open(fullpath, lud->opts);
    cb_free(fullpath);
    if (!file)
        return;
    playdate->file->close(file);

    lud->callback(path, lud->ud);
}

int cb_listfiles(
    const char* path, void (*callback)(const char* filename, void* userdata), void* ud,
    int showhidden, FileOptions fopts
)
{
    struct listfiles_ud lud;
    lud.ud = ud;
    lud.callback = callback;
    lud.path = path;
    lud.opts = fopts;

    return playdate->file->listfiles(path, process_file, &lud, showhidden);
}

char* strltrim(const char* str)
{
    if (str == NULL)
        return NULL;

    while (*str != '\0' && (*str == '\n' || *str == ' ' || *str == '\t'))
        str++;

    return (char*)str;
}

char* cb_url_encode_for_github_raw(const char* str)
{
    if (!str)
        return NULL;

    int space_count = 0;
    for (const char* p = str; *p; p++)
    {
        if (*p == ' ')
        {
            space_count++;
        }
    }

    size_t new_len = strlen(str) + space_count * 2;
    char* encoded = cb_malloc(new_len + 1);
    if (!encoded)
        return NULL;

    const char* p_in = str;
    char* p_out = encoded;
    while (*p_in)
    {
        if (*p_in == ' ')
        {
            *p_out++ = '%';
            *p_out++ = '2';
            *p_out++ = '0';
        }
        else
        {
            *p_out++ = *p_in;
        }
        p_in++;
    }
    *p_out = '\0';
    return encoded;
}

CB_FetchedNames cb_get_titles_from_db_by_crc(uint32_t crc)
{
    CB_FetchedNames names = {NULL, NULL, 0, false};

    char crc_string_upper[9];
    char crc_string_lower[9];

    snprintf(crc_string_upper, sizeof(crc_string_upper), "%08lX", (unsigned long)crc);
    snprintf(crc_string_lower, sizeof(crc_string_lower), "%08lx", (unsigned long)crc);

    char db_filename[32];
    snprintf(db_filename, sizeof(db_filename), "db/%.2s.json", crc_string_lower);

    char* json_string = cb_read_entire_file(db_filename, NULL, kFileRead | kFileReadData);
    if (!json_string)
    {
        return names;
    }

    json_value db_json;
    if (!parse_json_string(json_string, &db_json))
    {
        cb_free(json_string);
        return names;
    }
    cb_free(json_string);

    if (db_json.type == kJSONTable)
    {
        json_value game_entry = json_get_table_value(db_json, crc_string_upper);
        if (game_entry.type == kJSONTable)
        {
            json_value short_val = json_get_table_value(game_entry, "short");
            if (short_val.type == kJSONString && short_val.data.stringval)
            {
                names.short_name = cb_strdup(short_val.data.stringval);
            }

            json_value long_val = json_get_table_value(game_entry, "long");
            if (long_val.type == kJSONString && long_val.data.stringval)
            {
                names.detailed_name = cb_strdup(long_val.data.stringval);
            }
        }
    }

    free_json_data(db_json);
    return names;
}

CB_FetchedNames cb_get_titles_from_db(const char* fullpath)
{
    CB_FetchedNames names = {NULL, NULL, 0, true};

    uint32_t crc;
    names.failedToOpenROM = !cb_calculate_crc32(fullpath, kFileReadDataOrBundle, &crc);

    if (names.failedToOpenROM)
    {
        return names;
    }

    CB_FetchedNames names_from_db = cb_get_titles_from_db_by_crc(crc);
    names_from_db.crc32 = crc;
    names_from_db.failedToOpenROM = false;

    return names_from_db;
}

static char* articles[] = {", The", ", Las", ", A",   ", Le",  ", La", ", Los", ", An",
                           ", Les", ", Der", ", Die", ", Das", ", Un", NULL};

// arranges names like `Black Onyx, The (Japan)` -> `The Black Onyx (Japan)`
char* common_article_form(const char* input)
{
    // Find the first occurrence of " - " or " ("
    const char* split_pos = NULL;
    const char* dash_pos = strstr(input, " - ");
    const char* paren_pos = strstr(input, " (");

    if (dash_pos && paren_pos)
    {
        split_pos = (dash_pos < paren_pos) ? dash_pos : paren_pos;
    }
    else if (dash_pos)
    {
        split_pos = dash_pos;
    }
    else if (paren_pos)
    {
        split_pos = paren_pos;
    }

    if (!split_pos)
    {
        split_pos = input + strlen(input);
    }

    // split into A and B at split_pos
    size_t a_len = split_pos - input;
    char* a_part = cb_malloc(a_len + 1);
    if (!a_part)
        return cb_strdup(input);

    strncpy(a_part, input, a_len);
    a_part[a_len] = '\0';

    const char* b_part = split_pos;
    size_t b_len = strlen(b_part);

    // Check if A ends with any article
    for (int i = 0; articles[i] != NULL; i++)
    {
        size_t article_len = strlen(articles[i]);
        if (a_len >= article_len && strcmp(a_part + a_len - article_len, articles[i]) == 0)
        {

            // matching article found
            size_t new_a_len = a_len - article_len;
            a_part[new_a_len] = 0;

            size_t result_len = (article_len - 2) + 1 + new_a_len + b_len;
            char* result = cb_malloc(result_len + 1);
            if (!result)
            {
                cb_free(a_part);
                return cb_strdup(input);
            }

            char* p = result;
            memcpy(p, articles[i] + 2, article_len - 2);
            p += article_len - 2;
            *p++ = ' ';
            memcpy(p, a_part, new_a_len);
            p += new_a_len;
            memcpy(p, b_part, b_len);
            p += b_len;
            *p = '\0';

            cb_free(a_part);
            return result;
        }
    }

    cb_free(a_part);
    return cb_strdup(input);
}

#define initialSpoolErrorMsg "The following error(s) occurred:"

static size_t spoolC = 0;
char* spoolText = NULL;

// queue an error to show the user later
void spoolError(const char* fmt, ...)
{
    if (!spoolText)
        spoolText = cb_strdup(initialSpoolErrorMsg);

    va_list args;
    char str[2048];

    va_start(args, fmt);
    vsnprintf(str, sizeof(str), fmt, args);
    va_end(args);

    spoolC++;
    spoolText = cb_realloc(spoolText, strlen(spoolText) + strlen("\n\n") + strlen(str) + 1);
    if (!spoolText)
        return;

    strcpy(spoolText + strlen(spoolText), "\n\n");
    strcpy(spoolText + strlen(spoolText), str);
}

size_t getSpooledErrors(void)
{
    return spoolC;
}
const char* getSpooledErrorMessage(void)
{
    return spoolText;
}

void freeSpool(void)
{
    cb_free(spoolText);
    spoolText = NULL;
    spoolC = 0;
}

void* mallocz(size_t size)
{
    void* v = cb_malloc(size);
    if (!v)
        return NULL;

    memset(v, 0, size);
    return v;
}

float nnfmodf(float a, float b)
{
    float mod = fmodf(a, b);
    return mod >= 0 ? mod : mod + b;
}

bool string_has_descenders(const char* str)
{
    if (!str)
        return false;
    return strpbrk(str, "gjpqy") != NULL;
}
