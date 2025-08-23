#include "pd_api.h"
#include "patch_download_scene.h"
#include "patches_scene.h"
#include "utility.h"
#include "userstack.h"
#include "jparse.h"
#include "modal.h"
#include "http.h"
#include "script.h"

typedef void(*context_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context);
typedef void(*context_draw_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active);
typedef char*(*context_hint_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context);

#define SCROLL_RATE 2.3f
#define SCROLL_AREA 180

PatchDownloadContext* push_context(CB_PatchDownloadScene* pds);
static bool push_patch_list(CB_PatchDownloadScene* pds);

// modifies a string in-place to replace numeric escape sequences like &#127; with 
// the appropriate byte. Does not affect non-numeric escape sequences like &amp; etc.
static void decode_numeric_escapes(char *s) {
    char *read = s;
    char *write = s;

    while (*read) {
        if (read[0] == '&' && read[1] == '#' ) {
            char *p = read + 2;
            int val = 0;
            int valid = 0;

            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                p++;
                valid = 1;
            }

            if (valid && *p == ';' && val >= 0 && val <= 255) {
                *write++ = (char)val;
                read = p + 1; // skip past ';'
                continue;
            }
        }

        *write++ = *read++;
    }
    *write = '\0';
}

static void update_common(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    if (context->list) CB_ListView_update(context->list);
}

static void draw_common(CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active)
{
    PDRect frame = {x, 0, MIN(LCD_COLUMNS, LCD_COLUMNS - x), SCROLL_AREA};
    context->list->frame = frame;
    context->list->needsDisplay = true;
    
    CB_ListView_draw(
        context->list
    );
}

static json_value get_nth_patch_for_game(CB_PatchDownloadScene* pds, int n)
{
    json_value jv = { .type = kJSONNull };
    JsonArray* arr = (pds->game_hacks.type == kJSONArray)
        ? pds->game_hacks.data.arrayval
        : NULL;
    if (arr && n < arr->n)
    {
        const char* s = NULL;
        json_value j = arr->data[n];
        if (j.type == kJSONInteger)
        {
            char* hackkey_str = aprintf("%d", j.data.intval);
            json_value hacks = json_get_table_value(pds->rhdb, "hacks");
            json_value hack = json_get_table_value(hacks, hackkey_str);
            cb_free(hackkey_str);
            return hack;
        }
    }
    
    return jv;
}

static void context_top_level_select_download(unsigned flags, void* ud)
{
    CB_PatchDownloadScene* pds = ud;
    pds->http_in_progress = 0;
    if (flags & HTTP_ENABLE_DENIED)
    {
        CB_Modal* modal = CB_Modal_new(
            "CrankBoy must be granted networking privileges in order to download ROM hacks. You can do this from the Playdate OS settings.",
            NULL, NULL, NULL
        );
        modal->width = 350;
        modal->height = 180;
        CB_presentModal(modal->scene);
    }
    else if (flags & ~HTTP_ENABLE_ASKED)
    {
        char* s = aprintf("HTTP Error (flags=%x)", flags);
        CB_Modal* modal = CB_Modal_new(
            s,
            NULL, NULL, NULL
        );
        cb_free(s);
        CB_presentModal(modal->scene);
    }
    else
    {
        push_patch_list(pds);
    }
}

static void context_top_level_update(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    update_common(pds, context);
    
    if (CB_App->buttons_pressed & kButtonA)
    {
        switch(context->list->selectedItem)
        {
        case 0:
            {
                cb_play_ui_sound(CB_UISound_Confirm);
                CB_PatchDownloadScene* s = CB_PatchDownloadScene_new(pds->game);
                CB_presentModal(s->scene);
            }
            break;
        case 1:
            cb_play_ui_sound(CB_UISound_Confirm);
            {
                json_value jdomain = json_get_table_value(pds->rhdb, "domain");
                if (jdomain.type == kJSONString)
                {
                    pds->http_in_progress = 1;
                    enable_http(
                        jdomain.data.stringval,
                        "to download game patches",
                        context_top_level_select_download,
                        pds
                    );
                }
                else
                {
                    CB_Modal* modal = CB_Modal_new(
                        "Failed to determine patch host.\n \n(Is rhdb.json present?)",
                        NULL, NULL, NULL
                    );
                    CB_presentModal(modal->scene);
                }
            }
            break;
        }
    }
    else if (CB_App->buttons_pressed & kButtonB)
    {
        --pds->target_context_depth;
    }
}

static void context_top_level_draw(CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active)
{
    draw_common(pds, context, x, active);
}

static char* context_hack_list_hint(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    json_value hack = get_nth_patch_for_game(pds, context->list->selectedItem);
    json_value jauthor = json_get_table_value(hack, "author");
    const char* author = NULL;
    if (jauthor.type == kJSONString) author = jauthor.data.stringval;
    
    json_value jdate = json_get_table_value(hack, "reldate");
    const char* date = NULL;
    if (jdate.type == kJSONString) date = jdate.data.stringval;
    
    return aprintf(
        "Author: %s\nRelease Date: %s\n",
        author ? author : "?",
        date ? date : "?"
    );
}

static char* context_top_level_hint(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    switch (context->list->selectedItem)
    {
    case 0:
        return aprintf("Toggle installed patches and and rearrange the order in which they are applied.");
        break;
    case 1:
        return aprintf("Download ROM hacks, translations, etc. for \"%s.\"\n(Mirrored from romhacking.net)", pds->game->names->name_short_leading_article);
        break;
    }
    
    return NULL;
}

// a value that changes when selection changes.
static uint32_t get_hint_key(CB_PatchDownloadScene* pds)
{
    if (pds->target_context_depth != pds->context_depth_p) return -1;
    uint32_t key = (pds->context_depth << 24);
    PatchDownloadContext* context = &pds->context[pds->context_depth-1];
    key |= (context->list->selectedItem) & 0xFFFFFF;
    return key;
}

static context_fn context_free[PDSCT_MAX] = {
    NULL, NULL, NULL,
    NULL, NULL, NULL
};

static context_hint_fn context_hint[PDSCT_MAX] = {
    context_top_level_hint, context_hack_list_hint, NULL,
    NULL, NULL, NULL
};

static context_fn context_update[PDSCT_MAX] = {
    context_top_level_update, update_common, NULL,
    update_common, NULL, NULL
};

static context_draw_fn context_draw[PDSCT_MAX] = {
    context_top_level_draw, draw_common, NULL,
    draw_common, NULL, NULL
};

PatchDownloadContext* push_context(CB_PatchDownloadScene* pds)
{
    if (pds->context_depth >= CB_PATCHDOWNLOAD_STACK_MAX_DEPTH) return NULL;
    PatchDownloadContext* context = &pds->context[pds->context_depth++];
    pds->target_context_depth = pds->context_depth - 1;
    memset(context, 0, sizeof(*context));
    
    context->list = CB_ListView_new();
    
    return context;
}

void pop_context(CB_PatchDownloadScene* pds)
{
    PatchDownloadContext* context = &pds->context[--pds->context_depth];
    
    context_fn _free = context_free[context->type];
    if (_free) _free(pds, context);
    
    if (context->list)
    {
        CB_ListView_free(context->list);
    }
}

void CB_PatchDownloadScene_free(CB_PatchDownloadScene* pds)
{
    CB_Scene_free(pds->scene);
    while (pds->context_depth > 0)
    {
        pop_context(pds);
    }
    cb_free(pds->patches_dir_path);
    free_json_data(pds->rhdb);
    cb_free(pds);
}

void CB_PatchDownloadScene_update(CB_PatchDownloadScene* pds, uint32_t u32enc_dt)
{
    float dt = UINT32_AS_FLOAT(u32enc_dt);
    if (CB_App->pendingScene)
    {
        return;
    }
    
    if (pds->context_depth_p != pds->target_context_depth)
    {
        // scroll left/right
        int prev_context_depth = pds->context_depth_p;
        pds->context_depth_p = toward(pds->context_depth_p, pds->target_context_depth, dt * SCROLL_RATE);
        if (pds->context_depth_p < 0)
        {
            CB_dismiss(pds->scene);
            return;
        }
        else if (pds->context_depth_p < prev_context_depth - 1)
        {
            pop_context(pds);
        }
    }
    else if (!pds->http_in_progress)
    {
        // update selected
        PatchDownloadContext* context = &pds->context[pds->context_depth - 1];
        context_fn fn = context_update[context->type];
        if (fn) fn(pds, context);
    }
    
    int margin = 48;
    
    // draw scenes
    
    int n = (pds->context_depth_p >= 1) ? 2 : 1;
    
    playdate->graphics->clear(kColorWhite);
    
    for (int i = 0; i < n; ++i)
    {
        int ci = ceil(pds->context_depth_p) - i;
        PatchDownloadContext* context = &pds->context[ci];
        float d = ci - pds->context_depth_p;
        float x = d * (LCD_COLUMNS) + margin;
        context_draw_fn fn = context_draw[context->type];
        
        if (fn)
        {
            fn(pds, context, x, i == 0);
        }
    }
    
    // hint
    uint32_t hint_key = get_hint_key(pds);
    if (hint_key != pds->cached_hint_key)
    {
        pds->cached_hint_key = hint_key;
        cb_free(pds->cached_hint);
        
        if (hint_key == (uint32_t)(-1))
        {
            pds->cached_hint = NULL;
        }
        else
        {
            PatchDownloadContext* context = &pds->context[pds->context_depth-1];
            context_hint_fn fn = context_hint[context->type];
            if (fn)
            {
                pds->cached_hint = fn(pds, context);
            }
        }
    }
    
    if (pds->cached_hint)
    {
        int margin = 16;
        
        LCDFont* font = CB_App->labelFont;
        playdate->graphics->setFont(font);
        playdate->graphics->fillRect(
            0, SCROLL_AREA, LCD_COLUMNS, 2, kColorBlack
        );
        playdate->graphics->drawTextInRect(
            pds->cached_hint, strlen(pds->cached_hint), kUTF8Encoding, margin, SCROLL_AREA + margin/2, LCD_COLUMNS - 2 * margin, LCD_ROWS - SCROLL_AREA - margin, kWrapWord,
            kAlignTextLeft
        );
    }
}

static bool push_patch_list(CB_PatchDownloadScene* pds)
{
    if (!pds->header_name[0])
    {
        CB_Modal* modal = CB_Modal_new(
            "ROM lacks a title in its header, so CrankBoy cannot match it to any patch database",
            NULL, NULL, NULL
        );
        CB_presentModal(modal->scene);
        return false;
    }
    else if (pds->game_hacks.type != kJSONArray)
    {
        char* msg = aprintf("ROM header name \"%s\" not found in patch database", pds->header_name);
        CB_Modal* modal = CB_Modal_new(
            msg,
            NULL, NULL, NULL
        );
        cb_free(msg);
        CB_presentModal(modal->scene);
        return false;
    }
    
    PatchDownloadContext* context = push_context(pds);
    if (!context) return false;
    
    JsonArray* arr = (pds->game_hacks.type == kJSONArray)
        ? pds->game_hacks.data.arrayval
        : NULL;
    if (arr)
    {
        for (int i = 0; i < arr->n; ++i)
        {
            const char* s = NULL;
            json_value j = arr->data[i];
            if (j.type == kJSONInteger)
            {
                char* hackkey_str = aprintf("%d", j.data.intval);
                json_value hacks = json_get_table_value(pds->rhdb, "hacks");
                json_value hack = json_get_table_value(hacks, hackkey_str);
                cb_free(hackkey_str);
                json_value title = json_get_table_value(hack, "title");
                if (title.type == kJSONString)
                {
                    s = title.data.stringval;
                    decode_numeric_escapes((char*)s);
                }
            }
            
            CB_ListItemButton* itemButton;
            
            itemButton = CB_ListItemButton_new(s ? s : "?");
            array_push(context->list->items, itemButton);
        }
    }
    
    CB_ListView_reload(context->list);
    
    context->type = PDSCT_LIST_PATCHES;
    return true;
}

static bool push_top_level(CB_PatchDownloadScene* pds)
{
    CB_ListItemButton* itemButton;
    PatchDownloadContext* context = push_context(pds);
    if (!context) return false;
    
    context->type = PDSCT_TOP_LEVEL;
    
    itemButton = CB_ListItemButton_new("Manage…");
    array_push(context->list->items, itemButton);
    
    itemButton = CB_ListItemButton_new("Download…");
    array_push(context->list->items, itemButton);
    
    CB_ListView_reload(context->list);
    
    return true;
}

CB_PatchDownloadScene* CB_PatchDownloadScene_new(CB_Game* game)
{
    CB_Scene* scene = CB_Scene_new();
    CB_PatchDownloadScene* pds = allocz(CB_PatchDownloadScene);
    pds->scene = scene;
    pds->game = game;
    scene->managedObject = pds;
    
    pds->cached_hint_key = -2;
    pds->patches_dir_path = get_patches_directory(game->fullpath);

    // Create patch install directory if it doesn't already exist.
    // We must run this on the main stack, otherwise it can fail
    // in unpredictable ways, like truncated paths.
    call_with_main_stack_1(playdate->file->mkdir, pds->patches_dir_path);
    
    call_with_main_stack_3(parse_json, ROMHACK_DB_FILE, &pds->rhdb, kFileRead);
    
    // FIXME: we don't really need script info; this is just a convenient way to
    // get the header name.
    ScriptInfo* info = script_get_info_by_rom_path_and_get_header_name(
        game->fullpath, pds->header_name
    );
    script_info_free(info);
    
    json_value lookup = json_get_table_value(pds->rhdb, "lookup");
    json_value gamekey = json_get_table_value(lookup, pds->header_name);
    if (gamekey.type == kJSONString)
    {
        pds->gamekey = gamekey.data.stringval;
        json_value g2h = json_get_table_value(pds->rhdb, "g2h");
        pds->game_hacks = json_get_table_value(g2h, pds->gamekey);
    }
    
    scene->update = (void*)CB_PatchDownloadScene_update;
    scene->free = (void*)CB_PatchDownloadScene_free;
    
    push_top_level(pds);
    
    return pds;
}