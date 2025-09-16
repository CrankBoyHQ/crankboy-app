#include "homebrew_hub_scene.h"
#include "http.h"
#include "modal.h"
#include "jparse.h"
#include "scenes/image_conversion_scene.h"
#include "userstack.h"

#define SCROLL_RATE 2.3f
#define kDividerX 240
#define kRightPanePadding 10
#define PDS_FONT CB_App->bodyFont

#define HOLD_TIME_SUPPRESS_RELEASE 0.25f
#define HOLD_TIME_MARGIN 0.15f
#define HOLD_TIME 1.09f
#define HOLD_FADE_RATE 2.9f

#define DISK_IMAGE "homebrew.pdi"

typedef struct
{
    CB_HomebrewHubScene* hbs;
} PatchDownloadUD;
typedef void (*context_update_fn)(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt
);
typedef void (*context_free_fn)(CB_HomebrewHubScene* hbs, HomebrewHubContext* context);
typedef void (*context_draw_fn)(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, int x, bool active
);
typedef char* (*context_hint_fn)(CB_HomebrewHubScene* hbs, HomebrewHubContext* context);

static context_free_fn context_free[HBSCT_MAX] = {NULL, NULL, NULL};

static const char* hb_platforms[] = {
    "GB",
    "GBC",
};

static bool push_list_search(CB_HomebrewHubScene* hbs, const char* platform);

static char* context_list_search_hint(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    switch (context->list->selectedItem)
    {
    case 0:
        return aprintf(
            "%sUse LEFT & RIGHT to switch pages.",
            hbs->active_http_connection
                ? "Now loading...\n\n"
                : ""
        );
    default: {
            int index = context->list->selectedItem - 1;
            json_value jentries = json_get_table_value(hbs->jsearch, "entries");
            if (jentries.type == kJSONArray)
            {
                JsonArray* array = jentries.data.arrayval;
                if (index < array->n)
                {
                    json_value je = array->data[index];
                    if (je.type == kJSONTable)
                    {
                        const char* title = json_as_string(json_get_table_value(je, "title"));
                        const char* developer = json_as_string(json_get_table_value(je, "developer"));
                        const char* platform = json_as_string(json_get_table_value(je, "platform"));
                        const char* date = json_as_string(json_get_table_value(je, "date"));
                        if (!date) date = json_as_string(json_get_table_value(je, "forstadded_date"));
                        
                        // strip 'T' and onward
                        if (date)
                        {
                            char* date_tchar = strchr(date, 'T');
                            if (date_tchar) date_tchar[0] = 0;
                        }
                        
                        return aprintf(
                            "Title: %s\nDeveloper: %s\nPlatform: %s\nDate: %s",
                            title, developer, platform, date ? date : "unknown"
                        );
                    }
                }
            }
            
            return NULL;
        }
        break;
    }
}

static char* context_top_level_hint(CB_HomebrewHubScene* pds, HomebrewHubContext* context)
{
    switch (context->list->selectedItem)
    {
    case 0:
        return aprintf(
            "Browse GB homebrew from Homebrew Hub."
        );
        break;
    case 1:
        return aprintf(
            "Browse GB Color homebrew from Homebrew Hub.\n \nNote: CGB support in CrankBoy is still experimental."
        );
        break;
    default:
        return NULL;
    }
}

static void draw_common(
    CB_HomebrewHubScene* pds, HomebrewHubContext* context, int x, bool active
)
{
    int left_margin = 4;
    int right_margin = 0;

    int header_y = 0;

    PDRect frame = {
        x + left_margin, header_y, kDividerX - left_margin - right_margin, LCD_ROWS - header_y
    };

    context->list->frame = frame;
    context->list->needsDisplay = true;

    CB_ListView_draw(context->list);
}

static void update_common(CB_HomebrewHubScene* pds, HomebrewHubContext* context, float dt)
{
    if (context->list)
        CB_ListView_update(context->list);
}

static void context_top_level_update(CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt)
{
    update_common(hbs, context, dt);
    
    bool a_pressed = (CB_App->buttons_pressed & kButtonA);
    
    if (a_pressed)
    {
        push_list_search(hbs, hb_platforms[context->list->selectedItem]);
    }
}

const char* get_best_screenshot(JsonArray* screenshots)
{
    int bestidx = -1;
    for (int i = 0; i < screenshots->n; ++i)
    {
        const char* s = json_as_string(screenshots->data[i]);
        if (s)
        {
            if (!strcasecmp(s, "cover.png")) return s;
            else if (!strcasecmp(s, "cover.bmp")) return s;
            else if (!strcasecmp(s, "cover.jpg")) return s;
            else if (endswithi(s, ".png")) bestidx = i;
            else if (endswithi(s, ".bmp")) bestidx = i;
        }
    }
    
    if (bestidx < 0) return NULL;
    
    return json_as_string(screenshots->data[bestidx]);
}

static void cover_art_cb(unsigned flags, char* data, size_t data_len, CB_HomebrewHubScene* hbs)
{
    hbs->active_http_connection = 0;
    if (flags & (~HTTP_ENABLE_ASKED))
    {
        return;
    }
    else
    {
        size_t pdi_size;
        void* pdi_data = png_to_pdi(hbs->download_image_name, data, data_len, &pdi_size, LCD_COLUMNS - kDividerX, LCD_ROWS/2);
        if (pdi_data && pdi_size)
        {
            if (pdi_size < (1 << 16))
            {
                cb_write_entire_file(
                    DISK_IMAGE, pdi_data, pdi_size
                );
                playdate->system->logToConsole("successfully retrieved image");
            } else {
                playdate->system->logToConsole("Not saving homebrew.pdi because file size is too big (%u bytes)", (unsigned)pdi_size);
            }
        }
        
        if (pdi_data) cb_free(pdi_data);
    }
}

static void context_list_search_update(CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt)
{
    update_common(hbs, context, dt);
    
    bool a_pressed = (CB_App->buttons_pressed & kButtonA);
    bool l_pressed = (CB_App->buttons_pressed & kButtonLeft);
    bool r_pressed = (CB_App->buttons_pressed & kButtonRight);
    
    if (context->list->selectedItem <= 0)
    {
        if (l_pressed)
        {
            if (--context->i <= 0) context->i = MAX(1, hbs->max_pages);
        }
        else if (r_pressed || a_pressed)
        {
            if (++context->i > hbs->max_pages) context->i = 1;
        }
    }
    else
    {
        int selected = context->list->selectedItem - 1;
        unsigned dlii = (context->i << 16) | selected;
        if ((dlii != hbs->download_image_index || (!hbs->active_http_connection && !hbs->download_image)))
        {
            hbs->download_image_index = dlii;
            if (hbs->download_image)
            {
                playdate->graphics->freeBitmap(hbs->download_image);
                hbs->download_image = NULL;
            }
            
            hbs->download_image = call_with_main_stack_2(playdate->graphics->loadBitmap, DISK_IMAGE, NULL);
            if (hbs->download_image)
            {
                playdate->file->unlink(DISK_IMAGE, false);
            }
            else
            {
                json_value jentries = json_get_table_value(hbs->jsearch, "entries");
                if (jentries.type == kJSONArray)
                {
                    JsonArray* array = jentries.data.arrayval;
                    
                    if (selected < array->n)
                    {
                        json_value je = array->data[selected];
                        json_value jscreenshots = json_get_table_value(je, "screenshots");
                        const char* slug = json_as_string(json_get_table_value(je, "slug"));
                        const char* base = json_as_string(json_get_table_value(je, "basepath"));
                        if (jscreenshots.type == kJSONArray && slug)
                        {
                            JsonArray* screenshots = jscreenshots.data.arrayval;
                            const char* screenshot = get_best_screenshot(screenshots);
                            if (screenshot)
                            {
                                char* urlpath = aprintf(
                                    "%s/%s/entries/%s/%s",
                                    CB_App->hbStaticPath,
                                    base,
                                    slug,
                                    screenshot
                                );
                                hbs->download_image_name = screenshot;
                                
                                // get image
                                http_cancel(hbs->active_http_connection);
                                hbs->active_http_connection = http_get(
                                    CB_App->hbApiDomain,
                                    urlpath,
                                    "to retrieve cover art",
                                    (void*)cover_art_cb,
                                    12 * 1000,
                                    hbs
                                );
                                
                                cb_free(urlpath);
                            }
                        }
                    }
                }
            }
        }
        
        if (a_pressed)
        {
            push_list_search(hbs, hb_platforms[context->list->selectedItem]);
        }
    }
}



static context_hint_fn context_hint[HBSCT_MAX] = {
    context_top_level_hint, context_list_search_hint, NULL
};

static context_update_fn context_update[HBSCT_MAX] = {
    context_top_level_update, context_list_search_update, NULL
};

static context_draw_fn context_draw[HBSCT_MAX] = {
    draw_common, draw_common, NULL
};

static HomebrewHubContext* push_context(CB_HomebrewHubScene* hbs)
{
    if (hbs->context_depth >= CB_HBH_STACK_MAX_DEPTH)
        return NULL;
    HomebrewHubContext* context = &hbs->context[hbs->context_depth++];
    hbs->target_context_depth = hbs->context_depth - 1;
    memset(context, 0, sizeof(*context));

    context->list = CB_ListView_new();
    context->list->font = PDS_FONT;
    context->list->paddingTop = 15;
    context->list->paddingBottom = 15;
    return context;
}

static void pop_context(CB_HomebrewHubScene* hbs)
{
    playdate->system->logToConsole("Pop context\n");
    HomebrewHubContext* context = &hbs->context[--hbs->context_depth];

    context_free_fn _free = context_free[context->type];
    if (_free)
        _free(hbs, context);

    if (context->list)
    {
        CB_ListView_free(context->list);
    }
}

static void hbs_http_cancel(CB_HomebrewHubScene* hbs)
{
    http_cancel(hbs->active_http_connection);
    hbs->active_http_connection = 0;
}

static HomebrewHubContext* getFirstMatchingContext(CB_HomebrewHubScene* hbs, HomebrewHubSceneContextType type)
{
    for (int i = 0; i < CB_HBH_STACK_MAX_DEPTH; ++i)
    {
        if (hbs->context[i].type == type) return &hbs->context[i];
    }
    
    return NULL;
}

// a value that changes when selection changes.
static uint32_t get_hint_key(CB_HomebrewHubScene* pds)
{
    if (pds->target_context_depth != pds->context_depth_p)
        return -1;
    uint32_t key = (pds->context_depth << 24);
    HomebrewHubContext* context = &pds->context[pds->context_depth - 1];
    key |= (context->list->selectedItem) & 0xFFFFFF;
    return key + 1;
}

static void populate_search_listing(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    json_value jmaxpage = json_get_table_value(hbs->jsearch, "page_total");
    json_value jpage = json_get_table_value(hbs->jsearch, "page_current");
    
    if (jmaxpage.type == kJSONInteger && jpage.type == kJSONInteger)
    {
        hbs->max_pages = jmaxpage.data.intval;
        context->i = jpage.data.intval;
    }
    
    
    CB_ListView_clear(context->list);
    
    char* label = (hbs->max_pages)
        ? aprintf("< Page %d of %d >", context->i, hbs->max_pages)
        : aprintf("< Page %d >", context->i);
    CB_ListItemButton* itemButton = CB_ListItemButton_new(
        label
    );
    cb_free(label);
    array_push(context->list->items, itemButton);

    json_value jentries = json_get_table_value(hbs->jsearch, "entries");
    if (jentries.type == kJSONArray)
    {
        JsonArray* array = jentries.data.arrayval;
        for (int i = 0; i < array->n; ++i)
        {
            json_value je = array->data[i];
            json_value jtitle = json_get_table_value(je, "title");
            if (jtitle.type == kJSONString)
            {
                array_push(context->list->items, CB_ListItemButton_new(jtitle.data.stringval));
            }
            else
            {
                array_push(context->list->items, CB_ListItemButton_new("[Error]"));
            }
        }
    }
    CB_ListView_reload(context->list);
}

static void http_search_cb(unsigned flags, char* data, size_t data_len, CB_HomebrewHubScene* hbs)
{
    hbs->active_http_connection = 0;
    hbs->cached_hint_key = 0;
    
    if (flags & (HTTP_CANCELLED)) return;
    if (flags == (HTTP_ENABLE_DENIED | HTTP_ENABLE_ASKED)) return;
    
    if (flags & (~HTTP_ENABLE_ASKED))
    {
        hbs->target_context_depth = 0;
        char* s = aprintf("HTTP Error.\n \nCode: 0x%x", flags);
        CB_presentModal(
            CB_Modal_new(s, NULL, NULL, NULL)->scene
        );
        
        cb_free(s);
    }
    else
    {
        char* json_begin = memchr(data, '{', data_len);
        if (json_begin == NULL)
        {
        err_invalid_json:
            CB_presentModal(
                CB_Modal_new("Invalid JSON received", NULL, NULL, NULL)->scene
            );
            return;
        }
        
        HomebrewHubContext* context = getFirstMatchingContext(hbs, HBSCT_LIST_SEARCH);
        if (context)
        {
            free_json_data(hbs->jsearch);
            hbs->jsearch.type = kJSONNull;
            if (parse_json_string(data, &hbs->jsearch) == 0)
            {
                goto err_invalid_json;
            }
            
            populate_search_listing(hbs, context);
        }
    }
}

static void http_search(CB_HomebrewHubScene* hbs, int page_index, const char* platform)
{
    char* urlpath = aprintf("%s/search?platform=%s&page=%d", CB_App->hbApiPath, platform, MAX(page_index, 1));
    
    http_cancel(hbs->active_http_connection);
    
    hbs->max_pages = 0;
    hbs->active_http_connection = http_get(CB_App->hbApiDomain, urlpath, "to browse homebrew", (void*)http_search_cb, 15 * 1000, hbs);
    
    cb_free(urlpath);
}

static bool push_list_search(CB_HomebrewHubScene* hbs, const char* platform)
{
    CB_ListItemButton* itemButton;
    HomebrewHubContext* context = push_context(hbs);
    if (!context)
        return false;

    hbs->max_pages = 0;
    context->type = HBSCT_LIST_SEARCH;
    context->str = platform;
    context->i = 1; // page
    
    http_search(hbs, context->i, platform);
    
    itemButton = CB_ListItemButton_new("< Page 1 >");
    array_push(context->list->items, itemButton);
    
    CB_ListView_reload(context->list);
    
    return true;
}

static bool push_top_level(CB_HomebrewHubScene* hbs)
{
    CB_ListItemButton* itemButton;
    HomebrewHubContext* context = push_context(hbs);
    if (!context)
        return false;

    context->type = HBSCT_TOP_LEVEL;

    itemButton = CB_ListItemButton_new("Browse GB games…");
    array_push(context->list->items, itemButton);
    
    itemButton = CB_ListItemButton_new("Browse CGB games…");
    array_push(context->list->items, itemButton);

    CB_ListView_reload(context->list);

    return true;
}

void CB_HomebrewHubScene_update(CB_HomebrewHubScene* hbs, uint32_t u32enc_dt)
{
    float dt = UINT32_AS_FLOAT(u32enc_dt);
    if (CB_App->pendingScene)
    {
        return;
    }

    if (hbs->context_depth_p != hbs->target_context_depth)
    {
        hbs->context_depth_p =
            toward(hbs->context_depth_p, hbs->target_context_depth, dt * SCROLL_RATE);
        if (hbs->context_depth_p < 0)
        {
            CB_dismiss(hbs->scene);
            return;
        }
        else if (hbs->context_depth_p <= hbs->context_depth - 2 && hbs->context_depth > 0)
        {
            pop_context(hbs);
        }
    }
    else if (CB_App->buttons_pressed & kButtonB)
    {
        --hbs->target_context_depth;
        http_cancel(hbs->active_http_connection);
        hbs->active_http_connection = 0;
    }
    
    bool isAnimating = (hbs->context_depth_p != hbs->target_context_depth);
    playdate->graphics->clear(kColorWhite);
    int list_padding_top = 24;
    
    int n = (hbs->context_depth_p >= 1) ? 2 : 1;
    
    for (int i = 0; i < n; ++i)
    {
        int ci = ceil(hbs->context_depth_p) - i;
        if (ci < 0 || ci >= hbs->context_depth)
            continue;
        
        HomebrewHubContext* context = &hbs->context[ci];

        if (context->list)
        {
            context->list->paddingTop = list_padding_top;
            CB_ListView_invalidateLayout(context->list);
        }
        
        if (!isAnimating && i == 0)
        {
            int old_selection = -1;
            if (context->list)
            {
                old_selection = context->list->selectedItem;
            }

            context_update_fn fn = context_update[context->type];
            if (fn)
                fn(hbs, context, dt);

            if (context->list && old_selection != -1 &&
                old_selection != context->list->selectedItem)
            {
                cb_play_ui_sound(CB_UISound_Navigate);
            }
        }
        else if (isAnimating)
        {
            if (context->list)
                CB_ListView_update(context->list);
        }
        
        float d = ci - hbs->context_depth_p;
        float x = d * kDividerX;
        context_draw_fn fn = context_draw[context->type];
        if (context->list)
            context->list->hideScrollIndicator = isAnimating;
        if (fn)
            fn(hbs, context, x, i == 0);
    }
    
    int header_y = 0;
    
    playdate->graphics->fillRect(
        kDividerX, header_y, LCD_COLUMNS - kDividerX, LCD_ROWS - header_y, kColorWhite
    );

    uint32_t hint_key = get_hint_key(hbs);
    if (hint_key != hbs->cached_hint_key)
    {
        if (hint_key != (uint32_t)(-1))
        {
            hbs->cached_hint_key = hint_key;
            cb_free(hbs->cached_hint);
            HomebrewHubContext* context = &hbs->context[hbs->context_depth - 1];
            context_hint_fn fn = context_hint[context->type];
            if (fn)
                hbs->cached_hint = fn(hbs, context);
            else
                hbs->cached_hint = NULL;
        }
    }

    if (hbs->cached_hint)
    {
        LCDFont* font = CB_App->labelFont;
        playdate->graphics->setFont(font);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        int rightPaneX = kDividerX + kRightPanePadding;

        // Calculate dynamic top padding for the RIGHT hint pane (29px -> 20px)
        int hint_padding_top = 29;
        int rightPaneY = header_y + hint_padding_top;

        int rightPaneWidth = LCD_COLUMNS - kDividerX - (kRightPanePadding * 2);
        int rightPaneHeight = LCD_ROWS - rightPaneY;
        playdate->graphics->drawTextInRect(
            hbs->cached_hint, strlen(hbs->cached_hint), kUTF8Encoding, rightPaneX, rightPaneY,
            rightPaneWidth, rightPaneHeight, kWrapWord, kAlignTextLeft
        );
    }
    
    // TODO: only show in some contexts
    if (hbs->download_image)
    {
        int w, h;
        playdate->graphics->getBitmapData(
            hbs->download_image,
            &w, &h, NULL, NULL, NULL
        );
        playdate->graphics->setDrawMode(kDrawModeCopy);
        playdate->graphics->drawBitmap(
            hbs->download_image,
            kDividerX + MAX(0, (LCD_COLUMNS - kDividerX - w)/2), LCD_ROWS - h,
            kBitmapUnflipped
        );
    }

    playdate->graphics->drawLine(kDividerX, header_y, kDividerX, LCD_ROWS, 1, kColorBlack);
}

void CB_HomebrewHubScene_free(CB_HomebrewHubScene* hbs)
{
    if (hbs->active_http_connection)
    {
        http_cancel(hbs->active_http_connection);
        hbs->active_http_connection = 0;
    }

    CB_Scene_free(hbs->scene);
    while (hbs->context_depth > 0)
    {
        pop_context(hbs);
    }
    if (hbs->download_image) cb_free(hbs->download_image);
    cb_free(hbs->cached_hint);
    free_json_data(hbs->jsearch);
    cb_free(hbs);
}

CB_HomebrewHubScene* CB_HomebrewHubScene_new(
    void
)
{
    CB_Scene* scene = CB_Scene_new();
    CB_HomebrewHubScene* hbs = allocz(CB_HomebrewHubScene);
    hbs->scene = scene;
    hbs->option_hold_time = 0.0f;
    scene->managedObject = hbs;

    hbs->cached_hint_key = -2;

    scene->update = (void*)CB_HomebrewHubScene_update;
    scene->free = (void*)CB_HomebrewHubScene_free;

    push_top_level(hbs);

    return hbs;
}