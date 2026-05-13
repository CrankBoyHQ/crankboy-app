#include "modal.h"

#include "../app.h"
#include "../scene.h"
#include "../utility.h"

#define MODAL_ANIM_TIME 16
#define MODAL_DROP_TIME 12

static void build_wrapped_lines(CB_Modal* modal, int text_w)
{
    if (!modal->text)
        return;

    int capacity = 8;
    int count = 0;
    CB_ModalLine* lines = (CB_ModalLine*)cb_malloc(sizeof(CB_ModalLine) * capacity);

    const char* p = modal->text;
    while (*p)
    {
        const char* nl = strchr(p, '\n');
        int line_len = nl ? (int)(nl - p) : (int)strlen(p);

        if (line_len > 0)
        {
            const char* line = p;
            int remaining = line_len;

            while (remaining > 0)
            {
                int line_w = playdate->graphics->getTextWidth(
                    CB_App->bodyFont, line, remaining, kUTF8Encoding, 0
                );

                int seg_len;
                int seg_w;
                if (line_w <= text_w)
                {
                    seg_len = remaining;
                    seg_w = line_w;
                }
                else
                {
                    int fit = 0;
                    for (int i = 1; i <= remaining; i++)
                    {
                        if (playdate->graphics->getTextWidth(
                                CB_App->bodyFont, line, i, kUTF8Encoding, 0
                            ) > text_w)
                            break;
                        fit = i;
                    }

                    int break_at = fit;
                    for (int i = fit; i > 0; i--)
                    {
                        if (line[i - 1] == ' ')
                        {
                            break_at = i;
                            break;
                        }
                    }
                    if (break_at < 1)
                        break_at = fit > 0 ? fit : 1;

                    seg_len = break_at;
                    seg_w = playdate->graphics->getTextWidth(
                        CB_App->bodyFont, line, seg_len, kUTF8Encoding, 0
                    );
                }

                if (count == capacity)
                {
                    capacity *= 2;
                    lines = (CB_ModalLine*)cb_realloc(lines, sizeof(CB_ModalLine) * capacity);
                }
                lines[count].start = line;
                lines[count].len = seg_len;
                lines[count].width = seg_w;
                count++;

                remaining -= seg_len;
                line += seg_len;
                while (remaining > 0 && *line == ' ')
                {
                    line++;
                    remaining--;
                }
                if (seg_len == 0)
                    break;  // paranoia
            }
        }
        else
        {
            if (count == capacity)
            {
                capacity *= 2;
                lines = (CB_ModalLine*)cb_realloc(lines, sizeof(CB_ModalLine) * capacity);
            }
            lines[count].start = p;
            lines[count].len = 0;
            lines[count].width = 0;
            count++;
        }

        p = nl ? nl + 1 : p + line_len;
    }

    modal->wrapped_lines = lines;
    modal->wrapped_lines_count = count;
}

void CB_Modal_update(CB_Modal* modal)
{
    ++modal->master_timer;
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

    if (modal->accept_on_dock && playdate->system->isCrankDocked())
    {
        pushed |= kButtonA;
    }

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
    int y = -h + ((LCD_ROWS - h) / 2.0f + h) * p;

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

    int m = modal->margin;
    playdate->graphics->setFont(CB_App->bodyFont);
    if (modal->text)
    {
        int line_h = playdate->graphics->getFontHeight(CB_App->bodyFont);
        int text_x = x + m;
        int text_w = w - 2 * m;

        if (!modal->wrapped_lines)
            build_wrapped_lines(modal, text_w);

        int avail_h = h - 2 * m;
        int total_text_h = modal->wrapped_lines_count * line_h;
        int y_offset = (modal->options_count == 0 && total_text_h < avail_h)
                           ? (avail_h - total_text_h) / 2
                           : 0;
        int text_y = y + m + y_offset;

        for (int i = 0; i < modal->wrapped_lines_count; ++i)
        {
            const CB_ModalLine* ln = &modal->wrapped_lines[i];
            if (ln->len > 0)
            {
                playdate->graphics->drawText(
                    ln->start, ln->len, kUTF8Encoding, text_x + (text_w - ln->width) / 2, text_y
                );
            }
            text_y += line_h;
        }
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

    if (modal->warning != CB_MODAL_WARNING_NONE && !modal->icon)
    {
        modal->icon = playdate->graphics->loadBitmap("images/warning", NULL);
    }

    if (modal->icon)
    {
        if (!modal->icon_flashing ||
            (modal->master_timer % 15 < 7 || modal->master_timer >= 4 * 15))
        {
            int iw, ih;
            playdate->graphics->getBitmapData(modal->icon, &iw, &ih, NULL, NULL, NULL);

            int icon_x, icon_y;

            switch (modal->warning)
            {
            case CB_MODAL_WARNING_TOP:
                icon_x = LCD_COLUMNS / 2 - iw / 2;
                icon_y = y - ih / 2;
                playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);
                break;

            case CB_MODAL_WARNING_BOTTOM_LEFT:
                icon_x = x + m;
                icon_y = y + h - m - ih;
                playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);
                break;

            case CB_MODAL_WARNING_BOTTOM_RIGHT:
                icon_x = x + w - m - iw;
                icon_y = y + h - m - ih;
                playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);
                break;

            case CB_MODAL_WARNING_BOTTOM_LR:
                // Draw Left
                icon_x = x + m;
                icon_y = y + h - m - ih;
                playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);

                // Draw Right
                icon_x = x + w - m - iw;
                playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);
                break;

            default:
                if (modal->warning == CB_MODAL_WARNING_NONE)
                {
                    icon_x = LCD_COLUMNS / 2 - iw / 2;
                    icon_y = y - ih / 2;
                    playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);
                }
                break;
            }
        }
    }

    if (modal->exit || modal->droptimer < MODAL_DROP_TIME)
        return;

    if ((pushed & kButtonB) || (modal->options_count == 0 && (pushed & kButtonA)))
    {
        if (!modal->cannot_dismiss)
        {
            modal->exit = 1;
            modal->result = -1;
            cb_play_ui_sound(CB_UISound_Navigate);
        }
    }
    else if (pushed & kButtonA)
    {
        modal->exit = 1;
        modal->result = modal->option_selected;
        cb_play_ui_sound(CB_UISound_Confirm);
    }
    else
    {
        int d = !!(pushed & kButtonRight) - !!(pushed & kButtonLeft);
        if (d != 0)
        {
            int old_selection = modal->option_selected;
            modal->option_selected += d;
            if (modal->option_selected >= modal->options_count)
                modal->option_selected = modal->options_count - 1;
            if (modal->option_selected < 0)
                modal->option_selected = 0;

            if (modal->option_selected != old_selection)
            {
                cb_play_ui_sound(CB_UISound_Navigate);
            }
        }
    }
}

void CB_Modal_free(CB_Modal* modal)
{
    if (modal->callback)
        modal->callback(modal->ud, modal->result);

    if (modal->icon)
        playdate->graphics->freeBitmap(modal->icon);

    if (modal->dissolveMask)
    {
        playdate->graphics->freeBitmap(modal->dissolveMask);
    }

    for (size_t i = 0; i < MODAL_MAX_OPTIONS; ++i)
    {
        if (modal->options[i])
            cb_free(modal->options[i]);
    }
    if (modal->wrapped_lines)
        cb_free(modal->wrapped_lines);
    if (modal->text)
        cb_free(modal->text);
    CB_Scene_free(modal->scene);
    cb_free(modal);
}

CB_Modal* CB_Modal_new(char* text, char const* const* options, CB_ModalCallback callback, void* ud)
{
    CB_Modal* modal = allocz(CB_Modal);

    modal->width = 250;
    modal->height = 120;
    modal->margin = 24;

    modal->options_count = 0;
    if (options)
        for (size_t i = 0; options[i] && i < MODAL_MAX_OPTIONS; ++i)
        {
            modal->options[i] = cb_strdup(options[i]);
            modal->options_count++;
        }

    if (text)
        modal->text = cb_strdup(text);

    CB_Scene* scene = CB_Scene_new();
    scene->id = "modal";
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
