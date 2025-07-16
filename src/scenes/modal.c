#include "modal.h"

#include "app.h"
#include "scene.h"
#include "utility.h"

#define MODAL_ANIM_TIME 16
#define MODAL_DROP_TIME 12

void CB_Modal_update(CB_Modal* modal)
{
    if (modal->exit)
    {
        if (modal->droptimer-- <= 0)
            modal->droptimer = 0;
        if (modal->timer-- == 0)
        {
            CB_dismiss(modal->scene);
        }
    }
    else
    {
        if (++modal->timer > MODAL_ANIM_TIME)
            modal->timer = MODAL_ANIM_TIME;
        if (++modal->droptimer > MODAL_DROP_TIME)
            modal->droptimer = MODAL_DROP_TIME;
    }
    PDButtons pushed = CB_App->buttons_pressed;

    if (modal->setup == 0)
    {
        modal->setup = 1;

        // copy in what's on the screen
        uint8_t* src = playdate->graphics->getFrame();
        memcpy(modal->lcd, src, sizeof(modal->lcd));
    }

    uint8_t* lcd = playdate->graphics->getFrame();
    memcpy(lcd, modal->lcd, sizeof(modal->lcd));

    if (modal->dissolveMask)
    {
        playdate->graphics->clearBitmap(modal->dissolveMask, kColorWhite);

        int width, height, rowbytes;
        uint8_t* maskData;
        playdate->graphics->getBitmapData(
            modal->dissolveMask, &width, &height, &rowbytes, NULL, &maskData
        );

        uint32_t lfsr = 0;
        int tap2 = 5 + modal->exit;
        for (size_t y = 0; y < height; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                lfsr <<= 1;
                lfsr |= 1 & ((lfsr >> 1) ^ (lfsr >> tap2) ^ (lfsr >> 8) ^ (lfsr >> 31) ^ 1);
                if ((int)(lfsr % MODAL_ANIM_TIME) < modal->timer)
                {
                    if (((x % 2) == (y % 2)))
                    {
                        maskData[y * rowbytes + (x / 8)] &= ~(1 << (7 - (x % 8)));
                    }
                }
            }
        }

        playdate->graphics->setDrawMode(kDrawModeWhiteTransparent);
        playdate->graphics->drawBitmap(modal->dissolveMask, 0, 0, kBitmapUnflipped);
        playdate->graphics->setDrawMode(kDrawModeCopy);
    }

    playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);

    int w = modal->width;
    int x = (LCD_COLUMNS - w) / 2;
    int h = modal->height;
    float p = MIN(modal->droptimer, MODAL_DROP_TIME) / (float)MODAL_DROP_TIME;
    p = 1 - (1 - p) * sqrtf(1 - p);  // easing
    int y = -h + ((LCD_ROWS - h) / 2 + h) * p;

    int white_border_thickness = 1;
    int black_border_thickness = 2;
    int total_thickness = white_border_thickness + black_border_thickness;

    playdate->graphics->fillRect(x, y, w, h, kColorWhite);

    playdate->graphics->fillRect(
        x + white_border_thickness, y + white_border_thickness, w - (white_border_thickness * 2),
        h - (white_border_thickness * 2), kColorBlack
    );

    playdate->graphics->fillRect(
        x + total_thickness, y + total_thickness, w - (total_thickness * 2),
        h - (total_thickness * 2), kColorWhite
    );

    int m = 24;  // margin
    playdate->graphics->setFont(CB_App->bodyFont);
    if (modal->text)
    {
        // Apply a 2px vertical offset only for text-only modals
        // to achieve visual centering.
        int y_offset = (modal->options_count == 0) ? 2 : 0;
        playdate->graphics->drawTextInRect(
            modal->text, strlen(modal->text), kASCIIEncoding, x + m, y + m + y_offset, w - 2 * m,
            h - 2 * m, kWrapWord, kAlignTextCenter
        );
    }

    int spacing = w / (1 + modal->options_count);

    for (int i = 0; i < modal->options_count; ++i)
    {
        int ox = x + spacing * (i + 1);
        int oy = y + h - m - 8;
        int option_height = 20;

        if (i == modal->option_selected)
        {
            playdate->graphics->drawLine(
                ox - spacing / 3, oy + 4, ox + spacing / 3, oy + 4, 3, kColorBlack
            );
        }

        playdate->graphics->drawTextInRect(
            modal->options[i], strlen(modal->options[i]), kASCIIEncoding, ox - spacing / 2,
            oy - option_height, spacing, option_height, kWrapClip, kAlignTextCenter
        );
    }

    if (modal->exit || modal->droptimer < MODAL_DROP_TIME)
        return;
    if ((pushed & kButtonB) || (modal->options_count == 0 && (pushed & kButtonA)))
    {
        modal->exit = 1;
        modal->result = -1;
    }
    else if (pushed & kButtonA)
    {
        modal->exit = 1;
        modal->result = modal->option_selected;
    }
    else
    {
        int d = !!(pushed & kButtonRight) - !!(pushed & kButtonLeft);
        modal->option_selected += d;
        if (modal->option_selected >= modal->options_count)
            modal->option_selected = modal->options_count - 1;
        if (modal->option_selected < 0)
            modal->option_selected = 0;
    }
}

void CB_Modal_free(CB_Modal* modal)
{
    if (modal->callback)
        modal->callback(modal->ud, modal->result);

    if (modal->dissolveMask)
    {
        playdate->graphics->freeBitmap(modal->dissolveMask);
    }

    for (size_t i = 0; i < MODAL_MAX_OPTIONS; ++i)
    {
        if (modal->options[i])
            cb_free(modal->options[i]);
    }
    if (modal->text)
        cb_free(modal->text);
    CB_Scene_free(modal->scene);
    cb_free(modal);
}

CB_Modal* CB_Modal_new(char* text, char const* const* options, CB_ModalCallback callback, void* ud)
{
    CB_Modal* modal = cb_malloc(sizeof(CB_Modal));
    memset(modal, 0, sizeof(*modal));

    modal->width = 250;
    modal->height = 120;

    modal->options_count = 0;
    if (options)
        for (size_t i = 0; options[i] && i < MODAL_MAX_OPTIONS; ++i)
        {
            modal->options[i] = string_copy(options[i]);
            modal->options_count++;
        }

    if (text)
        modal->text = string_copy(text);

    CB_Scene* scene = CB_Scene_new();
    modal->scene = scene;
    scene->managedObject = modal;
    scene->update = (void*)CB_Modal_update;
    scene->free = (void*)CB_Modal_free;

    modal->callback = callback;
    modal->ud = ud;

    modal->setup = 0;

    modal->dissolveMask = playdate->graphics->newBitmap(LCD_COLUMNS, LCD_ROWS, kColorWhite);

    return modal;
}
