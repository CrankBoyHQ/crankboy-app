//
//  library_scene.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 15/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "library_scene.h"

#include "../../libs/lz4/lz4.h"
#include "../app.h"
#include "../http.h"
#include "../http_safe.h"
#include "../preferences.h"
#include "../revcheck.h"  // IWYU pragma: keep
#include "../scenes/modal.h"
#include "../script.h"
#include "../softpatch.h"
#include "../userstack.h"
#include "../utility.h"
#include "../version.h"
#include "credits_scene.h"
#include "emucore_game_scene.h"
#include "game_scene.h"
#include "homebrew_hub_scene.h"
#include "info_scene.h"
#include "settings_scene.h"

#include <string.h>

static void CB_LibraryScene_update(void* object, uint32_t u32enc_dt);
static void CB_LibraryScene_free(void* object);
static void CB_LibraryScene_reloadList(CB_LibraryScene* libraryScene);
static void CB_LibraryScene_menu(void* object);
static void CB_LibraryScene_draw(CB_LibraryScene* libraryScene, bool forAnimation);

static void collect_cover_filenames_callback(const char* filename, void* userdata)
{
    if (endswithi(filename, ".pdi"))
    {
        CB_Array* covers_array = userdata;
        char* basename_no_ext = cb_basename(filename, true);
        if (basename_no_ext)
        {
            array_push(covers_array, basename_no_ext);
        }
    }
}

static void CB_cover_compress_impl(
    CB_Game* game, void* lz4_state, size_t* io_cache_bytes, int* io_cached_count
)
{
    if (!game->coverPath || game->cover_compressed_data)
        return;

    const char* error = NULL;
    LCDBitmap* coverBitmap = playdate->graphics->loadBitmap(game->coverPath, &error);

    if (!coverBitmap)
        return;

    int width, height, rowbytes;
    uint8_t *mask_data, *pixel_data;
    playdate->graphics->getBitmapData(
        coverBitmap, &width, &height, &rowbytes, &mask_data, &pixel_data
    );

    bool has_mask = (mask_data != NULL);
    size_t original_size = rowbytes * height;
    if (has_mask)
        original_size *= 2;

    int max_dst_size = LZ4_compressBound(original_size);
    char* temp_compressed_buffer = cb_malloc(max_dst_size);

    uint8_t* uncompressed_buffer = cb_malloc(original_size);
    memcpy(uncompressed_buffer, pixel_data, rowbytes * height);
    if (has_mask)
    {
        memcpy(uncompressed_buffer + (rowbytes * height), mask_data, rowbytes * height);
    }

    int compressed_size = LZ4_compress_fast_extState(
        lz4_state, (const char*)uncompressed_buffer, temp_compressed_buffer, original_size,
        max_dst_size, 1
    );

    cb_free(uncompressed_buffer);

    if (compressed_size > 0)
    {
        char* final_buffer = cb_malloc(compressed_size);
        memcpy(final_buffer, temp_compressed_buffer, compressed_size);

        game->cover_compressed_data = final_buffer;
        game->cover_compressed_size = compressed_size;
        game->cover_width = width;
        game->cover_height = height;
        game->cover_rowbytes = rowbytes;
        game->cover_has_mask = has_mask;

        if (io_cache_bytes)
            *io_cache_bytes += compressed_size;
        if (io_cached_count)
            *io_cached_count += 1;
    }
    cb_free(temp_compressed_buffer);

    playdate->graphics->freeBitmap(coverBitmap);
}

void CB_cover_compress(CB_Game* game, void* lz4_state, size_t* io_cache_bytes, int* io_cached_count)
{
    CB_cover_compress_impl(game, lz4_state, io_cache_bytes, io_cached_count);
}

void CB_cover_free_compressed(CB_Game* game)
{
    if (game->cover_compressed_data)
    {
        cb_free(game->cover_compressed_data);
        game->cover_compressed_data = NULL;
    }
}

static bool cover_evict_lru(CB_LibraryScene* lib)
{
    uint32_t smallest = UINT32_MAX;
    CB_Game* to_evict = NULL;

    for (int i = 0; i < lib->games->length; i++)
    {
        CB_Game* g = lib->games->items[i];
        if (g->cover_compressed_data && g->cover_access_counter > 0 &&
            g->cover_access_counter < smallest)
        {
            smallest = g->cover_access_counter;
            to_evict = g;
        }
    }

    if (to_evict)
    {
        if (lib->cover_cache_bytes >= (size_t)to_evict->cover_compressed_size)
            lib->cover_cache_bytes -= to_evict->cover_compressed_size;
        lib->cover_cached_count--;
        CB_cover_free_compressed(to_evict);
        return true;
    }
    return false;
}

static void cover_background_fill(CB_LibraryScene* lib)
{
    if (lib->cover_cached_count >= MAX_COVER_COUNT)
    {
        while (lib->cover_cached_count >= MAX_COVER_COUNT)
        {
            if (!cover_evict_lru(lib))
                break;
        }
    }

    if (lib->cover_cached_count >= MAX_COVER_COUNT)
        return;

    if (lib->bg_fill_center < 0)
    {
        lib->bg_fill_center = lib->listView->selectedItem;
        if (lib->bg_fill_center < 0 || lib->bg_fill_center >= lib->games->length)
            lib->bg_fill_center = 0;
        lib->bg_fill_dist = 0;
        lib->bg_fill_dir = 0;
    }

    int total = lib->games->length;
    int found = 0;

    for (int tries = 0; tries < total * 2 && found < BG_FILL_BATCH_SIZE; tries++)
    {
        int idx = (lib->bg_fill_dir == 0) ? lib->bg_fill_center + lib->bg_fill_dist
                                          : lib->bg_fill_center - lib->bg_fill_dist;

        lib->bg_fill_dir = !lib->bg_fill_dir;
        if (lib->bg_fill_dir == 0)
            lib->bg_fill_dist++;

        if (idx < 0 || idx >= total)
            continue;

        CB_Game* g = lib->games->items[idx];
        if (g->coverPath && !g->cover_compressed_data)
        {
            CB_cover_compress_impl(
                g, lib->lz4_state, &lib->cover_cache_bytes, &lib->cover_cached_count
            );
            found++;
        }
    }
}
static int last_selected_game_index = 0;
static bool library_was_initialized_once = false;
static int last_panel_seam = LCD_COLUMNS / 2;
static CB_LibraryScene* s_active_library_scene = NULL;

// Animation state for the "Downloading cover..." text
static float coverDownloadAnimationTimer = 0.0f;
static int coverDownloadAnimationStep = 0;

typedef struct
{
    CB_LibraryScene* libraryScene;
    CB_Game* game;
} CoverDownloadUserdata;

static void save_last_selected_index(const char* rompath)
{
    cb_write_entire_file(LAST_SELECTED_FILE, rompath, strlen(rompath));
    return;
}

static intptr_t load_last_selected_index(CB_Array* games)
{
    char* content = cb_read_entire_file(LAST_SELECTED_FILE, NULL, kFileReadData);

    // default -- top of list
    if (!content)
    {
        return 0;
    }

    intptr_t found_index = 0;

    // First, try searching for a ROM whose path matches the given name
    for (int i = 0; i < games->length; ++i)
    {
        CB_Game* game = games->items[i];
        if (!strcmp(game->fullpath, content))
        {
            found_index = i;
            goto cleanup;
        }
    }

    // Failing that, convert the value to an integer.
    int index_from_file = atoi(content);
    if (index_from_file >= 0 && index_from_file < games->length)
    {
        found_index = index_from_file;
    }

cleanup:
    cb_free(content);
    return found_index;
}

static unsigned combined_display_mode(void)
{
    return preferences_display_name_mode | (preferences_display_article << 3) |
           (preferences_display_sort << 6);
}

static void set_download_status(
    CB_LibraryScene* self, CoverDownloadState state, const char* message
)
{
    self->coverDownloadState = state;
    if (self->coverDownloadMessage)
    {
        cb_free(self->coverDownloadMessage);
    }
    self->coverDownloadMessage = message ? cb_strdup(message) : NULL;
    self->scene->forceFullRefresh = true;
}

static void on_cover_download_finished(unsigned flags, char* data, size_t data_len, void* ud)
{
    CoverDownloadUserdata* userdata = ud;
    CB_LibraryScene* libraryScene = userdata->libraryScene;
    CB_Game* game = userdata->game;

    int currentSelectedIndex = libraryScene->listView->selectedItem;
    CB_Game* currentlySelectedGame = NULL;
    if (currentSelectedIndex >= 0 && currentSelectedIndex < libraryScene->games->length)
    {
        currentlySelectedGame = libraryScene->games->items[currentSelectedIndex];
    }

    bool stillOnSameGame = (currentlySelectedGame == game);
    char* rom_basename_no_ext = NULL;
    char* cover_dest_path = NULL;

    // The game may have been deleted while the download was in flight
    // (CB_LibraryScene_removeGame / CB_Game_free). Bail before ever
    // dereferencing it.
    bool game_still_exists = false;
    for (int i = 0; i < libraryScene->games->length; ++i)
    {
        if (libraryScene->games->items[i] == game)
        {
            game_still_exists = true;
            break;
        }
    }
    if (!game_still_exists)
        goto cleanup;

    if (flags & HTTP_WIFI_NOT_AVAILABLE)
    {
        if (stillOnSameGame)
        {
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Wi-Fi not available.");
        }
        goto cleanup;
    }

    if (flags & HTTP_NOT_FOUND)
    {
        if (stillOnSameGame)
        {
            set_download_status(libraryScene, COVER_DOWNLOAD_NO_GAME_IN_DB, "No cover found.");
        }
        goto cleanup;
    }
    else if ((flags & ~HTTP_ENABLE_ASKED) != 0 || data == NULL || data_len == 0)
    {
        if (stillOnSameGame)
        {
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Download failed.");
        }
        goto cleanup;
    }

    // Verify valid PDI header
    const char* pdi_header = "Playdate IMG";
    char* actual_data_start = strstr(data, pdi_header);

    if (actual_data_start == NULL)
    {
        if (stillOnSameGame)
        {
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Invalid file received.");
        }
        goto cleanup;
    }

    // Calculate length from the found header to the end of the buffer
    size_t new_data_len = data_len - (actual_data_start - data);

    rom_basename_no_ext = cb_basename(game->names->filename, true);
    if (!rom_basename_no_ext)
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        goto cleanup;
    }

    playdate->system->formatString(
        &cover_dest_path, "%s/%s.pdi", cb_gb_directory_path(CB_coversPath), rom_basename_no_ext
    );

    if (!cover_dest_path)
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        goto cleanup;
    }

    // Safety: Ensure previous file is gone before writing
    playdate->file->unlink(cover_dest_path, 0);

    if (cb_write_entire_file(cover_dest_path, actual_data_start, new_data_len))
    {
        if (game->coverPath)
        {
            cb_free(game->coverPath);
        }
        game->coverPath = cb_strdup(cover_dest_path);

        if (game->cover_compressed_data)
        {
            if (libraryScene->cover_cache_bytes >= (size_t)game->cover_compressed_size)
                libraryScene->cover_cache_bytes -= game->cover_compressed_size;
            libraryScene->cover_cached_count--;
        }
        CB_cover_free_compressed(game);
        CB_cover_compress(
            game, libraryScene->lz4_state, &libraryScene->cover_cache_bytes,
            &libraryScene->cover_cached_count
        );

        if (stillOnSameGame)
        {
            cb_clear_global_cover_cache();

            CB_App->coverArtCache.art = cb_load_and_scale_cover_art_from_path(
                game->coverPath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT
            );
            CB_App->coverArtCache.rom_path = cb_strdup(game->fullpath);

            set_download_status(libraryScene, COVER_DOWNLOAD_IDLE, NULL);
            CB_ListView_reload(libraryScene->listView);
        }
    }
    else
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Failed to save cover.");
    }

cleanup:
    if (cover_dest_path)
    {
        cb_free(cover_dest_path);
    }
    if (rom_basename_no_ext)
    {
        cb_free(rom_basename_no_ext);
    }

    if (data)
    {
        cb_free(data);
    }

    cb_free(userdata);
}

static void CB_LibraryScene_startCoverDownload(CB_LibraryScene* libraryScene)
{
    int selectedIndex = libraryScene->listView->selectedItem;
    if (selectedIndex < 0 || selectedIndex >= libraryScene->games->length)
    {
        return;
    }

    CB_Game* game = libraryScene->games->items[selectedIndex];

    set_download_status(libraryScene, COVER_DOWNLOAD_SEARCHING, "Searching for missing Cover...");

    if (game->names->name_database == NULL)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_NO_GAME_IN_DB, "No Cover found.");
        return;
    }

    char* encoded_name = cb_url_encode_for_github_raw(game->names->name_database);
    if (!encoded_name)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        return;
    }

    char* p = encoded_name;
    char* q = encoded_name;

    while (*p)
    {
        if (*p == '&' || *p == ':')
        {
            *q++ = '_';
            p++;
        }
        else if ((unsigned char)*p == 0xC3 && (unsigned char)*(p + 1) == 0xA9)
        {
            *q++ = 'e';
            p += 2;
        }
        else
        {
            if (p != q)
            {
                *q = *p;
            }
            q++;
            p++;
        }
    }
    *q = '\0';

    char* url_path;
    playdate->system->formatString(
        &url_path, "/CrankBoyHQ/crankboy-covers/raw/refs/heads/main/Combined_Boxarts/%s.pdi",
        encoded_name
    );

    cb_free(encoded_name);

    if (!url_path)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        return;
    }

    set_download_status(libraryScene, COVER_DOWNLOAD_DOWNLOADING, "Downloading cover...");

    coverDownloadAnimationTimer = 0.0f;
    coverDownloadAnimationStep = 0;
    libraryScene->scene->forceFullRefresh = true;

    CoverDownloadUserdata* userdata = cb_malloc(sizeof(CoverDownloadUserdata));
    userdata->libraryScene = libraryScene;
    userdata->game = game;

    if (libraryScene->activeCoverDownloadConnection)
    {
        // reclaim userdata of any in-flight download; the tombstoned
        // connection will never invoke the callback.
        cb_free(http_safe_ud(libraryScene->activeCoverDownloadConnection));
        http_safe_free(libraryScene->activeCoverDownloadConnection);
    }

    libraryScene->activeCoverDownloadConnection = http_safe_new();

    http_safe_replace_get(
        libraryScene->activeCoverDownloadConnection, "github.com", url_path,
        "to download missing cover art", on_cover_download_finished, 15000, userdata
    );

    cb_free(url_path);
}

static void load_game_prefs(const char* game_path, bool onlyIfPerGameEnabled)
{
    void* stored = preferences_store_subset(-1);
    bool useGame = false;
    char* settings_path = cb_game_config_path(game_path);
    if (settings_path)
    {
        call_with_main_stack_1(preferences_merge_from_disk, settings_path);
        cb_free(settings_path);

        if (!preferences_per_game && onlyIfPerGameEnabled)
        {
            useGame = false;
        }
        else
        {
            useGame = true;
        }
    }

    if (!useGame)
    {
        preferences_restore_subset(stored);
    }
    cb_free(stored);
}

static void play_launch_animation(CB_Game* game)
{
    CB_LibraryScene* libraryScene = s_active_library_scene;
    if (!libraryScene)
        return;

    // decide gap color (black or white)
    enum ScriptPreferredLaunchColor script_color = ScriptPreferredLaunchColor_None;
    if (game && game->names && game->names->name_header)
    {
        ScriptInfo* info = get_script_info(game->names->name_header);
        if (info)
        {
            script_color = info->launch_color;
            script_info_free(info);
        }
    }

    int boot_fade = preferences_boot_fade;
    if (game)
    {
        void* stored = preferences_store_subset(-1);
        load_game_prefs(game->fullpath, false);
        boot_fade = preferences_boot_fade;
        preferences_restore_subset(stored);
    }

    bool white_gap;
    if (script_color == ScriptPreferredLaunchColor_White)
        white_gap = true;
    else if (script_color == ScriptPreferredLaunchColor_Black)
        white_gap = false;
    else
        white_gap =
            (boot_fade == PREF_FADE_NONE || boot_fade == PREF_FADE_SHORT_WHITE ||
             boot_fade == PREF_FADE_LONG_WHITE);

    const int frames = 12;
    const int sideBarMax = 40;
    bool fadeOff = (boot_fade == PREF_FADE_NONE);
    int seam = last_panel_seam;
    if (seam < 0)
        seam = 0;
    if (seam > LCD_COLUMNS)
        seam = LCD_COLUMNS;

    int leftWidth = seam;
    int rightWidth = LCD_COLUMNS - seam;

    for (int i = 1; i <= frames; ++i)
    {
        if (i == frames)
        {
            // animation end: clear to black/white
            playdate->graphics->setDrawOffset(0, 0);
            playdate->graphics->clearClipRect();
            playdate->graphics->fillRect(
                0, 0, LCD_COLUMNS, LCD_ROWS, white_gap ? kColorWhite : kColorBlack
            );
        }
        else
        {
            float t = (float)i / (float)frames;
            float p = 0.5f * t + 0.5f * t * t;
            int L = (int)(leftWidth * p + 0.5f);
            int R = (int)(rightWidth * p + 0.5f);
            if (L > leftWidth)
                L = leftWidth;
            if (R > rightWidth)
                R = rightWidth;

            libraryScene->launchAnimShiftLeft = L;
            libraryScene->launchAnimShiftRight = R;
            libraryScene->launchAnimWhiteGap = white_gap;
            libraryScene->launchAnimSideBarWidth =
                fadeOff ? (sideBarMax * i + frames / 2) / frames : 0;
            libraryScene->scene->forceFullRefresh = true;

            CB_LibraryScene_draw(libraryScene, true);
        }

        playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);
        playdate->graphics->display();

        unsigned start = playdate->system->getCurrentTimeMilliseconds();
        while (playdate->system->getCurrentTimeMilliseconds() - start < 1000 / 60)
        {
        }
    }

    libraryScene->launchAnimShiftLeft = 0;
    libraryScene->launchAnimShiftRight = 0;
    libraryScene->launchAnimSideBarWidth = 0;
    libraryScene->launchAnimWhiteGap = false;
}

// option 0: launch as dmg
// option 1: launch as cgb
static void launch_dmg_or_cgb(CB_Game* game, int option)
{
    if (option == 0 || option == 1)
    {
        if (preferences_library_launch_animation)
        {
            play_launch_animation(game);
        }

        CB_GameScene* gameScene =
            CB_GameScene_new(game->fullpath, game->names->name_short_leading_article, option == 1);
        if (gameScene)
        {
            CB_present(gameScene->scene);
        }

        playdate->system->logToConsole("Present gameScene");
    }
}

// returns true if handled
static bool maybe_launch_emucore_game(CB_Game* game)
{
    const char* slug = game->names->system_slug;
    if (!slug || strcmp(slug, GB_SYSTEM_SLUG) == 0)
        return false;

    if (!CB_get_emucore_by_slug(slug))
    {
        CB_InfoScene* info =
            CB_InfoScene_new("No Core", "No emulator core is installed for this game's system.");
        CB_presentModal(info->scene);
        return true;
    }

    CB_EmucoreGameScene* es =
        CB_EmucoreGameScene_new(game->fullpath, slug, game->names->name_short_leading_article);
    CB_present(es->scene);
    return true;
}

static void launch_game_prompt_cgb(CB_Game* game, int launch)
{
    if (launch != 1)
        return;

    // check if game would use script
    ScriptInfo* info = get_script_info(game->names->name_header);
    void* prefs = preferences_store_subset(-1);
    load_game_prefs(game->fullpath, false);
    bool will_use_script = preferences_script_support;
    preferences_restore_subset(prefs);
    playdate->system->logToConsole("Will use script: %d", (int)will_use_script);

    if (will_use_script && info && info->launch_system != ScriptPreferredLaunchSystem_None)
    {
        launch_dmg_or_cgb(game, info->launch_system == ScriptPreferredLaunchSystem_CGB);
    }
    else
    {
        const char* options[] = {"DMG", "CGB", NULL};
        const char* options_cgb_not_recommended[] = {"DMG", "CGB*", NULL};

        switch (game->names->rom_cgb_support)
        {
        default:
            playdate->system->logToConsole(
                "WARNING: unexpected game platform (0x%x); launching as DMG",
                game->names->rom_cgb_support
            );
            launch_dmg_or_cgb(game, 0);
            break;
        case GB_SUPPORT_DMG:
            if (preferences_prompt_if_cgb_optional >= 2)
            {
                CB_Modal* modal = CB_Modal_new(
                    "This ROM is marked as DMG.\n \nYou can launch in DMG mode (recommended), "
                    "or try CrankBoy's experimental CGB (\"Color\") emulation regardless.",
                    options_cgb_not_recommended, (void*)launch_dmg_or_cgb, game
                );

                modal->width = 380;
                modal->height = 220;

                CB_presentModal(modal->scene);
            }
            else
            {
                launch_dmg_or_cgb(game, 0);
            }
            break;
        case GB_SUPPORT_CGB:
        {
            CB_Modal* modal = CB_Modal_new(
                "This ROM is marked CGB-only.\n \nCrankBoy only has experimental support for CGB "
                "ROMs. "
                "You can try launching this as a standard DMG ROM, or try CGB mode (\"Color\").",
                options, (void*)launch_dmg_or_cgb, game
            );

            modal->width = 380;
            modal->height = 220;

            CB_presentModal(modal->scene);
        }
        break;
        case GB_SUPPORT_DMG_AND_CGB:
        {
            if (preferences_prompt_if_cgb_optional)
            {
                CB_Modal* modal = CB_Modal_new(
                    "This ROM supports CGB mode.\n \nYou can launch in standard, "
                    "DMG mode (recommended), or try using CrankBoy's experimental "
                    "CGB (\"Color\") emulation.",
                    options_cgb_not_recommended, (void*)launch_dmg_or_cgb, game
                );

                modal->width = 380;
                modal->height = 220;

                CB_presentModal(modal->scene);
            }
            else
            {
                launch_dmg_or_cgb(game, 0);
            }
        }
        break;
        }
    }

    script_info_free(info);
}

static void _launch_game_check_sram(CB_Game* game)
{
    if (game->names->rom_has_battery)
    {
        uint32_t hash = 0;
        SoftPatch* patches = list_patches(game->fullpath, NULL);
        if (patches)
        {
            hash = patch_hash(patches);
            free_patches(patches);
        }

        // warn if potential save hazard
        void* prefs = preferences_store_subset(~(PREFBIT_save_slot | PREFBIT_script_support));
        load_game_prefs(game->fullpath, false);
        preferences_restore_subset(prefs);

        char* save_fname = cb_save_filename(game->fullpath, false);

        const char* options[] = {"Cancel", "Launch", NULL};

        size_t size;
        char* data = call_with_main_stack_5(
            cb_read_partial_file, save_fname, 0x20, &size, kFileReadData, true
        );
        cb_free(save_fname);
        if (!data || size != 0x20)
        {
            launch_game_prompt_cgb(game, 1);
        }
        else
        {
            uint64_t magic = *(uint64_t*)(void*)&data[0x18];
            if (magic != SRAM_MAGIC_NUMBER)
            {
                launch_game_prompt_cgb(game, 1);
            }
            else
            {
                uint32_t stored_hash = *(uint32_t*)(void*)&data[0x14];
                uint32_t flags = *(uint32_t*)(void*)&data[0x10];
                bool script = flags & 1;

                if (stored_hash != hash)
                {
                    CB_Modal* modal;
                    if (!stored_hash)
                    {
                        modal = CB_Modal_new(
                            "You have softpatches enabled, but this game's save data comes from an "
                            "unpatched ROM. To keep the save data separate, you may wish to change "
                            "the save slot in settings before launching.",
                            options, (void*)launch_game_prompt_cgb, game
                        );
                    }
                    else if (!hash)
                    {
                        char* msg = aprintf(
                            "You have no softpatches on, but this game's save data comes from a "
                            "patched ROM (code: %08X.) To keep the save data separate, you may "
                            "wish to change the save slot in settings before launching.",
                            stored_hash
                        );
                        modal = CB_Modal_new(msg, options, (void*)launch_game_prompt_cgb, game);
                        cb_free(msg);
                    }
                    else
                    {
                        char* msg = aprintf(
                            "This game's save data comes from a ROM with different softpatches "
                            "applied (saved code: %08X; your patches: %08X) Consider changing the "
                            "save slot in settings to keep your save data separate.",
                            stored_hash, hash
                        );
                        modal = CB_Modal_new(msg, options, (void*)launch_game_prompt_cgb, game);
                        cb_free(msg);
                    }
                    modal->width = 390;
                    modal->height = 210;
                    modal->icon_flashing = true;
                    modal->warning = CB_MODAL_WARNING_TOP;
                    CB_presentModal(modal->scene);
                }
                // TODO: script enabled disparity
                else
                {
                    launch_game_prompt_cgb(game, 1);
                }
            }
        }
    }
    else
    {
        launch_game_prompt_cgb(game, 1);
    }
}

static void launch_game(void* ud, int option)
{
    CB_Game* game = ud;

    switch (option)
    {
    case 0:  // launch w/ scripts enabled
    case 1:  // launch w/ scripts disabled
    case 4:  // launch w/ scripts enabled (don't set prompted)
    case 5:  // launch w/ scripts disabled (don't set prompted)
    {
        char* settings_path = cb_game_config_path(game->fullpath);
        if (settings_path)
        {
            void* prefs = preferences_store_subset(-1);
            preference_t global_scripts_enabled = preferences_script_support;
            load_game_prefs(game->fullpath, false);
            preference_t was_per_game = preferences_per_game;

            // Set preferences based on option.
            preferences_script_support = (option == 0 || option == 4);
            preferences_per_game = 1;
            if (option <= 3)
            {
                preferences_script_has_prompted = 1;
            }

            if (preferences_script_support || was_per_game || global_scripts_enabled)
            {
                playdate->system->logToConsole(
                    "switching to per-game prefs (%d/%d/%d)", preferences_script_support,
                    was_per_game, global_scripts_enabled
                );
                preferences_save_to_disk(settings_path, PREFBITS_ALWAYS_GLOBAL);
            }
            else
            {
                playdate->system->logToConsole("not switching to per-game prefs");
                // if global scripts disabled, AND we aren't using per-game prefs for this game, AND
                // we didn't ask to enable script support, then just mark prompted (and don't enable
                // per-game + script support.)
                preferences_save_to_disk(settings_path, ~(PREFBIT_script_has_prompted));
            }

            preferences_restore_subset(prefs);
            if (prefs)
                cb_free(prefs);
            cb_free(settings_path);
        }
        goto launch_normal;
    }

    case 2:
        // display information
        {
            show_game_script_info(game->fullpath, game->names->name_short_leading_article);
        }
        break;

    case 3:  // launch game normally (don't alter settings)
    launch_normal:
    {
        _launch_game_check_sram(game);
    }
    break;

    default:
        // do nothing
        break;
    }
}

static void launch_game_normal(void* ud, int option)
{
    if (option >= 0)
    {
        launch_game(ud, 3);
    }
}

static void disable_script_and_launch(void* ud, int option)
{
    CB_Game* game = ud;
    switch (option)
    {
    case 0:  // launch with scripts disabled
        launch_game(game, 5);
        break;
    case 1:  // launch with scripts as-is
        launch_game(game, 3);
        break;
    default:  // cancel
        break;
    }
}

static bool crank_would_cause_input(CB_Game* game)
{
    // TODO
    void* prefs = preferences_store_subset(-1);
    load_game_prefs(game->fullpath, true);
    int crank_mode = preferences_crank_mode;
    int crank_down_action = preferences_crank_down_action;
    preferences_restore_subset(prefs);
    cb_free(prefs);

    float crank_angle = playdate->system->getCrankAngle();
    bool docked = playdate->system->isCrankDocked();

    if (docked)
    {
        return false;
    }

    if (crank_mode == CRANK_MODE_START_SELECT)
    {
        const float triggerAngle = 45.0f;
        const float deadAngle = (crank_down_action == 0) ? 45.0f : 20.0f;

        bool in_active_zone = (crank_angle >= triggerAngle && crank_angle <= 360.0f - triggerAngle);
        bool in_down_dead_zone =
            (crank_angle > 180.0f - deadAngle && crank_angle < 180.0f + deadAngle);

        if (in_active_zone)
        {
            return in_down_dead_zone ? (crank_down_action == 1) : true;
        }
    }

    return false;
}

static void launch_game_script_prompt(CB_Game* game)
{
    bool launch = true;

    void* prefs = preferences_store_subset(-1);
    preferences_script_has_prompted = 0;
    load_game_prefs(game->fullpath, false);
    int has_prompted = preferences_script_has_prompted;
    int script_enabled = preferences_script_support;
    int is_per_game = preferences_per_game;
    preferences_restore_subset(prefs);
    cb_free(prefs);

    if (!is_per_game)
        script_enabled = preferences_script_support;

    ScriptInfo* info = get_script_info(game->names->name_header);
    if (info)
    {
        const struct CScriptInfo* csi = info->c_script_info;
        bool has_game_cb = csi && (csi->on_begin || csi->on_tick || csi->on_draw || csi->on_menu ||
                                   csi->on_settings || csi->on_end);

        if (!info->experimental && !has_prompted && has_game_cb)
        {
            const char* options[] = {"Yes", "No", "About", NULL};
            if (!info->info)
                options[2] = NULL;
            CB_Modal* modal = CB_Modal_new(
                "There is native Playdate support for this game.\n"
                "Would you like to enable it?",
                options, launch_game, game
            );

            modal->width = 290;
            modal->height = 152;

            CB_presentModal(modal->scene);
            launch = false;
        }
        else if (info->experimental && script_enabled && has_game_cb)
        {
            const char* options[] = {"Yes", "No", NULL};
            CB_Modal* modal = CB_Modal_new(
                "This game's script is marked as \"experimental\", so please expect glitches or "
                "even crashes.\n \nDisable script?",
                options, disable_script_and_launch, game
            );

            modal->width = 310;
            modal->height = 224;

            CB_presentModal(modal->scene);
            launch = false;
        }
        script_info_free(info);
    }

    if (launch)
    {
        launch_game(game, 3);
    }
}

static void launch_game_after_later_info(void* ud, int option)
{
    (void)option;
    launch_game_script_prompt((CB_Game*)ud);
}

static void launch_game_recommended_cb(void* ud, int option)
{
    // 0 = Apply, 1 = Later. B-dismiss (don't launch)
    CB_Game* game = ud;
    if (option == 0)
    {
        const struct ScriptRecommendedSettings* rec =
            script_get_recommended_for_game(game->names->name_header);
        if (rec)
        {
            char* settings_path = cb_game_config_path(game->fullpath);
            if (settings_path)
            {
                script_apply_recommended_settings(rec, settings_path);
                cb_free(settings_path);
            }
        }
        launch_game_script_prompt(game);
    }
    else if (option == 1)
    {
        void* stored = preferences_store_subset(~(preferences_bitfield_t)0);
        char* settings_path = cb_game_config_path(game->fullpath);
        if (settings_path)
        {
            preferences_merge_from_disk(settings_path);
            preferences_recommended_settings_ignored = 1;
            preferences_per_game = 1;
            preferences_save_to_disk(settings_path, PREFBITS_ALWAYS_GLOBAL);
            cb_free(settings_path);
        }
        preferences_restore_subset(stored);
        cb_free(stored);

        const char* info_options[] = {"OK", NULL, NULL};
        CB_Modal* info_modal = CB_Modal_new(
            "Recommended settings can be applied "
            "from the settings menu at any time.",
            info_options, launch_game_after_later_info, game
        );
        info_modal->width = 320;
        info_modal->height = 160;
        CB_presentModal(info_modal->scene);
    }
}

static void launch_game_prompt_if_script(void* ud, int option)
{
    if (option != 0)
        return;

    CB_Game* game = ud;

    if (preferences_library_remember_selection)
    {
        call_with_user_stack_1(save_last_selected_index, game->fullpath);
    }

    // emucore games bypass the normal dmg/cgb launch flow
    if (maybe_launch_emucore_game(game))
        return;

    const struct ScriptRecommendedSettings* rec =
        script_get_recommended_for_game(game->names->name_header);

    if (rec)
    {
        void* prefs = preferences_store_subset(-1);
        preferences_recommended_settings_ignored = 0;
        load_game_prefs(game->fullpath, false);
        bool ignored = preferences_recommended_settings_ignored;
        preferences_restore_subset(prefs);
        cb_free(prefs);

        if (!ignored)
        {
            char* settings_path = cb_game_config_path(game->fullpath);
            bool optimal = settings_path && script_check_recommended_settings(rec, settings_path);
            cb_free(settings_path);

            if (!optimal)
            {
                const char* options[] = {"Apply", "Ignore", NULL};
                char* msg = rec->message;
                char default_msg[256];
                if (!msg)
                {
                    const char* name = game->names->name_short_leading_article;
                    if (!name || !name[0])
                        name = game->names->name_header;
                    snprintf(
                        default_msg, sizeof(default_msg),
                        "%s has recommended settings from the CrankBoy community.\n\n", name
                    );
                    msg = default_msg;
                }
                CB_Modal* modal = CB_Modal_new(msg, options, launch_game_recommended_cb, game);

                modal->width = 350;
                modal->height = 200;

                CB_presentModal(modal->scene);
                return;
            }
        }
    }

    launch_game_script_prompt(game);
}

#if !defined(CRANKBOY_OFFICIAL_CATALOG)
static void on_update_modal_dismiss(void* ud, int option)
{
    mark_update_as_seen();
    free_pending_update_info((PendingUpdateInfo*)ud);
}
#endif

static int page_advance = 0;

__section__(".rare") static void CB_LibraryScene_event(
    void* object, PDSystemEvent event, uint32_t arg
)
{
    CB_LibraryScene* libraryScene = object;

    switch (event)
    {
    case kEventKeyPressed:
        playdate->system->logToConsole("Key pressed: %x\n", (unsigned)arg);

        switch (arg)
        {
        case 0x64:
            // [d] page up
            page_advance = -8;
            break;
        case 0x66:
            // [f] page down
            page_advance = 8;
            break;
        }
        break;
    default:
        break;
    }
}

static bool homebrew_hub_available(void)
{
    return CB_App->hbApiDomain && CB_App->hbApiPath;
}

static void library_push_get_roms_item(CB_ListView* listView)
{
    if (!homebrew_hub_available())
        return;
    array_push(listView->items, CB_ListItemButton_new(T(Library_GetRoms)));
}

CB_LibraryScene* CB_LibraryScene_new(void)
{
    CB_ASSERT(!CB_App->bundled_rom);

    clear_last_selected_preference();

    // setup completed without crashing
    CB_set_setup_canary(false);

#if !defined(CRANKBOY_OFFICIAL_CATALOG)
    CB_App->shouldCheckUpdateInfo = GITHUB_RELEASE || CB_App->forceCheckVersion;
#else
    CB_App->shouldCheckUpdateInfo = false;
#endif

    setCrankSoundsEnabled(true);

    CB_Scene* scene = CB_Scene_new();
    scene->id = "library";

    CB_LibraryScene* libraryScene = allocz(CB_LibraryScene);

    libraryScene->state = kLibraryStateInit;
    libraryScene->build_index = 0;

    libraryScene->scene = scene;
    scene->managedObject = libraryScene;

    scene->update = CB_LibraryScene_update;
    scene->free = CB_LibraryScene_free;
    scene->menu = CB_LibraryScene_menu;
    scene->event = CB_LibraryScene_event;

    libraryScene->model = (CB_LibrarySceneModel){.empty = true, .tab = CB_LibrarySceneTabList};

    libraryScene->games = CB_App->gameListCache;
    libraryScene->listView = CB_ListView_new();

    libraryScene->listView->selectedItem = 0;
    libraryScene->tab = CB_LibrarySceneTabList;
    libraryScene->lastSelectedItem = -1;
    libraryScene->last_display_name_mode = combined_display_mode();
    libraryScene->initialLoadComplete = false;
    libraryScene->coverDownloadState = COVER_DOWNLOAD_IDLE;
    libraryScene->activeCoverDownloadConnection = NULL;
    libraryScene->isReloading = library_was_initialized_once;
    library_was_initialized_once = true;
    libraryScene->update_modal_shown = false;
    libraryScene->migration_modal_shown = false;
    libraryScene->decompression_buffer = NULL;
    libraryScene->decompression_buffer_size = 0;

    libraryScene->available_covers = array_new();
    libraryScene->build_game_index = 0;
    libraryScene->lz4_state = cb_malloc(LZ4_sizeofState());
    libraryScene->preload_cover_index = 0;
    libraryScene->preload_cover_total = 0;
    libraryScene->cover_cache_bytes = 0;
    libraryScene->cover_cached_count = 0;
    libraryScene->cover_global_access_counter = 0;
    libraryScene->last_user_input_time_ms = 0;
    libraryScene->last_selection_for_idle = 0;
    libraryScene->bg_fill_center = -1;
    libraryScene->bg_fill_dist = 0;
    libraryScene->bg_fill_dir = 0;

    if (libraryScene->isReloading)
    {
        // skip game list build and cover preload on reload
        // keep current selection
        int sel = last_selected_game_index;
        int max_sel = libraryScene->games->length - (homebrew_hub_available() ? 0 : 1);
        if (sel < 0 || sel > max_sel)
            sel = 0;
        libraryScene->listView->selectedItem = sel;
        libraryScene->build_index = 0;
        libraryScene->progress_max_width =
            cb_calculate_progress_max_width(CB_App->subheadFont, PROGRESS_STYLE_PERCENT, 0);
        libraryScene->state = kLibraryStateBuildUIList;
    }

    cb_clear_global_cover_cache();

    return libraryScene;
}

static void set_display_and_sort_name(CB_Game* game);
static void CB_LibraryScene_updateDisplayNames(CB_LibraryScene* libraryScene)
{
    char* selectedFilename = NULL;
    if (libraryScene->listView->selectedItem >= 0 &&
        libraryScene->listView->selectedItem < libraryScene->games->length)
    {
        CB_Game* selectedGameBefore =
            libraryScene->games->items[libraryScene->listView->selectedItem];
        selectedFilename = cb_strdup(selectedGameBefore->names->filename);
    }

    for (int i = 0; i < libraryScene->games->length; i++)
    {
        CB_Game* game = libraryScene->games->items[i];
        set_display_and_sort_name(game);
    }

    cb_sort_games_array(libraryScene->games);
    CB_App->gameListCacheIsSorted = true;

    int newSelectedIndex = 0;
    if (selectedFilename)
    {
        for (int i = 0; i < libraryScene->games->length; i++)
        {
            CB_Game* currentGame = libraryScene->games->items[i];
            if (strcmp(currentGame->names->filename, selectedFilename) == 0)
            {
                newSelectedIndex = i;
                break;
            }
        }
        cb_free(selectedFilename);
    }

    libraryScene->listView->selectedItem = newSelectedIndex;

    CB_Array* items = libraryScene->listView->items;
    for (int i = 0; i < items->length; i++)
    {
        CB_ListItemButton* button = items->items[i];
        CB_ListItemButton_free(button);
    }
    array_clear(items);
    (void)array_reserve(items, libraryScene->games->length);

    for (int i = 0; i < libraryScene->games->length; i++)
    {
        CB_Game* game = libraryScene->games->items[i];
        CB_ListItemButton* itemButton = CB_ListItemButton_new(game->displayName);
        array_push(items, itemButton);
    }
    library_push_get_roms_item(libraryScene->listView);

    CB_ListView_reload(libraryScene->listView);
}

static void CB_LibraryScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    CB_LibraryScene* libraryScene = object;
    s_active_library_scene = libraryScene;

    if (libraryScene->state != kLibraryStateDone)
    {
        switch (libraryScene->state)
        {
        case kLibraryStateInit:
        {
            playdate->file->listfiles(
                cb_gb_directory_path(CB_coversPath), collect_cover_filenames_callback,
                libraryScene->available_covers, 0
            );
#ifdef CRANKBOY_OFFICIAL_CATALOG
            playdate->file->listfiles(
                "packed", collect_cover_filenames_callback, libraryScene->available_covers, 0
            );
#endif
            if (libraryScene->available_covers->length > 0)
            {
                qsort(
                    libraryScene->available_covers->items, libraryScene->available_covers->length,
                    sizeof(char*), cb_compare_strings
                );
#ifdef CRANKBOY_OFFICIAL_CATALOG
                for (int i = libraryScene->available_covers->length - 1; i > 0; --i)
                {
                    const char* a = libraryScene->available_covers->items[i];
                    const char* b = libraryScene->available_covers->items[i - 1];
                    if (cb_strcmp(a, b) == 0)
                    {
                        cb_free(libraryScene->available_covers->items[i]);
                        array_remove_at(libraryScene->available_covers, i);
                    }
                }
#endif
            }

            libraryScene->build_game_index = 0;
            libraryScene->progress_max_width =
                cb_calculate_progress_max_width(CB_App->subheadFont, PROGRESS_STYLE_PERCENT, 0);
            libraryScene->state = kLibraryStateBuildGameList;
            return;
        }

        case kLibraryStateBuildGameList:
        {
            if (libraryScene->build_game_index < CB_App->gameNameCache->length)
            {
                int batch_end = libraryScene->build_game_index + BUILD_BATCH_SIZE;
                if (batch_end > CB_App->gameNameCache->length)
                    batch_end = CB_App->gameNameCache->length;

                for (int i = libraryScene->build_game_index; i < batch_end; i++)
                {
                    CB_GameName* cachedName = CB_App->gameNameCache->items[i];
                    CB_Game* game = CB_Game_new(cachedName, libraryScene->available_covers);
                    array_push(CB_App->gameListCache, game);
                }

                libraryScene->build_game_index = batch_end;

                char progress_suffix[20];
                int total = CB_App->gameNameCache->length;
                int percentage =
                    (total > 0) ? ((float)libraryScene->build_game_index / total) * 100 : 99;
                if (percentage >= 100)
                    percentage = 99;
                snprintf(progress_suffix, sizeof(progress_suffix), "%d%%", percentage);

                cb_draw_logo_screen_centered_split(
                    CB_App->subheadFont, "Building Game List... ", progress_suffix,
                    libraryScene->progress_max_width
                );
            }
            else
            {
                cb_sort_games_array(CB_App->gameListCache);
                CB_App->gameListCacheIsSorted = true;

                libraryScene->build_index = 0;
                libraryScene->games = CB_App->gameListCache;

                // restore last-selected position now that the list is built
                if (preferences_library_remember_selection)
                {
                    last_selected_game_index = (int)(intptr_t)call_with_user_stack_1(
                        load_last_selected_index, CB_App->gameListCache
                    );
                    int sel = last_selected_game_index;
                    if (sel < 0 || sel >= libraryScene->games->length)
                        sel = 0;
                    libraryScene->listView->selectedItem = sel;
                }

                libraryScene->preload_cover_index = 0;
                libraryScene->preload_cover_total = 0;

                libraryScene->state = kLibraryStatePreloadCovers;
            }
            return;
        }

        case kLibraryStatePreloadCovers:
        {
            if (libraryScene->preload_cover_total == 0)
            {
                int center = libraryScene->listView->selectedItem;
                if (center < 0 || center >= libraryScene->games->length)
                    center = 0;

                libraryScene->last_selection_for_idle = center;

                int start = center - PRELOAD_HALF;
                if (start < 0)
                    start = 0;
                int end = center + PRELOAD_HALF + 1;
                if (end > libraryScene->games->length)
                    end = libraryScene->games->length;

                int count = 0;
                for (int i = start; i < end; i++)
                {
                    CB_Game* g = libraryScene->games->items[i];
                    if (g->coverPath && !g->cover_compressed_data)
                        count++;
                }
                libraryScene->preload_cover_total = count;
            }

            if (libraryScene->preload_cover_index < libraryScene->preload_cover_total)
            {
                int center = libraryScene->listView->selectedItem;
                int start = center - PRELOAD_HALF;
                if (start < 0)
                    start = 0;
                int end = center + PRELOAD_HALF + 1;
                if (end > libraryScene->games->length)
                    end = libraryScene->games->length;

                int batch_end = libraryScene->preload_cover_index + PRELOAD_BATCH_SIZE;
                if (batch_end > libraryScene->preload_cover_total)
                    batch_end = libraryScene->preload_cover_total;

                for (int i = start; i < end && libraryScene->preload_cover_index < batch_end; i++)
                {
                    CB_Game* g = libraryScene->games->items[i];
                    if (g->coverPath && !g->cover_compressed_data)
                    {
                        CB_cover_compress_impl(
                            g, libraryScene->lz4_state, &libraryScene->cover_cache_bytes,
                            &libraryScene->cover_cached_count
                        );
                        libraryScene->preload_cover_index++;
                    }
                }

                char progress_suffix[20];
                int pct = (libraryScene->preload_cover_total > 0)
                              ? ((float)libraryScene->preload_cover_index /
                                 libraryScene->preload_cover_total) *
                                    100
                              : 99;
                if (pct >= 100)
                    pct = 99;
                snprintf(progress_suffix, sizeof(progress_suffix), "%d%%", pct);

                cb_draw_logo_screen_centered_split(
                    CB_App->subheadFont, "Caching Covers... ", progress_suffix,
                    libraryScene->progress_max_width
                );
            }
            else
            {
                libraryScene->build_index = 0;
                libraryScene->state = kLibraryStateBuildUIList;
            }
            return;
        }

        case kLibraryStateBuildUIList:
        {
            const int chunk_size = 20;
            if (libraryScene->build_index < libraryScene->games->length)
            {
                for (int i = 0;
                     i < chunk_size && libraryScene->build_index < libraryScene->games->length; ++i)
                {
                    CB_Game* game = libraryScene->games->items[libraryScene->build_index];
                    CB_ListItemButton* itemButton = CB_ListItemButton_new(game->displayName);
                    array_push(libraryScene->listView->items, itemButton);
                    libraryScene->build_index++;
                }

                if (!libraryScene->isReloading)
                {
                    int total = libraryScene->games->length;
                    int percentage =
                        (total > 0) ? ((float)libraryScene->build_index / total) * 100 : 99;

                    if (percentage >= 100)
                    {
                        percentage = 99;
                    }

                    char progress_suffix[20];
                    snprintf(progress_suffix, sizeof(progress_suffix), "%d%%", percentage);

                    cb_draw_logo_screen_centered_split(
                        CB_App->subheadFont, "Loading Library... ", progress_suffix,
                        libraryScene->progress_max_width
                    );
                }
            }
            else
            {
                library_push_get_roms_item(libraryScene->listView);

                // full-screen instructions only when there is nothing to list at all
                if (libraryScene->listView->items->length > 0)
                {
                    libraryScene->tab = CB_LibrarySceneTabList;
                }
                else
                {
                    libraryScene->tab = CB_LibrarySceneTabEmpty;
                }

                libraryScene->listView->frame.height = playdate->display->getHeight();
                CB_ListView_reload(libraryScene->listView);
                libraryScene->state = kLibraryStateDone;
                libraryScene->last_user_input_time_ms =
                    playdate->system->getCurrentTimeMilliseconds();
            }
            return;
        }
        case kLibraryStateDone:
            break;
        }
    }

    // idle background fill
    if (libraryScene->state == kLibraryStateDone && libraryScene->initialLoadComplete)
    {
        unsigned int now = playdate->system->getCurrentTimeMilliseconds();
        if (now - libraryScene->last_user_input_time_ms > IDLE_THRESHOLD_MS)
        {
            cover_background_fill(libraryScene);
        }
    }

#if !defined(CRANKBOY_OFFICIAL_CATALOG)
    // Check for a pending update message when the library is active.
    if (libraryScene->initialLoadComplete && !libraryScene->update_modal_shown &&
        CB_App->shouldCheckUpdateInfo)
    {
        PendingUpdateInfo* update_info = get_pending_update();
        CB_App->shouldCheckUpdateInfo = false;
        if (update_info)
        {
            libraryScene->update_modal_shown = true;

            char* modal_result = aprintf(
                "CrankBoy Update!\n\n%s -> %s\n\n%s", get_current_version(), update_info->version,
                update_info->url
            );

            if (modal_result)
            {
                CB_Modal* modal =
                    CB_Modal_new(modal_result, NULL, on_update_modal_dismiss, update_info);
                cb_free(modal_result);

                if (modal)
                {
                    modal->width = 300;
                    modal->height = 180;

                    if (update_info->w > 0)
                    {
                        modal->width = update_info->w;
                    }

                    if (update_info->h > 0)
                    {
                        modal->height = update_info->h;
                    }

                    if (update_info->margin > 0)
                    {
                        modal->margin = update_info->margin;
                    }

                    CB_presentModal(modal->scene);
                    return;
                }
            }
            else
            {
                free_pending_update_info(update_info);
            }
        }
    }
#endif

    // Check if we need to show the file migration notification.
    if (libraryScene->initialLoadComplete && !libraryScene->migration_modal_shown &&
        CB_App->migration_modal_needed)
    {
        libraryScene->migration_modal_shown = true;
        CB_App->migration_modal_needed = false;

        char* modal_text = aprintf(
            "To improve compatability, your CrankBoy library has been moved to the shared "
            "folder:\n\n%s",
            CB_App->directory
        );
        if (modal_text)
        {
            CB_Modal* modal = CB_Modal_new(modal_text, NULL, NULL, NULL);
            cb_free(modal_text);

            if (modal)
            {
                modal->width = 350;
                modal->height = 180;
                CB_presentModal(modal->scene);
                return;
            }
        }
    }

    if (libraryScene->last_display_name_mode != combined_display_mode())
    {
        libraryScene->last_display_name_mode = combined_display_mode();
        CB_LibraryScene_updateDisplayNames(libraryScene);
    }

    float dt = UINT32_AS_FLOAT(u32enc_dt);

    if (libraryScene->coverDownloadState == COVER_DOWNLOAD_DOWNLOADING)
    {
        coverDownloadAnimationTimer += dt;
        if (coverDownloadAnimationTimer >= 0.5f)  // 500 ms
        {
            coverDownloadAnimationTimer -= 0.5f;
            coverDownloadAnimationStep = (coverDownloadAnimationStep + 1) % 4;
            libraryScene->scene->forceFullRefresh = true;
        }
    }

    CB_Scene_update(libraryScene->scene, dt);

    PDButtons pressed = CB_App->buttons_pressed;

    if (pressed)
    {
        libraryScene->last_user_input_time_ms = playdate->system->getCurrentTimeMilliseconds();
        libraryScene->bg_fill_center = -1;
    }

    // also track crank activity
    float crankChange = playdate->system->getCrankChange();
    if (crankChange != 0.0f)
    {
        libraryScene->last_user_input_time_ms = playdate->system->getCurrentTimeMilliseconds();
        libraryScene->bg_fill_center = -1;
    }

    if (pressed & kButtonA)
    {
        int selectedItem = libraryScene->listView->selectedItem;
        if (selectedItem == libraryScene->games->length)
        {
            // no "Get ROMs..." row without the API
            if (homebrew_hub_available())
            {
                cb_play_ui_sound(CB_UISound_Confirm);
                last_selected_game_index = selectedItem;

                CB_HomebrewHubScene* s = CB_HomebrewHubScene_new(0.0f, NULL);
                CB_presentModal(s->scene);
            }
        }
        else if (selectedItem >= 0 && selectedItem < libraryScene->games->length)
        {
            cb_play_ui_sound(CB_UISound_Confirm);

            last_selected_game_index = selectedItem;
            CB_Game* game = libraryScene->games->items[selectedItem];

            // warn if the crank is in a bad position
            if (crank_would_cause_input(game))
            {
                const char* options[] = {"Ignore", "Cancel", NULL};
                CB_Modal* modal = CB_Modal_new(
                    "The crank's current position will cause an input in-game.\n \nPlease dock the "
                    "crank now.",
                    options, launch_game_prompt_if_script, game
                );

                modal->width = 290;
                modal->height = 190;
                modal->accept_on_dock = 1;

                CB_presentModal(modal->scene);
            }
            else
            {
                launch_game_prompt_if_script(game, 0);
            }
        }
    }
    else if (pressed & kButtonB)
    {
        int selectedItem = libraryScene->listView->selectedItem;
        if (selectedItem >= 0 && selectedItem < libraryScene->games->length)
        {
            CB_Game* selectedGame = libraryScene->games->items[selectedItem];
            bool hasCover = (CB_App->coverArtCache.art.status == CB_COVER_ART_SUCCESS);
            bool hasDBMatch = (selectedGame->names->name_database != NULL);

            if (!hasCover && hasDBMatch && libraryScene->coverDownloadState == COVER_DOWNLOAD_IDLE)
            {
                cb_play_ui_sound(CB_UISound_Confirm);
                CB_LibraryScene_startCoverDownload(libraryScene);
            }
        }
    }

    if (CB_App->pendingScene)
    {
        return;
    }

    CB_LibraryScene_draw(libraryScene, false);

    // display errors to user if needed
    if (getSpooledErrors() > 0)
    {
        const char* spool = getSpooledErrorMessage();
        if (spool)
        {
            CB_InfoScene* infoScene = CB_InfoScene_new(NULL, NULL);

            char* spooldup = cb_strdup(spool);
            if (spooldup)
            {
                infoScene->text = spooldup;
                infoScene->textIsStatic = false;
                freeSpool();
            }
            else
            {
                freeSpool();

                infoScene->text =
                    "A critical error occurred:\n\nOut of Memory\n\nPlease restart CrankBoy.";
                infoScene->textIsStatic = true;

                infoScene->canClose = true;
            }
            CB_presentModal(infoScene->scene);
        }
        return;
    }

    libraryScene->initialLoadComplete = true;
}

static void CB_LibraryScene_draw(CB_LibraryScene* libraryScene, bool forAnimation)
{
    bool needsDisplay = forAnimation;

    if (!forAnimation &&
        (libraryScene->model.empty || libraryScene->model.tab != libraryScene->tab ||
         libraryScene->scene->forceFullRefresh))
    {
        needsDisplay = true;
        if (libraryScene->scene->forceFullRefresh)
        {
            libraryScene->scene->forceFullRefresh = false;
        }
    }

    libraryScene->model.empty = false;
    libraryScene->model.tab = libraryScene->tab;

    if (needsDisplay && !forAnimation)
    {
        playdate->graphics->clear(kColorWhite);
    }

    if (libraryScene->tab == CB_LibrarySceneTabList)
    {
        if (!forAnimation)
            CB_ListView_update(libraryScene->listView);

        int selectedIndex = libraryScene->listView->selectedItem;

        bool selectionChanged = (selectedIndex != libraryScene->lastSelectedItem);

        if (selectionChanged)
        {

            // Reset download state when user navigates away
            if (libraryScene->activeCoverDownloadConnection)
            {
                cb_free(http_safe_ud(libraryScene->activeCoverDownloadConnection));
                http_safe_free(libraryScene->activeCoverDownloadConnection);
                libraryScene->activeCoverDownloadConnection = NULL;
                playdate->system->logToConsole(
                    "Selection changed, closing active cover download connection."
                );
            }

            if (libraryScene->coverDownloadState != COVER_DOWNLOAD_IDLE)
            {
                libraryScene->coverDownloadState = COVER_DOWNLOAD_IDLE;
                if (libraryScene->coverDownloadMessage)
                {
                    cb_free(libraryScene->coverDownloadMessage);
                    libraryScene->coverDownloadMessage = NULL;
                }
            }
            cb_clear_global_cover_cache();

            if (libraryScene->initialLoadComplete)
            {
                cb_play_ui_sound(CB_UISound_Navigate);
            }

            if (selectedIndex >= 0 && selectedIndex < libraryScene->games->length)
            {
                CB_Game* selectedGame = libraryScene->games->items[selectedIndex];

                bool foundInCache = false;
                if (selectedGame->cover_compressed_data)
                {
                    int original_size = selectedGame->cover_rowbytes * selectedGame->cover_height;
                    if (selectedGame->cover_has_mask)
                        original_size *= 2;

                    if (libraryScene->decompression_buffer_size < (size_t)original_size)
                    {
                        char* new_buffer =
                            cb_realloc(libraryScene->decompression_buffer, original_size);
                        libraryScene->decompression_buffer = new_buffer;
                        libraryScene->decompression_buffer_size = original_size;
                    }

                    char* decompressed_buffer = libraryScene->decompression_buffer;
                    if (decompressed_buffer)
                    {
                        int decompressed_size = LZ4_decompress_safe(
                            selectedGame->cover_compressed_data, decompressed_buffer,
                            selectedGame->cover_compressed_size, original_size
                        );
                        if (decompressed_size == original_size)
                        {
                            LCDBitmap* new_bitmap = NULL;
                            if (selectedGame->cover_has_mask)
                            {
                                new_bitmap = playdate->graphics->newBitmap(
                                    selectedGame->cover_width, selectedGame->cover_height,
                                    kColorClear
                                );
                            }
                            else
                            {
                                new_bitmap = playdate->graphics->newBitmap(
                                    selectedGame->cover_width, selectedGame->cover_height,
                                    kColorWhite
                                );
                            }

                            if (new_bitmap)
                            {
                                int new_rowbytes;
                                uint8_t *new_pixel_data, *new_mask_data;
                                playdate->graphics->getBitmapData(
                                    new_bitmap, NULL, NULL, &new_rowbytes, &new_mask_data,
                                    &new_pixel_data
                                );
                                size_t copy_bytes =
                                    ((size_t)selectedGame->cover_rowbytes < (size_t)new_rowbytes)
                                        ? selectedGame->cover_rowbytes
                                        : (size_t)new_rowbytes;

                                uint8_t* src_ptr = (uint8_t*)decompressed_buffer;
                                uint8_t* dst_ptr = new_pixel_data;

                                for (int y = 0; y < selectedGame->cover_height; ++y)
                                {
                                    memcpy(dst_ptr, src_ptr, copy_bytes);
                                    src_ptr += selectedGame->cover_rowbytes;
                                    dst_ptr += new_rowbytes;
                                }

                                if (selectedGame->cover_has_mask && new_mask_data)
                                {
                                    dst_ptr = new_mask_data;
                                    for (int y = 0; y < selectedGame->cover_height; ++y)
                                    {
                                        memcpy(dst_ptr, src_ptr, copy_bytes);
                                        src_ptr += selectedGame->cover_rowbytes;
                                        dst_ptr += new_rowbytes;
                                    }
                                }

                                CB_App->coverArtCache.art.bitmap = new_bitmap;
                                CB_App->coverArtCache.art.original_width =
                                    selectedGame->cover_width;
                                CB_App->coverArtCache.art.original_height =
                                    selectedGame->cover_height;
                                CB_App->coverArtCache.art.scaled_width = selectedGame->cover_width;
                                CB_App->coverArtCache.art.scaled_height =
                                    selectedGame->cover_height;
                                CB_App->coverArtCache.art.status = CB_COVER_ART_SUCCESS;
                                CB_App->coverArtCache.rom_path = cb_strdup(selectedGame->fullpath);
                                foundInCache = true;

                                selectedGame->cover_access_counter =
                                    ++libraryScene->cover_global_access_counter;
                            }
                        }
                        else
                        {
                            playdate->system->logToConsole(
                                "LZ4 decompression failed for %s", selectedGame->fullpath
                            );
                        }
                    }
                }

                if (!foundInCache && selectedGame->coverPath != NULL)
                {
                    CB_App->coverArtCache.art = cb_load_and_scale_cover_art_from_path(
                        selectedGame->coverPath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT
                    );

                    if (CB_App->coverArtCache.art.status == CB_COVER_ART_ERROR_LOADING)
                    {
                        playdate->system->logToConsole(
                            "Error loading cover image, unlinking corrupt file: %s",
                            selectedGame->coverPath
                        );

                        playdate->file->unlink(selectedGame->coverPath, 0);

                        cb_free(selectedGame->coverPath);
                        selectedGame->coverPath = NULL;

                        CB_App->coverArtCache.art.status = CB_COVER_ART_FILE_NOT_FOUND;
                    }

                    CB_App->coverArtCache.rom_path = cb_strdup(selectedGame->fullpath);
                }
            }
        }

        int screenWidth = playdate->display->getWidth();
        int screenHeight = playdate->display->getHeight();

        int rightPanelWidth = THUMBNAIL_WIDTH + 1;

        // use actual thumbnail width if possible
        if (CB_App->coverArtCache.art.status == CB_COVER_ART_SUCCESS &&
            CB_App->coverArtCache.art.bitmap != NULL)
        {
            playdate->graphics->getBitmapData(
                CB_App->coverArtCache.art.bitmap, &rightPanelWidth, NULL, NULL, NULL, NULL
            );
            if (rightPanelWidth >= THUMBNAIL_WIDTH - 1)
                rightPanelWidth = THUMBNAIL_WIDTH;
            rightPanelWidth++;
        }

        int leftPanelWidth = screenWidth - rightPanelWidth;

        int animL = libraryScene->launchAnimShiftLeft;
        int animR = libraryScene->launchAnimShiftRight;

        libraryScene->listView->needsDisplay = libraryScene->listView->needsDisplay || needsDisplay;
        libraryScene->listView->frame = PDRectMake(-animL, 0, leftPanelWidth, screenHeight);
        last_panel_seam = leftPanelWidth;

#ifdef TARGET_SIMULATOR
        while (page_advance > 0)
        {
            --page_advance;
            CB_App->buttons_pressed = kButtonDown;
            CB_ListView_update(libraryScene->listView);
        }
        while (page_advance < 0)
        {
            ++page_advance;
            CB_App->buttons_pressed = kButtonUp;
            CB_ListView_update(libraryScene->listView);
        }
#endif

        CB_ListView_draw(libraryScene->listView);

        if (animL > 0 || animR > 0)
        {
            playdate->graphics->fillRect(
                leftPanelWidth - animL, 0, animL + animR, screenHeight,
                libraryScene->launchAnimWhiteGap ? kColorWhite : kColorBlack
            );

            if (libraryScene->launchAnimWhiteGap)
            {
                int leftEdge = leftPanelWidth - animL;
                int rightEdge = leftPanelWidth + animR;
                playdate->graphics->fillRect(leftEdge + 1, 0, 5, screenHeight, (uintptr_t)&lcdp_50);
                playdate->graphics->fillRect(
                    rightEdge - 6, 0, 5, screenHeight, (uintptr_t)&lcdp_50
                );
                playdate->graphics->fillRect(leftEdge, 0, 1, screenHeight, kColorBlack);
                playdate->graphics->fillRect(rightEdge - 1, 0, 1, screenHeight, kColorBlack);
            }
        }

        if (needsDisplay || libraryScene->listView->needsDisplay || selectionChanged)
        {
            if (!forAnimation)
                libraryScene->lastSelectedItem = selectedIndex;
            playdate->graphics->setDrawOffset(animR, 0);

            playdate->graphics->fillRect(
                leftPanelWidth + 1, 0, rightPanelWidth - 1, screenHeight, kColorWhite
            );

            if (selectedIndex >= 0 && selectedIndex < libraryScene->games->length)
            {
                if (CB_App->coverArtCache.art.status == CB_COVER_ART_SUCCESS &&
                    CB_App->coverArtCache.art.bitmap != NULL)
                {
                    int panel_content_width = rightPanelWidth - 1;
                    int coverX = leftPanelWidth + 1 +
                                 (panel_content_width - CB_App->coverArtCache.art.scaled_width) / 2;
                    int coverY = (screenHeight - CB_App->coverArtCache.art.scaled_height) / 2;

                    playdate->graphics->fillRect(
                        leftPanelWidth + 1, 0, rightPanelWidth - 1, screenHeight, kColorBlack
                    );
                    playdate->graphics->setDrawMode(kDrawModeCopy);
                    playdate->graphics->drawBitmap(
                        CB_App->coverArtCache.art.bitmap, coverX, coverY, kBitmapUnflipped
                    );
                }
                else
                {
                    bool had_error_loading =
                        CB_App->coverArtCache.art.status != CB_COVER_ART_FILE_NOT_FOUND;

                    if (had_error_loading)
                    {
                        const char* message = "Error";

                        if (CB_App->coverArtCache.art.status == CB_COVER_ART_INVALID_IMAGE)
                        {
                            message = "Invalid image";
                        }

                        playdate->graphics->setFont(CB_App->bodyFont);
                        int textWidth = playdate->graphics->getTextWidth(
                            CB_App->bodyFont, message, cb_strlen(message), kUTF8Encoding, 0
                        );
                        int panel_content_width = rightPanelWidth - 1;
                        int textX = leftPanelWidth + 1 + (panel_content_width - textWidth) / 2;
                        int textY =
                            (screenHeight - playdate->graphics->getFontHeight(CB_App->bodyFont)) /
                            2;

                        playdate->graphics->setDrawMode(kDrawModeFillBlack);
                        playdate->graphics->drawText(
                            message, cb_strlen(message), kUTF8Encoding, textX, textY
                        );
                    }
                    else
                    {
                        if (libraryScene->coverDownloadState != COVER_DOWNLOAD_IDLE &&
                            libraryScene->coverDownloadState != COVER_DOWNLOAD_COMPLETE)
                        {
                            char message[32];
                            const char* width_calc_string = NULL;

                            if (libraryScene->coverDownloadState == COVER_DOWNLOAD_DOWNLOADING)
                            {
                                const char* base_text = "Downloading cover";
                                // Animation sequence: 0 dots, 1 dots, 2 dot, 3 dots
                                const int dot_counts[] = {0, 1, 2, 3};
                                int num_dots = dot_counts[coverDownloadAnimationStep];

                                snprintf(message, sizeof(message), "%s", base_text);
                                for (int i = 0; i < num_dots; i++)
                                {
                                    strncat(message, ".", sizeof(message) - strlen(message) - 1);
                                }
                                // Use the full string for width calculation to prevent jitter
                                width_calc_string = "Downloading cover...";
                            }
                            else
                            {
                                const char* defaultMessage =
                                    libraryScene->coverDownloadMessage
                                        ? libraryScene->coverDownloadMessage
                                        : "Please wait...";
                                snprintf(message, sizeof(message), "%s", defaultMessage);
                            }

                            if (width_calc_string == NULL)
                            {
                                width_calc_string = message;
                            }

                            playdate->graphics->setFont(CB_App->bodyFont);
                            int textWidth = playdate->graphics->getTextWidth(
                                CB_App->bodyFont, width_calc_string, strlen(width_calc_string),
                                kUTF8Encoding, 0
                            );
                            int panel_content_width = rightPanelWidth - 1;
                            int textX = leftPanelWidth + 1 + (panel_content_width - textWidth) / 2;
                            int textY = (screenHeight -
                                         playdate->graphics->getFontHeight(CB_App->bodyFont)) /
                                        2;
                            playdate->graphics->setDrawMode(kDrawModeFillBlack);
                            playdate->graphics->drawText(
                                message, strlen(message), kUTF8Encoding, textX, textY
                            );
                        }
                        else
                        {
                            CB_Game* selectedGame = libraryScene->games->items[selectedIndex];
                            bool hasDBMatch = (selectedGame->names->name_database != NULL);

                            static const char* title = "Missing Cover";
                            char middle_message[32];

                            if (hasDBMatch)
                            {
                                snprintf(
                                    middle_message, sizeof(middle_message), "Press Ⓑ to download."
                                );
                            }
                            else
                            {
                                snprintf(
                                    middle_message, sizeof(middle_message), "No database match"
                                );
                            }

                            // Common messages for the footer
                            static const char* message_or = "- or -";
                            static const char* message_connect = "Connect to a computer";
                            static const char* message_copy = "and copy cover to:";
                            const char* message_path = cb_gb_directory_path(CB_coversPath);

                            LCDFont* titleFont = CB_App->bodyFont;
                            LCDFont* bodyFont = CB_App->subheadFont;
                            int large_gap = 12;
                            int small_gap = 3;
                            int titleHeight = playdate->graphics->getFontHeight(titleFont);
                            int messageHeight = playdate->graphics->getFontHeight(bodyFont);

                            // Calculate total height dynamically based on whether the "- or -" is
                            // shown
                            int containerHeight = titleHeight + large_gap + messageHeight +
                                                  large_gap + messageHeight + small_gap +
                                                  messageHeight + small_gap + messageHeight;
                            if (hasDBMatch)
                            {
                                containerHeight += large_gap + messageHeight;
                            }

                            int currentY = (screenHeight - containerHeight) / 2;
                            int panel_content_width = rightPanelWidth - 1;

                            playdate->graphics->setDrawMode(kDrawModeFillBlack);

                            // Draw Title (common)
                            playdate->graphics->setFont(titleFont);
                            int titleX = leftPanelWidth + 1 +
                                         (panel_content_width -
                                          playdate->graphics->getTextWidth(
                                              titleFont, title, strlen(title), kUTF8Encoding, 0
                                          )) /
                                             2;
                            playdate->graphics->drawText(
                                title, strlen(title), kUTF8Encoding, titleX, currentY
                            );
                            currentY += titleHeight + large_gap;

                            // Draw Middle Message (dynamic)
                            playdate->graphics->setFont(bodyFont);
                            int middle_message_X =
                                leftPanelWidth + 1 +
                                (panel_content_width - playdate->graphics->getTextWidth(
                                                           bodyFont, middle_message,
                                                           strlen(middle_message), kUTF8Encoding, 0
                                                       )) /
                                    2;
                            playdate->graphics->drawText(
                                middle_message, strlen(middle_message), kUTF8Encoding,
                                middle_message_X, currentY
                            );
                            currentY += messageHeight + large_gap;

                            // Draw Footer (partially conditional)
                            if (hasDBMatch)
                            {
                                int message_or_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message_or, strlen(message_or), kUTF8Encoding, 0
                                     )) /
                                        2;
                                playdate->graphics->drawText(
                                    message_or, strlen(message_or), kUTF8Encoding, message_or_X,
                                    currentY
                                );
                                currentY += messageHeight + large_gap;
                            }

                            int message_connect_X =
                                leftPanelWidth + 1 +
                                (panel_content_width - playdate->graphics->getTextWidth(
                                                           bodyFont, message_connect,
                                                           strlen(message_connect), kUTF8Encoding, 0
                                                       )) /
                                    2;
                            playdate->graphics->drawText(
                                message_connect, strlen(message_connect), kUTF8Encoding,
                                message_connect_X, currentY
                            );
                            currentY += messageHeight + small_gap;

                            int message_copy_X =
                                leftPanelWidth + 1 +
                                (panel_content_width -
                                 playdate->graphics->getTextWidth(
                                     bodyFont, message_copy, strlen(message_copy), kUTF8Encoding, 0
                                 )) /
                                    2;
                            playdate->graphics->drawText(
                                message_copy, strlen(message_copy), kUTF8Encoding, message_copy_X,
                                currentY
                            );
                            currentY += messageHeight + small_gap;

                            int message_path_X =
                                leftPanelWidth + 1 +
                                (panel_content_width -
                                 playdate->graphics->getTextWidth(
                                     bodyFont, message_path, strlen(message_path), kUTF8Encoding, 0
                                 )) /
                                    2;
                            playdate->graphics->drawText(
                                message_path, strlen(message_path), kUTF8Encoding, message_path_X,
                                currentY
                            );
                        }
                    }
                }
            }
            else if (selectedIndex == libraryScene->games->length)
            {
                const char* text =
                    "Download \"homebrew\" games for free from Homebrew Hub.\n\n"
                    "This feature is still experimental.\n\n"
                    "Parental lock is available.";

                LCDFont* font = CB_App->subheadFont;
                int margin = 6;
                int textX = leftPanelWidth + 1 + margin;
                int textWidth = rightPanelWidth - 1 - margin * 2;

                playdate->graphics->setFont(font);
                playdate->graphics->setDrawMode(kDrawModeFillBlack);

                int textHeight = playdate->graphics->getTextHeightForMaxWidth(
                    font, text, strlen(text), textWidth, kUTF8Encoding, kWrapWord, 0, 0
                );
                int textY = (screenHeight - textHeight) / 2;
                if (textY < 0)
                    textY = 0;

                playdate->graphics->drawTextInRect(
                    text, strlen(text), kUTF8Encoding, textX, textY, textWidth,
                    screenHeight - textY, kWrapWord, kAlignTextCenter
                );
            }

            // Draw separator line
            playdate->graphics->drawLine(
                leftPanelWidth, 0, leftPanelWidth, screenHeight, 1, kColorBlack
            );

            playdate->graphics->setDrawOffset(0, 0);
        }
    }
    else if (libraryScene->tab == CB_LibrarySceneTabEmpty)
    {
        if (needsDisplay)
        {
            static const char* title = "Use CrankBoy Manager";
            static const char* message1 = "- OR -";

            static const char* message2_num = "1.";
            static const char* message2_text = "Connect to a computer via USB";

            static const char* message3_num = "2.";
            static const char* message3_text1 = "For about 10s, hold ";
            static const char* message3_text2 = "LEFT + MENU + POWER";

            static const char* message4_num = "3.";
            static const char* message4_text1 = "Copy games to ";
            const char* message4_text2 = cb_gb_directory_path(CB_gamesPath);

            static const char* message5_text = "(Filenames must end with .gb, .gbc, or .gbz)";

            if (!forAnimation)
                playdate->graphics->clear(kColorWhite);

            int titleToMessageSpacing = 8;
            int messageLineSpacing = 4;
            int verticalOffset = 2;
            int textPartSpacing = 5;

            int titleHeight = playdate->graphics->getFontHeight(CB_App->titleFont);
            int subheadHeight = playdate->graphics->getFontHeight(CB_App->subheadFont);
            int messageHeight = playdate->graphics->getFontHeight(CB_App->bodyFont);
            int compositeLineHeight = (subheadHeight + verticalOffset > messageHeight)
                                          ? (subheadHeight + verticalOffset)
                                          : messageHeight;

            int numWidth1 = playdate->graphics->getTextWidth(
                CB_App->bodyFont, message2_num, strlen(message2_num), kUTF8Encoding, 0
            );
            int numWidth2 = playdate->graphics->getTextWidth(
                CB_App->bodyFont, message3_num, strlen(message3_num), kUTF8Encoding, 0
            );
            int numWidth3 = playdate->graphics->getTextWidth(
                CB_App->bodyFont, message4_num, strlen(message4_num), kUTF8Encoding, 0
            );
            int maxNumWidth = (numWidth1 > numWidth2) ? numWidth1 : numWidth2;
            maxNumWidth = (numWidth3 > maxNumWidth) ? numWidth3 : maxNumWidth;

            int textWidth4_part1 = playdate->graphics->getTextWidth(
                CB_App->bodyFont, message4_text1, strlen(message4_text1), kUTF8Encoding, 0
            );
            int textWidth4_part2 = playdate->graphics->getTextWidth(
                CB_App->subheadFont, message4_text2, strlen(message4_text2), kUTF8Encoding, 0
            );
            int totalInstructionWidth =
                maxNumWidth + 4 + textWidth4_part1 + textPartSpacing + textWidth4_part2;

            int titleX = (playdate->display->getWidth() -
                          playdate->graphics->getTextWidth(
                              CB_App->titleFont, title, strlen(title), kUTF8Encoding, 0
                          )) /
                         2;
            int blockAnchorX = (playdate->display->getWidth() - totalInstructionWidth) / 2;
            int numColX = blockAnchorX;
            int textColX = blockAnchorX + maxNumWidth + 4;

            int containerHeight = titleHeight + titleToMessageSpacing + messageHeight +
                                  messageLineSpacing + messageHeight + messageLineSpacing +
                                  compositeLineHeight + messageLineSpacing + compositeLineHeight +
                                  messageLineSpacing + messageHeight;

            int titleY = (playdate->display->getHeight() - containerHeight) / 2;

            int message1_Y = titleY + titleHeight + titleToMessageSpacing;
            int message2_Y = message1_Y + messageHeight + messageLineSpacing;
            int message3_Y = message2_Y + messageHeight + messageLineSpacing;
            int message4_Y = message3_Y + compositeLineHeight + messageLineSpacing;
            int message5_Y = message4_Y + compositeLineHeight + messageLineSpacing;

            playdate->graphics->setFont(CB_App->titleFont);
            playdate->graphics->drawText(title, strlen(title), kUTF8Encoding, titleX, titleY);

            playdate->graphics->setFont(CB_App->bodyFont);
            int message1Width = playdate->graphics->getTextWidth(
                CB_App->bodyFont, message1, strlen(message1), kUTF8Encoding, 0
            );
            int message1X = (playdate->display->getWidth() - message1Width) / 2;
            playdate->graphics->drawText(
                message1, strlen(message1), kUTF8Encoding, message1X, message1_Y
            );

            playdate->graphics->drawText(
                message2_num, strlen(message2_num), kUTF8Encoding, numColX, message2_Y
            );
            playdate->graphics->drawText(
                message2_text, strlen(message2_text), kUTF8Encoding, textColX, message2_Y
            );

            playdate->graphics->drawText(
                message3_num, strlen(message3_num), kUTF8Encoding, numColX, message3_Y
            );
            playdate->graphics->drawText(
                message3_text1, strlen(message3_text1), kUTF8Encoding, textColX, message3_Y
            );
            playdate->graphics->setFont(CB_App->subheadFont);
            int message3_text1_width = playdate->graphics->getTextWidth(
                CB_App->bodyFont, message3_text1, strlen(message3_text1), kUTF8Encoding, 0
            );
            playdate->graphics->drawText(
                message3_text2, strlen(message3_text2), kUTF8Encoding,
                textColX + message3_text1_width + textPartSpacing, message3_Y + verticalOffset
            );

            playdate->graphics->setFont(CB_App->bodyFont);
            playdate->graphics->drawText(
                message4_num, strlen(message4_num), kUTF8Encoding, numColX, message4_Y
            );
            playdate->graphics->drawText(
                message4_text1, strlen(message4_text1), kUTF8Encoding, textColX, message4_Y
            );
            playdate->graphics->setFont(CB_App->subheadFont);
            int message4_text1_width = playdate->graphics->getTextWidth(
                CB_App->bodyFont, message4_text1, strlen(message4_text1), kUTF8Encoding, 0
            );
            playdate->graphics->drawText(
                message4_text2, strlen(message4_text2), kUTF8Encoding,
                textColX + message4_text1_width + textPartSpacing, message4_Y + verticalOffset
            );

            playdate->graphics->setFont(CB_App->bodyFont);
            playdate->graphics->drawText(
                message5_text, strlen(message5_text), kUTF8Encoding, textColX, message5_Y
            );
        }
    }

    int sideBar = libraryScene->launchAnimSideBarWidth;
    if (sideBar > 0)
    {
        playdate->graphics->fillRect(0, 0, sideBar, LCD_ROWS, kColorBlack);
        playdate->graphics->fillRect(LCD_COLUMNS - sideBar, 0, sideBar, LCD_ROWS, kColorBlack);
    }
}

static void CB_LibraryScene_showSettings(void* userdata)
{
    CB_SettingsScene* settingsScene = CB_SettingsScene_new_userstack(NULL, NULL, userdata);
    CB_presentModal(settingsScene->scene);
}

static void CB_LibraryScene_menu(void* object)
{
    playdate->system->addMenuItem("Credits", CB_showCredits, object);
    playdate->system->addMenuItem("Help", (void*)CB_showHelp, 0);
    playdate->system->addMenuItem(T(pdmenu_settings), CB_LibraryScene_showSettings, object);
}

static void CB_LibraryScene_free(void* object)
{
    CB_LibraryScene* libraryScene = object;

    if (s_active_library_scene == libraryScene)
    {
        s_active_library_scene = NULL;
    }

    CB_Scene_free(libraryScene->scene);

    CB_ListView_free(libraryScene->listView);

    if (libraryScene->coverDownloadMessage)
    {
        cb_free(libraryScene->coverDownloadMessage);
    }

    if (libraryScene->activeCoverDownloadConnection)
    {
        cb_free(http_safe_ud(libraryScene->activeCoverDownloadConnection));
        http_safe_free(libraryScene->activeCoverDownloadConnection);
        libraryScene->activeCoverDownloadConnection = NULL;
    }

    if (libraryScene->decompression_buffer)
    {
        cb_free(libraryScene->decompression_buffer);
    }

    if (libraryScene->available_covers)
    {
        for (int i = 0; i < libraryScene->available_covers->length; i++)
            cb_free(libraryScene->available_covers->items[i]);
        array_free(libraryScene->available_covers);
    }

    if (libraryScene->lz4_state)
    {
        cb_free(libraryScene->lz4_state);
    }

    cb_free(libraryScene);
}

static void set_display_and_sort_name(CB_Game* game)
{
    // set display name
    switch (preferences_display_name_mode)
    {
    case DISPLAY_NAME_MODE_SHORT:
        game->displayName = (preferences_display_article) ? game->names->name_short
                                                          : game->names->name_short_leading_article;
        break;
    case DISPLAY_NAME_MODE_DETAILED:
        game->displayName = (preferences_display_article)
                                ? game->names->name_detailed
                                : game->names->name_detailed_leading_article;
        break;
    case DISPLAY_NAME_MODE_FILENAME:
    default:
        game->displayName = (preferences_display_article)
                                ? game->names->name_filename
                                : game->names->name_filename_leading_article;
        break;
    }

    // set sort name
    switch (preferences_display_sort)
    {
    default:
    case 0:
        game->sortName = game->names->name_filename;
        break;
    case 1:
        game->sortName = game->names->name_detailed;
        break;
    case 2:
        game->sortName = game->names->name_detailed_leading_article;
        break;
    case 3:
        game->sortName = game->names->name_filename_leading_article;
        break;
    }
}

CB_Game* CB_Game_new(CB_GameName* cachedName, CB_Array* available_covers)
{
    CB_Game* game = cb_malloc(sizeof(CB_Game));
    memset(game, 0, sizeof(CB_Game));

    char* fullpath_str = NULL;

    if (cachedName->fullpath)
    {
        fullpath_str = cb_strdup(cachedName->fullpath);
    }
#ifdef CRANKBOY_OFFICIAL_CATALOG
    else if (CB_App->packed_filenames && CB_App->packed_filenames->length > 0)
    {
        char** found = (char**)bsearch(
            &cachedName->filename, CB_App->packed_filenames->items,
            CB_App->packed_filenames->length, sizeof(char*), cb_compare_strings
        );
        if (found)
        {
            fullpath_str = aprintf("packed/%s", cachedName->filename);
        }
    }
#endif

    if (!fullpath_str)
    {
        char* games_dir = cb_system_directory_path_for_slug(cachedName->system_slug, CB_gamesPath);
        playdate->system->formatString(&fullpath_str, "%s/%s", games_dir, cachedName->filename);
        cb_free(games_dir);
    }

    game->fullpath = fullpath_str;

    game->names = cachedName;
    set_display_and_sort_name(game);

    char* basename_no_ext = cb_basename(cachedName->filename, true);

    char** found_cover_name_ptr = (char**)bsearch(
        &basename_no_ext, available_covers->items, available_covers->length, sizeof(char*),
        cb_compare_strings
    );

    if (found_cover_name_ptr == NULL)
    {
        char* cleanName_no_ext = cb_strdup(basename_no_ext);
        cb_sanitize_string_for_filename(cleanName_no_ext);
        found_cover_name_ptr = (char**)bsearch(
            &cleanName_no_ext, available_covers->items, available_covers->length, sizeof(char*),
            cb_compare_strings
        );
        cb_free(cleanName_no_ext);
    }

    if (found_cover_name_ptr)
    {
        const char* found_cover_name = *found_cover_name_ptr;

#ifdef CRANKBOY_OFFICIAL_CATALOG
        if (strncmp(game->fullpath, "packed/", 7) == 0)
        {
            game->coverPath = aprintf("packed/%s.pdi", found_cover_name);
        }
        else
#endif
        {
            char* covers_dir =
                cb_system_directory_path_for_slug(cachedName->system_slug, CB_coversPath);
            playdate->system->formatString(
                &game->coverPath, "%s/%s.pdi", covers_dir, found_cover_name
            );
            cb_free(covers_dir);
        }
    }
    else
    {
        game->coverPath = NULL;
    }

    cb_free(basename_no_ext);

    return game;
}

void CB_Game_free(CB_Game* game)
{
    cb_free(game->fullpath);
    cb_free(game->coverPath);
    CB_cover_free_compressed(game);
    cb_free(game);
}

bool CB_LibraryScene_removeGame(CB_Game* game)
{
    CB_LibraryScene* libraryScene = s_active_library_scene;
    if (!libraryScene || !game)
        return false;

    CB_Array* games = libraryScene->games;
    int idx = -1;
    for (int i = 0; i < games->length; i++)
    {
        if (games->items[i] == game)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return false;

    // paranoia: if last game in list, restart
    if (games->length <= 1)
        return false;

    array_remove_at(games, idx);

    CB_Array* items = libraryScene->listView->items;
    if (idx < (int)items->length)
    {
        CB_ListItemButton* button = items->items[idx];
        array_remove_at(items, idx);
        CB_ListItemButton_free(button);
    }

    int sel = libraryScene->listView->selectedItem;
    if (sel > idx)
        sel--;
    if (sel >= games->length)
        sel = games->length - 1;
    if (sel < 0)
        sel = 0;
    libraryScene->listView->selectedItem = sel;

    if (libraryScene->lastSelectedItem > idx)
        libraryScene->lastSelectedItem--;
    else if (libraryScene->lastSelectedItem == idx)
        libraryScene->lastSelectedItem = -1;

    CB_ListView_reload(libraryScene->listView);
    libraryScene->scene->forceFullRefresh = true;

    cb_clear_global_cover_cache();

    CB_Game_free(game);
    return true;
}
