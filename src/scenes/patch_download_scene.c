#include "pd_api.h"
#include "patch_download_scene.h"
#include "patches_scene.h"
#include "info_scene.h"
#include "utility.h"
#include "userstack.h"
#include "jparse.h"
#include "modal.h"
#include "http.h"
#include "script.h"
#include "softpatch.h"

typedef void(*context_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context);
typedef void(*context_draw_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active);
typedef char*(*context_hint_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context);

#define SCROLL_RATE 2.3f
#define SCROLL_AREA 180

PatchDownloadContext* push_context(CB_PatchDownloadScene* pds);
static bool push_patch_list(CB_PatchDownloadScene* pds);
static bool push_file_browser(CB_PatchDownloadScene* pds, json_value fs);
static void on_get_textfile(unsigned flags, char* data, size_t data_len, void* ud);
static void on_get_patch(unsigned flags, char* data, size_t data_len, void* ud);

enum file_type
{
    FT_DIRECTORY = 0,
    FT_TEXT = 1,
    FT_PATCH_SUPPORTED = 2,
    FT_PATCH_UNSUPPORTED = 3,
    FT_UNSUPPORTED = 4
};
#define FILETYPE_BITS 4

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

static json_value get_nth_patch_for_game(CB_PatchDownloadScene* pds, int n, int* o_hackkey)
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
            if (o_hackkey) *o_hackkey = j.data.intval;
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
    
    // already cancelled?
    if (pds->http_in_progress != 1)
    {
        pds->http_in_progress = 0;
        return;
    }
    
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
        char* s = aprintf("HTTP Error (flags=0x%03x)", flags);
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

static char* get_path_to_selected_item(CB_PatchDownloadScene* pds, int depth)
{
    PatchDownloadContext* context = &pds->context[depth];
    if (context->type != PDSCT_PATCH_FILES_BROWSE)
    {
        return aprintf("z%s", pds->filekey);
    }
    else
    {
        char* prefix = get_path_to_selected_item(pds, depth-1);
        if (!prefix) return NULL;
        int i = context->list->selectedItem;
        JsonObject* obj = (context->j.type == kJSONTable)
            ? context->j.data.tableval
            : NULL;
        if (!obj || i >= obj->n)
        {
            cb_free(prefix);
            return NULL;
        }
        char* full = aprintf("%s/%s", prefix, obj->data[i].key);
        cb_free(prefix);
        return full;
    }
}

static void context_patch_files_browse_update(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    update_common(pds, context);
    
    if (CB_App->buttons_pressed & kButtonA)
    {
        JsonObject* obj = (context->j.type == kJSONTable)
            ? context->j.data.tableval
            : NULL;
        
        int i = context->list->selectedItem;
        if (i < context->list->items->length)
        {
            const CB_ListItemButton* const button = context->list->items->items[i];
            enum file_type const ft = button->ud.uint & ((1 << FILETYPE_BITS) - 1);
            unsigned const file_size = (button->ud.uint >> FILETYPE_BITS);
            bool const unknown_file_size = (file_size >= (1 << (32 - FILETYPE_BITS)));
            
            if (unknown_file_size)
            {
                CB_Modal* modal = CB_Modal_new(
                    "Unknown file size.",
                    NULL, NULL, NULL
                );
                CB_presentModal(modal->scene);
                return;
            }
            
            if (ft == FT_DIRECTORY)
            {
                if (i < obj->n)
                {
                    push_file_browser(pds, obj->data[i].value);
                }
                return;
            }
            
            if (ft == FT_TEXT || ft == FT_PATCH_SUPPORTED)
            {
                char* path = get_path_to_selected_item(pds, pds->context_depth-1);
                if (!path)
                {
                    playdate->system->logToConsole("unable to get path to item");
                    return;
                }
                
                char* http_path = aprintf("%sextracted/%s", pds->prefix, path);
                cb_free(path);
                
                char* http_path_san = sanitize_url_path(http_path);
                cb_free(http_path);
                
                pds->http_in_progress = 1;
                if (ft == FT_TEXT)
                {
                    pds->text_file_title = obj->data[i].key;
                    http_get(
                        pds->domain, http_path_san, "to download this text file", 
                        on_get_textfile, 15000, pds, NULL
                    );
                }
                else
                {
                    pds->basename = obj->data[i].key;
                    CB_ASSERT(ft == FT_PATCH_SUPPORTED);
                    http_get(
                        pds->domain, http_path_san, "to download this patch file", 
                        on_get_patch, 15000, pds, NULL
                    );
                }
                cb_free(http_path_san);
            }
        }
    }
}

static void context_patch_list_update(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    update_common(pds, context);
    
    if (CB_App->buttons_pressed & kButtonA)
    {
        pds->selected_hack = get_nth_patch_for_game(pds, context->list->selectedItem, &pds->selected_hack_key);
        json_value jfilekey = json_get_table_value(pds->selected_hack, "filekey");
        const char* filekey = (jfilekey.type == kJSONString)
            ? jfilekey.data.stringval
            : "x";
        pds->filekey = filekey;
        char* fs_path = aprintf("z%s", filekey);
        pds->hack_fs = json_get_table_value(json_get_table_value(pds->rhdb, "fs"), fs_path);
        cb_free(fs_path);
        
        if (pds->hack_fs.type != kJSONTable || pds->selected_hack.type != kJSONTable)
        {
            // failed to find info on hack
            CB_Modal* modal = CB_Modal_new(
                "Patch database missing entry.",
                NULL, NULL, NULL
            );
            CB_presentModal(modal->scene);
        }
        else
        {
            PatchDownloadContext* c = push_context(pds);
            if (!c) return;
            
            c->type = PDSCT_PATCH_CHOOSE_INTERACTION;
            CB_ListItemButton* itemButton;
    
            itemButton = CB_ListItemButton_new("View Description…");
            array_push(c->list->items, itemButton);
            
            itemButton = CB_ListItemButton_new("Download Files…");
            array_push(c->list->items, itemButton);
            
            itemButton = CB_ListItemButton_new("View README…");
            array_push(c->list->items, itemButton);
            
            CB_ListView_reload(c->list);
        }
    }
}

static void on_get_patch(unsigned flags, char* data, size_t data_len, void* ud)
{
    CB_PatchDownloadScene* pds = ud;
    
    if (!pds->http_in_progress) return;
    pds->http_in_progress = 0;
    
    if ((flags & ~HTTP_ENABLE_ASKED) || !data || data_len == 0)
    {
        char* msg = aprintf("Failed to download patch file. (flags=0x%03x)", flags);
        CB_Modal* modal = CB_Modal_new(
            msg,
            NULL, NULL, NULL
        );
        cb_free(msg);
        CB_presentModal(modal->scene);
        return;
    }
    else
    {
        char* path = aprintf("%s/%s", pds->patches_dir_path, pds->basename);
        
        bool success = call_with_main_stack_3(cb_write_entire_file, path, data, data_len);
        cb_free(path);
        if (!success)
        {
            CB_Modal* modal = CB_Modal_new(
                "Failed to save patch file to disk after successfully downloading it.",
                NULL, NULL, NULL
            );
            CB_presentModal(modal->scene);
            return;
        }
        else
        {
            CB_Modal* modal = CB_Modal_new(
                "Patch file downloaded. Remember to enable the patch in settings > patches > manage",
                NULL, NULL, NULL
            );
            modal->width = 300;
            modal->height = 240;
            CB_presentModal(modal->scene);
            return;
        }
    }
}

static void on_get_textfile(unsigned flags, char* data, size_t data_len, void* ud)
{
    CB_PatchDownloadScene* pds = ud;
    
    if (!pds->http_in_progress) return;
    pds->http_in_progress = 0;
    
    if ((flags & ~HTTP_ENABLE_ASKED) || !data || data_len == 0)
    {
        char* msg = aprintf("Failed to download text file. (flags=0x%03x)", flags);
        CB_Modal* modal = CB_Modal_new(
            msg,
            NULL, NULL, NULL
        );
        cb_free(msg);
        CB_presentModal(modal->scene);
        return;
    }
    else
    {
        // paranoia
        data[data_len - 1] = 0;
        CB_InfoScene* infoScene = CB_InfoScene_new(pds->text_file_title, data);
        CB_presentModal(infoScene->scene);
    }
}

static void context_patch_choose_interaction_update(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    update_common(pds, context);
    
    if (CB_App->buttons_pressed & kButtonA)
    {
        switch(context->list->selectedItem)
        {
        case 0: // view description
            {
                cb_play_ui_sound(CB_UISound_Confirm);
                json_value jtitle = json_get_table_value(pds->selected_hack, "title");
                char* title = (jtitle.type == kJSONString) ? jtitle.data.stringval : NULL;
                if (title) decode_numeric_escapes(title);
                
                json_value jauthor = json_get_table_value(pds->selected_hack, "author");
                char* author = (jauthor.type == kJSONString) ? jauthor.data.stringval : NULL;
                if (author) decode_numeric_escapes(author);
                
                json_value jreldate = json_get_table_value(pds->selected_hack, "reldate");
                char* reldate = (jreldate.type == kJSONString) ? jreldate.data.stringval : NULL;
                
                json_value jdescription = json_get_table_value(pds->selected_hack, "description");
                char* description = (jdescription.type == kJSONString) ? jdescription.data.stringval : NULL;
                if (description) decode_numeric_escapes(description);
                
                json_value jrominfo = json_get_table_value(pds->selected_hack, "rominfo");
                char* rominfo = (jrominfo.type == kJSONString) ? jrominfo.data.stringval : NULL;
                if (rominfo) decode_numeric_escapes(rominfo);
                
                char* text = aprintf(
                    "Author: %s\nRelease Date: %s\n\n-- Description --\n\n%s\n\n-- ROM Info --\n\n%s",
                    author ? author : "(unknown)",
                    reldate ? reldate : "(missing)",
                    description ? description : "",
                    rominfo ? rominfo : ""
                );
                
                CB_InfoScene* infoScene = CB_InfoScene_new(title, text);
                cb_free(text);
                CB_presentModal(infoScene->scene);
            }
            break;
        case 1: // view patch files
            if (!push_file_browser(
                pds, pds->hack_fs
            )) {
                CB_Modal* modal = CB_Modal_new(
                    "Failed to open directory",
                    NULL, NULL, NULL
                );
                CB_presentModal(modal->scene);
            }
            break;
        case 2: // view README
            {
                char* path = aprintf("%spatches/%sreadme.txt", pds->prefix, pds->filekey);
                pds->http_in_progress = 1;
                pds->text_file_title = "README";
                http_get(
                    pds->domain, path, "to download a patch README", 
                    on_get_textfile, 15000, pds, NULL
                );
                cb_free(path);
            }
            break;
        default:
            break;
        }
    }
}

static void context_top_level_update(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    update_common(pds, context);
    
    if (CB_App->buttons_pressed & kButtonA)
    {
        switch(context->list->selectedItem)
        {
        case 0: // manage
            {
                cb_play_ui_sound(CB_UISound_Confirm);
                CB_PatchesScene* s = CB_PatchesScene_new(pds->game);
                CB_presentModal(s->scene);
            }
            break;
        case 1: // download
            cb_play_ui_sound(CB_UISound_Confirm);
            {
                pds->prefix = NULL;
                json_value jprefix = json_get_table_value(pds->rhdb, "prefix");
                if (jprefix.type == kJSONString)
                {
                    pds->prefix = jprefix.data.stringval;
                }
                
                pds->domain = NULL;
                json_value jdomain = json_get_table_value(pds->rhdb, "domain");
                if (jdomain.type == kJSONString)
                {
                    pds->domain = jdomain.data.stringval;
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
}

static void context_top_level_draw(CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active)
{
    draw_common(pds, context, x, active);
}

static char* context_patch_files_browse_hint(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    int i = context->list->selectedItem;
    if (i < context->list->items->length)
    {
        const CB_ListItemButton* const button = context->list->items->items[i];
        enum file_type const ft = button->ud.uint & ((1 << FILETYPE_BITS) - 1);
        unsigned const file_size = (button->ud.uint >> FILETYPE_BITS);
        bool const unknown_file_size = (file_size >= (1 << (32 - FILETYPE_BITS)));
        
        char* en_file_size = unknown_file_size ? aprintf("unknown") : en_human_bytes(file_size);
        char* v = NULL;
        switch(ft)
        {
        case FT_DIRECTORY:
            v = aprintf("Directory.\nPress Ⓐ to view contents.");
            break;
        case FT_TEXT:
            v = aprintf("Size: %s\nText file.\nPress Ⓐ to view contents.", en_file_size);
            break;
        case FT_PATCH_SUPPORTED:
            v = aprintf("Size: %s\nPress Ⓐ to download this patch file.", en_file_size);
            break;
        case FT_PATCH_UNSUPPORTED:
            v = aprintf("Size: %s\nCrankBoy does not support this type of patch yet.", en_file_size);
            break;
        case FT_UNSUPPORTED:
            v = aprintf("Size: %s\nCrankBoy does not know how to open this type of file.", en_file_size);
            break;
        default:
            break;
        }
        
        cb_free(en_file_size);
        
        return v;
    }
    
    return NULL;
}

static char* context_patch_choose_interaction_hint(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    json_value jtitle = json_get_table_value(pds->selected_hack, "title");
    const char* title = NULL;
    if (jtitle.type == kJSONString) title = jtitle.data.stringval;
    
    switch (context->list->selectedItem)
    {
    case 0:
        return aprintf(
            "View info for hack \"%s\"", title ? title : "?"
        );
    case 1:
        return aprintf(
            "Download patch files for hack \"%s\"", title ? title : "?"
        );
    case 2:
        return aprintf(
            "View README for hack \"%s\"", title ? title : "?"
        );
    default:
        return NULL;
    }
}

static char* context_hack_list_hint(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    json_value hack = get_nth_patch_for_game(pds, context->list->selectedItem, NULL);
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
    NULL, NULL
};

static context_hint_fn context_hint[PDSCT_MAX] = {
    context_top_level_hint, context_hack_list_hint, context_patch_choose_interaction_hint,
    context_patch_files_browse_hint, NULL
};

static context_fn context_update[PDSCT_MAX] = {
    context_top_level_update, context_patch_list_update, context_patch_choose_interaction_update,
    context_patch_files_browse_update, NULL
};

static context_draw_fn context_draw[PDSCT_MAX] = {
    context_top_level_draw, draw_common, draw_common,
    draw_common, NULL
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
    playdate->system->logToConsole("Pop context\n");
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
        else if (pds->context_depth_p <= pds->context_depth - 2 && pds->context_depth > 0)
        {
            pop_context(pds);
        }
    }
    else if (pds->http_in_progress == 0 && (CB_App->buttons_pressed & kButtonB))
    {
        // back out
        --pds->target_context_depth;
    }
    else if (pds->http_in_progress == 0)
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
    
    if (pds->http_in_progress)
    {
        // crude loading bar
        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, LCD_ROWS, (LCDColor)&lcdp_t_50[0]);
        
        pds->anim_t += dt;
        if (pds->anim_t >= 1) --pds->anim_t;
        
        int m = 8;
        int w = 128;
        int x = pds->anim_t * (LCD_COLUMNS + w) - w;
        playdate->graphics->fillRect(x, LCD_ROWS/2 - m/2, w, m/2, kColorBlack);
    }
    else
    {
        pds->anim_t = 0;
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
    missing_entry:;
        char* msg = aprintf("ROM header name \"%s\" not found in patch database", pds->header_name);
        CB_Modal* modal = CB_Modal_new(
            msg,
            NULL, NULL, NULL
        );
        modal->width = 300;
        modal->height = 240;
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
        if (arr->n == 0)
        {
            char* msg = aprintf("No patches found for \"%s\"", pds->header_name);
            CB_Modal* modal = CB_Modal_new(
                msg,
                NULL, NULL, NULL
            );
            cb_free(msg);
            CB_presentModal(modal->scene);
            return false;
        }
        
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
    else
    {
        // seems unlikely; we're already guarding
        goto missing_entry;
    }
    
    CB_ListView_reload(context->list);
    
    context->type = PDSCT_LIST_PATCHES;
    return true;
}

static bool push_file_browser(CB_PatchDownloadScene* pds, json_value fs)
{
    JsonObject* arr = (fs.type == kJSONTable)
        ? fs.data.tableval
        : NULL;
    if (!arr || arr->n == 0) return false;
    
    CB_ListItemButton* itemButton;
    PatchDownloadContext* context = push_context(pds);
    if (!context) return false;
    
    context->j = fs;
    
    for (size_t i = 0; i < arr->n; ++i)
    {
        const char* key = arr->data[i].key;
        
        if (arr->data[i].value.type == kJSONTable)
        {
            // directory
            char* text = aprintf("%s/", key);
            itemButton = CB_ListItemButton_new(text);
            itemButton->ud.uint = FT_DIRECTORY;
            cb_free(text);
            array_push(context->list->items, itemButton);
        }
        else
        {
            itemButton = CB_ListItemButton_new(key);
            
            const char* extension = strrchr(key, '.');
            if (!extension) extension = key + strlen(key); // (empty string)
            
            // supported patches
            if (extension_is_supported_patch_file(extension))
            {
                itemButton->ud.uint = FT_PATCH_SUPPORTED;
            }
            
            // any other patch file .bps, .bsdiff, .cht, .ips, .xdelta, .ups, .vcdiff
            else if (!strcasecmp(extension, ".bsdiff") || !strcasecmp(extension, ".cht") || !strcasecmp(extension, ".xdelta") || !strcasecmp(extension, ".vcdiff")
                || !strcasecmp(extension, ".ips") || !strcasecmp(extension, ".bps") || !strcasecmp(extension, ".ups"))
            {
                itemButton->ud.uint = FT_PATCH_UNSUPPORTED;
            }
            
            else if (!strcasecmp(extension, ".txt") || !strcasecmp(extension, ".md") || !strcasecmp(key, "readme") || !strcasecmp(key, "license"))
            {
                itemButton->ud.uint = FT_TEXT;
            }
            
            else
            {
                itemButton->ud.uint = FT_UNSUPPORTED;
            }
            
            unsigned size = -1;
            if (arr->data[i].value.type == kJSONInteger)
            {
                size = (unsigned)arr->data[i].value.data.intval;
            }
            
            if (size < (1 << (32 - FILETYPE_BITS)))
            {
                itemButton->ud.uint |= size << FILETYPE_BITS;
            }
            else
            {
                itemButton->ud.uint |= ((uintptr_t)-1) << FILETYPE_BITS;
            }
            
            array_push(context->list->items, itemButton);
        }
    }
    
    CB_ListView_reload(context->list);
    
    context->type = PDSCT_PATCH_FILES_BROWSE;
    
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