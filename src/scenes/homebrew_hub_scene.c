#include "homebrew_hub_scene.h"

#include "../app.h"
#include "../http.h"
#include "../jparse.h"
#include "../userstack.h"
#include "../utility.h"
#include "image_conversion_scene.h"
#include "modal.h"
#include "parental_lock_scene.h"

#include <string.h>

#define SCROLL_RATE 2.3f
#define kDividerX 240
#define kRightPanePadding 10
#define PDS_FONT CB_App->bodyFont

#define HOLD_TIME_SUPPRESS_RELEASE 0.25f
#define HOLD_TIME_MARGIN 0.15f
#define HOLD_TIME 1.09f
#define HOLD_FADE_RATE 2.9f
#define HEADER_ANIMATION_RATE 2.8f
#define HEADER_HEIGHT 18

typedef struct
{
    CB_HomebrewHubScene* hbs;
} PatchDownloadUD;
typedef void (*context_update_fn)(CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt);
typedef void (*context_free_fn)(CB_HomebrewHubScene* hbs, HomebrewHubContext* context);
typedef void (*context_draw_fn)(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, int x, bool active
);
typedef char* (*context_hint_fn)(CB_HomebrewHubScene* hbs, HomebrewHubContext* context);

static const char* hb_platforms[] = {
    "GB",
    "GBC",
};

static bool push_list_search(CB_HomebrewHubScene* hbs, const char* platform);
static bool push_list_files(CB_HomebrewHubScene* hbs, const json_value* entry);

static void user_quit(void* ud, int selected)
{
    if (selected == 0)
    {
        playdate->system->restartGame(playdate->system->getLaunchArgs(NULL));
    }
}

// callback when rom is downloaded
static void rom_get_cb(unsigned flags, char* data, size_t data_len, CB_HomebrewHubScene* hbs)
{
    hbs->active_download_type = HB_DL_NONE;

    if (flags & HTTP_CANCELLED)
        return;
    else if (flags & ~(HTTP_ENABLE_ASKED))
    {
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
        CB_presentModal(CB_Modal_new(msg, NULL, NULL, NULL)->scene);
        cb_free(msg);
    }
    else if (!data || !data_len)
    {
        CB_presentModal(CB_Modal_new("ROM empty", NULL, NULL, NULL)->scene);
        return;
    }

    // doctor header to remove 'requires CGB' flag,
    // which many homebrew erroneously set.
    bool did_doctor = false;
    if (hbs->doctor_header_cgb_flag && data_len >= 0x200 && (uint8_t)data[0x0143] == 0xC0)
    {
        data[0x0143] = 0x80;
        did_doctor = true;
        playdate->system->logToConsole("Doctored header.");

        uint8_t checksum = data[0x14D];
        uint8_t nc = 0;
        // update header checksum
        for (unsigned i = 0x134; i <= 0x14C; ++i)
        {
            nc = nc - (uint8_t)data[i] - 1;
        }

        data[0x14D] = nc;
        playdate->system->logToConsole("Header checksum: %02X -> %02X", checksum, nc);

        uint16_t gcheck =
            ((uint16_t)(uint8_t)data[0x14E] << 8) | ((uint16_t)(uint8_t)data[0x14F] << 0);
        gcheck -= checksum;
        gcheck += nc;
        data[0x14E] = (gcheck >> 8);
        data[0x14F] = gcheck & 0xFF;
    }

    // save rom
    if (!cb_write_entire_file(hbs->target_rom_path, data, data_len))
    {
        CB_presentModal(
            CB_Modal_new("Filesystem error, failed to save ROM.", NULL, NULL, NULL)->scene
        );
    }
    else
    {
        // try saving the cover art as well.
        // NOTE: race condition -- if rom downloads first, cover art will not be saved.
        if (hbs->target_cover_art_path && hbs->cover_art_data && hbs->cover_art_len)
        {
            if (!cb_write_entire_file(
                    hbs->target_cover_art_path, hbs->cover_art_data, hbs->cover_art_len
                ))
            {
                playdate->system->logToConsole("Failed to save cover art.");
            }
            else
            {
                playdate->system->logToConsole(
                    "Saved cover art to: %s", hbs->target_cover_art_path
                );
            }
        }

        // save as 'last selected' for library view
        cb_write_entire_file(
            LAST_SELECTED_FILE, hbs->target_rom_path, strlen(hbs->target_rom_path)
        );

        const char* options[] = {"Restart", "Do Not", NULL};

        char* s = aprintf(
            "ROM downloaded successfully; you must restart CrankBoy for it to appear.%s",
            did_doctor ? "\n\nThe ROM header was altered to indicate that it is DMG-compatible."
                       : ""
        );

        CB_Modal* modal = CB_Modal_new(s, options, user_quit, NULL);
        modal->width = 330;
        modal->height = did_doctor ? 210 : 110;
        modal->height += 34;
        CB_presentModal(modal->scene);

        cb_free(s);
    }
}

static char* context_list_files_hint(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    return aprintf("Press Ⓐ to download this ROM.");
}

static char* context_list_search_hint(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    switch (context->list->selectedItem)
    {
    case 0:
        return aprintf("Use LEFT & RIGHT to switch pages.");
    default:
    {
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
                    if (!date)
                        date = json_as_string(json_get_table_value(je, "firstadded_date"));

                    // strip 'T' and onward
                    if (date)
                    {
                        char* date_tchar = strchr(date, 'T');
                        if (date_tchar)
                            date_tchar[0] = 0;
                    }

                    return aprintf(
                        "Title: %s\nDeveloper: %s\nPlatform: %s\nDate: %s",
                        title ? title : "unknown", developer ? developer : "unknown",
                        platform ? platform : "unknown", date ? date : "unknown"
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
        return aprintf("Browse GB homebrew from Homebrew Hub.");
        break;
    case 1:
        return aprintf(
            "Browse GB Color homebrew from Homebrew Hub.\n \nNote: CGB support in CrankBoy is "
            "still experimental."
        );
        break;
    case 2:
        return aprintf(
            "This feature allows restricting homebrew ROM and ROM hack downloads behind a password."
        );
        break;
    default:
        return NULL;
    }
}

static void draw_common(CB_HomebrewHubScene* pds, HomebrewHubContext* context, int x, bool active)
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

static void draw_top_level(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, int x, bool active
)
{
    int left_margin = 20;
    int right_margin = 20;
    int header_y = hbs->header_animation_p * HEADER_HEIGHT + 0.5f;

    int listX = x;
    int listY = header_y;
    int listWidth = kDividerX;
    int listHeight = LCD_ROWS - header_y;

    LCDFont* font = context->list->font;
    int fontHeight = playdate->graphics->getFontHeight(font);

    // Draw list items manually with tab handling for right-aligned chevrons
    int rowY = listY + context->list->paddingTop - context->list->contentOffset;

    for (int i = 0; i < context->list->items->length; i++)
    {
        CB_ListItem* item = context->list->items->items[i];
        if (item->type != CB_ListViewItemTypeButton)
            continue;

        CB_ListItemButton* button = (CB_ListItemButton*)item;

        // Skip items outside the visible area
        if (rowY + item->height < listY || rowY > listY + listHeight)
        {
            rowY += item->height;
            continue;
        }

        bool selected = (i == context->list->selectedItem);

        if (selected)
        {
            playdate->graphics->fillRect(listX, rowY, listWidth, item->height, kColorBlack);
            playdate->graphics->setDrawMode(kDrawModeFillWhite);
        }
        else
        {
            playdate->graphics->setDrawMode(kDrawModeFillBlack);
        }

        // Split text at tab character
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

        // Draw left text
        playdate->graphics->setFont(font);
        playdate->graphics->drawText(
            leftText, strlen(leftText), kUTF8Encoding, listX + left_margin, textY
        );

        // Draw right text (chevron) aligned to right
        if (strlen(rightText) > 0)
        {
            int rightWidth = playdate->graphics->getTextWidth(
                font, rightText, strlen(rightText), kUTF8Encoding, 0
            );
            playdate->graphics->drawText(
                rightText, strlen(rightText), kUTF8Encoding,
                listX + listWidth - rightWidth - right_margin, textY
            );
        }

        cb_free(fullText);
        rowY += item->height;
    }

    playdate->graphics->setDrawMode(kDrawModeCopy);
}

static void update_common(CB_HomebrewHubScene* pds, HomebrewHubContext* context, float dt)
{
    if (context->list)
        CB_ListView_update(context->list);
}

static void confirm_download(CB_HomebrewHubScene* hbs, int option)
{
    // download file
    if (option == 1)
    {
        hbs->active_download_type = HB_DL_ROM;
        http_safe_replace_get(
            hbs->active_http_connection, CB_App->hbApiDomain, hbs->urlpath,
            "to download the selected ROM", (void*)rom_get_cb, 59 * 1001, hbs
        );
    }
    cb_free(hbs->urlpath);
}

static void context_list_files_update(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt
)
{
    update_common(hbs, context, dt);

    bool a_pressed = (CB_App->buttons_pressed & kButtonA);

    if (a_pressed)
    {
        if (http_safe_in_progress(hbs->active_http_connection_2))
        {
            // this seems to prevent a crash that occurs when two downloads are happening
            // simultaneously
            CB_presentModal(
                CB_Modal_new("Please wait for the current operation to finish.", NULL, NULL, NULL)
                    ->scene
            );
        }
        else
        {
            int selected = context->list->selectedItem;
            if (selected >= context->list->items->length)
                return;

            json_value je = *context->j;
            json_value jfiles = json_get_table_value(je, "files");
            if (jfiles.type != kJSONArray)
                return;
            JsonArray* a = jfiles.data.arrayval;

            CB_ListItemButton* button = (CB_ListItemButton*)context->list->items->items[selected];
            int fidx = button->ud.uint;

            if (fidx >= a->n)
                return;
            json_value jf = a->data[fidx];
            const char* fname = json_as_string(json_get_table_value(jf, "filename"));
            const char* slug = json_as_string(json_get_table_value(je, "slug"));
            const char* base = json_as_string(json_get_table_value(je, "basepath"));
            char* name = (char*)json_as_string(json_get_table_value(je, "title"));
            if (!cb_valid_basename(name))
            {
                name = cb_basename(fname, false);
            }
            else
            {
                name = aprintf("%s%s", name, get_extension(fname));
            }

            hbs->urlpath = aprintf("%s/%s/entries/%s/%s", CB_App->hbStaticPath, base, slug, fname);

            cb_free(hbs->target_rom_path);
            hbs->target_rom_path = aprintf("%s/%s", cb_gb_directory_path(CB_gamesPath), name);
            char* cover_art_name = cb_basename(name, true);
            cb_free(hbs->target_cover_art_path);
            hbs->target_cover_art_path =
                aprintf("%s/%s.pdi", cb_gb_directory_path(CB_coversPath), cover_art_name);
            cb_free(cover_art_name);
            cb_free(name);

            // we check kFileRead too because even if the rom is pdx only for some reason,
            // the user should probably still be informed.
            if (cb_file_exists(hbs->target_rom_path, kFileReadData | kFileRead))
            {
                const char* options[] = {"Cancel", "Overwrite", NULL};
                CB_Modal* modal = CB_Modal_new(
                    "This will replace an existing ROM of the same name. Proceed?", options,
                    (void*)confirm_download, hbs
                );
                modal->width = 350;
                modal->height = 140;
                CB_presentModal(modal->scene);
            }
            else
            {
                confirm_download(hbs, 1);
            }
        }
    }
}

static void context_top_level_update(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt
)
{
    update_common(hbs, context, dt);

    bool a_pressed = (CB_App->buttons_pressed & kButtonA);

    if (a_pressed)
    {
        if (context->list->selectedItem == 2)
        {
            http_safe_cancel(hbs->active_http_connection);
            http_safe_cancel(hbs->active_http_connection_2);
            CB_ParentalLockScene* plScene = CB_ParentalLockScene_new();
            CB_presentModal(plScene->scene);
        }
        else
        {
            if (CB_App->parentalLockEngaged)
            {
                CB_presentModal(CB_Modal_new("Parental Lock engaged.", NULL, NULL, NULL)->scene);
            }
            else
            {
                push_list_search(hbs, hb_platforms[context->list->selectedItem]);
            }
        }
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
            if (!strcasecmp(s, "cover.png"))
                return s;
            else if (!strcasecmp(s, "cover.bmp"))
                return s;
            else if (!strcasecmp(s, "cover.jpg"))
                return s;
            else if (endswithi(s, ".png"))
                bestidx = i;
            else if (endswithi(s, ".bmp"))
                bestidx = i;
        }
    }

    if (bestidx < 0)
        return NULL;

    return json_as_string(screenshots->data[bestidx]);
}

static void cover_art_cb(unsigned flags, char* data, size_t data_len, CB_HomebrewHubScene* hbs)
{
    if (flags & (~HTTP_ENABLE_ASKED))
    {
        return;
    }
    else
    {
        size_t pdi_size;
        void* pdi_data = png_to_pdi(
            hbs->download_image_name, data, data_len, &pdi_size, LCD_COLUMNS - kDividerX, 160
        );
        cb_free(hbs->cover_art_data);
        hbs->cover_art_data = png_to_pdi(
            hbs->download_image_name, data, data_len, &hbs->cover_art_len, 240, 240
        ); /* 240 x 240 is the preferred cover art dimensions */
        if (pdi_data && pdi_size)
        {
            if (pdi_size < (1 << 16))
            {
                cb_write_entire_file(DISK_IMAGE, pdi_data, pdi_size);
                playdate->system->logToConsole("successfully retrieved image");
            }
            else
            {
                playdate->system->logToConsole(
                    "Not saving " DISK_IMAGE " because file size is too big (%u bytes)",
                    (unsigned)pdi_size
                );
            }
        }

        if (pdi_data)
            cb_free(pdi_data);
    }
}

static void clear_page(CB_HomebrewHubScene* hbs, HomebrewHubContext* context);
static void http_search(CB_HomebrewHubScene* hbs, int page_index, const char* platform);

static void context_list_search_update(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt
)
{
    update_common(hbs, context, dt);

    bool a_pressed = (CB_App->buttons_pressed & kButtonA);
    bool l_pressed = (CB_App->buttons_pressed & kButtonLeft);
    bool r_pressed = (CB_App->buttons_pressed & kButtonRight);

    if (context->list->selectedItem <= 0)
    {
        context->show_image = false;
        int prev_i = context->i;

        if (l_pressed)
        {
            if (--context->i <= 0)
                context->i = MAX(1, hbs->max_pages);
        }
        else if (r_pressed || a_pressed)
        {
            if (++context->i > hbs->max_pages)
                context->i = 1;
        }

        if (prev_i != context->i)
        {
            clear_page(hbs, context);

            http_search(hbs, context->i, context->str);
        }
    }
    else
    {
        context->show_image = true;
        int selected = context->list->selectedItem - 1;
        unsigned dlii = (context->i << 16) | selected;
        if ((dlii != hbs->download_image_index ||
             (!http_safe_in_progress(hbs->active_http_connection_2) && !hbs->download_image)))
        {
            hbs->download_image_index = dlii;
            if (hbs->download_image)
            {
                playdate->graphics->freeBitmap(hbs->download_image);
                hbs->download_image = NULL;
            }

            hbs->download_image =
                call_with_main_stack_2(playdate->graphics->loadBitmap, DISK_IMAGE, NULL);
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
                                    "%s/%s/entries/%s/%s", CB_App->hbStaticPath, base, slug,
                                    screenshot
                                );
                                hbs->download_image_name = screenshot;

                                // TODO: associate cover art with slug, to be extra sure it matches
                                // when ROM download completes later.
                                cb_free(hbs->cover_art_data);
                                hbs->cover_art_data = NULL;

                                // get image
                                http_safe_replace_get(
                                    hbs->active_http_connection_2, CB_App->hbApiDomain, urlpath,
                                    "to retrieve cover art", (void*)cover_art_cb, 12 * 1000, hbs
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
            json_value jentries = json_get_table_value(hbs->jsearch, "entries");
            if (jentries.type == kJSONArray)
            {
                JsonArray* array = jentries.data.arrayval;

                if (selected < array->n)
                {
                    if (!push_list_files(hbs, &array->data[selected]))
                    {
                        CB_presentModal(
                            CB_Modal_new("Failed to list files.", NULL, NULL, NULL)->scene
                        );
                    }
                }
            }
        }
    }
}

static void clear_search(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    hbs->max_pages = 0;
    free_json_data(hbs->jsearch);
    hbs->jsearch.type = kJSONNull;
}

static context_free_fn context_free[HBSCT_MAX] = {NULL, clear_search, NULL};

static context_hint_fn context_hint[HBSCT_MAX] = {
    context_top_level_hint, context_list_search_hint, context_list_files_hint
};

static context_update_fn context_update[HBSCT_MAX] = {
    context_top_level_update, context_list_search_update, context_list_files_update
};

static context_draw_fn context_draw[HBSCT_MAX] = {draw_top_level, draw_common, draw_common};

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

static HomebrewHubContext* getFirstMatchingContext(
    CB_HomebrewHubScene* hbs, HomebrewHubSceneContextType type
)
{
    for (int i = 0; i < CB_HBH_STACK_MAX_DEPTH; ++i)
    {
        if (hbs->context[i].type == type)
            return &hbs->context[i];
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

static void clear_page(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    CB_ListView_clear(context->list);

    char* label = (hbs->max_pages) ? aprintf("< Page %d of %d >", context->i, hbs->max_pages)
                                   : aprintf("< Page %d >", context->i);
    CB_ListItemButton* itemButton = CB_ListItemButton_new(label);
    cb_free(label);
    array_push(context->list->items, itemButton);
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

    clear_page(hbs, context);

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
    hbs->active_download_type = HB_DL_NONE;
    hbs->cached_hint_key = 0;

    if (flags & (HTTP_CANCELLED))
        return;
    if (flags == (HTTP_ENABLE_DENIED | HTTP_ENABLE_ASKED))
        return;

    if (flags & (~HTTP_ENABLE_ASKED))
    {
        hbs->target_context_depth = 0;
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
        CB_presentModal(CB_Modal_new(msg, NULL, NULL, NULL)->scene);
        cb_free(msg);
    }
    else
    {
        char* json_begin = memchr(data, '{', data_len);
        if (json_begin == NULL)
        {
        err_invalid_json:
            CB_presentModal(CB_Modal_new("Invalid JSON received", NULL, NULL, NULL)->scene);
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
    /* Fetch Open Source games from Homebrew Hub */
    char* extra_flags =
        CB_App->hbSearchExtraFlags ? aprintf("&%s", CB_App->hbSearchExtraFlags) : aprintf("");
    char* urlpath = aprintf(
        "%s/search?&platform=%s&page=%d%s", CB_App->hbApiPath, platform, MAX(page_index, 1),
        extra_flags
    );
    cb_free(extra_flags);

    if (hbs->download_image)
    {
        playdate->graphics->freeBitmap(hbs->download_image);
        hbs->download_image = 0;
    }

    hbs->active_download_type = HB_DL_LIST;
    http_safe_replace_get(
        hbs->active_http_connection, CB_App->hbApiDomain, urlpath, "to browse homebrew",
        (void*)http_search_cb, 15 * 1000, hbs
    );

    cb_free(urlpath);
}

static bool push_list_files(CB_HomebrewHubScene* hbs, const json_value* entry)
{
    json_value v = json_get_table_value(*entry, "files");
    if (v.type != kJSONArray)
        return false;

    bool has_playable = false;

    JsonArray* a = v.data.arrayval;
    bool include[a->n];
    for (int i = 0; i < a->n; ++i)
    {
        json_value jf = a->data[i];
        const char* fname = json_as_string(json_get_table_value(jf, "filename"));
        bool playable = json_get_table_value(jf, "playable").type == kJSONTrue;
        if (playable && fname && (endswithi(fname, ".gb") || endswithi(fname, ".gbc")))
        {
            has_playable = true;
            include[i] = true;
        }
        else
        {
            include[i] = false;
        }
    }

    if (!has_playable)
        return false;

    CB_ListItemButton* itemButton;
    HomebrewHubContext* context = push_context(hbs);
    if (!context)
        return false;
    context->type = HBSCT_LIST_FILES;
    context->j = entry;
    context->show_image = true;

    int n = 0;
    for (int i = 0; i < a->n; ++i)
    {
        json_value jf = a->data[i];
        const char* fname = json_as_string(json_get_table_value(jf, "filename"));
        bool isdefault = json_get_table_value(jf, "default").type == kJSONTrue;

        if (!include[i])
            continue;

        itemButton = CB_ListItemButton_new(fname);
        itemButton->ud.uint = i;
        array_push(context->list->items, itemButton);
        if (isdefault)
            context->list->selectedItem = n;
        ++n;
    }

    CB_ListView_reload(context->list);

    return true;
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
    hbs->doctor_header_cgb_flag = !strcasecmp(platform, "GB");
    context->i = 1;  // page

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

    itemButton = CB_ListItemButton_new("Browse GB games\t>");
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new("Browse CGB games\t>");
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new("Parental Lock\t>");
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

    // stops some bugs relating to downloading for some reason.
    playdate->system->setAutoLockDisabled(true);

    if (hbs->is_dismissing)
    {
        // When dismissing from game scope, fade header back IN to match settings scene
        TOWARD(hbs->header_animation_p, 1.0f, dt * HEADER_ANIMATION_RATE);
        if (hbs->header_animation_p == 1.0f)
        {
            CB_dismiss(hbs->scene);
            return;
        }
    }
    else
    {
        // Hub scene fades header OUT (opposite of patch download which fades IN)
        TOWARD(hbs->header_animation_p, 0.0f, dt * HEADER_ANIMATION_RATE);

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
            if (hbs->context_depth == 1)
            {
                // At the top level panel
                // Opposite logic: if started with header (game scope), fade it back in
                if (hbs->started_without_header)
                {
                    --hbs->target_context_depth;
                }
                else
                {
                    hbs->is_dismissing = true;
                }
            }
            else
            {
                --hbs->target_context_depth;
            }
            http_safe_cancel(hbs->active_http_connection);
            hbs->active_download_type = HB_DL_NONE;
        }
    }

    int header_y = hbs->header_animation_p * HEADER_HEIGHT + 0.5f;
    bool isAnimating = (hbs->context_depth_p != hbs->target_context_depth);
    playdate->graphics->clear(kColorWhite);
    int list_padding_top = 24 - (int)(9.0f * hbs->header_animation_p);

    int n = (hbs->context_depth_p >= 1) ? 2 : 1;
    if (hbs->is_dismissing)
    {
        n = 1;
    }

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
            context->list->hideScrollIndicator = isAnimating || hbs->is_dismissing;
        if (fn)
            fn(hbs, context, x, i == 0 && !hbs->is_dismissing);
    }

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
        int hint_padding_top = 29 - (int)(9.0f * hbs->header_animation_p);
        int rightPaneY = header_y + hint_padding_top;

        int rightPaneWidth = LCD_COLUMNS - kDividerX - (kRightPanePadding * 2);
        int rightPaneHeight = LCD_ROWS - rightPaneY;
        playdate->graphics->drawTextInRect(
            hbs->cached_hint, strlen(hbs->cached_hint), kUTF8Encoding, rightPaneX, rightPaneY,
            rightPaneWidth, rightPaneHeight, kWrapWord, kAlignTextLeft
        );
    }

    // TODO: only show in some contexts
    if (hbs->download_image && hbs->context[hbs->context_depth - 1].show_image)
    {
        int w, h;
        playdate->graphics->getBitmapData(hbs->download_image, &w, &h, NULL, NULL, NULL);
        playdate->graphics->setDrawMode(kDrawModeCopy);
        playdate->graphics->drawBitmap(
            hbs->download_image, kDividerX + MAX(0, (LCD_COLUMNS - kDividerX - w) / 2),
            LCD_ROWS - h, kBitmapUnflipped
        );
    }
    else if (http_safe_in_progress(hbs->active_http_connection_2))
    {
        // Cover art loading - show spinner in lower right only if not on page entry
        HomebrewHubContext* current_context = &hbs->context[hbs->context_depth - 1];
        if (current_context->list && current_context->list->selectedItem > 0)
        {
            draw_spinny((kDividerX + LCD_COLUMNS) / 2, 180, 34);
        }
    }

    playdate->graphics->drawLine(kDividerX, header_y, kDividerX, LCD_ROWS, 1, kColorBlack);

    // Draw header with game name if header is visible
    if (header_y > 0 && hbs->header_name[0])
    {
        const char* name = hbs->header_name;
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

    // Show loading modal when downloading ROM or refreshing list (blocking UI)
    if ((hbs->active_download_type == HB_DL_ROM || hbs->active_download_type == HB_DL_LIST) &&
        http_safe_in_progress(hbs->active_http_connection))
    {
        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, LCD_ROWS, (LCDColor)&lcdp_t_50[0]);

        int box_w = 260;
        int box_h = 70;
        int box_x = (LCD_COLUMNS - box_w) / 2;
        int box_y = (LCD_ROWS - box_h) / 2;
        playdate->graphics->fillRect(box_x, box_y, box_w, box_h, kColorWhite);
        playdate->graphics->drawRect(box_x, box_y, box_w, box_h, kColorBlack);

        hbs->anim_t += dt;
        if (hbs->anim_t >= 0.5f)
        {
            hbs->anim_t -= 0.5f;
            hbs->loading_anim_step = (hbs->loading_anim_step + 1) % 3;
        }

        int num_dots = hbs->loading_anim_step + 1;
        char dots[4] = "...";
        dots[num_dots] = '\0';

        const char* base_text =
            (hbs->active_download_type == HB_DL_ROM) ? "Downloading ROM" : "Refreshing List";

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
        hbs->anim_t = 0;
        hbs->loading_anim_step = 0;
    }
}

void CB_HomebrewHubScene_free(CB_HomebrewHubScene* hbs)
{
    http_safe_free(hbs->active_http_connection);
    http_safe_free(hbs->active_http_connection_2);
    playdate->system->setAutoLockDisabled(false);

    CB_Scene_free(hbs->scene);
    cb_free(hbs->cover_art_data);
    while (hbs->context_depth > 0)
    {
        pop_context(hbs);
    }
    cb_free(hbs->target_rom_path);
    if (hbs->download_image)
        playdate->graphics->freeBitmap(hbs->download_image);
    cb_free(hbs->cached_hint);
    free_json_data(hbs->jsearch);
    cb_free(hbs);
}

static void CB_HomebrewHubScene_didSelectSettings(void* userdata)
{
    CB_HomebrewHubScene* hbs = userdata;

    // Opposite of patch download: if we started WITH header (game scope),
    // we need to fade it back IN before dismissing
    if (hbs->started_without_header)
    {
        hbs->target_context_depth = -1;
    }
    else
    {
        hbs->is_dismissing = true;
    }
}

static void CB_HomebrewHubScene_menu(void* object)
{
    CB_HomebrewHubScene* hbs = object;
    playdate->system->removeAllMenuItems();
    playdate->system->addMenuItem("settings", CB_HomebrewHubScene_didSelectSettings, hbs);
}

CB_HomebrewHubScene* CB_HomebrewHubScene_new(float initial_header_p, const char* header_name)
{
    CB_Scene* scene = CB_Scene_new();
    CB_HomebrewHubScene* hbs = allocz(CB_HomebrewHubScene);
    hbs->scene = scene;
    hbs->option_hold_time = 0.0f;
    hbs->header_animation_p = initial_header_p;
    hbs->started_without_header = (initial_header_p < 1.0f);
    hbs->is_dismissing = false;
    scene->managedObject = hbs;

    hbs->active_http_connection = http_safe_new();
    hbs->active_http_connection_2 = http_safe_new();
    hbs->active_download_type = HB_DL_NONE;

    hbs->cached_hint_key = -2;
    hbs->loading_anim_step = 0;

    if (header_name)
    {
        strncpy(hbs->header_name, header_name, sizeof(hbs->header_name) - 1);
        hbs->header_name[sizeof(hbs->header_name) - 1] = '\0';
    }
    else
    {
        hbs->header_name[0] = '\0';
    }

    scene->update = (void*)CB_HomebrewHubScene_update;
    scene->free = (void*)CB_HomebrewHubScene_free;
    scene->menu = (void*)CB_HomebrewHubScene_menu;

    push_top_level(hbs);

    return hbs;
}
