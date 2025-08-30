#include "patch_download_scene.h"

#include "../http.h"
#include "../jparse.h"
#include "../softpatch.h"
#include "../userstack.h"
#include "../utility.h"
#include "info_scene.h"
#include "modal.h"
#include "patches_scene.h"
#include "pd_api.h"
#include "settings_scene.h"

#define HEADER_HEIGHT 18
#define SCROLL_RATE 2.3f
#define kDividerX 240
#define kRightPanePadding 10
#define PDS_FONT CB_App->bodyFont

#define HOLD_TIME_SUPPRESS_RELEASE 0.25f
#define HOLD_TIME_MARGIN 0.15f
#define HOLD_TIME 1.09f
#define HOLD_FADE_RATE 2.9f

static const uint8_t black_transparent_dither[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55
};
static const uint8_t white_transparent_dither[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55
};

typedef struct
{
    CB_PatchDownloadScene* pds;
} PatchDownloadUD;
typedef void (*context_update_fn)(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
);
typedef void (*context_free_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context);
typedef void (*context_draw_fn)(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active
);
typedef char* (*context_hint_fn)(CB_PatchDownloadScene* pds, PatchDownloadContext* context);

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

static void CB_PatchDownloadScene_didSelectLibrary(void* userdata)
{
    CB_PatchDownloadScene* pds = userdata;
    if (pds->settingsScene)
    {
        pds->settingsScene->shouldDismiss = true;
    }
    --pds->target_context_depth;
}

static void CB_PatchDownloadScene_didSelectSettings(void* userdata)
{
    CB_PatchDownloadScene* pds = userdata;

    if (pds->started_without_header)
    {
        pds->is_dismissing = true;
    }
    else
    {
        --pds->target_context_depth;
    }
}

static void CB_PatchDownloadScene_menu(void* object)
{
    CB_PatchDownloadScene* pds = object;
    playdate->system->removeAllMenuItems();
    playdate->system->addMenuItem("library", CB_PatchDownloadScene_didSelectLibrary, pds);
    playdate->system->addMenuItem("settings", CB_PatchDownloadScene_didSelectSettings, pds);
}

static void check_for_patches_callback(const char* path, void* userdata)
{
    bool* has_patches_flag = userdata;
    if (*has_patches_flag)
    {
        return;
    }
    const char* extension = strrchr(path, '.');
    if (extension_is_supported_patch_file(extension))
        *has_patches_flag = true;
}

// modifies a string in-place to replace numeric escape sequences like &#127; with
// the appropriate byte. Does not affect non-numeric escape sequences like &amp; etc.
static void decode_numeric_escapes(char* s)
{
    char* read = s;
    char* write = s;

    while (*read)
    {
        if (read[0] == '&' && read[1] == '#')
        {
            char* p = read + 2;
            int val = 0;
            int valid = 0;

            while (*p >= '0' && *p <= '9')
            {
                val = val * 10 + (*p - '0');
                p++;
                valid = 1;
            }

            if (valid && *p == ';' && val >= 0 && val <= 255)
            {
                *write++ = (char)val;
                read = p + 1;  // skip past ';'
                continue;
            }
        }

        *write++ = *read++;
    }
    *write = '\0';
}

static void update_common(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    if (context->list)
        CB_ListView_update(context->list);
}

static void draw_common(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active
)
{
    int left_margin = 0;
    int right_margin = 0;

    int header_y = pds->header_animation_p * HEADER_HEIGHT + 0.5f;

    if (context->type == PDSCT_TOP_LEVEL)
    {
        left_margin = 20;
        right_margin = 20;
    }

    PDRect frame = {
        x + left_margin, header_y, kDividerX - left_margin - right_margin, LCD_ROWS - header_y
    };

    context->list->frame = frame;
    context->list->needsDisplay = true;

    CB_ListView_draw(context->list);
}

static json_value get_nth_patch_for_game(CB_PatchDownloadScene* pds, int n, int* o_hackkey)
{
    json_value jv = {.type = kJSONNull};
    JsonArray* arr = (pds->game_hacks.type == kJSONArray) ? pds->game_hacks.data.arrayval : NULL;
    if (arr && n < arr->n)
    {
        const char* s = NULL;
        json_value j = arr->data[n];
        if (j.type == kJSONInteger)
        {
            if (o_hackkey)
                *o_hackkey = j.data.intval;
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

    pds->is_fetching_list = false;

    // already cancelled?
    if (pds->http_in_progress != 1)
    {
        pds->http_in_progress = 0;
        return;
    }

    pds->http_in_progress = 0;

    if (pds->list_fetch_error_message)
    {
        cb_free(pds->list_fetch_error_message);
        pds->list_fetch_error_message = NULL;
    }

    if (flags & HTTP_WIFI_NOT_AVAILABLE)
    {
        pds->list_fetch_error_message = cb_strdup("Wi-Fi not available.");
    }
    else if (flags & HTTP_ENABLE_DENIED)
    {
        CB_Modal* modal = CB_Modal_new(
            "CrankBoy must be granted networking privileges in order to download ROM hacks. You "
            "can do this from the Playdate OS settings.",
            NULL, NULL, NULL
        );
        modal->width = 350;
        modal->height = 180;
        CB_presentModal(modal->scene);
    }
    else if (flags & ~HTTP_ENABLE_ASKED)
    {
        pds->list_fetch_error_message = cb_strdup("Searching patches failed.");
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
        char* prefix = get_path_to_selected_item(pds, depth - 1);
        if (!prefix)
            return NULL;
        int i = context->list->selectedItem;
        JsonObject* obj = (context->j.type == kJSONTable) ? context->j.data.tableval : NULL;
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

static void context_patch_files_browse_update(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
)
{
    update_common(pds, context);

    if (CB_App->buttons_pressed & kButtonA)
    {
        JsonObject* obj = (context->j.type == kJSONTable) ? context->j.data.tableval : NULL;

        int i = context->list->selectedItem;
        if (i < context->list->items->length)
        {
            const CB_ListItemButton* const button = context->list->items->items[i];
            enum file_type const ft = button->ud.uint & ((1 << FILETYPE_BITS) - 1);
            unsigned const file_size = (button->ud.uint >> FILETYPE_BITS);
            bool const unknown_file_size = (file_size >= (1 << (32 - FILETYPE_BITS)));

            if (unknown_file_size)
            {
                CB_Modal* modal = CB_Modal_new("Unknown file size.", NULL, NULL, NULL);
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
                char* path = get_path_to_selected_item(pds, pds->context_depth - 1);
                if (!path)
                {
                    playdate->system->logToConsole("unable to get path to item");
                    return;
                }

                char* http_path = aprintf("%sextracted/%s", pds->prefix, path);
                cb_free(path);

                char* http_path_san = sanitize_url_path(http_path);
                cb_free(http_path);

                PatchDownloadUD* userdata = cb_malloc(sizeof(PatchDownloadUD));
                userdata->pds = pds;

                pds->http_in_progress = 1;
                if (ft == FT_TEXT)
                {
                    pds->text_file_title = obj->data[i].key;
                    http_get(
                        pds->domain, http_path_san, "to download this text file", on_get_textfile,
                        15000, userdata, &pds->active_http_connection
                    );
                }
                else
                {
                    pds->basename = obj->data[i].key;
                    CB_ASSERT(ft == FT_PATCH_SUPPORTED);
                    http_get(
                        pds->domain, http_path_san, "to download this patch file", on_get_patch,
                        15000, userdata, &pds->active_http_connection
                    );
                }
                cb_free(http_path_san);
            }
        }
    }
}

static void context_patch_list_update(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
)
{
    update_common(pds, context);

    if (CB_App->buttons_pressed & kButtonA)
    {
        pds->selected_hack =
            get_nth_patch_for_game(pds, context->list->selectedItem, &pds->selected_hack_key);
        json_value jfilekey = json_get_table_value(pds->selected_hack, "filekey");
        const char* filekey = (jfilekey.type == kJSONString) ? jfilekey.data.stringval : "x";
        pds->filekey = filekey;
        char* fs_path = aprintf("z%s", filekey);
        pds->hack_fs = json_get_table_value(json_get_table_value(pds->rhdb, "fs"), fs_path);
        cb_free(fs_path);

        if (pds->hack_fs.type != kJSONTable || pds->selected_hack.type != kJSONTable)
        {
            // failed to find info on hack
            CB_Modal* modal = CB_Modal_new("Patch database missing entry.", NULL, NULL, NULL);
            CB_presentModal(modal->scene);
        }
        else
        {
            PatchDownloadContext* c = push_context(pds);
            if (!c)
                return;

            c->type = PDSCT_PATCH_CHOOSE_INTERACTION;
            CB_ListItemButton* itemButton;

            itemButton = CB_ListItemButton_new("Download files\t>");
            array_push(c->list->items, itemButton);

            itemButton = CB_ListItemButton_new("Patch info\t>");
            array_push(c->list->items, itemButton);

            json_value fs = pds->hack_fs;

            bool has_readme = (json_get_table_value(fs, "readme.txt").type != kJSONNull) ||
                              (json_get_table_value(fs, "README.txt").type != kJSONNull) ||
                              (json_get_table_value(fs, "readme.md").type != kJSONNull) ||
                              (json_get_table_value(fs, "Read me.txt").type != kJSONNull) ||
                              (json_get_table_value(fs, "Read Me.txt").type != kJSONNull) ||
                              (json_get_table_value(fs, "read_me.txt").type != kJSONNull) ||
                              (json_get_table_value(fs, "Read_Me.txt").type != kJSONNull) ||
                              (json_get_table_value(fs, "readme").type != kJSONNull);

            itemButton = CB_ListItemButton_new("Readme\t>");
            itemButton->ud.uint = has_readme ? 0 : 1;  // 1 means disabled
            array_push(c->list->items, itemButton);

            bool has_changelog = (json_get_table_value(fs, "changelog.txt").type != kJSONNull) ||
                                 (json_get_table_value(fs, "releasenotes.txt").type != kJSONNull);

            itemButton = CB_ListItemButton_new("Changelog\t>");
            itemButton->ud.uint = has_changelog ? 0 : 1;  // 1 means disabled
            array_push(c->list->items, itemButton);

            CB_ListView_reload(c->list);
        }
    }
}

static void on_get_patch(unsigned flags, char* data, size_t data_len, void* ud)
{
    PatchDownloadUD* pud = ud;
    CB_PatchDownloadScene* pds = pud->pds;
    pds->active_http_connection = NULL;

    if (!pds->http_in_progress)
    {
        cb_free(pud);
        return;
    }

    pds->http_in_progress = 0;

    if ((flags & ~HTTP_ENABLE_ASKED) || !data || data_len == 0)
    {
        char* msg = aprintf("Failed to download patch file. (flags=0x%03x)", flags);
        CB_Modal* modal = CB_Modal_new(msg, NULL, NULL, NULL);
        cb_free(msg);
        CB_presentModal(modal->scene);
        cb_free(pud);
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
                "Failed to save patch file to disk after successfully downloading it.", NULL, NULL,
                NULL
            );
            CB_presentModal(modal->scene);
            cb_free(pud);
            return;
        }
        else
        {
            if (!pds->has_local_patches)
            {
                pds->has_local_patches = true;
                if (pds->context[0].type == PDSCT_TOP_LEVEL)
                {
                    CB_ListItemButton* manage_button = pds->context[0].list->items->items[0];
                    manage_button->ud.uint = 0;  // enabled
                }
            }

            CB_Modal* modal = CB_Modal_new(
                "Patch file downloaded. Remember to enable the patch in settings > patches > "
                "manage",
                NULL, NULL, NULL
            );
            modal->width = 300;
            modal->height = 140;
            CB_presentModal(modal->scene);
            cb_free(pud);
            return;
        }
    }
}

static void on_get_textfile(unsigned flags, char* data, size_t data_len, void* ud)
{
    PatchDownloadUD* pud = ud;
    CB_PatchDownloadScene* pds = pud->pds;
    pds->active_http_connection = NULL;

    if (!pds->http_in_progress)
    {
        cb_free(pud);
        return;
    }

    pds->http_in_progress = 0;

    if ((flags & ~HTTP_ENABLE_ASKED) || !data || data_len == 0)
    {
        char* msg = aprintf("Failed to download text file. (flags=0x%03x)", flags);
        CB_Modal* modal = CB_Modal_new(msg, NULL, NULL, NULL);
        cb_free(msg);
        CB_presentModal(modal->scene);
        cb_free(pud);
        return;
    }
    else
    {
        // paranoia
        data[data_len - 1] = 0;
        CB_InfoScene* infoScene = CB_InfoScene_new(pds->text_file_title, data);
        CB_presentModal(infoScene->scene);
        cb_free(pud);
    }
}

static void context_patch_choose_interaction_update(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
)
{
    update_common(pds, context);

    if (CB_App->buttons_pressed & kButtonA)
    {
        switch (context->list->selectedItem)
        {
        case 0:  // Download Files
            if (!push_file_browser(pds, pds->hack_fs))
            {
                CB_Modal* modal = CB_Modal_new("Failed to open directory", NULL, NULL, NULL);
                CB_presentModal(modal->scene);
            }
            break;
        case 1:  // Patch Info
        {
            cb_play_ui_sound(CB_UISound_Confirm);
            json_value jtitle = json_get_table_value(pds->selected_hack, "title");
            char* title = (jtitle.type == kJSONString) ? jtitle.data.stringval : NULL;
            if (title)
                decode_numeric_escapes(title);

            json_value jauthor = json_get_table_value(pds->selected_hack, "author");
            char* author = (jauthor.type == kJSONString) ? jauthor.data.stringval : NULL;
            if (author)
                decode_numeric_escapes(author);

            json_value jreldate = json_get_table_value(pds->selected_hack, "reldate");
            char* reldate = (jreldate.type == kJSONString) ? jreldate.data.stringval : NULL;

            json_value jdescription = json_get_table_value(pds->selected_hack, "description");
            char* description =
                (jdescription.type == kJSONString) ? jdescription.data.stringval : NULL;
            if (description)
                decode_numeric_escapes(description);

            json_value jrominfo = json_get_table_value(pds->selected_hack, "rominfo");
            char* rominfo = (jrominfo.type == kJSONString) ? jrominfo.data.stringval : NULL;
            if (rominfo)
                decode_numeric_escapes(rominfo);

            char* text = aprintf(
                "Author: %s\nRelease Date: %s\n\n-- Description --\n\n%s\n\n-- ROM Info --\n\n%s",
                author ? author : "(unknown)", reldate ? reldate : "(missing)",
                description ? description : "", rominfo ? rominfo : ""
            );

            CB_InfoScene* infoScene = CB_InfoScene_new(title, text);
            cb_free(text);
            CB_presentModal(infoScene->scene);
        }
        break;
        case 2:  // Readme
        {
            const CB_ListItemButton* button =
                context->list->items->items[context->list->selectedItem];
            if (button->ud.uint == 1)
            {  // is disabled
                break;
            }
            cb_play_ui_sound(CB_UISound_Confirm);

            json_value fs = pds->hack_fs;
            const char* readme_filename = NULL;
            if (json_get_table_value(fs, "readme.txt").type != kJSONNull)
                readme_filename = "readme.txt";
            else if (json_get_table_value(fs, "README.txt").type != kJSONNull)
                readme_filename = "README.txt";
            else if (json_get_table_value(fs, "readme.md").type != kJSONNull)
                readme_filename = "readme.md";
            else if (json_get_table_value(fs, "Read me.txt").type != kJSONNull)
                readme_filename = "Read me.txt";
            else if (json_get_table_value(fs, "Read Me.txt").type != kJSONNull)
                readme_filename = "Read Me.txt";
            else if (json_get_table_value(fs, "read_me.txt").type != kJSONNull)
                readme_filename = "read_me.txt";
            else if (json_get_table_value(fs, "Read_Me.txt").type != kJSONNull)
                readme_filename = "Read_Me.txt";
            else if (json_get_table_value(fs, "readme").type != kJSONNull)
                readme_filename = "readme";

            if (readme_filename)
            {
                char* fs_path = aprintf("z%s/%s", pds->filekey, readme_filename);
                char* http_path = aprintf("%sextracted/%s", pds->prefix, fs_path);
                cb_free(fs_path);

                char* http_path_san = sanitize_url_path(http_path);
                cb_free(http_path);

                PatchDownloadUD* userdata = cb_malloc(sizeof(PatchDownloadUD));
                userdata->pds = pds;

                pds->http_in_progress = 1;
                pds->text_file_title = "Readme";
                http_get(
                    pds->domain, http_path_san, "to download a patch README", on_get_textfile,
                    15000, userdata, &pds->active_http_connection
                );
                cb_free(http_path_san);
            }
        }
        break;
        case 3:  // Changelog
        {
            const CB_ListItemButton* button =
                context->list->items->items[context->list->selectedItem];
            if (button->ud.uint == 1)
            {  // is disabled
                break;
            }
            cb_play_ui_sound(CB_UISound_Confirm);

            json_value fs = pds->hack_fs;
            const char* changelog_filename = NULL;
            if (json_get_table_value(fs, "changelog.txt").type != kJSONNull)
            {
                changelog_filename = "changelog.txt";
            }
            else if (json_get_table_value(fs, "releasenotes.txt").type != kJSONNull)
            {
                changelog_filename = "releasenotes.txt";
            }

            if (changelog_filename)
            {
                char* fs_path = aprintf("z%s/%s", pds->filekey, changelog_filename);
                char* http_path = aprintf("%sextracted/%s", pds->prefix, fs_path);
                cb_free(fs_path);

                char* http_path_san = sanitize_url_path(http_path);
                cb_free(http_path);

                PatchDownloadUD* userdata = cb_malloc(sizeof(PatchDownloadUD));
                userdata->pds = pds;

                pds->http_in_progress = 1;
                pds->text_file_title = "Changelog";
                http_get(
                    pds->domain, http_path_san, "to download the changelog", on_get_textfile, 15000,
                    userdata, &pds->active_http_connection
                );
                cb_free(http_path_san);
            }
        }
        break;
        default:
            break;
        }
    }
}

char* get_rom_info(CB_PatchDownloadScene* pds)
{
    return aprintf("ROM Header title: %s\nCRC32: %X", pds->header_name, pds->game->names->crc32);
}

static void context_top_level_update(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
)
{
    update_common(pds, context);

    if (context->list->selectedItem == 0 && !pds->has_local_patches)
    {
        if (CB_App->buttons_down & kButtonA)
        {
            pds->option_hold_time += dt;
        }
        else
        {
            pds->option_hold_time -= HOLD_FADE_RATE * dt;
        }

        if (pds->option_hold_time >= HOLD_TIME)
        {
            pds->option_hold_time = 0;
            cb_play_ui_sound(CB_UISound_Confirm);
            char* rom_basename = cb_basename(pds->game->fullpath, true);
            char* msg = aprintf(
                "1. Place your Playdate in disk mode by holding LEFT+MENU+LOCK for ten seconds.\n"
                "2. Via USB connection, add patch files to: Data/*crankboy/patches/%s/\n"
                "3. Finally, enable them from this screen (settings > Patches > Manage "
                "patches).\n\n"
                "You may find patches on romhacking.net or romhack.ing",
                rom_basename
            );
            cb_free(rom_basename);

            CB_InfoScene* infoScene =
                CB_InfoScene_new(pds->game->names->name_short_leading_article, msg);
            CB_presentModal(infoScene->scene);
            cb_free(msg);
            return;
        }

        if (pds->option_hold_time < 0)
            pds->option_hold_time = 0;
    }
    else
    {
        pds->option_hold_time = 0;
    }

    bool a_pressed = (CB_App->buttons_pressed & kButtonA);

    if (context->list->selectedItem == 0 && !pds->has_local_patches)
    {
        if (pds->option_hold_time >= HOLD_TIME_SUPPRESS_RELEASE)
        {
            a_pressed = false;
        }
    }

    if (a_pressed)
    {
        switch (context->list->selectedItem)
        {
        case 0:  // manage
        {
            const CB_ListItemButton* button =
                context->list->items->items[context->list->selectedItem];
            if (button->ud.uint == 1)
            {  // is disabled
                break;
            }

            cb_play_ui_sound(CB_UISound_Confirm);
            CB_PatchesScene* s = CB_PatchesScene_new(pds->game);
            CB_presentModal(s->scene);
        }
        break;
        case 1:  // download
            cb_play_ui_sound(CB_UISound_Confirm);
            {
                if (pds->has_presented_patch_list)
                {
                    push_patch_list(pds);
                    break;
                }

                if (pds->list_fetch_error_message)
                {
                    cb_free(pds->list_fetch_error_message);
                    pds->list_fetch_error_message = NULL;
                }

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
                    pds->is_fetching_list = true;
                    pds->loading_anim_timer = 0.0f;
                    pds->loading_anim_step = 0;
                    context->list->needsDisplay = true;
                    enable_http(
                        jdomain.data.stringval, "to download game patches",
                        context_top_level_select_download, pds
                    );
                }
                else
                {
                    CB_Modal* modal = CB_Modal_new(
                        "Failed to determine patch host.\n \n(Is rhdb.json present?)", NULL, NULL,
                        NULL
                    );
                    CB_presentModal(modal->scene);
                }
            }
            break;
        case 2:  // rom info
        {
            cb_play_ui_sound(CB_UISound_Confirm);
            char* text = get_rom_info(pds);
            CB_InfoScene* infoScene =
                CB_InfoScene_new(pds->game->names->name_short_leading_article, text);
            cb_free(text);
            CB_presentModal(infoScene->scene);
        }
        break;
        }
    }
}

static void context_top_level_draw(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active
)
{
    int header_y = pds->header_animation_p * HEADER_HEIGHT + 0.5f;

    if (pds->is_fetching_list && active)
    {
        playdate->graphics->fillRect(x, header_y, kDividerX, LCD_ROWS - header_y, kColorWhite);

        const char* base_text = "Searching patches";
        const char* width_calc_string = "Searching patches...";
        char message[32];

        const int dot_counts[] = {0, 1, 2, 3};
        int num_dots = dot_counts[pds->loading_anim_step];
        snprintf(message, sizeof(message), "%s", base_text);
        for (int i = 0; i < num_dots; i++)
        {
            strncat(message, ".", sizeof(message) - strlen(message) - 1);
        }

        playdate->graphics->setFont(PDS_FONT);
        int msg_width = playdate->graphics->getTextWidth(
            PDS_FONT, width_calc_string, strlen(width_calc_string), kUTF8Encoding, 0
        );
        int textX = x + (kDividerX - msg_width) / 2;
        int textY =
            header_y + (LCD_ROWS - header_y) / 2 - playdate->graphics->getFontHeight(PDS_FONT) / 2;

        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        playdate->graphics->drawText(message, strlen(message), kUTF8Encoding, textX, textY);
    }
    else if (pds->list_fetch_error_message && active)
    {
        playdate->graphics->fillRect(x, header_y, kDividerX, LCD_ROWS - header_y, kColorWhite);

        const char* message = pds->list_fetch_error_message;

        playdate->graphics->setFont(PDS_FONT);
        int msg_width =
            playdate->graphics->getTextWidth(PDS_FONT, message, strlen(message), kUTF8Encoding, 0);
        int textX = x + (kDividerX - msg_width) / 2;
        int textY =
            header_y + (LCD_ROWS - header_y) / 2 - playdate->graphics->getFontHeight(PDS_FONT) / 2;

        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        playdate->graphics->drawText(message, strlen(message), kUTF8Encoding, textX, textY);
    }
    else
    {
        int left_margin = 20;
        int right_margin = 20;
        CB_ListView* listView = context->list;
        int listX = x;
        listView->frame.x = listX;
        listView->frame.y = header_y;
        listView->frame.width = kDividerX;
        listView->frame.height = LCD_ROWS - header_y;
        playdate->graphics->setFont(PDS_FONT);
        int fontHeight = playdate->graphics->getFontHeight(PDS_FONT);
        playdate->graphics->setClipRect(
            listX, listView->frame.y, listView->frame.width, listView->frame.height
        );
        for (int i = 0; i < listView->items->length; i++)
        {
            CB_ListItemButton* button = listView->items->items[i];
            CB_ListItem* item = &button->item;
            int rowY = listView->frame.y + item->offsetY - listView->contentOffset;
            if (rowY + item->height < listView->frame.y ||
                rowY > listView->frame.y + listView->frame.height)
                continue;
            bool selected = (i == listView->selectedItem && active);
            if (selected)
            {
                playdate->graphics->fillRect(listX, rowY, kDividerX, item->height, kColorBlack);
                playdate->graphics->setDrawMode(kDrawModeFillWhite);
            }
            else
            {
                playdate->graphics->setDrawMode(kDrawModeFillBlack);
            }
            char* fullText = cb_strdup(button->title);
            char* rightText = strchr(fullText, '\t');
            char* leftText = fullText;
            if (rightText != NULL)
            {
                *rightText = '\0';
                rightText++;
            }
            else
            {
                rightText = "";
            }
            int textY = rowY + (item->height - fontHeight) / 2;

            bool is_disabled = button->ud.uint == 1;

            playdate->graphics->drawText(
                leftText, strlen(leftText), kUTF8Encoding, listX + left_margin, textY
            );

            if (strlen(rightText) > 0 && !is_disabled)
            {
                int rightWidth = playdate->graphics->getTextWidth(
                    PDS_FONT, rightText, strlen(rightText), kUTF8Encoding, 0
                );
                playdate->graphics->drawText(
                    rightText, strlen(rightText), kUTF8Encoding,
                    listX + kDividerX - rightWidth - right_margin, textY
                );
            }

            if (is_disabled)
            {
                const uint8_t* dither =
                    selected ? white_transparent_dither : black_transparent_dither;
                int leftWidth = playdate->graphics->getTextWidth(
                    PDS_FONT, leftText, strlen(leftText), kUTF8Encoding, 0
                );
                playdate->graphics->fillRect(
                    listX + left_margin, textY, leftWidth, fontHeight, (LCDColor)dither
                );
            }

            if (i == 0 && selected && !pds->has_local_patches &&
                pds->option_hold_time > HOLD_TIME_SUPPRESS_RELEASE)
            {
                float p = (pds->option_hold_time - HOLD_TIME_SUPPRESS_RELEASE) /
                          (HOLD_TIME - HOLD_TIME_MARGIN - HOLD_TIME_SUPPRESS_RELEASE);
                if (p > 1.0f)
                    p = 1.0f;

                playdate->graphics->fillRect(listX, rowY, kDividerX * p, item->height, kColorXOR);
            }

            cb_free(fullText);
        }
        playdate->graphics->clearClipRect();
        playdate->graphics->setDrawMode(kDrawModeCopy);
        listView->needsDisplay = false;
    }
}

static char* context_patch_files_browse_hint(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context
)
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
        switch (ft)
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
            v = aprintf(
                "Size: %s\nCrankBoy does not support this type of patch yet.", en_file_size
            );
            break;
        case FT_UNSUPPORTED:
            v = aprintf(
                "Size: %s\nCrankBoy does not know how to open this type of file.", en_file_size
            );
            break;
        default:
            break;
        }

        cb_free(en_file_size);

        return v;
    }

    return NULL;
}

static char* context_patch_choose_interaction_hint(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context
)
{
    json_value jtitle = json_get_table_value(pds->selected_hack, "title");
    const char* title = NULL;
    if (jtitle.type == kJSONString)
        title = jtitle.data.stringval;

    switch (context->list->selectedItem)
    {
    case 0:
        return aprintf("Download patch files for hack \"%s\"", title ? title : "?");
    case 1:
        return aprintf("View patch info for hack \"%s\"", title ? title : "?");
    case 2:
    {
        const CB_ListItemButton* button = context->list->items->items[context->list->selectedItem];
        if (button->ud.uint == 1)
        {  // is disabled
            return aprintf("No readme available for this hack.");
        }
        return aprintf("View readme for hack \"%s\"", title ? title : "?");
    }
    case 3:
    {
        const CB_ListItemButton* button = context->list->items->items[context->list->selectedItem];
        if (button->ud.uint == 1)
        {  // is disabled
            return aprintf("No changelog available for this hack.");
        }
        return aprintf("View changelog for hack \"%s\"", title ? title : "?");
    }
    default:
        return NULL;
    }
}

static char* context_hack_list_hint(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    json_value hack = get_nth_patch_for_game(pds, context->list->selectedItem, NULL);
    json_value jauthor = json_get_table_value(hack, "author");
    const char* author = NULL;
    if (jauthor.type == kJSONString)
        author = jauthor.data.stringval;

    json_value jdate = json_get_table_value(hack, "reldate");
    const char* date = NULL;
    if (jdate.type == kJSONString)
        date = jdate.data.stringval;

    return aprintf("Author: %s\nRelease Date: %s\n", author ? author : "?", date ? date : "?");
}

static char* context_top_level_hint(CB_PatchDownloadScene* pds, PatchDownloadContext* context)
{
    switch (context->list->selectedItem)
    {
    case 0:
        if (!pds->has_local_patches)
        {
            return aprintf(
                "No local patches found.\n \nHold Ⓐ to view instructions for adding patches "
                "manually."
            );
        }

        return aprintf(
            "Toggle installed patches and and rearrange the order in which they are applied."
        );
        break;
    case 1:
        return aprintf(
            "Download ROM hacks, translations, etc. for \"%s.\"\n(Mirrored from romhacking.net)",
            pds->game->names->name_short_leading_article
        );
        break;
    case 2:
        return get_rom_info(pds);
    default:
        return NULL;
    }
}

// a value that changes when selection changes.
static uint32_t get_hint_key(CB_PatchDownloadScene* pds)
{
    if (pds->target_context_depth != pds->context_depth_p)
        return -1;
    uint32_t key = (pds->context_depth << 24);
    PatchDownloadContext* context = &pds->context[pds->context_depth - 1];
    key |= (context->list->selectedItem) & 0xFFFFFF;
    return key;
}

static context_free_fn context_free[PDSCT_MAX] = {NULL, NULL, NULL, NULL, NULL};

static context_hint_fn context_hint[PDSCT_MAX] = {
    context_top_level_hint, context_hack_list_hint, context_patch_choose_interaction_hint,
    context_patch_files_browse_hint, NULL
};

static context_update_fn context_update[PDSCT_MAX] = {
    context_top_level_update, context_patch_list_update, context_patch_choose_interaction_update,
    context_patch_files_browse_update, NULL
};

static context_draw_fn context_draw[PDSCT_MAX] = {
    context_top_level_draw, draw_common, context_top_level_draw, draw_common, NULL
};

PatchDownloadContext* push_context(CB_PatchDownloadScene* pds)
{
    if (pds->context_depth >= CB_PATCHDOWNLOAD_STACK_MAX_DEPTH)
        return NULL;
    PatchDownloadContext* context = &pds->context[pds->context_depth++];
    pds->target_context_depth = pds->context_depth - 1;
    memset(context, 0, sizeof(*context));

    context->list = CB_ListView_new();
    context->list->font = PDS_FONT;
    context->list->paddingTop = 15;
    context->list->paddingBottom = 15;
    return context;
}

void pop_context(CB_PatchDownloadScene* pds)
{
    playdate->system->logToConsole("Pop context\n");
    PatchDownloadContext* context = &pds->context[--pds->context_depth];

    context_free_fn _free = context_free[context->type];
    if (_free)
        _free(pds, context);

    if (context->list)
    {
        CB_ListView_free(context->list);
    }
}

void CB_PatchDownloadScene_free(CB_PatchDownloadScene* pds)
{
    if (pds->active_http_connection)
    {
        http_cancel_and_cleanup(pds->active_http_connection);
        pds->active_http_connection = NULL;
    }

    CB_Scene_free(pds->scene);
    while (pds->context_depth > 0)
    {
        pop_context(pds);
    }
    cb_free(pds->patches_dir_path);
    cb_free(pds->cached_hint);
    cb_free(pds->list_fetch_error_message);
    cb_free(pds);
}

void CB_PatchDownloadScene_update(CB_PatchDownloadScene* pds, uint32_t u32enc_dt)
{
    float dt = UINT32_AS_FLOAT(u32enc_dt);
    if (CB_App->pendingScene)
    {
        return;
    }

    if (pds->is_dismissing)
    {
        TOWARD(pds->header_animation_p, 0.0f, dt * HEADER_ANIMATION_RATE);
        if (pds->header_animation_p == 0.0f)
        {
            CB_dismiss(pds->scene);
            return;
        }
    }
    else
    {
        TOWARD(pds->header_animation_p, 1.0f, dt * HEADER_ANIMATION_RATE);

        if (pds->context_depth_p != pds->target_context_depth)
        {
            pds->context_depth_p =
                toward(pds->context_depth_p, pds->target_context_depth, dt * SCROLL_RATE);
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
        else if (CB_App->buttons_pressed & kButtonB)
        {
            if (pds->http_in_progress)
            {
                if (pds->is_fetching_list)
                {
                    // "Searching patches..." is not a real cancellable request.
                    // Just flag it as cancelled.
                    pds->http_in_progress = 0;
                    pds->is_fetching_list = false;
                    if (pds->list_fetch_error_message)
                    {
                        cb_free(pds->list_fetch_error_message);
                        pds->list_fetch_error_message = NULL;
                    }
                    pds->context[pds->context_depth - 1].list->needsDisplay = true;
                }
                else if (pds->active_http_connection)
                {
                    http_cancel_and_cleanup(pds->active_http_connection);
                    pds->active_http_connection = NULL;
                    pds->http_in_progress = 0;
                }
            }
            else if (pds->list_fetch_error_message)
            {
                cb_free(pds->list_fetch_error_message);
                pds->list_fetch_error_message = NULL;
                pds->context[pds->context_depth - 1].list->needsDisplay = true;
            }
            else if (pds->context_depth == 1)
            {
                // At the top level panel
                if (pds->started_without_header)
                {
                    pds->is_dismissing = true;
                }
                else
                {
                    --pds->target_context_depth;
                }
            }
            else
            {
                --pds->target_context_depth;
            }
        }

        if (pds->is_fetching_list)
        {
            pds->loading_anim_timer += dt;
            if (pds->loading_anim_timer >= 0.5f)
            {
                pds->loading_anim_timer -= 0.5f;
                pds->loading_anim_step = (pds->loading_anim_step + 1) % 4;
            }
        }
    }

    int header_y = pds->header_animation_p * HEADER_HEIGHT + 0.5f;
    bool isAnimating = (pds->context_depth_p != pds->target_context_depth);
    playdate->graphics->clear(kColorWhite);

    // Dynamically calculate top padding for the LEFT list view (24px -> 15px)
    int list_padding_top = 24 - (int)(9.0f * pds->header_animation_p);

    int n = (pds->context_depth_p >= 1) ? 2 : 1;
    if (pds->is_dismissing)
    {
        n = 1;
    }

    for (int i = 0; i < n; ++i)
    {
        int ci = ceil(pds->context_depth_p) - i;
        if (ci < 0 || ci >= pds->context_depth)
            continue;

        PatchDownloadContext* context = &pds->context[ci];

        if (context->list)
        {
            context->list->paddingTop = list_padding_top;
            CB_ListView_invalidateLayout(context->list);
        }

        if (!pds->is_dismissing)
        {
            if (!isAnimating && i == 0 && pds->http_in_progress == 0)
            {
                context_update_fn fn = context_update[context->type];
                if (fn)
                    fn(pds, context, dt);
            }
            else if (isAnimating)
            {
                if (context->list)
                    CB_ListView_update(context->list);
            }
        }

        float d = ci - pds->context_depth_p;
        float x = d * kDividerX;
        context_draw_fn fn = context_draw[context->type];
        if (context->list)
            context->list->hideScrollIndicator = isAnimating || pds->is_dismissing;
        if (fn)
            fn(pds, context, x, i == 0);
    }

    playdate->graphics->fillRect(
        kDividerX, header_y, LCD_COLUMNS - kDividerX, LCD_ROWS - header_y, kColorWhite
    );

    uint32_t hint_key = get_hint_key(pds);
    if (hint_key != pds->cached_hint_key)
    {
        if (hint_key != (uint32_t)(-1))
        {
            pds->cached_hint_key = hint_key;
            cb_free(pds->cached_hint);
            PatchDownloadContext* context = &pds->context[pds->context_depth - 1];
            context_hint_fn fn = context_hint[context->type];
            if (fn)
                pds->cached_hint = fn(pds, context);
            else
                pds->cached_hint = NULL;
        }
    }

    if (pds->cached_hint)
    {
        LCDFont* font = CB_App->labelFont;
        playdate->graphics->setFont(font);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
        int rightPaneX = kDividerX + kRightPanePadding;

        // Calculate dynamic top padding for the RIGHT hint pane (29px -> 20px)
        int hint_padding_top = 29 - (int)(9.0f * pds->header_animation_p);
        int rightPaneY = header_y + hint_padding_top;

        int rightPaneWidth = LCD_COLUMNS - kDividerX - (kRightPanePadding * 2);
        int rightPaneHeight = LCD_ROWS - rightPaneY;
        playdate->graphics->drawTextInRect(
            pds->cached_hint, strlen(pds->cached_hint), kUTF8Encoding, rightPaneX, rightPaneY,
            rightPaneWidth, rightPaneHeight, kWrapWord, kAlignTextLeft
        );
    }

    playdate->graphics->drawLine(kDividerX, header_y, kDividerX, LCD_ROWS, 1, kColorBlack);

    if (header_y > 0)
    {
        const char* name = pds->game->names->name_short_leading_article;
        playdate->graphics->setFont(CB_App->labelFont);
        int nameWidth = playdate->graphics->getTextWidth(
            CB_App->labelFont, name, strlen(name), kUTF8Encoding, 0
        );
        int textX = LCD_COLUMNS / 2 - nameWidth / 2;
        int fontHeight = playdate->graphics->getFontHeight(CB_App->labelFont);
        int vertical_offset = string_has_descenders(name) ? 1 : 2;
        int textY = ((header_y - fontHeight) / 2) + vertical_offset;

        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, header_y, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeFillWhite);
        playdate->graphics->drawText(name, strlen(name), kUTF8Encoding, textX, textY);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
    }

    if (pds->http_in_progress && !pds->is_fetching_list)
    {
        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, LCD_ROWS, (LCDColor)&lcdp_t_50[0]);
        pds->anim_t += dt;
        if (pds->anim_t >= 1)
            --pds->anim_t;
        int m = 8;
        int w = 128;
        int x = pds->anim_t * (LCD_COLUMNS + w) - w;
        playdate->graphics->fillRect(x, LCD_ROWS / 2 - m / 2, w, m / 2, kColorBlack);
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
        pds->list_fetch_error_message = cb_strdup("No patches found.");
        return false;
    }

    PatchDownloadContext* context = push_context(pds);
    if (!context)
        return false;

    JsonArray* arr = (pds->game_hacks.type == kJSONArray) ? pds->game_hacks.data.arrayval : NULL;
    if (arr)
    {
        if (arr->n == 0)
        {
            pds->list_fetch_error_message = cb_strdup("No patches found.");
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
    pds->has_presented_patch_list = true;
    return true;
}

static bool push_file_browser(CB_PatchDownloadScene* pds, json_value fs)
{
    JsonObject* arr = (fs.type == kJSONTable) ? fs.data.tableval : NULL;
    if (!arr || arr->n == 0)
        return false;

    CB_ListItemButton* itemButton;
    PatchDownloadContext* context = push_context(pds);
    if (!context)
        return false;

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
            if (!extension)
                extension = key + strlen(key);  // (empty string)

            // supported patches
            if (extension_is_supported_patch_file(extension))
            {
                itemButton->ud.uint = FT_PATCH_SUPPORTED;
            }

            // any other patch file .bps, .bsdiff, .cht, .ips, .xdelta, .ups, .vcdiff
            else if (!strcasecmp(extension, ".bsdiff") || !strcasecmp(extension, ".cht") ||
                     !strcasecmp(extension, ".xdelta") || !strcasecmp(extension, ".vcdiff") ||
                     !strcasecmp(extension, ".ips") || !strcasecmp(extension, ".bps") ||
                     !strcasecmp(extension, ".ups"))
            {
                itemButton->ud.uint = FT_PATCH_UNSUPPORTED;
            }

            else if (!strcasecmp(extension, ".txt") || !strcasecmp(extension, ".md") ||
                     !strcasecmp(key, "readme") || !strcasecmp(key, "license"))
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
    if (!context)
        return false;

    context->type = PDSCT_TOP_LEVEL;

    itemButton = CB_ListItemButton_new("Manage patches\t>");
    itemButton->ud.uint = pds->has_local_patches ? 0 : 1;
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new("Download patches\t>");
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new("ROM Info\t>");
    array_push(context->list->items, itemButton);

    CB_ListView_reload(context->list);

    return true;
}

CB_PatchDownloadScene* CB_PatchDownloadScene_new(
    CB_Game* game, struct CB_SettingsScene* settingsScene, float initial_header_p
)
{
    CB_Scene* scene = CB_Scene_new();
    CB_PatchDownloadScene* pds = allocz(CB_PatchDownloadScene);
    pds->scene = scene;
    pds->settingsScene = settingsScene;
    pds->game = game;
    pds->header_animation_p = initial_header_p;
    pds->started_without_header = (initial_header_p < 1.0f);
    pds->option_hold_time = 0.0f;
    pds->has_presented_patch_list = false;
    pds->is_dismissing = false;
    scene->managedObject = pds;

    pds->loading_anim_timer = 0.0f;
    pds->loading_anim_step = 0;

    pds->cached_hint_key = -2;
    pds->patches_dir_path = get_patches_directory(game->fullpath);

    // Create patch install directory if it doesn't already exist.
    // We must run this on the main stack, otherwise it can fail
    // in unpredictable ways, like truncated paths.
    call_with_main_stack_1(playdate->file->mkdir, pds->patches_dir_path);

    bool has_local_patches = false;
    call_with_main_stack_4(
        playdate->file->listfiles, pds->patches_dir_path, check_for_patches_callback,
        &has_local_patches, 0
    );
    pds->has_local_patches = has_local_patches;

    pds->rhdb = CB_App->rhdb_cache;

    if (game->names->name_header)
    {
        strncpy(pds->header_name, game->names->name_header, sizeof(pds->header_name) - 1);
        pds->header_name[sizeof(pds->header_name) - 1] = '\0';
    }
    else
    {
        pds->header_name[0] = '\0';
    }

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
    scene->menu = (void*)CB_PatchDownloadScene_menu;

    push_top_level(pds);

    return pds;
}
