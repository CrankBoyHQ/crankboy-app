//
//  coverflow.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "coverflow.h"

#include "app.h"
#include "covers.h"
#include "preferences.h"
#include "utility.h"

#include <string.h>

// card geometry (LP-crate packing)
#define FLOW_RADIUS 5
#define FLOW_SLOT_COUNT (2 * FLOW_RADIUS + 1)
#define FLOW_CENTER_H 200
#define FLOW_SIDE_H 126
#define FLOW_MIN_H 96
#define FLOW_H_FAR_DECAY 10
#define FLOW_TOP_MARGIN 10  // below the label/counter row
#define FLOW_SLIVER 20      // side cover peek beyond the stacked cover
#define FLOW_TILT 0.45f
#define FLOW_TILT_FAR 0.05f
#define FLOW_TILT_MIN 0.35f

// category switch push
#define FLOW_SWITCH_TIME 0.28f
#define FLOW_TITLE_TOP 212

// wrap-around
#define FLOW_WRAP_SLIDE 4.0f
#define FLOW_WRAP_MIN_ITEMS 4  // glide only for lists big enough to read as scrolling
#define FLOW_FADE_TIME 0.35f   // outgoing stack keeps gliding while the new one settles
#define FLOW_FADE_TRAVEL (FLOW_WRAP_SLIDE * 48.0f)

// input
#define FLOW_CRANK_STEP_DEG 40.0f
#define FLOW_CRANK_IDLE_RESET 0.5f
#define FLOW_REPEAT_DELAY 0.35f
#define FLOW_REPEAT_RATE1 0.14f
#define FLOW_REPEAT_RATE2 0.07f
#define FLOW_REPEAT_FAST_AFTER 1.2f

#define FLOW_DISK_LOADS_PER_FRAME 2  // disk loads per frame while filling slots (hitch budget)

// rationale: except for the two solid styles these are fine 1-px textures on
// black or white; dark styles keep dark cover art separable, the light gray
// checker is the exact bit-inverse of the dark gray one
static const uint8_t kFlowPatterns[CB_LIB_BG_COUNT][16] = {
    [CB_LIB_BG_LIGHT] =
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
         0xFF},
    [CB_LIB_BG_LIGHT_GRAY] =
        {0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
         0xFF},
    [CB_LIB_BG_STRIPE] =
        {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
         0xFF},
    [CB_LIB_BG_DARK_GRAY] =
        {0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
         0xFF},
    [CB_LIB_BG_DARK] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF
    },
};

const uint8_t* cb_library_bg_pattern(int style)
{
    if (style < 0 || style >= CB_LIB_BG_COUNT)
        style = CB_LIB_BG_STRIPE;
    return kFlowPatterns[style];
}

static void flow_fill_bg(int x, int y, int w, int h, const uint8_t* pattern)
{
    if (w <= 0 || h <= 0)
        return;
    playdate->graphics->fillRect(x, y, w, h, (LCDColor)(uintptr_t)pattern);
}

typedef struct
{
    int index;
    LCDBitmap* bitmap;
} CB_FlowSlot;

// outgoing card captured at wrap (bitmap ownership moves to the fade pool);
// NULL bitmap = placeholder sheet
typedef struct
{
    LCDBitmap* bitmap;
    int dx, dy, dw, dh;
    float scaleX, scaleY;
} CB_FlowFadeCard;

struct CB_CoverFlow
{
    int direction;
    float holdTime;
    float timeSinceStep;
    float crankAccum;
    float crankIdleTime;

    float offset;

    // category switch: push the new category in over a snapshot of the old frame
    uint8_t* switchSnap;  // pre-switch screen rows (LCD_ROWS * LCD_ROWSIZE)
    float switchTime;     // push progress clock
    int switchDir;        // +1: new category pushes in from the bottom, -1: from the top
    bool switchActive;

    float textScrollTime;
    float labelScrollTime;
    float counterScrollTime;
    bool needsTextScroll;
    int lastTextItem;
    int lastCounterItem;
    const char* lastLabel;

    CB_FlowSlot slots[FLOW_SLOT_COUNT];
    void* decomp_buffer;
    size_t decomp_buffer_size;

    // wrap-around: outgoing stack captured from the slots, slides out while
    // the new stack settles
    CB_FlowFadeCard fadeCards[FLOW_SLOT_COUNT];
    int fadeCount;
    float fadeTime;  // wrap fade progress clock; inactive when fadeCount == 0
    int fadeDir;     // wrap travel direction (sign of the step delta)

    bool needsDisplay;

    int bgStyle;  // CB_LIB_BG_*, stashed by CB_CoverFlow_draw before first use
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
    cf->lastCounterItem = -1;
    cf->lastLabel = NULL;
    cf->needsDisplay = true;
    return cf;
}

static void flow_fade_clear(CB_CoverFlow* cf);
static void flow_capture_fade_stack(
    CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int sel, int delta
);
static void flow_draw_lines_panel(
    int x, int y, int w, int h, const CB_FlowLine* lines, int lineCount, bool drawText
);
static LCDBitmap* flow_slot_bitmap(CB_CoverFlow* cf, int index);
// re-bind the push band clip after a marquee replaces it (no-op outside pushes)
static void flow_restore_push_clip(bool hasClip, int clipTop, int clipBot, int width)
{
    if (hasClip)
        playdate->graphics->setClipRect(0, clipTop, width, clipBot - clipTop);
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
    flow_fade_clear(cf);
    cf->needsDisplay = true;
}

static void flow_fade_clear(CB_CoverFlow* cf)
{
    for (int i = 0; i < cf->fadeCount; ++i)
    {
        if (cf->fadeCards[i].bitmap)
        {
            playdate->graphics->freeBitmap(cf->fadeCards[i].bitmap);
        }
        cf->fadeCards[i].bitmap = NULL;
    }
    cf->fadeCount = 0;
    cf->fadeTime = 0.0f;
}

// snapshot the screen and start pushing the new category in over it
void CB_CoverFlow_startSwitch(CB_CoverFlow* cf, int dir)
{
    if (!cf)
        return;

    if (!cf->switchSnap)
    {
        cf->switchSnap = cb_malloc(LCD_ROWS * LCD_ROWSIZE);
        if (!cf->switchSnap)
            return;  // out of memory for the old-frame snapshot: switch instantly
    }

    uint8_t* fb = playdate->graphics->getFrame();
    if (fb)
        memcpy(cf->switchSnap, fb, LCD_ROWS * LCD_ROWSIZE);

    cf->switchTime = 0.0f;
    cf->switchDir = dir;
    cf->switchActive = true;
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
    if (cf->switchSnap)
    {
        cb_free(cf->switchSnap);
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
        if (n >= FLOW_WRAP_MIN_ITEMS)
        {
            flow_capture_fade_stack(cf, ctx, sel, delta);
            cf->offset = (float)-delta * FLOW_WRAP_SLIDE;
        }
        // small lists: plain jump, offset stays 0
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
    if (!game->cvi)
    {
        if (!game->coverPath)
            return NULL;
        CB_cover_load(game);
    }
    // center-card shape: the 200-high variant always fits, the 240 box
    // only when the cover itself is short enough
    LCDBitmap* bitmap =
        CB_cover_decode(game, 400, FLOW_CENTER_H, &cf->decomp_buffer, &cf->decomp_buffer_size);
    if (!bitmap)
        playdate->system->logToConsole(
            "coverflow: decode failed %s (%u variants)", game->coverPath ? game->coverPath : "-",
            game->cvi ? game->cvi->num_variants : 0
        );
    return bitmap;
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

// center-card width: the actual bitmap scaled to FLOW_CENTER_H, else the
// placeholder square
static float flow_anchor_width(CB_CoverFlow* cf, int sel, int games_n)
{
    LCDBitmap* centerBitmap = (sel < games_n) ? flow_slot_bitmap(cf, sel) : NULL;
    if (centerBitmap)
    {
        int w, h;
        playdate->graphics->getBitmapData(centerBitmap, &w, &h, NULL, NULL, NULL);
        return (w > 0 && h > 0) ? w * (FLOW_CENTER_H / (float)h) : FLOW_CENTER_H;
    }
    return FLOW_CENTER_H;
}

// per-card geometry for the LP-crate packing. rel includes the slide offset
// (signed); cx includes xShift. Bitmap dims ≤ 0 (or a NULL bitmap) fall back
// to the square placeholder sheet
static void flow_card_metrics(
    float rel, float anchorW, float screenMid, int xShift, float bmW, float bmH, float* dw,
    float* dh, float* scaleX, float* scaleY, float* cx
)
{
    float absRel = fabsf(rel);
    float front = absRel > 1.0f ? 1.0f : absRel;
    float far = absRel > 1.0f ? absRel - 1.0f : 0.0f;

    float targetHeight =
        FLOW_CENTER_H - (FLOW_CENTER_H - FLOW_SIDE_H) * front - FLOW_H_FAR_DECAY * far;
    if (targetHeight < FLOW_MIN_H)
        targetHeight = FLOW_MIN_H;
    float tilt = 1.0f - FLOW_TILT * front - FLOW_TILT_FAR * far;
    if (tilt < FLOW_TILT_MIN)
        tilt = FLOW_TILT_MIN;

    if (bmW > 0.0f && bmH > 0.0f)
    {
        *scaleY = targetHeight / bmH;
        *scaleX = (*scaleY) * tilt;
        *dw = bmW * (*scaleX);
        *dh = bmH * (*scaleY);
    }
    else
    {
        *scaleX = 0.0f;
        *scaleY = 0.0f;
        *dw = targetHeight * tilt;
        *dh = targetHeight;
    }

    float sideSign = rel > 0.0f ? 1.0f : -1.0f;
    *cx = screenMid + xShift + sideSign * (anchorW / 2.0f + FLOW_SLIVER * absRel - (*dw) / 2.0f);
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
        bool needsDiskLoad = !game->cvi && game->coverPath;
        if (needsDiskLoad && diskLoads >= FLOW_DISK_LOADS_PER_FRAME)
            continue;

        LCDBitmap* bitmap = flow_load_cover(cf, game);
        if (needsDiskLoad)
            diskLoads++;

        // occupy the slot even on failure so we don't retry every frame;
        // failing slots refresh on the next invalidateAll (category
        // switch, cover download/delete)
        free_slot->index = idx;
        free_slot->bitmap = bitmap;
    }
}

// capture the outgoing stack at wrap time: geometry mirrors the scene's packing,
// slot bitmaps move into the fade pool (ownership transfer, no duplication)
static void flow_capture_fade_stack(
    CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int sel, int delta
)
{
    flow_fade_clear(cf);

    int games_n = ctx->games ? ctx->games->length : 0;
    int itemCount = ctx->itemCount;
    if (itemCount <= 0)
        return;

    int screenWidth = playdate->display->getWidth();
    float dw0 = flow_anchor_width(cf, sel, games_n);
    float cy = FLOW_TOP_MARGIN + FLOW_CENTER_H / 2.0f;

    int count = 0;
    for (int idx = sel - FLOW_RADIUS; idx <= sel + FLOW_RADIUS; ++idx)
    {
        if (idx < 0 || idx >= itemCount)
            continue;

        bool isTrailing = (idx >= games_n);
        LCDBitmap* bitmap = isTrailing ? NULL : flow_slot_bitmap(cf, idx);

        float bmW = 0.0f, bmH = 0.0f;
        if (bitmap)
        {
            int w, h;
            playdate->graphics->getBitmapData(bitmap, &w, &h, NULL, NULL, NULL);
            if (w <= 0 || h <= 0)
            {
                bitmap = NULL;
            }
            else
            {
                bmW = (float)w;
                bmH = (float)h;
            }
        }

        float dw, dh, scaleX, scaleY, cx;
        flow_card_metrics(
            (float)(idx - sel) - cf->offset, dw0, screenWidth / 2.0f, ctx->xShift, bmW, bmH, &dw,
            &dh, &scaleX, &scaleY, &cx
        );

        if (count >= FLOW_SLOT_COUNT)
            break;

        CB_FlowFadeCard* card = &cf->fadeCards[count++];
        card->bitmap = bitmap;
        card->dx = (int)(cx - dw / 2);
        card->dy = (int)(cy - dh / 2);
        card->dw = (int)dw;
        card->dh = (int)dh;
        card->scaleX = scaleX;
        card->scaleY = scaleY;

        if (bitmap)
        {
            for (int si = 0; si < FLOW_SLOT_COUNT; ++si)
            {
                if (cf->slots[si].bitmap == bitmap)
                {
                    cf->slots[si].bitmap = NULL;
                    cf->slots[si].index = -1;
                    break;
                }
            }
        }
    }

    cf->fadeCount = count;
    cf->fadeTime = 0.0f;
    cf->fadeDir = delta > 0 ? 1 : -1;
    cf->needsDisplay = true;
}

// outgoing wrap cards: slide along with the wrap glide while the new stack
// settles, no dithering (mid-step overlays made covers read as flat grey)
static void flow_draw_fade_stack(CB_CoverFlow* cf, float progress)
{
    if (cf->fadeCount == 0)
        return;
    if (progress >= 1.0f)
        return;

    int shift = (int)(-(float)cf->fadeDir * FLOW_FADE_TRAVEL * progress);

    for (int i = 0; i < cf->fadeCount; ++i)
    {
        CB_FlowFadeCard* card = &cf->fadeCards[i];
        int x = card->dx + shift;
        int y = card->dy;

        LCDColor coverBorder = CB_LIB_BG_IS_LIGHT(cf->bgStyle) ? kColorBlack : kColorWhite;
        if (card->bitmap)
        {
            playdate->graphics->drawRect(x - 1, y - 1, card->dw + 2, card->dh + 2, coverBorder);
            playdate->graphics->drawScaledBitmap(card->bitmap, x, y, card->scaleX, card->scaleY);
        }
        else
        {
            playdate->graphics->fillRect(x, y, card->dw, card->dh, kColorWhite);
            playdate->graphics->drawRect(x, y, card->dw, card->dh, kColorBlack);
        }
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

    // category switch push progress; normal drawing resumes when it finishes
    if (cf->switchActive)
    {
        cf->switchTime += dt;
        if (cf->switchTime >= FLOW_SWITCH_TIME)
        {
            cf->switchActive = false;
        }
        cf->needsDisplay = true;
    }

    // wrap-around fade progress
    if (cf->fadeCount > 0)
    {
        cf->fadeTime += dt;
        if (cf->fadeTime >= FLOW_FADE_TIME)
        {
            flow_fade_clear(cf);
        }
        cf->needsDisplay = true;
    }

    int sel = ctx->selection ? *ctx->selection : -1;
    if (sel != cf->lastTextItem)
    {
        cf->lastTextItem = sel;
        cf->textScrollTime = 0;
        cf->needsTextScroll = false;
    }
    // selection changes the counter text: restart its marquee
    if (sel != cf->lastCounterItem)
    {
        cf->lastCounterItem = sel;
        cf->counterScrollTime = 0;
    }
    // category switches change the label text: restart its marquee
    if (ctx->categoryName != cf->lastLabel)
    {
        cf->lastLabel = ctx->categoryName;
        cf->labelScrollTime = 0;
    }

    flow_update_slots(cf, ctx);
}

static LCDBitmap* flow_slot_bitmap(CB_CoverFlow* cf, int index)
{
    CB_FlowSlot* slot = flow_find_slot(cf, index);
    return slot ? slot->bitmap : NULL;
}

// outlined text: on light backgrounds a white halo under a black core, on
// dark ones a black halo under a white core; the halo covers the bg pattern
// around the glyphs so the core reads solid
static void flow_draw_text_outlined(const char* text, int len, int x, int y, bool light)
{
    static const int offs[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0},
                                   {1, 0},   {-1, 1}, {0, 1},  {1, 1}};
    playdate->graphics->setDrawMode(light ? kDrawModeFillWhite : kDrawModeFillBlack);
    for (int i = 0; i < 8; i++)
    {
        playdate->graphics->drawText(text, len, kUTF8Encoding, x + offs[i][0], y + offs[i][1]);
    }
    playdate->graphics->setDrawMode(light ? kDrawModeFillBlack : kDrawModeFillWhite);
    playdate->graphics->drawText(text, len, kUTF8Encoding, x, y);
}

// top-left: category name; static with margin when it fits, otherwise a
// marquee: the head's 8px screen margin is consumed by the pan (letters
// exit through the display edge) and the name slides until its tail has
// an 8px gap to the cover - both endpoints keep their gap
static bool flow_draw_category_label(
    CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int screenWidth, float offY, bool hasClip,
    int clipTop, int clipBot, int cardLeft, int cardRight
)
{
    bool scrolling = false;
    if (!ctx->categoryName)
        return scrolling;

    LCDFont* catFont = CB_App->labelFont ? CB_App->labelFont : CB_App->bodyFont;
    playdate->graphics->setFont(catFont);
    playdate->graphics->setDrawMode(kDrawModeFillWhite);

    int labelW = playdate->graphics->getTextWidth(
        catFont, ctx->categoryName, strlen(ctx->categoryName), kUTF8Encoding, 0
    );
    int panRange = labelW + 16 - cardLeft;
    if (panRange > 8)
    {
        float off = cb_marquee_offset(
            &cf->labelScrollTime, catFont, ctx->categoryName, cardLeft - 16, &scrolling
        );
        int drawX = 8 - (int)off;
        int drawY = 4 + (int)offY;
        int cardTop = FLOW_TOP_MARGIN + (int)offY;

        // pass 1: the region left of the cover, full height
        int p1Top = hasClip ? CB_MAX(0, clipTop) : 0;
        int p1Bot = hasClip ? clipBot : FLOW_TITLE_TOP;
        if (p1Bot > p1Top)
        {
            playdate->graphics->setClipRect(0, p1Top, cardLeft, p1Bot - p1Top);
            flow_draw_text_outlined(
                ctx->categoryName, strlen(ctx->categoryName), drawX, drawY,
                CB_LIB_BG_IS_LIGHT(cf->bgStyle)
            );
        }

        // pass 2: the free strip above the cover (full width, minus the
        // counter's territory) - glyphs peek over the card's top edge
        int p2Top = hasClip ? CB_MAX(0, clipTop) : 0;
        int p2Bot = cardTop - 1;
        if (p2Bot > p2Top)
        {
            playdate->graphics->setClipRect(0, p2Top, cardRight + 8, p2Bot - p2Top);
            flow_draw_text_outlined(
                ctx->categoryName, strlen(ctx->categoryName), drawX, drawY,
                CB_LIB_BG_IS_LIGHT(cf->bgStyle)
            );
        }

        playdate->graphics->clearClipRect();
        flow_restore_push_clip(hasClip, clipTop, clipBot, screenWidth);
    }
    else
    {
        flow_draw_text_outlined(
            ctx->categoryName, strlen(ctx->categoryName), 8, 4 + (int)offY,
            CB_LIB_BG_IS_LIGHT(cf->bgStyle)
        );
    }
    playdate->graphics->setDrawMode(kDrawModeCopy);
    return scrolling;
}

// the LP-crate card stack; empty frames show the placeholder panels from the
// caller-provided ctx
static void flow_draw_cards(
    CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int sel, int itemCount, int games_n,
    int screenWidth, float offY, float dw0
)
{
    // text layers may leave kDrawModeFillWhite behind: pin the deck's mode
    playdate->graphics->setDrawMode(kDrawModeCopy);

    // cards centered in the zone between top margin and title band
    float centerY = FLOW_TOP_MARGIN + FLOW_CENTER_H / 2.0f + offY;

    static const int drawOrder[FLOW_SLOT_COUNT] = {-5, 5, -4, 4, -3, 3, -2, 2, -1, 1, 0};

    for (int k = 0; k < FLOW_SLOT_COUNT; k++)
    {
        int relIndex = drawOrder[k];
        int i = sel + relIndex;
        if (i < 0 || i >= itemCount)
            continue;

        float rel = (float)relIndex - cf->offset;
        float absRel = fabsf(rel);
        if (absRel > (float)FLOW_RADIUS + 0.2f)
            continue;

        bool isTrailingCard = (i >= games_n);
        LCDBitmap* bitmap = isTrailingCard ? NULL : flow_slot_bitmap(cf, i);

        float bmW = 0.0f, bmH = 0.0f;
        bool unusable = false;
        if (bitmap)
        {
            int w, h;
            playdate->graphics->getBitmapData(bitmap, &w, &h, NULL, NULL, NULL);
            if (w <= 0 || h <= 0)
                unusable = true;  // zero-sized bitmap: skip the card
            else
            {
                bmW = (float)w;
                bmH = (float)h;
            }
        }
        if (unusable)
            continue;

        float dw, dh, scaleX, scaleY, cx;
        flow_card_metrics(
            rel, dw0, screenWidth / 2.0f, ctx->xShift, bmW, bmH, &dw, &dh, &scaleX, &scaleY, &cx
        );
        // placeholder text only on front cards
        float front = absRel > 1.0f ? 1.0f : absRel;

        int dx = (int)(cx - dw / 2);
        int dy = (int)(centerY - dh / 2);

        if (bitmap)
        {
            playdate->graphics->drawRect(
                dx - 1, dy - 1, (int)dw + 2, (int)dh + 2,
                CB_LIB_BG_IS_LIGHT(cf->bgStyle) ? kColorBlack : kColorWhite
            );
            playdate->graphics->drawScaledBitmap(bitmap, dx, dy, scaleX, scaleY);
        }
        else
        {
            flow_draw_lines_panel(dx, dy, (int)dw, (int)dh, ctx->placeholder, 6, front < 0.5f);
        }
    }
}

// title band (marquee when narrow)
static bool flow_draw_title(
    CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int sel, int games_n, int screenWidth,
    int screenHeight, float offY, bool hasClip, int clipTop, int clipBot
)
{
    // snap to an even top row so the band fill shares the bg pattern's phase
    int bandY = FLOW_TITLE_TOP + (int)offY;
    if (bandY & 1)
        bandY -= 1;
    flow_fill_bg(0, bandY, screenWidth, screenHeight - bandY, kFlowPatterns[cf->bgStyle]);

    const char* title = NULL;
    if (sel >= 0 && sel < games_n)
    {
        CB_Game* game = ctx->games->items[sel];
        title = game->displayName;
    }
    else if (sel == games_n)
    {
        title = ctx->trailingTitle;
    }

    if (!title)
        return false;

    bool scrolling = false;
    LCDFont* font = CB_App->subheadFont;
    playdate->graphics->setFont(font);
    playdate->graphics->setDrawMode(kDrawModeFillWhite);

    int margin = 8;
    int textWidth = playdate->graphics->getTextWidth(font, title, strlen(title), kUTF8Encoding, 0);
    int availWidth = screenWidth - margin * 2;
    int fontHeight = playdate->graphics->getFontHeight(font);
    int textY = FLOW_TITLE_TOP + (int)offY + (screenHeight - FLOW_TITLE_TOP - fontHeight) / 2;

    float off = cb_marquee_offset(&cf->textScrollTime, font, title, availWidth, &scrolling);

    // scrolling mode: both endpoints keep an 8px gap (left display edge /
    // right display edge); mid-pan, letters still exit through the edges
    int titleX = scrolling ? 8 - (int)off : margin + (availWidth - textWidth) / 2;

    if (scrolling)
    {
        int bandTop = FLOW_TITLE_TOP + (int)offY;
        int bandBot = bandTop + (screenHeight - FLOW_TITLE_TOP);
        if (hasClip)
        {
            // keep the marquee inside the incoming category's band while pushing
            bandTop = CB_MAX(bandTop, clipTop);
            bandBot = CB_MIN(bandBot, clipBot);
        }
        if (bandBot > bandTop)
        {
            playdate->graphics->setClipRect(0, bandTop, screenWidth, bandBot - bandTop);
            flow_draw_text_outlined(
                title, strlen(title), titleX, textY, CB_LIB_BG_IS_LIGHT(cf->bgStyle)
            );
            playdate->graphics->clearClipRect();
        }
    }
    else
    {
        flow_draw_text_outlined(
            title, strlen(title), titleX, textY, CB_LIB_BG_IS_LIGHT(cf->bgStyle)
        );
    }
    playdate->graphics->setDrawMode(kDrawModeCopy);
    flow_restore_push_clip(hasClip, clipTop, clipBot, screenWidth);
    return scrolling;
}

// position counter, top-right (games only; the "Get ROMs..." card is not
// counted); marquee inside the right strip when narrow
static bool flow_draw_counter(
    CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int sel, int games_n, int screenWidth,
    float offY, int cardRight
)
{
    bool scrolling = false;
    if (sel < 0 || sel >= games_n)
        return scrolling;

    char counter[16];
    snprintf(counter, sizeof(counter), "%d / %d", sel + 1, games_n);
    LCDFont* counterFont = CB_App->labelFont ? CB_App->labelFont : CB_App->bodyFont;
    playdate->graphics->setFont(counterFont);
    playdate->graphics->setDrawMode(kDrawModeFillWhite);

    int stripLeft = cardRight + 10;
    int stripW = screenWidth - 8 - stripLeft;
    if (stripW >= 12)
    {
        float off =
            cb_marquee_offset(&cf->counterScrollTime, counterFont, counter, stripW, &scrolling);
        int cw = playdate->graphics->getTextWidth(
            counterFont, counter, strlen(counter), kUTF8Encoding, 0
        );
        playdate->graphics->setClipRect(stripLeft, 0, stripW, FLOW_TITLE_TOP);
        flow_draw_text_outlined(
            counter, strlen(counter), stripLeft + (stripW - cw) + (int)off, 4 + (int)offY,
            CB_LIB_BG_IS_LIGHT(cf->bgStyle)
        );
        playdate->graphics->clearClipRect();
    }
    playdate->graphics->setDrawMode(kDrawModeCopy);
    return scrolling;
}

// scene chrome: text layers the card stacks slide over/behind - category
// label (top-left), title band (bottom), position counter (top-right)
static void flow_draw_chrome(
    CB_CoverFlow* cf, const CB_CoverFlowContext* ctx, int sel, int games_n, int screenWidth,
    int screenHeight, float offY, bool hasClip, int clipTop, int clipBot, float dw0
)
{
    float centerCardMid = screenWidth / 2.0f + ctx->xShift;
    int cardLeft = (int)(centerCardMid - dw0 / 2.0f);
    int cardRight = (int)(centerCardMid + dw0 / 2.0f);

    bool labelScrolling = flow_draw_category_label(
        cf, ctx, screenWidth, offY, hasClip, clipTop, clipBot, cardLeft, cardRight
    );
    bool titleScrolling = flow_draw_title(
        cf, ctx, sel, games_n, screenWidth, screenHeight, offY, hasClip, clipTop, clipBot
    );
    bool counterScrolling = flow_draw_counter(cf, ctx, sel, games_n, screenWidth, offY, cardRight);

    cf->needsTextScroll = titleScrolling || labelScrolling || counterScrolling;
}

// greedy word-wrap: fills up to maxSlices (offset,len) row slices, breaking at
// spaces (slices trimmed of trailing spaces); UTF-8 sequences stay intact;
// returns the slice count
static int flow_wrap_slices(
    LCDFont* font, const char* text, int maxW, int* outOff, int* outLen, int maxSlices
)
{
    if (!text || !*text || maxSlices <= 0 || maxW <= 0)
        return 0;

    int len = (int)strlen(text);
    int count = 0;
    int start = 0;

    while (start < len && count < maxSlices)
    {
        // starting on a newline: "\n\n" (or more) = one blank spacer row; a
        // single '\n' is just a plain line break
        if (text[start] == '\n')
        {
            if (start + 1 < len && text[start + 1] == '\n')
            {
                outOff[count] = start;
                outLen[count] = 0;
                count++;
                while (start < len && text[start] == '\n')
                    start++;
            }
            else
            {
                start++;
            }
            continue;
        }

        int breakAt = -1;  // index just past the last space in the run
        int i = start;
        int overAt = len;  // first index that exceeds the width budget
        for (; i < len; ++i)
        {
            if (text[i] == ' ')
                breakAt = i + 1;
            if (text[i] == '\n')
            {
                overAt = i;  // hard paragraph break at the newline
                break;
            }
            if (playdate->graphics->getTextWidth(
                    font, text + start, i - start + 1, kUTF8Encoding, 0
                ) > maxW)
            {
                overAt = i;
                break;
            }
        }

        int endRun;
        if (i < len && text[i] == '\n')
        {
            endRun = i;  // slice ends at the newline; it gets skipped below
            overAt = len;
        }
        else if (overAt == len)
            endRun = len;  // remainder fits
        else if (breakAt > start)
            endRun = breakAt;  // last space wins
        else
        {
            endRun = overAt;  // single long word: hard split
            while (endRun > start && ((uint8_t)text[endRun] & 0xC0) == 0x80)
                endRun--;  // don't cut UTF-8 sequences
        }
        if (endRun <= start)
            endRun = start + 1;  // always make progress

        int sliceEnd = endRun;
        while (sliceEnd > start && text[sliceEnd - 1] == ' ')
            sliceEnd--;  // no trailing spaces in the slice

        if (sliceEnd > start)
        {
            outOff[count] = start;
            outLen[count] = sliceEnd - start;
            count++;
        }
        start = endRun;
        while (start < len && text[start] == ' ')
            start++;  // only spaces; newlines are handled by the blank-row branch
    }
    return count;
}

// draw a text panel inside a white card frame (empty-cover states, homebrew
// banner); long lines word-wrap, empty lines are spacer rows, the stack is
// vertically centered and drops lines bottom-first when too tall
static void flow_draw_lines_panel(
    int x, int y, int w, int h, const CB_FlowLine* lines, int lineCount, bool drawText
)
{
    playdate->graphics->fillRect(x, y, w, h, kColorWhite);
    playdate->graphics->drawRect(x, y, w, h, kColorBlack);

    if (!drawText || !lines || lineCount <= 0)
        return;

    LCDFont* font = CB_App->subheadFont;
    playdate->graphics->setFont(font);
    playdate->graphics->setDrawMode(kDrawModeFillBlack);

    const int fontH = playdate->graphics->getFontHeight(font);
    const int pad = 4;
    const int gap = 4;
    const int innerW = w - pad * 2;
    if (innerW <= 0 || h < fontH)
        return;

    const int maxRows = (h - pad * 2) / fontH;

    // wrap the lines one by one, dropping bottom-first when out of height
    int rowOff[24], rowLen[24], rowW[24];
    int rowLine[24];  // which input line each row belongs to
    int rowUseW[24];  // layout width
    int lineFirstRow[6], lineRows[6], lineRowH[6];
    int rowCount = 0;
    int linesShown = 0;
    for (int i = 0; i < lineCount && linesShown < 6 && rowCount < maxRows; ++i)
    {
        if (!lines[i].text)
            continue;
        // per-line font: the composition can override (path line = Roobert)
        LCDFont* lineFont = lines[i].font ? (LCDFont*)lines[i].font : font;
        int lineFontH = playdate->graphics->getFontHeight(lineFont);
        if (!lines[i].text[0])
        {
            // empty line: one blank spacer row
            if (rowCount >= maxRows)
                break;
            lineFirstRow[linesShown] = rowCount;
            lineRows[linesShown] = 1;
            lineRowH[linesShown] = fontH;
            rowOff[rowCount] = 0;
            rowLen[rowCount] = 0;
            rowLine[rowCount] = i;
            rowW[rowCount] = 0;
            rowUseW[rowCount] = 0;
            rowCount++;
            linesShown++;
            continue;
        }

        int tOff[24], tLen[24];
        int slices = flow_wrap_slices(lineFont, lines[i].text, innerW, tOff, tLen, 24);
        if (slices <= 0 || rowCount + slices > maxRows)
            break;  // this line does not fully fit; later lines drop with it

        lineFirstRow[linesShown] = rowCount;
        lineRows[linesShown] = slices;
        lineRowH[linesShown] = lineFontH;
        for (int s = 0; s < slices; ++s)
        {
            rowOff[rowCount + s] = tOff[s];
            rowLen[rowCount + s] = tLen[s];
            rowLine[rowCount + s] = i;
            rowW[rowCount + s] = playdate->graphics->getTextWidth(
                lineFont, lines[i].text + tOff[s], (unsigned)tLen[s], kUTF8Encoding, 0
            );
            rowUseW[rowCount + s] =
                (lines[i].hintWidth > rowW[rowCount + s]) ? lines[i].hintWidth : rowW[rowCount + s];
        }
        rowCount += slices;
        linesShown++;
    }
    if (rowCount == 0)
        return;

    // rows drawn tight, small gap between logical lines (per-line "extra gap
    // before" hint supported), stack centered
    int extraGap = 0;
    for (int i = 0; i < linesShown; ++i)
        extraGap += lines[rowLine[lineFirstRow[i]]].gapBefore;
    int totalH = 0;
    for (int i = 0; i < linesShown; ++i)
        totalH += lineRows[i] * lineRowH[i];
    totalH += (linesShown - 1) * gap + extraGap;
    int rowY = y + (h - totalH) / 2;
    for (int i = 0; i < linesShown; ++i)
    {
        LCDFont* lineFont = lines[i].font ? (LCDFont*)lines[i].font : font;
        playdate->graphics->setFont(lineFont);
        for (int s = 0; s < lineRows[i]; ++s)
        {
            int r = lineFirstRow[i] + s;
            if (rowLen[r] > 0)
            {
                playdate->graphics->drawText(
                    lines[rowLine[r]].text + rowOff[r], (unsigned)rowLen[r], kUTF8Encoding,
                    x + pad + (innerW - rowUseW[r]) / 2, rowY
                );
            }
            rowY += lineRowH[i];
        }
        rowY += gap;
        if (i + 1 < linesShown)
            rowY += lines[rowLine[lineFirstRow[i + 1]]].gapBefore;
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
                  cf->needsTextScroll || ctx->placeholderAnimated || ctx->xShift != 0;
    if (!redraw)
        return;

    cf->needsDisplay = false;

    int screenWidth = playdate->display->getWidth();
    int screenHeight = playdate->display->getHeight();

    cf->bgStyle = preferences_library_background;
    if (cf->bgStyle < 0 || cf->bgStyle >= CB_LIB_BG_COUNT)
        cf->bgStyle = CB_LIB_BG_STRIPE;  // scripts may write any value
    const uint8_t* bgPattern = kFlowPatterns[cf->bgStyle];

    if (cf->switchActive)
    {
        // category switch: old frame scrolls out, new category scrolls in over it
        float p = CB_MIN(1.0f, cf->switchTime / FLOW_SWITCH_TIME);
        float eased = cb_easeInOutQuad(p);
        int shift = (int)(screenHeight * eased);  // rows the old frame has already moved

        // incoming band shares the normal path's background phase: fill the
        // whole LCD with the pattern first, then restore the outgoing snapshot
        // over it
        flow_fill_bg(0, 0, screenWidth, screenHeight, bgPattern);

        uint8_t* fb = playdate->graphics->getFrame();
        if (fb && cf->switchSnap && shift < screenHeight)
        {
            int count = screenHeight - shift;
            if (cf->switchDir > 0)
            {
                // pushed up: snapshot rows [shift, H) land at [0, H - shift)
                memcpy(fb, cf->switchSnap + shift * LCD_ROWSIZE, count * LCD_ROWSIZE);
            }
            else
            {
                // pushed down: snapshot rows [0, H - shift) land at [shift, H)
                memcpy(fb + shift * LCD_ROWSIZE, cf->switchSnap, count * LCD_ROWSIZE);
            }
        }

        int bandTop, bandBot;
        float offY;
        if (cf->switchDir > 0)
        {
            bandTop = screenHeight - shift;
            bandBot = screenHeight;
            offY = (float)(screenHeight - shift);
        }
        else
        {
            bandTop = 0;
            bandBot = shift;
            offY = -(float)(screenHeight - shift);
        }

        playdate->graphics->setDrawMode(kDrawModeCopy);
        playdate->graphics->setClipRect(0, bandTop, screenWidth, bandBot - bandTop);

        float dw0 = flow_anchor_width(cf, sel, games_n);
        float fadeProgress =
            (cf->fadeCount > 0) ? CB_MIN(1.0f, cf->fadeTime / FLOW_FADE_TIME) : 1.0f;
        flow_draw_chrome(
            cf, ctx, sel, games_n, screenWidth, screenHeight, offY, true, bandTop, bandBot, dw0
        );
        flow_draw_fade_stack(cf, fadeProgress);
        flow_draw_cards(cf, ctx, sel, itemCount, games_n, screenWidth, offY, dw0);

        playdate->graphics->clearClipRect();
        playdate->graphics->markUpdatedRows(0, screenHeight - 1);
        return;
    }

    flow_fill_bg(0, 0, screenWidth, screenHeight, bgPattern);
    playdate->graphics->setDrawMode(kDrawModeCopy);

    float dw0 = flow_anchor_width(cf, sel, games_n);

    // text behind; then the outgoing wrap stack above it; the incoming deck
    // stays topmost (exits slide out beneath the incoming one)
    flow_draw_chrome(cf, ctx, sel, games_n, screenWidth, screenHeight, 0.0f, false, 0, 0, dw0);
    float fadeProgress = (cf->fadeCount > 0) ? CB_MIN(1.0f, cf->fadeTime / FLOW_FADE_TIME) : 1.0f;
    flow_draw_fade_stack(cf, fadeProgress);
    flow_draw_cards(cf, ctx, sel, itemCount, games_n, screenWidth, 0.0f, dw0);
}
