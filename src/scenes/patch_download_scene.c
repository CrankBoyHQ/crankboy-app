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
#include "utility.h"

#define HEADER_ANIMATION_RATE 2.8f
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

static PatchDownloadContext* push_context(CB_PatchDownloadScene* pds);
static bool push_patch_list(CB_PatchDownloadScene* pds);
static bool push_file_browser(CB_PatchDownloadScene* pds, json_value fs);
static void on_get_textfile(unsigned flags, char* data, size_t data_len, void* ud);
static void on_get_patch(unsigned flags, char* data, size_t data_len, void* ud);
static void on_permission_granted_for_download(unsigned flags, void* ud);
static void initiate_download_with_permission_check(
    CB_PatchDownloadScene* pds, PendingDownloadType type, const char* purpose, char* http_path_san
);

enum file_type
{
    FT_DIRECTORY = 0,
    FT_TEXT = 1,
    FT_PATCH_SUPPORTED = 2,
    FT_PATCH_UNSUPPORTED = 3,
    FT_UNSUPPORTED = 4
};
#define FILETYPE_BITS 4
#define FT_DOWNLOADED_BIT (1 << FILETYPE_BITS)
#define FILE_META_BITS (FILETYPE_BITS + 1)

typedef struct
{
    char* name;
    enum file_type type;
    unsigned int original_index;
    unsigned int size;
} FileBrowserItem;

static int compare_file_browser_items(const void* a, const void* b)
{
    const FileBrowserItem* itemA = (const FileBrowserItem*)a;
    const FileBrowserItem* itemB = (const FileBrowserItem*)b;
    return strcasecmp(itemA->name, itemB->name);
}

static void CB_PatchDownloadScene_didSelectLibrary(void* userdata)
{
    CB_PatchDownloadScene* pds = userdata;
    if (pds->settingsScene)
    {
        pds->settingsScene->shouldDismiss = true;
    }
    pds->target_context_depth = -1;
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
        pds->target_context_depth = -1;
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
    int left_margin = 4;
    int right_margin = 0;

    int header_y = pds->header_animation_p * HEADER_HEIGHT + 0.5f;

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

static void on_permission_granted_for_download(unsigned flags, void* ud)
{
    CB_PatchDownloadScene* pds = ud;

    if ((flags & ~HTTP_ENABLE_ASKED) != 0)
    {
        pds->http_in_progress = 0;
        pds->pending_download_type = PD_NONE;

        if (pds->pending_http_path)
        {
            cb_free(pds->pending_http_path);
            pds->pending_http_path = NULL;
        }

        char* msg;
        if (flags & HTTP_WIFI_NOT_AVAILABLE)
        {
            msg = cb_strdup("Wi-Fi not available.");
        }
        else if (flags & HTTP_ENABLE_DENIED)
        {
            msg = cb_strdup("Network permission was denied.");
        }
        else
        {
            msg = aprintf("A network error occurred. (0x%03x)", flags);
        }

        CB_Modal* modal = CB_Modal_new(msg, NULL, NULL, NULL);
        cb_free(msg);
        CB_presentModal(modal->scene);
        return;
    }

    PatchDownloadUD* userdata = cb_malloc(sizeof(PatchDownloadUD));
    userdata->pds = pds;

    if (pds->pending_download_type == PD_PATCH)
    {
        pds->active_http_connection = http_get(
            pds->domain, pds->pending_http_path, "to download this patch file", on_get_patch, 15000,
            userdata
        );
    }
    else if (pds->pending_download_type == PD_TEXTFILE)
    {
        pds->active_http_connection = http_get(
            pds->domain, pds->pending_http_path, "to download this text file", on_get_textfile,
            15000, userdata
        );
    }

    cb_free(pds->pending_http_path);
    pds->pending_http_path = NULL;
}

static void initiate_download_with_permission_check(
    CB_PatchDownloadScene* pds, PendingDownloadType type, const char* purpose, char* http_path_san
)
{
    if (pds->pending_http_path)
    {
        cb_free(pds->pending_http_path);
    }
    pds->pending_http_path = cb_strdup(http_path_san);
    pds->pending_download_type = type;
    pds->http_in_progress = 1;

    enable_http(pds->domain, purpose, on_permission_granted_for_download, pds);
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

        int selected_idx = context->list->selectedItem;
        int original_idx = context->index_map[selected_idx];

        JsonObject* obj = (context->j.type == kJSONTable) ? context->j.data.tableval : NULL;
        if (!obj || original_idx >= obj->n)
        {
            cb_free(prefix);
            return NULL;
        }
        char* full = aprintf("%s/%s", prefix, obj->data[original_idx].key);
        cb_free(prefix);
        return full;
    }
}

static void on_enable_patch_modal_close(void* ud, int option)
{
    if (option == 0)
    {
        CB_PatchDownloadScene* pds = ud;
        SoftPatch* patches = call_with_main_stack_2(list_patches, pds->game->fullpath, NULL);
        if (patches)
        {
            char* basename_no_ext = cb_basename(pds->basename, true);

            for (SoftPatch* patch = patches; patch->fullpath; ++patch)
            {
                if (strcasecmp(patch->basename, basename_no_ext) == 0)
                {
                    patch->state = PATCH_ENABLED;
                    break;
                }
            }
            cb_free(basename_no_ext);

            for (SoftPatch* patch = patches; patch->fullpath; ++patch)
            {
                if (patch->state < 0)
                    patch->state = PATCH_DISABLED;
            }

            call_with_main_stack_2(save_patches_state, pds->game->fullpath, patches);
            free_patches(patches);
        }
    }
}

static void context_patch_files_browse_update(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
)
{
    update_common(pds, context);

    if (context->list->items->length == 1)
    {
        CB_ListItemButton* button = context->list->items->items[0];
        if (strcmp(button->title, "<empty>") == 0)
        {
            // This is the <empty> placeholder, which is not interactive.
            return;
        }
    }

    if (CB_App->buttons_pressed & kButtonA)
    {
        JsonObject* obj = (context->j.type == kJSONTable) ? context->j.data.tableval : NULL;
        int selected_idx = context->list->selectedItem;

        if (obj && selected_idx < context->index_map_size)
        {
            const CB_ListItemButton* const button = context->list->items->items[selected_idx];
            if ((button->ud.uint & FT_DOWNLOADED_BIT) != 0)
            {
                // Already downloaded, do nothing.
                return;
            }

            cb_play_ui_sound(CB_UISound_Confirm);

            int original_idx = context->index_map[selected_idx];
            enum file_type const ft = button->ud.uint & ((1 << FILETYPE_BITS) - 1);

            if (ft == FT_DIRECTORY)
            {
                push_file_browser(pds, obj->data[original_idx].value);
                return;
            }

            unsigned const file_size = (button->ud.uint >> FILE_META_BITS);
            bool const unknown_file_size = (file_size >= (1 << (32 - FILE_META_BITS)));

            if (unknown_file_size)
            {
                CB_Modal* modal = CB_Modal_new("Unknown file size.", NULL, NULL, NULL);
                CB_presentModal(modal->scene);
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

                if (ft == FT_TEXT)
                {
                    pds->text_file_title = obj->data[original_idx].key;
                    initiate_download_with_permission_check(
                        pds, PD_TEXTFILE, "to download this text file", http_path_san
                    );
                }
                else
                {
                    pds->basename = obj->data[original_idx].key;
                    initiate_download_with_permission_check(
                        pds, PD_PATCH, "to download this patch file", http_path_san
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
        cb_play_ui_sound(CB_UISound_Confirm);
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

            itemButton = CB_ListItemButton_new("Readme\t>");
            itemButton->ud.uint = 0;
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
    pds->active_http_connection = 0;

    if (!pds->http_in_progress)
    {
        cb_free(pud);
        return;
    }

    pds->http_in_progress = 0;

    if ((flags & ~HTTP_ENABLE_ASKED) || !data || data_len == 0)
    {
        if (flags & HTTP_NOT_FOUND)
            pds->post_download_command = PDC_DOWNLOAD_FAILED_NOT_FOUND;
        else if (flags & HTTP_WIFI_NOT_AVAILABLE)
            pds->post_download_command = PDC_DOWNLOAD_FAILED_WIFI;
        else
        {
            pds->post_download_command = PDC_DOWNLOAD_FAILED_OTHER;
            pds->post_download_flags = flags;
        }
    }
    else
    {
        pds->pending_download_type = PD_PROCESSING;

        char* path = aprintf("%s/%s", pds->patches_dir_path, pds->basename);
        bool success = call_with_main_stack_3(cb_write_entire_file, path, data, data_len);
        cb_free(path);

        if (!success)
        {
            pds->post_download_command = PDC_SAVE_FAILED;
        }
        else
        {
            // Add the newly downloaded file to the local files list for immediate UI update
            pds->local_files->count++;
            pds->local_files->files =
                cb_realloc(pds->local_files->files, sizeof(char*) * pds->local_files->count);
            pds->local_files->files[pds->local_files->count - 1] = cb_strdup(pds->basename);

            if (pds->context_depth > 0)
            {
                PatchDownloadContext* context = &pds->context[pds->context_depth - 1];
                if (context->type == PDSCT_PATCH_FILES_BROWSE)
                {
                    for (int i = 0; i < context->list->items->length; i++)
                    {
                        CB_ListItemButton* button = context->list->items->items[i];
                        if (strcmp(button->title, pds->basename) == 0)
                        {
                            button->ud.uint |= FT_DOWNLOADED_BIT;
                            pds->cached_hint_key = -1;
                            break;
                        }
                    }
                }
            }

            if (!pds->has_local_patches)
            {
                pds->has_local_patches = true;
                if (pds->context[0].type == PDSCT_TOP_LEVEL)
                {
                    CB_ListItemButton* manage_button = pds->context[0].list->items->items[0];
                    manage_button->ud.uint = 0;  // enabled
                }
            }

            pds->post_download_command = PDC_DOWNLOAD_SUCCESS;
        }
    }

    pds->pending_download_type = PD_NONE;
    cb_free(pud);
}

static void on_get_textfile(unsigned flags, char* data, size_t data_len, void* ud)
{
    PatchDownloadUD* pud = ud;
    CB_PatchDownloadScene* pds = pud->pds;
    pds->active_http_connection = 0;

    if (!pds->http_in_progress)
    {
        cb_free(pud);
        return;
    }

    pds->http_in_progress = 0;
    pds->pending_download_type = PD_NONE;

    if ((flags & ~HTTP_ENABLE_ASKED) || !data || data_len == 0)
    {
        if (flags & HTTP_NOT_FOUND)
            pds->post_download_command = PDC_DOWNLOAD_FAILED_NOT_FOUND;
        else if (flags & HTTP_WIFI_NOT_AVAILABLE)
            pds->post_download_command = PDC_DOWNLOAD_FAILED_WIFI;
        else
        {
            pds->post_download_command = PDC_DOWNLOAD_FAILED_OTHER;
            pds->post_download_flags = flags;
        }
    }
    else
    {
        pds->pending_download_type = PD_PROCESSING;
        data[data_len] = 0;  // Null-terminate
        if (pds->post_download_text_data)
        {
            cb_free(pds->post_download_text_data);
        }
        pds->post_download_text_data = cb_strdup(data);
        pds->post_download_command = PDC_TEXTFILE_SUCCESS;
    }

    cb_free(pud);
}

static bool hash_match(json_value jhack, CB_Game* game)
{
    if (!game || !game->names) return false;
    
    json_value jrominfo = json_get_table_value(jhack, "rominfo");
    char* rominfo = (jrominfo.type == kJSONString) ? jrominfo.data.stringval : NULL;
    if (rominfo)
        decode_numeric_escapes(rominfo);
    else
        return false;
        
    char* crc32 = aprintf("%08x", game->names->crc32);
    
    if (strstr_i(rominfo, crc32))
    {
        cb_free(crc32);
        return true;
    }
    else
    {
        cb_free(crc32);
        return false;
    }
}

static void context_patch_choose_interaction_update(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
)
{
    update_common(pds, context);

    if (CB_App->buttons_pressed & kButtonA)
    {
        cb_play_ui_sound(CB_UISound_Confirm);

        switch (context->list->selectedItem)
        {
        case 0:  // Download Files
            if (!push_file_browser(pds, pds->hack_fs))
            {
                CB_Modal* modal = CB_Modal_new("Failed to open directory", NULL, NULL, NULL);
                CB_presentModal(modal->scene);
            }
            else
            {
                if (!hash_match(pds->selected_hack, pds->game))
                {
                    CB_Modal* modal = CB_Modal_new("ROM not listed in hack info.\nYou should read the hack info and/or README to verify this ROM's compatability before applying any patches, lest glitches occur.", NULL, NULL, NULL);
                    modal->width = 320;
                    modal->height = 190;
                    CB_presentModal(modal->scene);
                }
            }
            break;
        case 1:  // Patch Info
        {
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
            char* fs_path = aprintf("%sreadme.txt", pds->filekey);
            char* http_path = aprintf("%spatches/%s", pds->prefix, fs_path);
            cb_free(fs_path);

            char* http_path_san = sanitize_url_path(http_path);
            cb_free(http_path);

            pds->http_in_progress = 1;
            pds->text_file_title = "Readme";
            initiate_download_with_permission_check(
                pds, PD_TEXTFILE, "to download a patch README", http_path_san
            );
            cb_free(http_path_san);
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

                pds->http_in_progress = 1;
                pds->text_file_title = "Changelog";
                initiate_download_with_permission_check(
                    pds, PD_TEXTFILE, "to download the changelog", http_path_san
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
    return aprintf("ROM Header title: %s\n \nCRC32: %X\nInternal save: %s", pds->header_name, pds->game->names->crc32, pds->game->names->rom_has_battery ? "Yes" : "No");
}

static void context_top_level_update(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, float dt
)
{
    update_common(pds, context);
    
    // force refresh of hint
    pds->cached_hint_key = -2;

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
                "2. Via USB connection, add patch files to: %s/%s/\n"
                "3. Finally, enable them from this screen (settings > Patches > Manage "
                "patches).\n\n"
                "You may find patches on romhacking.net or romhack.ing",
                cb_gb_directory_path(CB_patchesPath),
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
                    if (push_patch_list(pds))
                    {
                        pds->context_depth_p = pds->target_context_depth;
                    }
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

    if (pds->list_fetch_error_message && active)
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

static void context_patch_files_browse_draw(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context, int x, bool active
)
{
    draw_common(pds, context, x, active);

    CB_ListView* listView = context->list;
    int fontHeight = playdate->graphics->getFontHeight(PDS_FONT);
    int left_margin = 4;

    playdate->graphics->setClipRect(
        listView->frame.x, listView->frame.y, listView->frame.width, listView->frame.height
    );

    for (int i = 0; i < listView->items->length; i++)
    {
        CB_ListItemButton* button = listView->items->items[i];

        CB_ListItem* item = &button->item;
        enum file_type const ft = button->ud.uint & ((1 << FILETYPE_BITS) - 1);

        bool is_downloaded = (button->ud.uint & FT_DOWNLOADED_BIT) != 0;
        bool is_unsupported = (ft == FT_UNSUPPORTED || ft == FT_PATCH_UNSUPPORTED);

        if (is_downloaded || is_unsupported)
        {
            int rowY = listView->frame.y + item->offsetY - listView->contentOffset;
            if (rowY + item->height < listView->frame.y ||
                rowY > listView->frame.y + listView->frame.height)
                continue;

            bool selected = (i == listView->selectedItem && active);
            const uint8_t* dither = selected ? white_transparent_dither : black_transparent_dither;

            int textY = rowY + (item->height - fontHeight) / 2;

            int textWidth = playdate->graphics->getTextWidth(
                PDS_FONT, button->title, strlen(button->title), kUTF8Encoding, 0
            );

            playdate->graphics->fillRect(
                listView->frame.x + left_margin, textY, textWidth, fontHeight, (LCDColor)dither
            );
        }
    }
    playdate->graphics->clearClipRect();
}

static char* context_patch_files_browse_hint(
    CB_PatchDownloadScene* pds, PatchDownloadContext* context
)
{
    if (context->list->items->length == 1)
    {
        CB_ListItemButton* button = context->list->items->items[0];
        if (strcmp(button->title, "<empty>") == 0)
        {
            return cb_strdup("This directory contains no supported files.");
        }
    }

    int i = context->list->selectedItem;
    if (i < context->list->items->length)
    {
        const CB_ListItemButton* const button = context->list->items->items[i];

        if ((button->ud.uint & FT_DOWNLOADED_BIT) != 0)
        {
            return cb_strdup("This patch file has already been downloaded.");
        }

        enum file_type const ft = button->ud.uint & ((1 << FILETYPE_BITS) - 1);
        unsigned const file_size = (button->ud.uint >> FILE_META_BITS);
        bool const unknown_file_size = (file_size >= (1 << (32 - FILE_META_BITS)));

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
        else
        {
            #define PATCH_MANAGE_MSG "Toggle installed patches and and rearrange the order in which they are applied."
            SoftPatch* patches = list_patches(pds->game->fullpath, NULL);
            if (patches)
            {
                uint32_t hash = patch_hash(patches);
                free_patches(patches);
                
                if (hash)
                {
                    if (pds->game->names->rom_has_battery)
                    {
                        return aprintf(
                            PATCH_MANAGE_MSG "\n \nPatch code: %08X\n \nNote: because patches are in use, and this ROM has an internal save system, you may wish to use a separate save file. Before launching the game, please adjust settings > Save Slot.",
                            hash
                        );
                    } else {
                        return aprintf(
                            PATCH_MANAGE_MSG "\n \nPatch code: %08X",
                            hash
                        );
                    }
                }
            }
            return aprintf(
                PATCH_MANAGE_MSG
            );
        }
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
    context_top_level_draw, draw_common, context_top_level_draw, context_patch_files_browse_draw,
    NULL
};

static PatchDownloadContext* push_context(CB_PatchDownloadScene* pds)
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

static void pop_context(CB_PatchDownloadScene* pds)
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

    if (context->index_map)
    {
        cb_free(context->index_map);
        context->index_map = NULL;
    }
}

void CB_PatchDownloadScene_free(CB_PatchDownloadScene* pds)
{
    if (pds->active_http_connection)
    {
        http_cancel(pds->active_http_connection);
        pds->active_http_connection = 0;
    }

    CB_Scene_free(pds->scene);
    while (pds->context_depth > 0)
    {
        pop_context(pds);
    }
    cb_free(pds->patches_dir_path);
    cb_free(pds->cached_hint);
    cb_free(pds->list_fetch_error_message);
    cb_free(pds->post_download_text_data);

    if (pds->local_files)
    {
        for (int i = 0; i < pds->local_files->count; ++i)
        {
            cb_free(pds->local_files->files[i]);
        }
        cb_free(pds->local_files->files);
        cb_free(pds->local_files);
    }

    cb_free(pds);
}

static void list_local_patches_callback(const char* path, void* userdata)
{
    CB_LocalFileSet* file_set = userdata;
    const char* basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    if (strlen(basename) == 0)
        return;

    // Check if the file is a patch file before adding
    const char* extension = strrchr(basename, '.');
    if (extension_is_supported_patch_file(extension))
    {
        file_set->count++;
        file_set->files = cb_realloc(file_set->files, sizeof(char*) * file_set->count);
        file_set->files[file_set->count - 1] = cb_strdup(basename);
    }
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
        else if ((CB_App->buttons_pressed & kButtonB) && !pds->http_in_progress)
        {
            if (pds->context_depth == 1)
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
                PatchDownloadContext* current_context = &pds->context[pds->context_depth - 1];
                --pds->target_context_depth;
                if (current_context->type == PDSCT_LIST_PATCHES)
                {
                    // Instantly go back from patch list to top level
                    pop_context(pds);
                    pds->context_depth_p = pds->target_context_depth;
                }
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
                int old_selection = -1;
                if (context->list)
                {
                    old_selection = context->list->selectedItem;
                }

                context_update_fn fn = context_update[context->type];
                if (fn)
                    fn(pds, context, dt);

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

    if (pds->http_in_progress)
    {
        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, LCD_ROWS, (LCDColor)&lcdp_t_50[0]);

        int box_w = 260;
        int box_h = 70;
        int box_x = (LCD_COLUMNS - box_w) / 2;
        int box_y = (LCD_ROWS - box_h) / 2;
        playdate->graphics->fillRect(box_x, box_y, box_w, box_h, kColorWhite);
        playdate->graphics->drawRect(box_x, box_y, box_w, box_h, kColorBlack);

        pds->anim_t += dt;
        if (pds->anim_t >= 0.5f)
        {
            pds->anim_t -= 0.5f;
            pds->loading_anim_step = (pds->loading_anim_step + 1) % 3;
        }

        int num_dots = pds->loading_anim_step + 1;
        char dots[4] = "...";
        dots[num_dots] = '\0';

        const char* base_text;

        switch (pds->pending_download_type)
        {
        case PD_PATCH:
            base_text = "Downloading patch";
            break;
        case PD_TEXTFILE:
            if (pds->text_file_title && strcasecmp(pds->text_file_title, "Readme") == 0)
            {
                base_text = "Downloading Readme";
            }
            else if (pds->text_file_title && strcasecmp(pds->text_file_title, "Changelog") == 0)
            {
                base_text = "Downloading Changelog";
            }
            else
            {
                base_text = "Downloading text file";
            }
            break;
        case PD_PROCESSING:
            base_text = "Processing";
            break;
        default:
            base_text = "Please wait";
            break;
        }

        playdate->graphics->setFont(CB_App->bodyFont);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);

        int base_text_width = playdate->graphics->getTextWidth(
            CB_App->bodyFont, base_text, strlen(base_text), kUTF8Encoding, 0
        );
        int max_dots_width =
            playdate->graphics->getTextWidth(CB_App->bodyFont, "...", 3, kUTF8Encoding, 0);
        int total_text_width = base_text_width + max_dots_width;

        int font_height = playdate->graphics->getFontHeight(CB_App->bodyFont);
        int text_y = box_y + (box_h - font_height) / 2;
        int start_x = box_x + (box_w - total_text_width) / 2;

        playdate->graphics->drawText(base_text, strlen(base_text), kUTF8Encoding, start_x, text_y);
        playdate->graphics->drawText(
            dots, strlen(dots), kUTF8Encoding, start_x + base_text_width, text_y
        );
    }
    else
    {
        pds->anim_t = 0;
        pds->loading_anim_step = 0;
    }

    if (pds->post_download_command != PDC_NONE)
    {
        char* msg = NULL;
        CB_Modal* modal = NULL;

        switch (pds->post_download_command)
        {
        case PDC_DOWNLOAD_SUCCESS:
        {
            const char* options[] = {"Yes", "No", NULL};
            msg = cb_strdup("Patch downloaded successfully.\nWould you like to enable it now?");
            modal = CB_Modal_new(msg, options, on_enable_patch_modal_close, pds);
            modal->width = 320;
            modal->height = 140;
            break;
        }
        case PDC_DOWNLOAD_FAILED_NOT_FOUND:
            msg = cb_strdup("The requested file was not found on the server.");
            modal = CB_Modal_new(msg, NULL, NULL, NULL);
            break;
        case PDC_DOWNLOAD_FAILED_WIFI:
            msg = cb_strdup("Wi-Fi not available.");
            modal = CB_Modal_new(msg, NULL, NULL, NULL);
            break;
        case PDC_DOWNLOAD_FAILED_OTHER:
            msg = aprintf("Failed to download file. (Error: 0x%03x)", pds->post_download_flags);
            modal = CB_Modal_new(msg, NULL, NULL, NULL);
            break;
        case PDC_SAVE_FAILED:
            msg = cb_strdup("Failed to save patch file to disk after successfully downloading it.");
            modal = CB_Modal_new(msg, NULL, NULL, NULL);
            break;
        case PDC_TEXTFILE_SUCCESS:
        {
            CB_InfoScene* infoScene =
                CB_InfoScene_new(pds->text_file_title, pds->post_download_text_data);
            CB_presentModal(infoScene->scene);
            cb_free(pds->post_download_text_data);
            pds->post_download_text_data = NULL;
            break;
        }
        default:
            break;
        }

        if (modal)
        {
            CB_presentModal(modal->scene);
        }
        if (msg)
        {
            cb_free(msg);
        }

        pds->post_download_command = PDC_NONE;
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
    return true;
}

static bool has_supported_files_recursive(json_value fs)
{
    if (fs.type != kJSONTable)
        return false;

    JsonObject* arr = fs.data.tableval;
    for (size_t i = 0; i < arr->n; ++i)
    {
        const char* key = arr->data[i].key;
        json_value value = arr->data[i].value;

        if (value.type == kJSONTable)
        {
            if (has_supported_files_recursive(value))
            {
                return true;
            }
        }
        else
        {
            const char* extension = strrchr(key, '.');
            if (!extension)
                extension = key + strlen(key);
            if (extension_is_supported_patch_file(extension) || !strcasecmp(extension, ".txt") ||
                !strcasecmp(extension, ".md") || !strcasecmp(key, "readme") ||
                !strcasecmp(key, "license"))
                return true;
        }
    }
    return false;
}

static bool is_file_downloaded(CB_PatchDownloadScene* pds, const char* filename)
{
    if (!pds->local_files)
        return false;
    for (int i = 0; i < pds->local_files->count; ++i)
    {
        if (strcasecmp(pds->local_files->files[i], filename) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool push_file_browser(CB_PatchDownloadScene* pds, json_value fs)
{
    JsonObject* arr = (fs.type == kJSONTable) ? fs.data.tableval : NULL;
    if (!arr)
        return false;

    PatchDownloadContext* context = push_context(pds);
    if (!context)
        return false;

    context->j = fs;

    FileBrowserItem* items_to_sort = cb_malloc(sizeof(FileBrowserItem) * arr->n);
    int items_count = 0;

    for (size_t i = 0; i < arr->n; ++i)
    {
        const char* key = arr->data[i].key;
        enum file_type ft = FT_UNSUPPORTED;

        if (arr->data[i].value.type == kJSONTable)
        {
            ft = FT_DIRECTORY;
        }
        else
        {
            const char* extension = strrchr(key, '.');
            if (!extension)
                extension = key + strlen(key);

            if (extension_is_supported_patch_file(extension))
            {
                ft = FT_PATCH_SUPPORTED;
            }
            else if (extension_is_unsupported_patch_file(extension))
            {
                ft = FT_PATCH_UNSUPPORTED;
            }
            else if (!strcasecmp(extension, ".txt") || !strcasecmp(extension, ".md") ||
                     !strcasecmp(key, "readme") || !strcasecmp(key, "license"))
            {
                ft = FT_TEXT;
            }
        }

        items_to_sort[items_count].name = (char*)key;
        items_to_sort[items_count].type = ft;
        items_to_sort[items_count].original_index = i;

        unsigned size = (arr->data[i].value.type == kJSONInteger)
                            ? (unsigned)arr->data[i].value.data.intval
                            : -1;
        items_to_sort[items_count].size = size;
        items_count++;
    }

    qsort(items_to_sort, items_count, sizeof(FileBrowserItem), compare_file_browser_items);

    context->index_map = cb_malloc(sizeof(int) * items_count);
    context->index_map_size = 0;

    // Group 1: Supported Patch Files
    for (int i = 0; i < items_count; i++)
    {
        if (items_to_sort[i].type == FT_PATCH_SUPPORTED)
        {
            CB_ListItemButton* itemButton = CB_ListItemButton_new(items_to_sort[i].name);
            uintptr_t ud = items_to_sort[i].type;

            if (is_file_downloaded(pds, items_to_sort[i].name))
            {
                ud |= FT_DOWNLOADED_BIT;
            }

            if (items_to_sort[i].size < (1 << (32 - FILE_META_BITS)))
            {
                ud |= items_to_sort[i].size << FILE_META_BITS;
            }
            else
            {
                ud |= ((uintptr_t)-1) << FILE_META_BITS;
            }
            itemButton->ud.uint = ud;
            array_push(context->list->items, itemButton);
            context->index_map[context->index_map_size++] = items_to_sort[i].original_index;
        }
    }

    // Group 2: Text Files
    for (int i = 0; i < items_count; i++)
    {
        if (items_to_sort[i].type == FT_TEXT)
        {
            CB_ListItemButton* itemButton = CB_ListItemButton_new(items_to_sort[i].name);
            uintptr_t ud = items_to_sort[i].type;
            if (items_to_sort[i].size < (1 << (32 - FILE_META_BITS)))
            {
                ud |= items_to_sort[i].size << FILE_META_BITS;
            }
            else
            {
                ud |= ((uintptr_t)-1) << FILE_META_BITS;
            }
            itemButton->ud.uint = ud;
            array_push(context->list->items, itemButton);
            context->index_map[context->index_map_size++] = items_to_sort[i].original_index;
        }
    }

    // Group 3: Directories
    for (int i = 0; i < items_count; i++)
    {
        if (items_to_sort[i].type == FT_DIRECTORY)
        {
            char* text = aprintf("%s/", items_to_sort[i].name);
            CB_ListItemButton* itemButton = CB_ListItemButton_new(text);
            cb_free(text);
            uintptr_t ud = items_to_sort[i].type;
            ud |= ((uintptr_t)-1) << FILE_META_BITS;
            itemButton->ud.uint = ud;
            array_push(context->list->items, itemButton);
            context->index_map[context->index_map_size++] = items_to_sort[i].original_index;
        }
    }

    // Group 4: Unsupported Files
    for (int i = 0; i < items_count; i++)
    {
        if (items_to_sort[i].type == FT_UNSUPPORTED ||
            items_to_sort[i].type == FT_PATCH_UNSUPPORTED)
        {
            CB_ListItemButton* itemButton = CB_ListItemButton_new(items_to_sort[i].name);
            uintptr_t ud = items_to_sort[i].type;
            if (items_to_sort[i].size < (1 << (32 - FILE_META_BITS)))
            {
                ud |= items_to_sort[i].size << FILE_META_BITS;
            }
            else
            {
                ud |= ((uintptr_t)-1) << FILE_META_BITS;
            }
            itemButton->ud.uint = ud;
            array_push(context->list->items, itemButton);
            context->index_map[context->index_map_size++] = items_to_sort[i].original_index;
        }
    }

    cb_free(items_to_sort);

    if (context->list->items->length == 0)
    {
        CB_ListItemButton* itemButton = CB_ListItemButton_new("<empty>");
        array_push(context->list->items, itemButton);
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
    pds->is_dismissing = false;
    scene->managedObject = pds;

    pds->post_download_command = PDC_NONE;
    pds->post_download_flags = 0;
    pds->post_download_text_data = NULL;

    pds->loading_anim_timer = 0.0f;
    pds->loading_anim_step = 0;

    pds->cached_hint_key = -2;
    pds->patches_dir_path = get_patches_directory(game->fullpath);

    // Create patch install directory if it doesn't already exist.
    // We must run this on the main stack, otherwise it can fail
    // in unpredictable ways, like truncated paths.
    call_with_main_stack_1(playdate->file->mkdir, pds->patches_dir_path);

    pds->local_files = allocz(CB_LocalFileSet);
    call_with_main_stack_4(
        playdate->file->listfiles, pds->patches_dir_path, list_local_patches_callback,
        pds->local_files, 0
    );

    pds->has_local_patches = (pds->local_files->count > 0);

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
