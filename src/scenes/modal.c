#include "modal.h"

#include "../app.h"
#include "../scene.h"
#include "../utility.h"

#define MODAL_ANIM_TIME 16
#define MODAL_DROP_TIME 12
#define PULSE_PERIOD 30

#define MODAL_WIDTH 360
#define MODAL_MARGIN 20
#define MODAL_MARGIN_MIN 12
#define MODAL_MIN_HEIGHT 90
#define TITLE_GAP 10
#define PARAGRAPH_GAP 11
#define TEXT_BUTTON_SPACING 20
#define BUTTON_HEIGHT 40
#define BUTTON_BOTTOM_GAP 10

static int cb_modal_text_height(const char* text, int width)
{
    int total = 0;
    int paragraphs = 0;
    const char* p = text;
    while (p && *p)
    {
        const char* end = strstr(p, "\n\n");
        int len = end ? (int)(end - p) : (int)strlen(p);
        if (len > 0)
        {
            total += playdate->graphics->getTextHeightForMaxWidth(
                CB_App->bodyFont, p, len, width, kUTF8Encoding, kWrapWord, 0, 0
            );
            paragraphs++;
        }
        if (!end)
            break;
        p = end + 2;
    }
    return total + (paragraphs > 0 ? (paragraphs - 1) * PARAGRAPH_GAP : 0);
}

static void cb_modal_draw_text(const char* text, int x, int y, int width)
{
    const char* p = text;
    while (p && *p)
    {
        const char* end = strstr(p, "\n\n");
        int len = end ? (int)(end - p) : (int)strlen(p);
        if (len > 0)
        {
            int ph = playdate->graphics->getTextHeightForMaxWidth(
                CB_App->bodyFont, p, len, width, kUTF8Encoding, kWrapWord, 0, 0
            );
            playdate->graphics->drawTextInRect(
                p, len, kUTF8Encoding, x, y, width, ph, kWrapWord, kAlignTextCenter
            );
            y += ph + PARAGRAPH_GAP;
        }
        if (!end)
            break;
        p = end + 2;
    }
}

void CB_Modal_auto_size(CB_Modal* modal)
{
    int frame = 3;
    int margin = MODAL_MARGIN;

    for (int pass = 0; pass < 2; ++pass)
    {
        int title_h = modal->title ? playdate->graphics->getFontHeight(CB_App->subheadFont) : 0;
        int title_gap = modal->title ? TITLE_GAP : 0;
        int text_h = modal->text ? cb_modal_text_height(modal->text, MODAL_WIDTH - 2 * margin) : 0;
        int content = title_h + title_gap + text_h;

        int h = modal->options_count > 0 ? frame * 2 + margin + content + TEXT_BUTTON_SPACING +
                                               BUTTON_HEIGHT + BUTTON_BOTTOM_GAP
                                         : frame * 2 + margin * 2 + content;

        if (h <= MODAL_MAX_HEIGHT || margin == MODAL_MARGIN_MIN)
        {
            modal->margin = margin;
            modal->height = CB_MIN(CB_MAX(h, MODAL_MIN_HEIGHT), MODAL_MAX_HEIGHT);
            modal->width = MODAL_WIDTH;
            return;
        }

        margin = MODAL_MARGIN_MIN;
    }
}

void CB_Modal_set_title(CB_Modal* modal, const char* title)
{
    if (modal->title)
        cb_free(modal->title);
    modal->title = title ? cb_strdup(title) : NULL;
    CB_Modal_auto_size(modal);
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
    int radius = 8;

    cb_fillRoundRect(PDRectMake(x, y, w, h), radius, kColorWhite);

    cb_fillRoundRect(
        PDRectMake(
            x + white_border_thickness, y + white_border_thickness,
            w - (white_border_thickness * 2), h - (white_border_thickness * 2)
        ),
        radius - white_border_thickness, kColorBlack
    );

    cb_fillRoundRect(
        PDRectMake(
            x + total_thickness, y + total_thickness, w - (total_thickness * 2),
            h - (total_thickness * 2)
        ),
        radius - total_thickness, kColorWhite
    );

    int m = modal->margin;

    int title_h = modal->title ? playdate->graphics->getFontHeight(CB_App->subheadFont) : 0;
    int title_gap = modal->title ? TITLE_GAP : 0;

    int text_h = 0;
    if (modal->text)
    {
        playdate->graphics->setFont(CB_App->bodyFont);
        text_h = cb_modal_text_height(modal->text, w - 2 * m);
    }

    int content_h = title_h + title_gap + text_h;
    int avail_h = h - 2 * m;
    int y_offset =
        (modal->options_count == 0 && content_h < avail_h) ? (avail_h - content_h) / 2 : 0;

    int cursor_y = y + m + y_offset;

    if (modal->title)
    {
        playdate->graphics->setFont(CB_App->subheadFont);
        int title_width = playdate->graphics->getTextWidth(
            CB_App->subheadFont, modal->title, strlen(modal->title), kUTF8Encoding, 0
        );
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        playdate->graphics->drawText(
            modal->title, strlen(modal->title), kUTF8Encoding, x + (w - title_width) / 2, cursor_y
        );
        cursor_y += title_h + title_gap;
    }

    if (modal->text)
    {
        playdate->graphics->setFont(CB_App->bodyFont);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        cb_modal_draw_text(modal->text, x + m, cursor_y, w - 2 * m);
    }

    int fontHeight = playdate->graphics->getFontHeight(CB_App->bodyFont);
    int button_h = BUTTON_HEIGHT;
    int button_radius = 6;

    int iw = 0;
    int ih = 0;
    if (modal->warning != CB_MODAL_WARNING_NONE)
    {
        if (!modal->icon)
            modal->icon =
                playdate->graphics->loadBitmap(CB_get_forwarded_path("images/warning"), NULL);
        if (modal->icon)
            playdate->graphics->getBitmapData(modal->icon, &iw, &ih, NULL, NULL, NULL);
    }

    int spacing;
    int first_center_x;

    if (modal->options_count == 3)
    {
        int button_margin = 20;
        spacing = (w - 2 * button_margin) / 3;
        first_center_x = x + button_margin + spacing / 2;
    }
    else
    {
        spacing = w / (1 + modal->options_count);
        first_center_x = x + spacing;
    }

    int button_w = spacing - 8;

    int pulse_t = modal->master_timer % PULSE_PERIOD;
    bool pulse_on = pulse_t < PULSE_PERIOD / 2;

    for (int i = 0; i < modal->options_count; ++i)
    {
        int ox = first_center_x + spacing * i;
        int bx = ox - button_w / 2;
        int by = y + h - total_thickness - BUTTON_BOTTOM_GAP - button_h;

        if (modal->options_count == 1)
        {
            bool left_warn =
                (modal->warning == CB_MODAL_WARNING_BOTTOM_LEFT ||
                 modal->warning == CB_MODAL_WARNING_BOTTOM_LR);
            bool right_warn =
                (modal->warning == CB_MODAL_WARNING_BOTTOM_RIGHT ||
                 modal->warning == CB_MODAL_WARNING_BOTTOM_LR);
            if (left_warn || right_warn)
            {
                int inset = iw + 4;
                int left = x + m + (left_warn ? inset : 0);
                int right = x + w - m - (right_warn ? inset : 0);
                bx = left;
                button_w = right - left;
            }
        }

        if (i == modal->option_selected)
        {
            cb_fillRoundRect(PDRectMake(bx, by, button_w, button_h), button_radius, kColorBlack);
            playdate->graphics->setDrawMode(kDrawModeFillWhite);

            if (pulse_on)
            {
                cb_drawRoundRect(
                    PDRectMake(bx - 2, by - 2, button_w + 4, button_h + 4), button_radius + 2, 2,
                    kColorBlack
                );
            }
        }
        else
        {
            cb_fillRoundRect(PDRectMake(bx, by, button_w, button_h), button_radius, kColorWhite);
            cb_drawRoundRect(PDRectMake(bx, by, button_w, button_h), button_radius, 2, kColorBlack);
            playdate->graphics->setDrawMode(kDrawModeFillBlack);
        }

        int text_y = by + (button_h - fontHeight) / 2;
        playdate->graphics->drawTextInRect(
            modal->options[i], strlen(modal->options[i]), kUTF8Encoding, bx, text_y, button_w,
            button_h, kWrapClip, kAlignTextCenter
        );
    }

    playdate->graphics->setDrawMode(kDrawModeCopy);

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
                icon_y = y + h - total_thickness - BUTTON_BOTTOM_GAP - (BUTTON_HEIGHT + ih) / 2;
                playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);
                break;

            case CB_MODAL_WARNING_BOTTOM_RIGHT:
                icon_x = x + w - m - iw;
                icon_y = y + h - total_thickness - BUTTON_BOTTOM_GAP - (BUTTON_HEIGHT + ih) / 2;
                playdate->graphics->drawBitmap(modal->icon, icon_x, icon_y, kBitmapUnflipped);
                break;

            case CB_MODAL_WARNING_BOTTOM_LR:
                // Draw Left
                icon_x = x + m;
                icon_y = y + h - total_thickness - BUTTON_BOTTOM_GAP - (BUTTON_HEIGHT + ih) / 2;
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
    if (modal->text)
        cb_free(modal->text);
    if (modal->title)
        cb_free(modal->title);
    CB_Scene_free(modal->scene);
    cb_free(modal);
}

CB_Modal* CB_Modal_new(
    const char* text, char const* const* options, CB_ModalCallback callback, void* ud
)
{
    CB_Modal* modal = allocz(CB_Modal);

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

    CB_Modal_auto_size(modal);

    return modal;
}
