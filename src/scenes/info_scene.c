#include "info_scene.h"

#include "../app.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// pixels per degree
#define CRANK_RATE 1.1f

// pixels per second
#define SCROLL_RATE 80.3f
#define SCROLL_INDICATOR_WIDTH 2
#define SCROLL_INDICATOR_MIN_HEIGHT 10

// The height of a blank line in pixels.
#define EMPTY_LINE_HEIGHT 15

// Extra vertical space to add after a list item.
#define BULLET_POINT_SPACING 5

// Height of the header bar
#define HEADER_HEIGHT 18

// Helper to detect if a line is a list item and return its prefix length
static bool get_list_item_prefix_len(const char* text, int text_len, int* out_prefix_len)
{
    if (text_len <= 0)
        return false;

    // Check for numbered list (e.g., "1. ", "12. ")
    const char* p = text;
    if (isdigit((unsigned char)*p))
    {
        const char* start = p;
        while (isdigit((unsigned char)*p) && (p - text < text_len))
        {
            p++;
        }
        if ((p - text < text_len - 1) && *p == '.' && *(p + 1) == ' ')
        {
            *out_prefix_len = (p - start) + 2;
            return true;
        }
    }

    // Check for standard bullet point
    if (text_len >= 2 && strncmp(text, "- ", 2) == 0)
    {
        *out_prefix_len = 2;
        return true;
    }

    *out_prefix_len = 0;
    return false;
}

static void CB_InfoScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    CB_InfoScene* infoScene = object;
    LCDFont* font = CB_App->bodyFont;
    playdate->graphics->setFont(font);

    // Header height is 0 if no title is provided
    int header_height = 0;
    if (infoScene->title && *infoScene->title)
    {
        header_height = HEADER_HEIGHT;
    }

    float dt = UINT32_AS_FLOAT(u32enc_dt);

    int margin = 14;
    int top_margin = margin + header_height;
    int width = LCD_COLUMNS - margin * 2;
    int tracking = 0;
    int extraLeading = 0;

    infoScene->scroll += playdate->system->getCrankChange() * CRANK_RATE;
    int buttonsDown = CB_App->buttons_down;
    int scrollDir = !!(buttonsDown & kButtonDown) - !!(buttonsDown & kButtonUp);
    infoScene->scroll += scrollDir * dt * SCROLL_RATE;

    // --- Find the widest list prefix to align all list items ---
    int max_prefix_width = 0;
    const char* text_ptr = infoScene->text;
    while (*text_ptr)
    {
        const char* next_newline = strchr(text_ptr, '\n');
        int line_len = next_newline ? (next_newline - text_ptr) : strlen(text_ptr);

        int prefix_len = 0;
        if (get_list_item_prefix_len(text_ptr, line_len, &prefix_len))
        {
            int prefix_width = playdate->graphics->getTextWidth(
                font, text_ptr, prefix_len, kUTF8Encoding, tracking
            );
            if (prefix_width > max_prefix_width)
            {
                max_prefix_width = prefix_width;
            }
        }

        if (next_newline)
        {
            text_ptr = next_newline + 1;
        }
        else
        {
            break;
        }
    }

    // --- Calculate total text height ---
    float total_text_height = 0.0f;
    text_ptr = infoScene->text;

    while (*text_ptr)
    {
        const char* next_newline = strchr(text_ptr, '\n');
        int line_len = next_newline ? (next_newline - text_ptr) : strlen(text_ptr);

        if (line_len == 0)
        {
            total_text_height += EMPTY_LINE_HEIGHT;
        }
        else
        {
            int current_indent = 0;
            const char* text_to_measure = text_ptr;
            int len_to_measure = line_len;

            int prefix_len = 0;
            bool is_list = get_list_item_prefix_len(text_ptr, line_len, &prefix_len);

            if (is_list)
            {
                current_indent = max_prefix_width;  // Use max width for consistent indent
                text_to_measure += prefix_len;
                len_to_measure -= prefix_len;
            }

            float line_height = playdate->graphics->getTextHeightForMaxWidth(
                font, text_to_measure, len_to_measure, width - current_indent, kUTF8Encoding,
                kWrapWord, tracking, extraLeading
            );
            total_text_height += line_height;

            if (is_list)
            {
                bool is_last_line = (next_newline == NULL) || (*(next_newline + 1) == '\0');
                if (!is_last_line)
                {
                    total_text_height += BULLET_POINT_SPACING;
                }
            }
        }

        if (next_newline)
        {
            text_ptr = next_newline + 1;
        }
        else
        {
            break;
        }
    }

    // --- SCROLLBAR LOGIC ---
    float visible_height = LCD_ROWS - (top_margin + margin);
    if (total_text_height > visible_height)
    {
        float max_scroll = total_text_height - visible_height;
        if (infoScene->scroll > max_scroll)
            infoScene->scroll = max_scroll;
        if (infoScene->scroll < 0)
            infoScene->scroll = 0;
    }
    else
    {
        infoScene->scroll = 0;
    }

    // --- Draw everything ---
    playdate->graphics->clear(kColorWhite);

    // --- Draw Header (only if title exists) ---
    if (header_height > 0)
    {
        const char* name = infoScene->title;
        playdate->graphics->setFont(CB_App->labelFont);
        int nameWidth = playdate->graphics->getTextWidth(
            CB_App->labelFont, name, strlen(name), kUTF8Encoding, 0
        );
        int textX = LCD_COLUMNS / 2 - nameWidth / 2;
        int fontHeight = playdate->graphics->getFontHeight(CB_App->labelFont);

        int vertical_offset = string_has_descenders(name) ? 1 : 2;
        int textY = ((header_height - fontHeight) / 2) + vertical_offset;

        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, header_height, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeFillWhite);
        playdate->graphics->drawText(name, strlen(name), kUTF8Encoding, textX, textY);
    }

    if (header_height > 0)
    {
        playdate->graphics->setClipRect(0, header_height, LCD_COLUMNS, LCD_ROWS - header_height);
    }

    // --- Draw Text Content ---
    playdate->graphics->setDrawMode(kDrawModeFillBlack);
    playdate->graphics->setFont(font);
    float current_y = top_margin - infoScene->scroll;
    text_ptr = infoScene->text;

    while (*text_ptr)
    {
        const char* next_newline = strchr(text_ptr, '\n');
        int line_len = next_newline ? (next_newline - text_ptr) : strlen(text_ptr);

        if (line_len == 0)
        {
            current_y += EMPTY_LINE_HEIGHT;
        }
        else
        {
            const char* text_to_draw = text_ptr;
            int current_indent = 0;

            int prefix_len = 0;
            bool is_list = get_list_item_prefix_len(text_ptr, line_len, &prefix_len);

            if (is_list)
            {
                // Draw the list prefix (e.g., "1. ") at the start
                playdate->graphics->drawText(
                    text_ptr, prefix_len, kUTF8Encoding, (int)margin, (int)current_y
                );

                // Set the uniform indent for the text block
                current_indent = max_prefix_width;
                text_to_draw += prefix_len;
                line_len -= prefix_len;
            }

            int line_height = playdate->graphics->getTextHeightForMaxWidth(
                font, text_to_draw, line_len, width - current_indent, kUTF8Encoding, kWrapWord,
                tracking, extraLeading
            );

            playdate->graphics->drawTextInRect(
                text_to_draw, line_len, kUTF8Encoding, margin + current_indent, (int)current_y,
                width - current_indent, line_height, kWrapWord, kAlignTextLeft
            );

            current_y += line_height;

            if (is_list)
            {
                bool is_last_line = (next_newline == NULL) || (*(next_newline + 1) == '\0');
                if (!is_last_line)
                {
                    current_y += BULLET_POINT_SPACING;
                }
            }
        }

        if (next_newline)
        {
            text_ptr = next_newline + 1;
        }
        else
        {
            break;
        }
    }

    if (header_height > 0)
    {
        playdate->graphics->clearClipRect();
    }

    // --- Draw Scrollbar ---
    if (total_text_height > visible_height)
    {
        int scrollAreaY = top_margin;
        int scrollAreaHeight = (int)visible_height;

        float handleHeight_f = (float)scrollAreaHeight * (visible_height / total_text_height);
        int handleHeight = (int)fmaxf(handleHeight_f, SCROLL_INDICATOR_MIN_HEIGHT);

        int handleY =
            scrollAreaY + (int)((float)scrollAreaHeight * (infoScene->scroll / total_text_height));

        int indicatorX = LCD_COLUMNS - SCROLL_INDICATOR_WIDTH - 2;  // 2px inset

        playdate->graphics->fillRect(
            indicatorX - 1, handleY - 1, SCROLL_INDICATOR_WIDTH + 2, handleHeight + 2, kColorWhite
        );

        playdate->graphics->fillRect(
            indicatorX, handleY, SCROLL_INDICATOR_WIDTH, handleHeight, kColorBlack
        );
    }

    playdate->graphics->display();

    if (buttonsDown & (kButtonB | kButtonA))
    {
        if (infoScene->canClose)
        {
            CB_dismiss(infoScene->scene);
        }
    }
}

static void CB_InfoScene_free(void* object)
{
    CB_InfoScene* infoScene = object;
    cb_free(infoScene->title);

    if (infoScene->text && !infoScene->textIsStatic)
    {
        cb_free(infoScene->text);
    }

    CB_Scene_free(infoScene->scene);
    cb_free(infoScene);
}

CB_InfoScene* CB_InfoScene_new(const char* title, const char* text)
{
    CB_InfoScene* infoScene = cb_malloc(sizeof(CB_InfoScene));
    if (!infoScene)
        return NULL;
    memset(infoScene, 0, sizeof(*infoScene));
    playdate->system->getCrankChange();

    CB_Scene* scene = CB_Scene_new();
    infoScene->scene = scene;
    infoScene->title = title ? cb_strdup(title) : NULL;
    infoScene->text = text ? cb_strdup(text) : NULL;

    // Trim any trailing whitespace from the text to prevent calculation errors.
    if (infoScene->text)
    {
        int len = strlen(infoScene->text);
        while (len > 0 && isspace((unsigned char)infoScene->text[len - 1]))
        {
            infoScene->text[--len] = '\0';
        }
    }

    infoScene->canClose = true;
    infoScene->textIsStatic = false;
    scene->managedObject = infoScene;
    scene->update = (void*)CB_InfoScene_update;
    scene->free = (void*)CB_InfoScene_free;
    return infoScene;
}
