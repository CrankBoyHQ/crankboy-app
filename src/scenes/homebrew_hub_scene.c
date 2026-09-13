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
static void clear_page(CB_HomebrewHubScene* hbs, HomebrewHubContext* context);
static void http_search(CB_HomebrewHubScene* hbs, int page_index, const char* platform);

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
            msg = cb_strdup(T(net_wifi_unavailable));
        }
        else if (flags & HTTP_ENABLE_DENIED)
        {
            msg = cb_strdup(T(net_permission_denied));
        }
        else
        {
            msg = aprintf(T(net_error), flags);
        }
        CB_presentModal(CB_Modal_new(msg, NULL, NULL, NULL)->scene);
        cb_free(msg);
    }
    else if (!data || !data_len)
    {
        CB_presentModal(CB_Modal_new(T(hhub_rom_empty), NULL, NULL, NULL)->scene);
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
        CB_presentModal(CB_Modal_new(T(hhub_rom_save_failed), NULL, NULL, NULL)->scene);
    }
    else
    {
        // try saving the cover art from the slug-keyed disk cache.
        if (hbs->target_cover_art_path && hbs->target_rom_slug)
        {
            char* cover_path = aprintf(
                "%s/%s_cover.pdi", cb_gb_directory_path(CB_hbCachePath), hbs->target_rom_slug
            );
            size_t cover_len = 0;
            char* cover = cb_read_entire_file(cover_path, &cover_len, kFileReadData | kFileRead);

            if (cover && cover_len)
            {
                if (!cb_write_entire_file(hbs->target_cover_art_path, cover, cover_len))
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
            else
            {
                // ROM beat the screenshot download; save the cover once it lands.
                hbs->pending_cover_save = true;
            }

            cb_free(cover);
            cb_free(cover_path);
        }

        // save as 'last selected' for library view
        cb_write_entire_file(
            LAST_SELECTED_FILE, hbs->target_rom_path, strlen(hbs->target_rom_path)
        );

        const char* options[] = {T(label_restart), T(label_do_not), NULL};

        const char* suffix = did_doctor ? T(hhub_rom_downloaded_dmg) : "";
        char* s = aprintf("%s%s", T(hhub_rom_downloaded), suffix);

        CB_Modal* modal = CB_Modal_new(s, options, user_quit, NULL);
        CB_presentModal(modal->scene);

        cb_free(s);
    }

    cb_free(data);
}

static char* context_list_files_hint(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    return aprintf(T(hhub_download_hint));
}

static char* context_list_search_hint(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    switch (context->list->selectedItem)
    {
    case 0:
    {
        json_value jentries = json_get_table_value(hbs->jsearch, "entries");
        JsonArray* array =
            (jentries.type == kJSONArray) ? (JsonArray*)jentries.data.arrayval : NULL;
        if (!array || array->n == 0)
            return NULL;
        return aprintf(T(hhub_switch_page_hint));
    }
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
                    const char* typetag = json_as_string(json_get_table_value(je, "typetag"));
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

                    char typetag_buf[32];
                    if (typetag && *typetag)
                    {
                        snprintf(typetag_buf, sizeof(typetag_buf), "%s", typetag);
                        if (typetag_buf[0] >= 'a' && typetag_buf[0] <= 'z')
                            typetag_buf[0] -= 32;
                        typetag = typetag_buf;
                    }

                    return aprintf(
                        "Title: %s\nDev: %s\nType: %s\nDate: %s", title ? title : "unknown",
                        developer ? developer : "unknown", typetag ? typetag : "unknown",
                        date ? date : "unknown"
                    );
                }
            }
        }

        return NULL;
    }
    break;
    }
}

// Returns a display-friendly copy of a comma-separated list, joining
// tokens with ", " and capitalizing the first letter of each token
// (e.g. "rpg,action" -> "Rpg, Action", "RPG,Action" -> "RPG, Action").
static char* hb_pretty_csv(const char* csv)
{
    char* result = aprintf("");
    const char* p = csv ? csv : "";
    while (*p)
    {
        const char* comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        const char *s = p, *e = p + len;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
            e--;

        char* next;
        if (s < e)
        {
            char first = (*s >= 'a' && *s <= 'z') ? (char)(*s - 32) : *s;
            next = aprintf("%s%c%.*s%s", result, first, (int)(e - s - 1), s + 1, comma ? ", " : "");
        }
        else
        {
            next = aprintf("%s%s", result, comma ? ", " : "");
        }
        cb_free(result);
        result = next;
        if (!comma)
            break;
        p = comma + 1;
    }
    return result;
}

static char* context_top_level_hint(CB_HomebrewHubScene* hbs, HomebrewHubContext* context)
{
    switch (context->list->selectedItem)
    {
    case 0:
        return aprintf(T(hhub_browse_gb_desc));
        break;
    case 1:
    case 3:
    {
        const char* desc =
            (context->list->selectedItem == 1) ? T(hhub_search_gb_desc) : T(hhub_search_cgb_desc);
        char* tags = hb_pretty_csv(CB_App->hbTagKeywords);
        char* types = hb_pretty_csv(CB_App->hbTypetagKeywords);
        char* hint = aprintf(
            "%s\n\n%s: %s\n\n%s: %s", desc, T(hhub_search_tags_label), tags,
            T(hhub_search_types_label), types
        );
        cb_free(tags);
        cb_free(types);
        return hint;
    }
    case 2:
        return aprintf(T(hhub_browse_cgb_desc));
        break;
    case 4:
        return aprintf(T(hhub_parental_lock_desc));
        break;
    default:
        return NULL;
    }
}

static void draw_common(CB_HomebrewHubScene* pds, HomebrewHubContext* context, int x, bool active)
{
    int left_margin = 0;
    int right_margin = 0;

    int header_y = pds->header_animation_p * CB_HEADER_HEIGHT + 0.5f;

    PDRect frame = {
        x + left_margin, header_y, kDividerX - left_margin - right_margin, LCD_ROWS - header_y
    };

    context->list->frame = frame;
    context->list->textInset = 8;
    context->list->needsDisplay = true;

    CB_ListView_draw(context->list);
}

static void draw_top_level(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, int x, bool active
)
{
    int left_margin = 20;
    int right_margin = 20;
    int header_y = hbs->header_animation_p * CB_HEADER_HEIGHT + 0.5f;

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
            T(net_rom_download_reason), (void*)rom_get_cb, 59 * 1001, hbs
        );
    }
    cb_free(hbs->urlpath);
    hbs->urlpath = NULL;
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
            CB_presentModal(CB_Modal_new(T(hhub_please_wait), NULL, NULL, NULL)->scene);
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

            cb_free(hbs->target_rom_slug);
            hbs->target_rom_slug = slug ? cb_strdup(slug) : NULL;
            hbs->pending_cover_save = false;

            // we check kFileRead too because even if the rom is pdx only for some reason,
            // the user should probably still be informed.
            if (cb_file_exists(hbs->target_rom_path, kFileReadData | kFileRead))
            {
                const char* options[] = {T(label_cancel), T(label_overwrite), NULL};
                CB_Modal* modal =
                    CB_Modal_new(T(hhub_overwrite_prompt), options, (void*)confirm_download, hbs);
                CB_presentModal(modal->scene);
            }
            else
            {
                confirm_download(hbs, 1);
            }
        }
    }
}

#ifdef CRANKBOY_PDKEYBOARD
static void open_search_keyboard(
    CB_HomebrewHubScene* hbs, const char* platform, const char* initial_text
)
{
    PDKeyboard* kb = CB_init_keyboard(PDKBF_DEFAULT, NULL, NULL);
    if (!kb)
        return;

    pdkb_set_max_bytes(kb, 64);
    pdkb_set_content(kb, initial_text ? initial_text : "");
    pdkb_open(kb);

    hbs->keyboard = kb;
    hbs->search_platform = platform;
    hbs->search_result_handled = false;
    cb_play_ui_sound(CB_UISound_Confirm);
}

static void draw_search_field(CB_HomebrewHubScene* hbs)
{
    const char* content = pdkb_get_content(hbs->keyboard);
    bool empty = !content || !*content;
    const char* display = empty ? T(hhub_search_placeholder) : content;

    int kb_top = pdkb_get_visible_y(hbs->keyboard);
    playdate->graphics->fillRect(0, 0, LCD_COLUMNS, kb_top, (LCDColor)&lcdp_t_50[0]);

    LCDFont* font = CB_App->bodyFont;
    playdate->graphics->setFont(font);

    int box_x = 16;
    int box_w = LCD_COLUMNS - 32;
    int box_h = 36;

    // slide down/up in sync with the keyboard's open/close animation
    float open_p = pdkb_get_open_p(hbs->keyboard);
    float eased = 1.0f - (1.0f - open_p) * (1.0f - open_p);
    int box_y = (int)(-(float)box_h + ((16.0f + box_h) * eased));

    playdate->graphics->setDrawMode(kDrawModeCopy);
    playdate->graphics->fillRect(box_x, box_y, box_w, box_h, kColorWhite);
    playdate->graphics->drawRect(box_x, box_y, box_w, box_h, kColorBlack);

    int text_x = box_x + 8;
    int text_y = box_y + (box_h - playdate->graphics->getFontHeight(font)) / 2;
    int text_w = playdate->graphics->getTextWidth(font, display, strlen(display), kUTF8Encoding, 0);

    playdate->graphics->setDrawMode(kDrawModeFillBlack);
    playdate->graphics->drawText(display, strlen(display), kUTF8Encoding, text_x, text_y);

    unsigned now = playdate->system->getCurrentTimeMilliseconds();
    if ((now / 500) % 2 == 0)
    {
        int cursor_x = empty ? text_x : text_x + text_w + 2;
        playdate->graphics->fillRect(cursor_x, box_y + 6, 2, box_h - 12, kColorBlack);
    }
}

static void update_search_keyboard(CB_HomebrewHubScene* hbs, float dt)
{
    PDKeyboard* kb = hbs->keyboard;
    if (!kb)
        return;

    draw_search_field(hbs);

    playdate->graphics->setDrawMode(kDrawModeCopy);
    pdkb_update(kb, dt);

    if (!hbs->search_result_handled)
    {
        int result = pdkb_get_result(kb);
        if (result != 0)
        {
            if (result > 0)
            {
                const char* content = pdkb_get_content(kb);

                if (hbs->context_depth > 0 &&
                    hbs->context[hbs->context_depth - 1].type == HBSCT_LIST_SEARCH)
                {
                    HomebrewHubContext* ctx = &hbs->context[hbs->context_depth - 1];
                    cb_free(hbs->search_query);
                    hbs->search_query = (content && *content) ? cb_strdup(content) : NULL;
                    ctx->i = 1;
                    clear_page(hbs, ctx);
                    http_search(hbs, 1, ctx->str);
                }
                else if (content && *content)
                {
                    cb_free(hbs->search_query);
                    hbs->search_query = cb_strdup(content);
                    push_list_search(hbs, hbs->search_platform);
                }
            }

            // cancel or main-view empty: leave search_query as-is
            hbs->search_result_handled = true;
        }
    }

    if (pdkb_get_state(kb) == PDKBS_CLOSED)
        hbs->keyboard = NULL;
}
#endif

static void context_top_level_update(
    CB_HomebrewHubScene* hbs, HomebrewHubContext* context, float dt
)
{
    update_common(hbs, context, dt);

    bool a_pressed = (CB_App->buttons_pressed & kButtonA);

    if (a_pressed)
    {
        int sel = context->list->selectedItem;

        if (sel == 4)
        {
            http_safe_cancel(hbs->active_http_connection);
            http_safe_cancel(hbs->active_http_connection_2);
            hbs->active_download_type = HB_DL_NONE;
            CB_ParentalLockScene* plScene = CB_ParentalLockScene_new();
            CB_presentModal(plScene->scene);
        }
        else if (CB_App->parentalLockEngaged)
        {
            CB_presentModal(CB_Modal_new(T(hhub_engaged), NULL, NULL, NULL)->scene);
        }
        else if (sel % 2 == 1)
        {
#ifdef CRANKBOY_PDKEYBOARD
            open_search_keyboard(hbs, hb_platforms[sel / 2], NULL);
#endif
        }
        else
        {
            cb_free(hbs->search_query);
            hbs->search_query = NULL;
            push_list_search(hbs, hb_platforms[sel / 2]);
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
            else if (cb_file_has_extension(s, ".png"))
                bestidx = i;
            else if (cb_file_has_extension(s, ".bmp"))
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
        int img_width = 0, img_height = 0;
        unsigned char* img =
            cb_decode_png(hbs->download_image_name, data, data_len, &img_width, &img_height);

        size_t pdi_size = 0;
        void* pdi_data = NULL;
        size_t cover_size = 0;
        void* cover_data = NULL;

        if (img)
        {
            pdi_data = rgba_to_pdi(
                hbs->download_image_name, img, img_width, img_height, &pdi_size,
                LCD_COLUMNS - kDividerX, 160
            );
            cover_data = rgba_to_pdi(
                hbs->download_image_name, img, img_width, img_height, &cover_size, 240, 240
            );

            cb_free_decoded_image(img);
        }

        if (hbs->download_image_slug)
        {
            if (pdi_data && pdi_size)
            {
                char* cache_path = aprintf(
                    "%s/%s_preview.pdi", cb_gb_directory_path(CB_hbCachePath),
                    hbs->download_image_slug
                );

                if (pdi_size < (1 << 16))
                {
                    cb_write_entire_file(cache_path, pdi_data, pdi_size);
                    playdate->system->logToConsole("successfully retrieved image");
                }
                else
                {
                    playdate->system->logToConsole(
                        "Not saving cover art because file size is too big (%u bytes)",
                        (unsigned)pdi_size
                    );
                }

                cb_free(cache_path);
            }

            // Cache the 240x240 cover art keyed by slug; the ROM save path
            // reads this from disk so the cover always matches the game.
            if (cover_data && cover_size)
            {
                char* cover_path = aprintf(
                    "%s/%s_cover.pdi", cb_gb_directory_path(CB_hbCachePath),
                    hbs->download_image_slug
                );
                cb_write_entire_file(cover_path, cover_data, cover_size);
                cb_free(cover_path);
            }

            // Race: ROM finished before this screenshot; save the cover now.
            if (hbs->pending_cover_save && hbs->target_rom_slug && cover_data && cover_size &&
                !strcmp(hbs->download_image_slug, hbs->target_rom_slug))
            {
                cb_write_entire_file(hbs->target_cover_art_path, cover_data, cover_size);
                hbs->pending_cover_save = false;
            }
        }

        if (pdi_data)
            cb_free(pdi_data);
        if (cover_data)
            cb_free(cover_data);

        cb_free(data);
    }
}

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

            json_value jentries = json_get_table_value(hbs->jsearch, "entries");
            if (jentries.type == kJSONArray)
            {
                JsonArray* array = jentries.data.arrayval;

                if (selected < array->n)
                {
                    json_value je = array->data[selected];
                    const char* slug = json_as_string(json_get_table_value(je, "slug"));

                    if (slug)
                    {
                        char* preview_path = aprintf(
                            "%s/%s_preview.pdi", cb_gb_directory_path(CB_hbCachePath), slug
                        );
                        char* cover_path =
                            aprintf("%s/%s_cover.pdi", cb_gb_directory_path(CB_hbCachePath), slug);

                        hbs->download_image = call_with_main_stack_2(
                            playdate->graphics->loadBitmap, preview_path, NULL
                        );

                        // Preview + cover are written together; treat them as a
                        // pair. If either file is missing, re-download so both
                        // are regenerated (a lone preview would otherwise never
                        // regain its cover).
                        bool cover_exists = cb_file_exists(cover_path, kFileReadData | kFileRead);

                        if (!hbs->download_image || !cover_exists)
                        {
                            json_value jscreenshots = json_get_table_value(je, "screenshots");
                            const char* base = json_as_string(json_get_table_value(je, "basepath"));

                            if (jscreenshots.type == kJSONArray)
                            {
                                JsonArray* screenshots = jscreenshots.data.arrayval;
                                const char* screenshot = get_best_screenshot(screenshots);
                                if (screenshot)
                                {
                                    char* urlpath = aprintf(
                                        "%s/%s/entries/%s/%s", CB_App->hbStaticPath, base, slug,
                                        screenshot
                                    );
                                    // owned copy: `screenshot` borrows into the jsearch
                                    // tree, which clear_search() may free while this
                                    // cover-art request is still in flight.
                                    cb_free(hbs->download_image_name);
                                    hbs->download_image_name = cb_strdup(screenshot);
                                    cb_free(hbs->download_image_slug);
                                    hbs->download_image_slug = cb_strdup(slug);

                                    // get image
                                    http_safe_replace_get(
                                        hbs->active_http_connection_2, CB_App->hbApiDomain, urlpath,
                                        T(net_cover_art_retrieve_reason), (void*)cover_art_cb,
                                        12 * 1000, hbs
                                    );

                                    cb_free(urlpath);
                                }
                            }
                        }

                        cb_free(preview_path);
                        cb_free(cover_path);
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
                        CB_presentModal(CB_Modal_new(T(hhub_failed_list), NULL, NULL, NULL)->scene);
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

    char* label = (hbs->max_pages) ? aprintf(T(hhub_page_of), context->i, hbs->max_pages)
                                   : aprintf(T(hhub_page), context->i);
    CB_ListItemButton* itemButton = CB_ListItemButton_new(label);
    itemButton->is_header = true;
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

    json_value jentries = json_get_table_value(hbs->jsearch, "entries");
    JsonArray* array = (jentries.type == kJSONArray) ? (JsonArray*)jentries.data.arrayval : NULL;

    if (!array || array->n == 0)
    {
        CB_ListView_clear(context->list);
        CB_ListItemButton* nb = CB_ListItemButton_new(T(hhub_no_results));
        nb->is_header = true;
        array_push(context->list->items, nb);
        CB_ListView_reload(context->list);
        return;
    }

    clear_page(hbs, context);

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
            array_push(context->list->items, CB_ListItemButton_new(T(status_error)));
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
            msg = cb_strdup(T(net_wifi_unavailable));
        }
        else if (flags & HTTP_ENABLE_DENIED)
        {
            msg = cb_strdup(T(net_permission_denied));
        }
        else
        {
            msg = aprintf(T(net_error), flags);
        }
        CB_presentModal(CB_Modal_new(msg, NULL, NULL, NULL)->scene);
        cb_free(msg);
    }
    else
    {
        if (!data || data_len == 0)
            goto err_invalid_json;

        char* json_begin = memchr(data, '{', data_len);
        if (json_begin == NULL)
        {
        err_invalid_json:
            CB_presentModal(CB_Modal_new(T(hhub_invalid_json), NULL, NULL, NULL)->scene);
            cb_free(data);
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

        cb_free(data);
    }
}

// Returns the caller-owned, comma-separated, URL-encoded tag list from the
// configured extra flags (the Open Source filter).
static char* hb_extra_tags(void)
{
    const char* flags = CB_App->hbSearchExtraFlags;
    if (!flags || !*flags)
        return aprintf("");

    // The extra flags are a single "tags=<value>" parameter; the value may
    // itself be comma-separated (e.g. "Open%20Source").
    if (strncasecmp(flags, "tags=", 5) != 0)
        return aprintf("");
    return aprintf("%s", flags + 5);
}

#define HB_MAX_TAGS 64
#define HB_MAX_WORDS 64

// Splits `s` in-place on whitespace, filling `words` with pointers to each
// null-terminated token. Returns the token count.
static int hb_tokenize(char* s, char** words, int max_words)
{
    int n = 0;
    char* p = s;
    while (*p)
    {
        while (*p == ' ' || *p == '\t')
            *p++ = 0;
        if (!*p)
            break;
        if (n >= max_words)
            break;
        words[n++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    return n;
}

// Returns the number of query words matched (or 0) if the multi-word `tag`
// matches the query words starting at index `i` (case-insensitive).
static int hb_tag_matches_at(const char* tag, char** qwords, int qn, int i)
{
    const char* p = tag;
    int k = 0;
    while (*p)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        const char* w = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (i + k >= qn)
            return 0;
        size_t wlen = (size_t)(p - w);
        if (strlen(qwords[i + k]) != wlen)
            return 0;
        if (strncasecmp(qwords[i + k], w, wlen) != 0)
            return 0;
        k++;
    }
    return k;
}

// Splits `s` (mutable) on commas into trimmed, null-terminated tokens stored
// in `out`. Returns the token count.
static int hb_split_csv(char* s, const char** out, int max)
{
    int count = 0;
    char* p = s;
    while (*p && count < max)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        char* tok = p;
        char* comma = strchr(p, ',');
        if (comma)
            *comma = 0;
        char* end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        if (*p)
            out[count++] = p;
        if (!comma)
            break;
        p = comma + 1;
    }
    return count;
}

// Splits the search query into detected tags, a typetag, and the remaining
// text. On return: *tags_out is a caller-owned, comma-separated, URL-encoded
// tag list (or ""); *typetag_out is caller-owned (or NULL); *q_out is
// caller-owned remaining text (or NULL when empty).
static void hb_build_search_fragments(
    CB_HomebrewHubScene* hbs, char** tags_out, char** typetag_out, char** q_out
)
{
    *tags_out = aprintf("");
    *typetag_out = NULL;
    *q_out = NULL;

    const char* query = hbs->search_query;
    if (!query || !*query)
        return;

    // Parse the comma-separated keyword lists.
    char* taglist = CB_App->hbTagKeywords ? cb_strdup(CB_App->hbTagKeywords) : NULL;
    char* typetaglist = CB_App->hbTypetagKeywords ? cb_strdup(CB_App->hbTypetagKeywords) : NULL;
    const char* tags[HB_MAX_TAGS];
    const char* typetags[HB_MAX_TAGS];
    int tag_count = taglist ? hb_split_csv(taglist, tags, HB_MAX_TAGS) : 0;
    int typetag_count = typetaglist ? hb_split_csv(typetaglist, typetags, HB_MAX_TAGS) : 0;

    // Tokenize the query.
    char* qcopy = cb_strdup(query);
    char* qwords[HB_MAX_WORDS];
    int qn = hb_tokenize(qcopy, qwords, HB_MAX_WORDS);

    int matched[HB_MAX_TAGS];
    int matched_count = 0;
    char* remaining[HB_MAX_WORDS];
    int remaining_count = 0;
    const char* detected_typetag = NULL;

    int i = 0;
    while (i < qn)
    {
        // Check for a typetag keyword (single word). The first match wins;
        // any further typetag words are ignored.
        const char* tt = NULL;
        for (int t = 0; t < typetag_count; ++t)
        {
            if (strcasecmp(qwords[i], typetags[t]) == 0)
            {
                tt = typetags[t];
                break;
            }
        }
        if (tt)
        {
            // "game boy" is the console name, not the "game" typetag.
            if (strcasecmp(qwords[i], "game") == 0 && i + 1 < qn &&
                strcasecmp(qwords[i + 1], "boy") == 0)
            {
                tt = NULL;
            }
        }
        if (tt)
        {
            if (!detected_typetag)
                detected_typetag = tt;
            i++;
            continue;
        }

        // Check genre tags (longest multi-word match wins).
        int best = -1;
        int best_words = 0;
        for (int t = 0; t < tag_count; ++t)
        {
            int m = hb_tag_matches_at(tags[t], qwords, qn, i);
            if (m > best_words)
            {
                best = t;
                best_words = m;
            }
        }

        if (best >= 0)
        {
            matched[matched_count++] = best;
            i += best_words;
        }
        else
        {
            remaining[remaining_count++] = qwords[i];
            i++;
        }
    }

    // Build the comma-separated tags list.
    char* tagsfrag = aprintf("");
    for (int m = 0; m < matched_count; ++m)
    {
        char* enc = url_encode(tags[matched[m]]);
        char* next = aprintf("%s%s%s", tagsfrag, m ? "," : "", enc);
        cb_free(enc);
        cb_free(tagsfrag);
        tagsfrag = next;
    }
    *tags_out = tagsfrag;

    *typetag_out = detected_typetag ? cb_strdup(detected_typetag) : NULL;

    // Build the remaining query text.
    if (remaining_count > 0)
    {
        char* q = aprintf("");
        for (int r = 0; r < remaining_count; ++r)
        {
            char* next = aprintf("%s%s%s", q, r ? " " : "", remaining[r]);
            cb_free(q);
            q = next;
        }
        *q_out = q;
    }

    cb_free(qcopy);
    cb_free(taglist);
    cb_free(typetaglist);
}

static void http_search(CB_HomebrewHubScene* hbs, int page_index, const char* platform)
{
    /* Fetch games from Homebrew Hub */
    char* extra_tags = hb_extra_tags();
    char* detected_tags = NULL;
    char* detected_typetag = NULL;
    char* q_text = NULL;
    hb_build_search_fragments(hbs, &detected_tags, &detected_typetag, &q_text);

    // Combine into a single comma-separated tags parameter (the API only
    // honors one `tags` param; repeated `&tags=` silently drops all but the last).
    char* all_tags;
    if (extra_tags[0] && detected_tags[0])
        all_tags = aprintf("%s,%s", extra_tags, detected_tags);
    else if (extra_tags[0])
        all_tags = aprintf("%s", extra_tags);
    else
        all_tags = aprintf("%s", detected_tags);

    char* tags_param = all_tags[0] ? aprintf("&tags=%s", all_tags) : aprintf("");
    char* typetag_param = detected_typetag ? aprintf("&typetag=%s", detected_typetag) : aprintf("");

    char* q = q_text ? url_encode(q_text) : NULL;

    char* urlpath;
    if (q)
    {
        urlpath = aprintf(
            "%s/search?&platform=%s&page=%d%s%s&q=%s", CB_App->hbApiPath, platform,
            MAX(page_index, 1), tags_param, typetag_param, q
        );
    }
    else
    {
        urlpath = aprintf(
            "%s/search?&platform=%s&page=%d%s%s", CB_App->hbApiPath, platform, MAX(page_index, 1),
            tags_param, typetag_param
        );
    }

    cb_free(extra_tags);
    cb_free(detected_tags);
    cb_free(detected_typetag);
    cb_free(all_tags);
    cb_free(tags_param);
    cb_free(typetag_param);
    cb_free(q_text);
    cb_free(q);

    if (hbs->download_image)
    {
        playdate->graphics->freeBitmap(hbs->download_image);
        hbs->download_image = 0;
    }

    hbs->active_download_type = HB_DL_LIST;
    http_safe_replace_get(
        hbs->active_http_connection, CB_App->hbApiDomain, urlpath, T(net_browse_homebrew_reason),
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
    bool* include = cb_malloc(a->n * sizeof(bool));
    if (!include)
        return false;
    for (int i = 0; i < a->n; ++i)
    {
        json_value jf = a->data[i];
        const char* fname = json_as_string(json_get_table_value(jf, "filename"));
        bool playable = json_get_table_value(jf, "playable").type == kJSONTrue;
        if (playable && fname &&
            (cb_file_has_extension(fname, ".gb") || cb_file_has_extension(fname, ".gbc")))
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
    {
        cb_free(include);
        return false;
    }

    CB_ListItemButton* itemButton;
    HomebrewHubContext* context = push_context(hbs);
    if (!context)
    {
        cb_free(include);
        return false;
    }
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

    cb_free(include);
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

    itemButton = CB_ListItemButton_new("Page 1");
    itemButton->is_header = true;
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

    itemButton = CB_ListItemButton_new(T(hhub_browse_gb));
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new(T(hhub_search_gb));
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new(T(hhub_browse_cgb));
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new(T(hhub_search_cgb));
    array_push(context->list->items, itemButton);

    itemButton = CB_ListItemButton_new(T(hhub_parental_lock));
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

#ifdef CRANKBOY_PDKEYBOARD
    bool kb_active = (hbs->keyboard != NULL);
#else
    bool kb_active = false;
#endif

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
        else if (!kb_active && (CB_App->buttons_pressed & kButtonB))
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

    int header_y = hbs->header_animation_p * CB_HEADER_HEIGHT + 0.5f;
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
            context->list->paddingTop = (context->type == HBSCT_LIST_SEARCH) ? 8 : list_padding_top;
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
            if (fn && !kb_active)
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

        // Calculate dynamic top padding for the RIGHT hint pane (29px -> 15px)
        int base_padding = 29;
        if (hbs->context_depth > 0)
        {
            HomebrewHubContext* ctx = &hbs->context[hbs->context_depth - 1];
            if (ctx->type == HBSCT_LIST_SEARCH)
                base_padding = 15;
        }
        int hint_padding_top = base_padding - (int)(9.0f * hbs->header_animation_p);
        int rightPaneY = header_y + hint_padding_top;

        int rightPaneWidth = LCD_COLUMNS - kDividerX - (kRightPanePadding * 2);
        cb_draw_text_paragraphs(
            font, hbs->cached_hint, rightPaneX, rightPaneY, rightPaneWidth, kAlignTextLeft
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
            draw_progress_ring((kDividerX + LCD_COLUMNS) / 2, 180, 34, 4);
        }
    }

    playdate->graphics->drawLine(kDividerX, header_y, kDividerX, LCD_ROWS, 1, kColorBlack);

    // Draw header with game name if header is visible
    if (header_y > 0 && hbs->header_name[0])
    {
        cb_draw_header(hbs->header_name, header_y);
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

        const char* base_text = (hbs->active_download_type == HB_DL_ROM) ? T(hhub_downloading_rom)
                                                                         : T(hhub_refreshing_list);

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

#ifdef CRANKBOY_PDKEYBOARD
    if (kb_active)
        update_search_keyboard(hbs, dt);
#endif
}

void CB_HomebrewHubScene_free(CB_HomebrewHubScene* hbs)
{
    http_safe_free(hbs->active_http_connection);
    http_safe_free(hbs->active_http_connection_2);
    playdate->system->setAutoLockDisabled(false);

    CB_Scene_free(hbs->scene);
    while (hbs->context_depth > 0)
    {
        pop_context(hbs);
    }
    cb_free(hbs->target_rom_path);
    cb_free(hbs->target_cover_art_path);
    cb_free(hbs->target_rom_slug);
    cb_free(hbs->urlpath);
    if (hbs->download_image)
        playdate->graphics->freeBitmap(hbs->download_image);
    cb_free(hbs->download_image_name);
    cb_free(hbs->download_image_slug);
    cb_free(hbs->search_query);
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

static void CB_HomebrewHubScene_didSelectSearch(void* userdata)
{
    CB_HomebrewHubScene* hbs = userdata;
#ifdef CRANKBOY_PDKEYBOARD
    if (hbs->context_depth > 0)
    {
        HomebrewHubContext* ctx = &hbs->context[hbs->context_depth - 1];
        if (ctx->type == HBSCT_LIST_SEARCH)
            open_search_keyboard(hbs, ctx->str, hbs->search_query);
    }
#endif
}

static void CB_HomebrewHubScene_menu(void* object)
{
    CB_HomebrewHubScene* hbs = object;
    playdate->system->removeAllMenuItems();
    playdate->system->addMenuItem(T(pdmenu_library), CB_HomebrewHubScene_didSelectSettings, hbs);

#ifdef CRANKBOY_PDKEYBOARD
    if (hbs->context_depth > 0 && hbs->context[hbs->context_depth - 1].type == HBSCT_LIST_SEARCH)
    {
        playdate->system->addMenuItem(T(pdmenu_search), CB_HomebrewHubScene_didSelectSearch, hbs);
    }
#endif
}

CB_HomebrewHubScene* CB_HomebrewHubScene_new(float initial_header_p, const char* header_name)
{
    CB_Scene* scene = CB_Scene_new();
    scene->id = "homebrew-hub";
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
