//
//  coverflow.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "coverflow.h"

#include "app.h"
#include "scenes/library_scene.h"
#include "utility.h"

#include <string.h>

#define FLOW_RADIUS 3
#define FLOW_SLOT_COUNT (2 * FLOW_RADIUS + 1)

#define FLOW_CENTER_Y 96
#define FLOW_CENTER_H 168
#define FLOW_SIDE_H 126
#define FLOW_SPACING 100
#define FLOW_TILT 0.45f
#define FLOW_TITLE_TOP 196

#define FLOW_CRANK_STEP_DEG 40.0f
#define FLOW_CRANK_IDLE_RESET 0.5f
#define FLOW_REPEAT_DELAY 0.35f
#define FLOW_REPEAT_RATE1 0.14f
#define FLOW_REPEAT_RATE2 0.07f
#define FLOW_REPEAT_FAST_AFTER 1.2f

#define FLOW_CARD_ASPECT 0.83f

typedef struct
{
    int index;
    LCDBitmap* bitmap;
} CB_FlowSlot;

struct CB_CoverFlow
{
    int direction;
    float holdTime;
    float timeSinceStep;
    float crankAccum;
    float crankIdleTime;

    float offset;

    float textScrollTime;
    float textScrollOffset;
    bool needsTextScroll;
    int lastTextItem;

    CB_FlowSlot slots[FLOW_SLOT_COUNT];
    void* decomp_buffer;
    size_t decomp_buffer_size;

    bool needsDisplay;
};

CB_CoverFlow* CB_CoverFlow_new(void)
{
    CB_CoverFlow* cf = allocz(CB_CoverFlow);
    for (int i = 0; i < FLOW_SLOT_COUNT; i++)
    {
        cf->slots[i].index = -1;
        cf->slots[i].bitmap = NULL;
    }
    cf->lastTextItem = -1;
    cf->needsDisplay = true;
    return cf;
}

void CB_CoverFlow_invalidateAll(CB_CoverFlow* cf)
{
    if (!cf)
        return;
    for (int i = 0; i < FLOW_SLOT_COUNT; i++)
    {
        if (cf->slots[i].bitmap)
        {
            playdate->graphics->freeBitmap(cf->slots[i].bitmap);
            cf->slots[i].bitmap = NULL;
        }
        cf->slots[i].index = -1;
    }
    cf->needsDisplay = true;
}

void CB_CoverFlow_free(CB_CoverFlow* cf)
{
    if (!cf)
        return;
    CB_CoverFlow_invalidateAll(cf);
    if (cf->decomp_buffer)
    {
        cb_free(cf->decomp_buffer);
    }
    cb_free(cf);
}

static void flow_step(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int delta)
{
    int n = ctx->itemCount;
    if (n <= 0 || !ctx->selection)
        return;

    int sel = *ctx->selection;
    if (sel < 0)
        sel = 0;

    int next = sel + delta;
    bool wrapped = false;
    if (next >= n)
    {
        next = 0;
        wrapped = true;
    }
    else if (next < 0)
    {
        next = n - 1;
        wrapped = true;
    }

    if (wrapped)
    {
        cf->offset = 0;
    }
    else
    {
        cf->offset -= delta;
    }

    *ctx->selection = next;
    cf->timeSinceStep = 0;
    cf->needsDisplay = true;
}

static LCDBitmap* flow_load_cover(CB_CoverFlow* cf, CB_Game* game)
{
    if (game->cover_compressed_data)
    {
        return CB_decompress_game_cover(game, &cf->decomp_buffer, &cf->decomp_buffer_size);
    }
    if (game->coverPath)
    {
        CB_LoadedCoverArt art = cb_load_and_scale_cover_art_from_path(
            game->coverPath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT
        );
        if (art.status == CB_COVER_ART_SUCCESS)
        {
            return art.bitmap;
        }
    }
    return NULL;
}

static CB_FlowSlot* flow_find_slot(CB_CoverFlow* cf, int index)
{
    for (int i = 0; i < FLOW_SLOT_COUNT; i++)
    {
        if (cf->slots[i].index == index)
            return &cf->slots[i];
    }
    return NULL;
}

// ensure covers around the selection are decompressed; evict out-of-window slots
static void flow_update_slots(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx)
{
    if (!ctx->selection)
        return;
    int sel = *ctx->selection;
    int games_n = ctx->games ? ctx->games->length : 0;

    // while flinging through many items, only bother with the center cover
    int radius = (fabsf(cf->offset) > 1.5f) ? 0 : FLOW_RADIUS;

    int lo = sel - radius;
    if (lo < 0)
        lo = 0;
    int hi = sel + radius;
    if (hi >= games_n)
        hi = games_n - 1;

    // evict slots outside the window
    for (int i = 0; i < FLOW_SLOT_COUNT; i++)
    {
        int idx = cf->slots[i].index;
        if (idx >= 0 && (idx < lo || idx > hi))
        {
            if (cf->slots[i].bitmap)
            {
                playdate->graphics->freeBitmap(cf->slots[i].bitmap);
                cf->slots[i].bitmap = NULL;
            }
            cf->slots[i].index = -1;
        }
    }

    // fill missing slots; limit disk loads per frame to avoid hitches
    int diskLoads = 0;
    for (int idx = lo; idx <= hi; idx++)
    {
        if (flow_find_slot(cf, idx))
            continue;

        CB_FlowSlot* free_slot = flow_find_slot(cf, -1);
        if (!free_slot)
            break;

        CB_Game* game = ctx->games->items[idx];
        bool fromDisk = !game->cover_compressed_data && game->coverPath;
        if (fromDisk && diskLoads >= 2)
            continue;

        LCDBitmap* bitmap = flow_load_cover(cf, game);
        if (fromDisk)
            diskLoads++;

        // occupy the slot even on failure so we don't retry every frame
        free_slot->index = idx;
        free_slot->bitmap = bitmap;
    }
}

void CB_CoverFlow_update(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx)
{
    float dt = CB_App->dt;

    PDButtons pushed = CB_App->buttons_pressed;
    PDButtons down = CB_App->buttons_down;

    if (pushed & kButtonRight)
    {
        flow_step(cf, ctx, 1);
    }
    else if (pushed & kButtonLeft)
    {
        flow_step(cf, ctx, -1);
    }

    int dir = (down & kButtonRight) ? 1 : (down & kButtonLeft) ? -1 : 0;
    if (dir != cf->direction)
    {
        cf->direction = dir;
        cf->holdTime = 0;
    }

    cf->timeSinceStep += dt;

    if (dir != 0)
    {
        cf->holdTime += dt;
        if (cf->holdTime >= FLOW_REPEAT_DELAY)
        {
            float rate =
                (cf->holdTime >= FLOW_REPEAT_FAST_AFTER) ? FLOW_REPEAT_RATE2 : FLOW_REPEAT_RATE1;
            if (cf->timeSinceStep >= rate)
            {
                flow_step(cf, ctx, dir);
            }
        }
    }

    float crankChange = CB_App->crankChange;
    if (crankChange != 0.0f)
    {
        cf->crankAccum += crankChange;
        cf->crankIdleTime = 0;
    }
    else
    {
        cf->crankIdleTime += dt;
        if (cf->crankIdleTime > FLOW_CRANK_IDLE_RESET)
        {
            cf->crankAccum = 0;
        }
    }

    while (cf->crankAccum >= FLOW_CRANK_STEP_DEG)
    {
        cf->crankAccum -= FLOW_CRANK_STEP_DEG;
        flow_step(cf, ctx, 1);
    }
    while (cf->crankAccum <= -FLOW_CRANK_STEP_DEG)
    {
        cf->crankAccum += FLOW_CRANK_STEP_DEG;
        flow_step(cf, ctx, -1);
    }

    // slide animation: ease offset toward 0, fast when far, slow when near
    if (cf->offset != 0.0f)
    {
        float speed = dt * (2.0f + 9.0f * fabsf(cf->offset));
        cf->offset = toward(cf->offset, 0.0f, speed);
        if (fabsf(cf->offset) < 0.01f)
            cf->offset = 0.0f;
        cf->needsDisplay = true;
    }

    int sel = ctx->selection ? *ctx->selection : -1;
    if (sel != cf->lastTextItem)
    {
        cf->lastTextItem = sel;
        cf->textScrollTime = 0;
        cf->textScrollOffset = 0;
        cf->needsTextScroll = false;
    }

    flow_update_slots(cf, ctx);
}

static LCDBitmap* flow_slot_bitmap(CB_CoverFlow* cf, int index)
{
    CB_FlowSlot* slot = flow_find_slot(cf, index);
    return slot ? slot->bitmap : NULL;
}

// draw a placeholder card (missing cover, or trailing non-game item)
static void flow_draw_card(
    int x, int y, int w, int h, const char* text, LCDFont* font, bool drawText
)
{
    playdate->graphics->fillRect(x, y, w, h, kColorWhite);
    playdate->graphics->drawRect(x, y, w, h, kColorBlack);

    if (drawText && text)
    {
        playdate->graphics->setFont(font);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        playdate->graphics->drawTextInRect(
            text, strlen(text), kUTF8Encoding, x + 6, y + 6, w - 12, h - 12, kWrapWord,
            kAlignTextCenter
        );
    }
}

void CB_CoverFlow_draw(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx)
{
    if (!ctx->selection)
        return;

    int sel = *ctx->selection;
    int itemCount = ctx->itemCount;
    int games_n = ctx->games ? ctx->games->length : 0;

    bool redraw = cf->needsDisplay || ctx->forceRedraw || cf->offset != 0.0f ||
                  cf->needsTextScroll || ctx->statusText != NULL;
    if (!redraw)
        return;

    cf->needsDisplay = false;

    int screenWidth = playdate->display->getWidth();
    int screenHeight = playdate->display->getHeight();

    playdate->graphics->clear(kColorWhite);
    playdate->graphics->setDrawMode(kDrawModeCopy);

    static const int drawOrder[FLOW_SLOT_COUNT] = {-3, 3, -2, 2, -1, 1, 0};

    for (int k = 0; k < FLOW_SLOT_COUNT; k++)
    {
        int ri = drawOrder[k];
        int i = sel + ri;
        if (i < 0 || i >= itemCount)
            continue;

        float rel = ri - cf->offset;
        float a = fabsf(rel);
        if (a > (float)FLOW_RADIUS + 0.2f)
            continue;

        float t = a > 1.0f ? 1.0f : a;
        float h_target = FLOW_CENTER_H + (FLOW_SIDE_H - FLOW_CENTER_H) * t;
        float tilt = 1.0f - FLOW_TILT * t;
        float cx = screenWidth / 2.0f + rel * FLOW_SPACING;
        float cy = FLOW_CENTER_Y;

        bool isTrailingCard = (i >= games_n);
        LCDBitmap* bitmap = isTrailingCard ? NULL : flow_slot_bitmap(cf, i);

        if (bitmap)
        {
            int w, h;
            playdate->graphics->getBitmapData(bitmap, &w, &h, NULL, NULL, NULL);
            if (w <= 0 || h <= 0)
                continue;

            float ys = h_target / h;
            float xs = ys * tilt;
            float dw = w * xs;
            float dh = h * ys;

            int dx = (int)(cx - dw / 2);
            int dy = (int)(cy - dh / 2);

            playdate->graphics->drawRect(dx - 1, dy - 1, (int)dw + 2, (int)dh + 2, kColorBlack);
            playdate->graphics->drawScaledBitmap(bitmap, dx, dy, xs, ys);
        }
        else
        {
            float cw = h_target * FLOW_CARD_ASPECT * tilt;
            int dx = (int)(cx - cw / 2);
            int dy = (int)(cy - h_target / 2);

            const char* text = NULL;
            if (isTrailingCard)
            {
                text = T(Library_GetRoms);
            }
            else
            {
                CB_Game* game = ctx->games->items[i];
                text = game->displayName;
            }

            flow_draw_card(dx, dy, (int)cw, (int)h_target, text, CB_App->bodyFont, t < 0.5f);
        }
    }

    // title area
    playdate->graphics->fillRect(
        0, FLOW_TITLE_TOP, screenWidth, screenHeight - FLOW_TITLE_TOP, kColorWhite
    );

    const char* title = ctx->statusText;
    if (!title)
    {
        if (sel >= 0 && sel < games_n)
        {
            CB_Game* game = ctx->games->items[sel];
            title = game->displayName;
        }
        else if (sel == games_n)
        {
            title = T(Library_GetRoms);
        }
    }

    if (title)
    {
        LCDFont* font = CB_App->subheadFont;
        playdate->graphics->setFont(font);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);

        int margin = 8;
        int textWidth =
            playdate->graphics->getTextWidth(font, title, strlen(title), kUTF8Encoding, 0);
        int availWidth = screenWidth - margin * 2;
        int fontHeight = playdate->graphics->getFontHeight(font);
        int textY = FLOW_TITLE_TOP + (screenHeight - FLOW_TITLE_TOP - fontHeight) / 2;

        if (textWidth <= availWidth)
        {
            cf->needsTextScroll = false;
            cf->textScrollOffset = 0;
            playdate->graphics->drawText(
                title, strlen(title), kUTF8Encoding, (screenWidth - textWidth) / 2, textY
            );
        }
        else
        {
            // marquee scroll (same cycle as listview)
            cf->needsTextScroll = true;

            const float SCROLL_SPEED_PPS = 50.0f;
            const float MIN_SCROLL_DURATION = 0.75f;
            const float SCROLL_BACK_FACTOR = 2.0f / 3.0f;
            const float pauseStart = 0.7f;
            const float pauseEnd = 1.5f;

            float maxOffset = textWidth - availWidth;
            float scrollToEnd = maxOffset / SCROLL_SPEED_PPS;
            if (scrollToEnd < MIN_SCROLL_DURATION)
                scrollToEnd = MIN_SCROLL_DURATION;
            float scrollToStart = scrollToEnd * SCROLL_BACK_FACTOR;
            if (scrollToStart < MIN_SCROLL_DURATION)
                scrollToStart = MIN_SCROLL_DURATION;

            float cycle = pauseStart + scrollToEnd + pauseEnd + scrollToStart;
            float time = fmodf(cf->textScrollTime, cycle);
            cf->textScrollTime += CB_App->dt;

            if (time < pauseStart)
            {
                cf->textScrollOffset = 0;
            }
            else if (time < pauseStart + scrollToEnd)
            {
                float p = (time - pauseStart) / scrollToEnd;
                cf->textScrollOffset = cb_easeInOutQuad(p) * maxOffset;
            }
            else if (time < pauseStart + scrollToEnd + pauseEnd)
            {
                cf->textScrollOffset = maxOffset;
            }
            else
            {
                float p = (time - pauseStart - scrollToEnd - pauseEnd) / scrollToStart;
                cf->textScrollOffset = (1.0f - cb_easeInOutQuad(p)) * maxOffset;
            }

            playdate->graphics->setClipRect(
                margin, FLOW_TITLE_TOP, availWidth, screenHeight - FLOW_TITLE_TOP
            );
            playdate->graphics->drawText(
                title, strlen(title), kUTF8Encoding, margin - (int)cf->textScrollOffset, textY
            );
            playdate->graphics->clearClipRect();
        }
    }

    // position counter, top-right (games only; the "Get ROMs..." card is not counted)
    if (sel >= 0 && sel < games_n)
    {
        char counter[16];
        snprintf(counter, sizeof(counter), "%d / %d", sel + 1, games_n);
        playdate->graphics->setFont(CB_App->bodyFont);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        int cw = playdate->graphics->getTextWidth(
            CB_App->bodyFont, counter, strlen(counter), kUTF8Encoding, 0
        );
        playdate->graphics->drawText(
            counter, strlen(counter), kUTF8Encoding, screenWidth - cw - 6, 4
        );
    }
}
