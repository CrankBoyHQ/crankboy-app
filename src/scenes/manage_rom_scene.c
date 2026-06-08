#include "manage_rom_scene.h"

#include "../../libs/pdll/pdll.h"
#include "../app.h"
#include "../gbz.h"
#include "../preferences.h"
#include "../utility.h"
#include "modal.h"
#include "settings_scene.h"

#include <math.h>
#include <string.h>

#define HEADER_ANIMATION_RATE 2.8f
#define HEADER_HEIGHT 18
#define INFO_LEFT_X 14
#define INFO_VALUE_X 105
#define INFO_TOP_Y 9
#define INFO_ROW_H 21
#define ACTION_TOP_Y 141
#define ACTION_ROW_H 22
#define ACTION_WIDTH 200

static const uint8_t kDisabledDither[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                            0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};

static const struct
{
    uint8_t v;
    const char* name;
} kMapperNames[] = {
    {0x00, "ROM"},           {0x01, "MBC1"},         {0x02, "MBC1+RAM"},
    {0x03, "MBC1+SRAM"},     {0x05, "MBC2"},         {0x06, "MBC2+SRAM"},
    {0x08, "ROM+RAM"},       {0x09, "ROM+SRAM"},     {0x0B, "MMM01"},
    {0x0C, "MMM01+RAM"},     {0x0D, "MMM01+SRAM"},   {0x0F, "MBC3+RTC"},
    {0x10, "MBC3+RTC+SRAM"}, {0x11, "MBC3"},         {0x12, "MBC3+RAM"},
    {0x13, "MBC3+SRAM"},     {0x19, "MBC5"},         {0x1A, "MBC5+RAM"},
    {0x1B, "MBC5+SRAM"},     {0x1C, "MBC5+Vib"},     {0x1D, "MBC5+Vib+RAM"},
    {0x1E, "MBC5+Vib+SRAM"}, {0x20, "MBC6"},         {0x22, "MBC7+Acc+Vib+SRAM"},
    {0xFC, "Pocket Camera"}, {0xFD, "Bandai TAMA5"}, {0xFE, "HuC3"},
    {0xFF, "HuC1+SRAM"},
};

static const char* mapper_name_for(uint8_t v)
{
    for (size_t i = 0; i < sizeof(kMapperNames) / sizeof(kMapperNames[0]); ++i)
    {
        if (kMapperNames[i].v == v)
            return kMapperNames[i].name;
    }
    return NULL;
}

static bool ends_with_icase(const char* s, const char* suffix)
{
    size_t ls = strlen(s);
    size_t lf = strlen(suffix);
    if (lf > ls)
        return false;
    for (size_t i = 0; i < lf; ++i)
    {
        char a = s[ls - lf + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static void read_rom_header(CB_ManageRomScene* self)
{
    self->mapper_byte = 0;
    self->cgb_flag = 0;
    self->header_ok = false;
    self->compressed = ends_with_icase(self->game->fullpath, ".gbz");

    if (self->compressed)
    {
        size_t sz = 0;
        char* data = cb_read_partial_file(
            self->game->fullpath, GBZ_GZ_OFFSET, &sz, kFileRead | kFileReadData, false
        );
        if (data)
        {
            GBZ_Header h;
            if (sz >= GBZ_GZ_OFFSET && gbz_parse_header(&h, (const uint8_t*)data, sz))
            {
                self->cgb_flag = gbz_read_header_byte(&h, 0x143);
                self->mapper_byte = gbz_read_header_byte(&h, 0x147);
                self->header_ok = true;
            }
            cb_free(data);
        }
    }
    else
    {
        SDFile* f = playdate->file->open(self->game->fullpath, kFileRead | kFileReadData);
        if (f)
        {
            uint8_t buf[5] = {0};
            if (playdate->file->seek(f, 0x143, SEEK_SET) >= 0 &&
                playdate->file->read(f, buf, sizeof(buf)) == (int)sizeof(buf))
            {
                self->cgb_flag = buf[0];
                self->mapper_byte = buf[4];
                self->header_ok = true;
            }
            playdate->file->close(f);
        }
    }
}

typedef const char* (*ce_get_rom_info_fn)(const uint8_t* rom, size_t size);
typedef const char* (*ce_get_rom_header_name_fn)(const uint8_t* rom, size_t size);

static void fetch_core_rom_info(CB_ManageRomScene* self)
{
    self->core_info_text = NULL;
    self->core_header_name = NULL;
    if (!self->game || !self->game->names || !self->game->names->system_slug)
        return;
    const char* slug = self->game->names->system_slug;
    if (strcmp(slug, GB_SYSTEM_SLUG) == 0)
        return;
    emucore_t* core = CB_get_emucore_by_slug(slug);
    if (!core || !core->path)
        return;

    size_t rom_size = 0;
    char* rom = cb_read_entire_file_maybe_compressed(
        self->game->fullpath, &rom_size, kFileRead | kFileReadData
    );
    if (!rom || rom_size == 0)
    {
        if (rom)
            cb_free(rom);
        return;
    }

    pdll_t* pdll = core->pdll;
    bool opened_here = false;
    if (!pdll)
    {
        pdll = pdll_open(playdate, core->path, PDLL_FILE_PDX | PDLL_FILE_DATA, 2);
        opened_here = (pdll != NULL);
    }
    if (pdll)
    {
        ce_get_rom_info_fn get_info = (ce_get_rom_info_fn)pdll_symbol(pdll, "get_rom_info");
        if (get_info)
        {
            const char* s = get_info((const uint8_t*)rom, rom_size);
            if (s)
                self->core_info_text = cb_strdup(s);
        }
        ce_get_rom_header_name_fn get_header =
            (ce_get_rom_header_name_fn)pdll_symbol(pdll, "get_rom_header_name");
        if (get_header)
        {
            const char* s = get_header((const uint8_t*)rom, rom_size);
            if (s)
                self->core_header_name = cb_strdup(s);
        }
        if (opened_here)
        {
            pdll_close(pdll);
        }
    }

    cb_free(rom);
}

static void draw_info_row(int y, const char* label, const char* value)
{
    playdate->graphics->drawText(label, strlen(label), kUTF8Encoding, INFO_LEFT_X, y);
    if (value && *value)
    {
        playdate->graphics->drawText(value, strlen(value), kUTF8Encoding, INFO_VALUE_X, y);
    }
    else
    {
        playdate->graphics->drawText("N/A", strlen("N/A"), kUTF8Encoding, INFO_VALUE_X, y);
    }
}

static void cb_delete_rom_confirmed(void* ud, int option)
{
    if (option != 1)
        return;
    CB_ManageRomScene* self = ud;
    if (!self || !self->game || !self->game->fullpath)
        return;

    playdate->file->unlink(self->game->fullpath, 0);

    CB_Game* game = self->game;
    self->game = NULL;

    if (!CB_LibraryScene_removeGame(game))
    {
        playdate->system->restartGame(playdate->system->getLaunchArgs(NULL));
        return;
    }

    if (self->scene->parentScene && self->scene->parentScene->id &&
        strcmp(self->scene->parentScene->id, "settings") == 0)
    {
        CB_SettingsScene* parent = self->scene->parentScene->managedObject;
        if (parent)
            parent->shouldDismiss = true;
    }

    self->dismiss = true;
}

struct script_unlink_ud
{
    const char* dir;
    const char* prefix;
    size_t prefix_len;
};

static void script_unlink_cb(const char* filename, void* vd)
{
    struct script_unlink_ud* ud = vd;
    size_t flen = strlen(filename);
    if (flen <= ud->prefix_len)
        return;
    if (strncmp(filename, ud->prefix, ud->prefix_len) != 0)
        return;
    if (flen < 4 || strcmp(filename + flen - 4, ".bin") != 0)
        return;
    char* full = aprintf("%s/%s", ud->dir, filename);
    if (full)
    {
        playdate->file->unlink(full, 0);
        cb_free(full);
    }
}

static void clear_save_confirmed(void* ud, int option)
{
    if (option != 1)
        return;
    CB_ManageRomScene* self = ud;
    if (!self || !self->game || !self->game->fullpath)
        return;

    int saved_slot = preferences_save_slot;
    preferences_save_slot = self->save_slot_at_open;

    char* sav = cb_save_filename(self->game->fullpath, false);
    if (sav)
    {
        playdate->file->unlink(sav, 0);
        cb_free(sav);
    }

    preferences_save_slot = saved_slot;

    char* base_no_ext = cb_basename(self->game->fullpath, true);
    if (base_no_ext)
    {
        char* prefix = NULL;
        if (self->save_slot_at_open == 0)
        {
            prefix = aprintf("%s.script.", base_no_ext);
        }
        else
        {
            prefix = aprintf("%s.%c.script.", base_no_ext, 'A' + self->save_slot_at_open);
        }
        if (prefix)
        {
            const char* saves_dir = cb_gb_directory_path(CB_savesPath);
            if (saves_dir)
            {
                struct script_unlink_ud cb_ud = {
                    .dir = saves_dir,
                    .prefix = prefix,
                    .prefix_len = strlen(prefix),
                };
                playdate->file->listfiles(saves_dir, script_unlink_cb, &cb_ud, 0);
            }
            cb_free(prefix);
        }
        cb_free(base_no_ext);
    }
}

static void delete_cover_confirmed(void* ud, int option)
{
    if (option != 1)
        return;
    CB_ManageRomScene* self = ud;
    if (!self || !self->game || !self->game->coverPath)
        return;

    CB_Game* game = self->game;
    playdate->file->unlink(game->coverPath, 0);
    cb_free(game->coverPath);
    game->coverPath = NULL;

    if (CB_App->coverCache)
    {
        for (int i = CB_App->coverCache->length - 1; i >= 0; i--)
        {
            CB_CoverCacheEntry* entry = CB_App->coverCache->items[i];
            if (strcmp(entry->rom_path, game->fullpath) == 0)
            {
                array_remove_at(CB_App->coverCache, i);
                cb_free(entry->rom_path);
                cb_free(entry->compressed_data);
                cb_free(entry);
                break;
            }
        }
    }
    cb_clear_global_cover_cache();
}

static const char* yes_no_options[] = {"No", "Yes", NULL};

static void invoke_action(CB_ManageRomScene* self, int idx)
{
    cb_play_ui_sound(CB_UISound_Confirm);
    char* msg = NULL;
    CB_ModalCallback cb = NULL;

    if (idx == 0)
    {
        msg = aprintf("Delete this ROM?\n%s", self->basename ? self->basename : "");
        cb = cb_delete_rom_confirmed;
    }
    else if (idx == 1)
    {
        int sidx = self->save_slot_at_open;
        if (sidx < 0)
            sidx = 0;
        if (sidx >= (int)(sizeof(save_slot_labels) / sizeof(save_slot_labels[0])))
            sidx = (int)(sizeof(save_slot_labels) / sizeof(save_slot_labels[0])) - 1;
        const char* slot_label = save_slot_labels[sidx];
        msg = aprintf("Confirm delete\nsave data for %s?", slot_label);
        cb = clear_save_confirmed;
    }
    else if (idx == 2)
    {
        if (!self->game->coverPath)
            return;
        msg = aprintf("Delete this cover art?");
        cb = delete_cover_confirmed;
    }

    if (!msg || !cb)
    {
        if (msg)
            cb_free(msg);
        return;
    }

    CB_Modal* modal = CB_Modal_new(msg, yes_no_options, cb, self);
    cb_free(msg);
    if (modal)
    {
        modal->width = 280;
        modal->height = 140;
        CB_presentModal(modal->scene);
    }
}

static void draw_action_row(int y, const char* label, bool selected, bool disabled)
{
    int x = (LCD_COLUMNS - ACTION_WIDTH) / 2;
    if (selected)
    {
        playdate->graphics->fillRect(x, y, ACTION_WIDTH, ACTION_ROW_H, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeFillWhite);
    }
    else
    {
        playdate->graphics->drawRect(x, y, ACTION_WIDTH, ACTION_ROW_H, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeCopy);
    }
    int tw =
        playdate->graphics->getTextWidth(CB_App->bodyFont, label, strlen(label), kUTF8Encoding, 0);
    int fh = playdate->graphics->getFontHeight(CB_App->bodyFont);
    int tx = x + (ACTION_WIDTH - tw) / 2;
    int ty = y + (ACTION_ROW_H - fh) / 2 + 1;
    playdate->graphics->drawText(label, strlen(label), kUTF8Encoding, tx, ty);

    if (disabled && !selected)
    {
        playdate->graphics->fillRect(x, y, ACTION_WIDTH, ACTION_ROW_H, (LCDColor)kDisabledDither);
    }

    playdate->graphics->setDrawMode(kDrawModeCopy);
}

static void CB_ManageRomScene_update(void* object, uint32_t u32enc_dt)
{
    float dt = UINT32_AS_FLOAT(u32enc_dt);
    CB_ManageRomScene* self = object;

    if (CB_App->pendingScene)
        return;

    if (self->is_dismissing)
    {
        TOWARD(self->header_animation_p, 0.0f, dt * HEADER_ANIMATION_RATE);
        if (self->header_animation_p == 0.0f)
        {
            CB_dismiss(self->scene);
            return;
        }
    }
    else if (self->dismiss)
    {
        CB_dismiss(self->scene);
        return;
    }
    else
    {
        TOWARD(self->header_animation_p, 1.0f, dt * HEADER_ANIMATION_RATE);
    }

    PDButtons pushed = CB_App->buttons_pressed;
    if (pushed & kButtonB)
    {
        cb_play_ui_sound(CB_UISound_Navigate);
        if (self->started_without_header)
            self->is_dismissing = true;
        else
            self->dismiss = true;
        return;
    }
    if (pushed & kButtonUp)
    {
        if (self->cursorIndex > 0)
        {
            self->cursorIndex--;
            cb_play_ui_sound(CB_UISound_Navigate);
        }
    }
    if (pushed & kButtonDown)
    {
        int maxIndex = self->game->coverPath ? 2 : 1;
        if (self->cursorIndex < maxIndex)
        {
            self->cursorIndex++;
            cb_play_ui_sound(CB_UISound_Navigate);
        }
    }
    if (pushed & kButtonA)
    {
        invoke_action(self, self->cursorIndex);
        return;
    }

    // compute filename scroll offset
    float filename_scroll_offset = 0.0f;
    if (self->basename)
    {
        playdate->graphics->setFont(CB_App->bodyFont);
        int text_width = playdate->graphics->getTextWidth(
            CB_App->bodyFont, self->basename, strlen(self->basename), kUTF8Encoding, 0
        );
        int available = LCD_COLUMNS - INFO_VALUE_X;
        if (text_width > available)
        {
            self->filename_scroll_time += dt;
            float maxOffset = text_width - available + 5;
            float pauseAtStart = 0.7f;
            float pauseAtEnd = 1.5f;
            float scrollDuration = CB_MAX(maxOffset / 50.0f, 0.75f);
            float scrollBackDuration = CB_MAX(scrollDuration * (2.0f / 3.0f), 0.75f);
            float totalCycle = pauseAtStart + scrollDuration + pauseAtEnd + scrollBackDuration;
            float t = fmodf(self->filename_scroll_time, totalCycle);

            if (t < pauseAtStart)
                filename_scroll_offset = 0.0f;
            else if (t < pauseAtStart + scrollDuration)
                filename_scroll_offset =
                    cb_easeInOutQuad((t - pauseAtStart) / scrollDuration) * maxOffset;
            else if (t < pauseAtStart + scrollDuration + pauseAtEnd)
                filename_scroll_offset = maxOffset;
            else
                filename_scroll_offset =
                    maxOffset -
                    cb_easeInOutQuad(
                        (t - (pauseAtStart + scrollDuration + pauseAtEnd)) / scrollBackDuration
                    ) * maxOffset;
        }
    }

    // ----- draw -----
    int header_y = self->header_animation_p * HEADER_HEIGHT + 0.5f;
    playdate->graphics->clear(kColorWhite);

    // info rows
    playdate->graphics->setFont(CB_App->bodyFont);
    int y = INFO_TOP_Y + header_y;

    // filename with scroll if needed
    playdate->graphics->drawText("Filename:", 9, kUTF8Encoding, INFO_LEFT_X, y);
    if (self->basename)
    {
        playdate->graphics->setFont(CB_App->bodyFont);
        int text_width = playdate->graphics->getTextWidth(
            CB_App->bodyFont, self->basename, strlen(self->basename), kUTF8Encoding, 0
        );
        int available = LCD_COLUMNS - INFO_VALUE_X;
        if (text_width > available)
        {
            playdate->graphics->setClipRect(INFO_VALUE_X, y, available, INFO_ROW_H);
            playdate->graphics->drawText(
                self->basename, strlen(self->basename), kUTF8Encoding,
                INFO_VALUE_X - (int)filename_scroll_offset, y
            );
            playdate->graphics->clearClipRect();
        }
        else
        {
            playdate->graphics->drawText(
                self->basename, strlen(self->basename), kUTF8Encoding, INFO_VALUE_X, y
            );
        }
    }
    else
    {
        playdate->graphics->drawText("N/A", 3, kUTF8Encoding, INFO_VALUE_X, y);
    }
    y += INFO_ROW_H;

    const bool is_gb = self->game->names && self->game->names->system_slug &&
                       strcmp(self->game->names->system_slug, GB_SYSTEM_SLUG) == 0;

    draw_info_row(y, "Format:", self->compressed ? "compressed" : "uncompressed");
    y += INFO_ROW_H;

    {
        // Header: GB pulls it from the cart header byte, emucore from the
        // core's get_rom_header_name() callback.
        const char* hdr = NULL;
        if (is_gb)
        {
            hdr = (self->game->names && self->game->names->name_header)
                      ? self->game->names->name_header
                      : NULL;
        }
        else
        {
            hdr =
                (self->core_header_name && *self->core_header_name) ? self->core_header_name : NULL;
        }
        draw_info_row(y, "Header:", hdr);
        y += INFO_ROW_H;
    }

    if (is_gb)
    {
        char mapper_buf[64];
        const char* mname = self->header_ok ? mapper_name_for(self->mapper_byte) : NULL;
        if (mname)
            snprintf(mapper_buf, sizeof(mapper_buf), "0x%02X (%s)", self->mapper_byte, mname);
        else if (self->header_ok)
            snprintf(mapper_buf, sizeof(mapper_buf), "0x%02X", self->mapper_byte);
        else
            snprintf(mapper_buf, sizeof(mapper_buf), "?");
        draw_info_row(y, "Mapper:", mapper_buf);
        y += INFO_ROW_H;
    }
    else if (self->core_info_text)
    {
        // Emucore
        const char* p = self->core_info_text;
        const int max_rows = (ACTION_TOP_Y - INFO_TOP_Y) / INFO_ROW_H - 1;  // leave room for CRC32
        int rows_drawn = (y - INFO_TOP_Y) / INFO_ROW_H;
        while (*p && rows_drawn < max_rows)
        {
            const char* nl = strchr(p, '\n');
            const char* line_end = nl ? nl : p + strlen(p);
            const char* tab = memchr(p, '\t', (size_t)(line_end - p));
            char label[64] = {0}, value[128] = {0};
            if (tab)
            {
                size_t llen = (size_t)(tab - p);
                if (llen >= sizeof(label))
                    llen = sizeof(label) - 1;
                memcpy(label, p, llen);
                label[llen] = '\0';
                size_t vlen = (size_t)(line_end - (tab + 1));
                if (vlen >= sizeof(value))
                    vlen = sizeof(value) - 1;
                memcpy(value, tab + 1, vlen);
                value[vlen] = '\0';
            }
            else
            {
                size_t llen = (size_t)(line_end - p);
                if (llen >= sizeof(label))
                    llen = sizeof(label) - 1;
                memcpy(label, p, llen);
                label[llen] = '\0';
            }
            draw_info_row(y, label, *value ? value : NULL);
            y += INFO_ROW_H;
            ++rows_drawn;
            if (!nl)
                break;
            p = nl + 1;
        }
    }

    {
        char crc_buf[16];
        if (self->game->names && self->game->names->crc32 != 0)
            snprintf(crc_buf, sizeof(crc_buf), "%08lX", (unsigned long)self->game->names->crc32);
        else
            snprintf(crc_buf, sizeof(crc_buf), "—");
        draw_info_row(y, "CRC32:", crc_buf);
        y += INFO_ROW_H;
    }

    if (is_gb)
    {
        const char* sys_str = "DMG";
        if (self->header_ok)
        {
            if (self->cgb_flag == 0x80)
                sys_str = "DMG / CGB (optional)";
            else if (self->cgb_flag == 0xC0)
                sys_str = "CGB";
        }
        draw_info_row(y, "System:", sys_str);
        y += INFO_ROW_H;
    }

    // action rows
    static const char* action_labels[] = {
        "Delete ROM",
        "Clear save data",
        "Delete cover art",
    };
    for (int i = 0; i < self->actionCount; ++i)
    {
        int ay = ACTION_TOP_Y + header_y + i * (ACTION_ROW_H + 2);
        bool disabled = (i == 2 && !self->game->coverPath);
        draw_action_row(ay, action_labels[i], i == self->cursorIndex, disabled);
    }

    if (header_y > 0)
    {
        LCDFont* font = CB_App->labelFont;
        const char* name = self->header_name;
        playdate->graphics->setFont(font);
        int nameWidth =
            playdate->graphics->getTextWidth(font, name, strlen(name), kUTF8Encoding, 0);
        int textX = LCD_COLUMNS / 2 - nameWidth / 2;
        int fontHeight = playdate->graphics->getFontHeight(font);
        int vertical_offset = string_has_descenders(name) ? 1 : 2;
        int textY = ((header_y - fontHeight) / 2) + vertical_offset;

        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, header_y, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeFillWhite);
        playdate->graphics->drawText(name, strlen(name), kUTF8Encoding, textX, textY);
        playdate->graphics->setDrawMode(kDrawModeFillBlack);
    }
}

static void CB_ManageRomScene_free(void* object)
{
    CB_ManageRomScene* self = object;
    if (!self)
        return;
    if (self->basename)
        cb_free(self->basename);
    if (self->core_info_text)
        cb_free(self->core_info_text);
    if (self->core_header_name)
        cb_free(self->core_header_name);
    CB_Scene_free(self->scene);
    cb_free(self);
}

static void CB_ManageRomScene_didSelectSettings(void* userdata)
{
    CB_ManageRomScene* self = userdata;
    if (self->started_without_header)
        self->is_dismissing = true;
    else
        self->dismiss = true;
}

static void CB_ManageRomScene_didSelectLibrary(void* userdata)
{
    CB_ManageRomScene* self = userdata;
    if (self->scene->parentScene && self->scene->parentScene->id &&
        strcmp(self->scene->parentScene->id, "settings") == 0)
    {
        CB_SettingsScene* parent = self->scene->parentScene->managedObject;
        if (parent)
            parent->shouldDismiss = true;
    }
    if (self->started_without_header)
        self->is_dismissing = true;
    else
        self->dismiss = true;
}

static void CB_ManageRomScene_menu(void* object)
{
    CB_ManageRomScene* self = object;
    playdate->system->removeAllMenuItems();
    playdate->system->addMenuItem("library", CB_ManageRomScene_didSelectLibrary, self);
    playdate->system->addMenuItem("settings", CB_ManageRomScene_didSelectSettings, self);
}

CB_ManageRomScene* CB_ManageRomScene_new(CB_Game* game, float initial_header_p)
{
    if (!game)
        return NULL;

    CB_ManageRomScene* self = cb_malloc(sizeof(CB_ManageRomScene));
    if (!self)
        return NULL;
    memset(self, 0, sizeof(*self));

    self->game = game;
    self->header_animation_p = initial_header_p;
    self->started_without_header = (initial_header_p < 1.0f);
    self->is_dismissing = false;
    strncpy(self->header_name, "Manage ROM", sizeof(self->header_name) - 1);
    self->header_name[sizeof(self->header_name) - 1] = '\0';
    self->cursorIndex = 0;
    self->actionCount = 3;
    self->save_slot_at_open = preferences_save_slot;
    self->basename = cb_basename(game->fullpath, false);

    if (game->names && game->names->system_slug &&
        strcmp(game->names->system_slug, GB_SYSTEM_SLUG) == 0)
    {
        read_rom_header(self);
    }
    else
    {
        fetch_core_rom_info(self);
    }

    CB_Scene* scene = CB_Scene_new();
    if (!scene)
    {
        if (self->basename)
            cb_free(self->basename);
        cb_free(self);
        return NULL;
    }
    scene->id = "manage_rom";
    scene->managedObject = self;
    scene->update = CB_ManageRomScene_update;
    scene->free = CB_ManageRomScene_free;
    scene->menu = (void*)CB_ManageRomScene_menu;
    self->scene = scene;

    return self;
}
