//
//  library_scene.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 15/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "library_scene.h"

#include "../../libs/lz4/lz4.h"
#include "../../libs/minigb_apu/minigb_apu.h"
#include "../app.h"
#include "../dtcm.h"
#include "../http.h"
#include "../jparse.h"
#include "../preferences.h"
#include "../scenes/modal.h"
#include "../script.h"
#include "../userstack.h"
#include "../utility.h"
#include "../version.h"
#include "credits_scene.h"
#include "game_scene.h"
#include "info_scene.h"
#include "settings_scene.h"
#include "softpatch.h"

#include <string.h>

#define HOLD_TIME 1.09f
#define DELETE_COVER_HOLD_TIME 5.09f

static void CB_LibraryScene_update(void* object, uint32_t u32enc_dt);
static void CB_LibraryScene_free(void* object);
static void CB_LibraryScene_reloadList(CB_LibraryScene* libraryScene);
static void CB_LibraryScene_menu(void* object);
static int last_selected_game_index = 0;
static bool has_loaded_initial_index = false;
static bool library_was_initialized_once = false;

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

    if (cb_write_entire_file(cover_dest_path, actual_data_start, new_data_len))
    {
        if (game->coverPath)
        {
            cb_free(game->coverPath);
        }
        game->coverPath = cb_strdup(cover_dest_path);

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

    libraryScene->activeCoverDownloadConnection = 0;

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

    libraryScene->activeCoverDownloadConnection = http_get(
        "github.com", url_path, "to download missing cover art", on_cover_download_finished, 15000,
        userdata
    );

    cb_free(url_path);
}

static void CB_LibraryScene_deleteCoverConfirmed(void* ud, int option)
{
    if (option == 1)
    {
        CB_Game* game = ud;
        CB_LibraryScene* libraryScene = (CB_LibraryScene*)CB_App->scene->managedObject;

        if (game && game->coverPath)
        {
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
            libraryScene->showCrc = false;
            libraryScene->scene->forceFullRefresh = true;
        }
    }
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

// option 0: launch as dmg
// option 1: launch as cgb
static void launch_dmg_or_cgb(CB_Game* game, int option)
{
    if (option == 0 || option == 1)
    {
        CB_GameScene* gameScene =
            CB_GameScene_new(game->fullpath, game->names->name_short_leading_article, option == 1);
        if (gameScene)
        {
            CB_present(gameScene->scene);
        }

        playdate->system->logToConsole("Present gameScene");
    }
}

static void launch_game_prompt_cgb(CB_Game* game, int launch)
{
    if (launch != 1) return;
    
    // check if game would use script
    ScriptInfo* info = get_script_info(game->names->name_header);
    void* prefs = preferences_store_subset(-1);
    load_game_prefs(game->fullpath, true);
    bool will_use_script = preferences_script_support;
    preferences_restore_subset(prefs);
    playdate->system->logToConsole("Will use script: %d", (int)will_use_script);
    
    if (will_use_script && info)
    {
        launch_dmg_or_cgb(game, info->launch_cgb ? 1 : 0);
    }
    else
    {
        const char* options[] = {"DMG", "CGB", NULL};
        const char* options_cgb_not_recommended[] = {"DMG", "CGB*", NULL};
        
        switch (game->names->rom_cgb_support)
        {
        default:
            playdate->system->logToConsole("WARNING: unexpected game platform (0x%x); launching as DMG", game->names->rom_cgb_support);
            launch_dmg_or_cgb(game, 0);
            break;
        case GB_SUPPORT_DMG:
            launch_dmg_or_cgb(game, 0);
            break;
        case GB_SUPPORT_CGB:
            {
                CB_Modal* modal = CB_Modal_new(
                    "This ROM is marked CGB-only. CrankBoy only has experimental support for CGB (i.e. Color) ROMs. You can try launching this as a standard DMG (non-Color) ROM, or try experimental CGB mode.",
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
                        "This ROM optionally supports CGB mode (\"Color\"). You can launch in standard, non-Color DMG mode (recommended), or try using CrankBoy's experimental CGB emulation (likely to fail).",
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
        
        const char* options[] = {
            "Cancel",
            "Launch",
            NULL
        };
        
        size_t size;
        char* data = call_with_main_stack_5(cb_read_partial_file, save_fname, 0x20, &size, kFileReadData, true);
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
                uint32_t stored_hash = *(uint32_t*)(void*)&data[14];
                uint32_t flags = *(uint32_t*)(void*)&data[0x10];
                bool script = flags & 1;
                
                if (stored_hash != hash)
                {
                    CB_Modal* modal;
                    if (!stored_hash)
                    {
                        modal = CB_Modal_new(
                            "You have softpatches enabled, but this game's save data comes from an unpatched ROM. To keep the save data separate, you may wish to change the save slot in settings before launching.",
                            options, (void*)launch_game_prompt_cgb, game
                        );
                    }
                    else if (!hash)
                    {
                        char* msg = aprintf("You have no softpatches on, but this game's save data comes from a patched ROM (code: %08X.) To keep the save data separate, you may wish to change the save slot in settings before launching.", stored_hash);
                        modal = CB_Modal_new(
                            msg,
                            options, (void*)launch_game_prompt_cgb, game
                        );
                        cb_free(msg);
                    }
                    else
                    {
                        char* msg = aprintf("This game's save data comes from a ROM with different softpatches applied (saved code: %08X; your patches: %08X) Consider changing the save slot in settings to keep your save data separate.", stored_hash, hash);
                        modal = CB_Modal_new(
                            msg,
                            options, (void*)launch_game_prompt_cgb, game
                        );
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
                preferences_save_to_disk(
                    settings_path,
                    ~(PREFBITS_NEVER_GLOBAL)
                );
            }
            else
            {
                playdate->system->logToConsole("not switching to per-game prefs");
                // if global scripts disabled, AND we aren't using per-game prefs for this game, AND
                // we didn't ask to enable script support, then just mark prompted (and don't enable
                // per-game + script support.)
                preferences_save_to_disk(
                    settings_path, ~(PREFBIT_script_has_prompted)
                );
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

static void apply_lsdj_settings_and_launch(void* ud, int option)
{
    if (option != 0)
    {
        return;
    }

    CB_Game* game = ud;
    char* settings_path = cb_game_config_path(game->fullpath);

    if (settings_path)
    {
        void* stored_globals = preferences_store_subset(~(preferences_bitfield_t)0);

        preferences_merge_from_disk(settings_path);

        // Optimal settings for LSDj
        preferences_per_game = 1;
        preferences_sound_mode = 2;        // Accurate
        preferences_audio_sync = 1;        // Accurate
        preferences_sample_rate = 0;       // High
        preferences_headphone_audio = 1;   // Stereo
        preferences_frame_skip = 1;        // 30fps
        preferences_dither_stable = 0;     // Off
        preferences_disable_autolock = 1;  // On
        preferences_overclock = 0;         // Off
        preferences_itcm = 1;              // On
        preferences_uncap_fps = 0;         // Off

        preferences_save_to_disk(
            settings_path, PREFBITS_LIBRARY_ONLY
        );

        preferences_restore_subset(stored_globals);
        cb_free(stored_globals);
        cb_free(settings_path);
    }

    launch_game(game, 3);
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

static void launch_game_prompt_if_script(void* ud, int option)
{
    if (option != 0)
        return;

    CB_Game* game = ud;

    if (preferences_library_remember_selection)
    {
        call_with_user_stack_1(save_last_selected_index, game->fullpath);
    }

    bool launch = true;

#ifndef NOLUA
    // Prompt for use game script

    // check if user has already accepted/rejected script prompt for this game before
    void* prefs = preferences_store_subset(-1);
    preferences_script_has_prompted = 0;  // ignore global ver. of this setting
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
        if (!info->experimental && !has_prompted)
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
        else if (info->experimental && script_enabled)
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
    else if (game->names->name_header && !memcmp(game->names->name_header, "LSDj", 4))
    {
        bool settings_are_optimal = false;
        char* settings_path = cb_game_config_path(game->fullpath);

        if (playdate->file->stat(settings_path, NULL) == 0)
        {
            void* stored_prefs = preferences_store_subset(~(preferences_bitfield_t)0);
            preferences_merge_from_disk(settings_path);

            if (preferences_per_game == 1 && preferences_sound_mode == 2 &&
                preferences_audio_sync == 1 && preferences_sample_rate == 0 &&
                preferences_headphone_audio == 1 && preferences_frame_skip == 1 &&
                preferences_dither_stable == 0 && preferences_overclock == 0 &&
                preferences_itcm == 1 && preferences_disable_autolock == 1 &&
                preferences_uncap_fps == 0)
            {
                settings_are_optimal = true;
            }

            preferences_restore_subset(stored_prefs);
            cb_free(stored_prefs);
        }

        cb_free(settings_path);

        if (!settings_are_optimal)
        {
            const char* options[] = {"OK", NULL, NULL};
            CB_Modal* modal = CB_Modal_new(
                "LSDj requires accurate timing.\n\nTo ensure it runs "
                "correctly, CrankBoy will apply the recommended settings.",
                options, apply_lsdj_settings_and_launch, game
            );

            modal->width = 350;
            modal->height = 200;

            CB_presentModal(modal->scene);
            launch = false;
        }
    }
#endif

    if (launch)
    {
        launch_game(game, 3);
    }
}

static void on_update_modal_dismiss(void* ud, int option)
{
    mark_update_as_seen();
    free_pending_update_info((PendingUpdateInfo*)ud);
}

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

CB_LibraryScene* CB_LibraryScene_new(void)
{
    CB_App->shouldCheckUpdateInfo = true;
    
    setCrankSoundsEnabled(true);

    if (!has_loaded_initial_index)
    {
        last_selected_game_index =
            (int)(intptr_t)call_with_user_stack_1(load_last_selected_index, CB_App->gameListCache);
        has_loaded_initial_index = true;
    }

    CB_Scene* scene = CB_Scene_new();

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

    int selected_item = 0;
    if (preferences_library_remember_selection)
    {
        selected_item = last_selected_game_index;
        // Safety check if games were removed
        if (selected_item < 0 ||
            (libraryScene->games->length > 0 && selected_item >= libraryScene->games->length))
        {
            selected_item = 0;
        }
    }

    libraryScene->listView->selectedItem = selected_item;
    libraryScene->tab = CB_LibrarySceneTabList;
    libraryScene->lastSelectedItem = -1;
    libraryScene->last_display_name_mode = combined_display_mode();
    libraryScene->initialLoadComplete = false;
    libraryScene->coverDownloadState = COVER_DOWNLOAD_IDLE;
    libraryScene->showCrc = false;
    libraryScene->isReloading = library_was_initialized_once;
    library_was_initialized_once = true;
    libraryScene->bButtonHoldTimer = 0.0f;
    libraryScene->deleteCoverModalShown = false;
    libraryScene->update_modal_shown = false;
    libraryScene->decompression_buffer = NULL;
    libraryScene->decompression_buffer_size = 0;

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
    array_reserve(items, libraryScene->games->length);

    for (int i = 0; i < libraryScene->games->length; i++)
    {
        CB_Game* game = libraryScene->games->items[i];
        CB_ListItemButton* itemButton = CB_ListItemButton_new(game->displayName);
        array_push(items, itemButton);
    }

    CB_ListView_reload(libraryScene->listView);
}

static void CB_LibraryScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    CB_LibraryScene* libraryScene = object;

    if (libraryScene->state != kLibraryStateDone)
    {
        switch (libraryScene->state)
        {
        case kLibraryStateInit:
        {
            libraryScene->build_index = 0;
            libraryScene->progress_max_width =
                cb_calculate_progress_max_width(CB_App->subheadFont, PROGRESS_STYLE_PERCENT, 0);
            libraryScene->state = kLibraryStateBuildUIList;
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
            }
            return;
        }
        case kLibraryStateDone:
            break;
        }
    }

    // Check for a pending update message when the library is active.
    if (libraryScene->initialLoadComplete && !libraryScene->update_modal_shown && CB_App->shouldCheckUpdateInfo)
    {
        PendingUpdateInfo* update_info = get_pending_update();
        CB_App->shouldCheckUpdateInfo = false;
        if (update_info)
        {
            libraryScene->update_modal_shown = true;

            char* modal_result = aprintf(
                "CrankBoy Update!\n\nNew: %s\nInstalled: %s\n\n%s", update_info->version,
                get_current_version(), update_info->url
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

    if (libraryScene->last_display_name_mode != combined_display_mode())
    {
        libraryScene->last_display_name_mode = combined_display_mode();
        CB_LibraryScene_updateDisplayNames(libraryScene);
    }

    float dt = UINT32_AS_FLOAT(u32enc_dt);

    // B-button long press detection for showing CRC and delete modal
    PDButtons current_buttons;
    playdate->system->getButtonState(&current_buttons, NULL, NULL);
    if (current_buttons & kButtonB)
    {
        libraryScene->bButtonHoldTimer += dt;
        bool hasCover = (CB_App->coverArtCache.art.status == CB_COVER_ART_SUCCESS);

        if (hasCover)
        {
            // After 1 second, show CRC
            if (libraryScene->bButtonHoldTimer >= HOLD_TIME && !libraryScene->showCrc)
            {
                libraryScene->showCrc = true;
                libraryScene->scene->forceFullRefresh = true;
                cb_play_ui_sound(CB_UISound_Confirm);
            }

            // After 5 seconds, show delete confirmation modal
            if (libraryScene->bButtonHoldTimer >= DELETE_COVER_HOLD_TIME &&
                !libraryScene->deleteCoverModalShown)
            {
                libraryScene->deleteCoverModalShown = true;

                int selectedItem = libraryScene->listView->selectedItem;
                if (selectedItem >= 0 && selectedItem < libraryScene->games->length)
                {
                    CB_Game* game = libraryScene->games->items[selectedItem];

                    // Make sure there is a cover to delete
                    if (game->coverPath)
                    {
                        const char* options[] = {"No", "Yes", NULL};
                        CB_Modal* modal = CB_Modal_new(
                            "Delete this cover art?", options, CB_LibraryScene_deleteCoverConfirmed,
                            game
                        );
                        modal->width = 240;
                        CB_presentModal(modal->scene);
                    }
                }
            }
        }
    }
    else
    {
        if (libraryScene->bButtonHoldTimer > 0.0f)
        {
            libraryScene->bButtonHoldTimer = 0.0f;
            libraryScene->deleteCoverModalShown = false;
        }
    }

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

    if (pressed & kButtonA)
    {
        int selectedItem = libraryScene->listView->selectedItem;
        if (selectedItem >= 0 && selectedItem < libraryScene->listView->items->length)
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

            // A cover is present and we're showing the CRC
            // A short press restorex the cover.
            if (hasCover && libraryScene->showCrc)
            {
                libraryScene->showCrc = false;
                libraryScene->scene->forceFullRefresh = true;
                cb_play_ui_sound(CB_UISound_Navigate);
            }
            // A cover is missing, but we can download it.
            else if (!hasCover && hasDBMatch &&
                     libraryScene->coverDownloadState == COVER_DOWNLOAD_IDLE)
            {
                cb_play_ui_sound(CB_UISound_Confirm);
                CB_LibraryScene_startCoverDownload(libraryScene);
            }
            // No cover and no DB match. Toggle CRC display.
            else if ((!hasCover && !hasDBMatch) ||
                     libraryScene->coverDownloadState == COVER_DOWNLOAD_NO_GAME_IN_DB)
            {
                libraryScene->showCrc = !libraryScene->showCrc;
                libraryScene->scene->forceFullRefresh = true;
                cb_play_ui_sound(CB_UISound_Navigate);
            }
        }
    }

    bool needsDisplay = false;

    if (libraryScene->model.empty || libraryScene->model.tab != libraryScene->tab ||
        libraryScene->scene->forceFullRefresh)
    {
        needsDisplay = true;
        if (libraryScene->scene->forceFullRefresh)
        {
            libraryScene->scene->forceFullRefresh = false;
        }
    }

    libraryScene->model.empty = false;
    libraryScene->model.tab = libraryScene->tab;

    if (needsDisplay)
    {
        playdate->graphics->clear(kColorWhite);
    }

    if (libraryScene->tab == CB_LibrarySceneTabList)
    {
        CB_ListView_update(libraryScene->listView);

        int selectedIndex = libraryScene->listView->selectedItem;

        bool selectionChanged = (selectedIndex != libraryScene->lastSelectedItem);

        if (selectionChanged)
        {
            libraryScene->showCrc = false;

            // Reset download state when user navigates away
            if (libraryScene->activeCoverDownloadConnection)
            {
                playdate->system->logToConsole(
                    "Selection changed, closing active cover download connection."
                );
                http_cancel(libraryScene->activeCoverDownloadConnection);
                libraryScene->activeCoverDownloadConnection = 0;
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
                if (CB_App->coverCache)
                {
                    for (int i = 0; i < CB_App->coverCache->length; i++)
                    {
                        CB_CoverCacheEntry* entry = CB_App->coverCache->items[i];
                        if (strcmp(entry->rom_path, selectedGame->fullpath) == 0)
                        {
                            if (libraryScene->decompression_buffer_size < entry->original_size)
                            {
                                libraryScene->decompression_buffer = cb_realloc(
                                    libraryScene->decompression_buffer, entry->original_size
                                );
                                libraryScene->decompression_buffer_size = entry->original_size;
                            }

                            char* decompressed_buffer = libraryScene->decompression_buffer;
                            if (decompressed_buffer)
                            {
                                int decompressed_size = LZ4_decompress_safe(
                                    entry->compressed_data, decompressed_buffer,
                                    entry->compressed_size, entry->original_size
                                );
                                if (decompressed_size == entry->original_size)
                                {
                                    LCDBitmap* new_bitmap = NULL;
                                    if (entry->has_mask)
                                    {
                                        new_bitmap = playdate->graphics->newBitmap(
                                            entry->width, entry->height, kColorClear
                                        );
                                    }
                                    else
                                    {
                                        new_bitmap = playdate->graphics->newBitmap(
                                            entry->width, entry->height, kColorWhite
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
                                        size_t copy_bytes = (entry->rowbytes < (size_t)new_rowbytes)
                                                                ? entry->rowbytes
                                                                : (size_t)new_rowbytes;

                                        uint8_t* src_ptr = (uint8_t*)decompressed_buffer;
                                        uint8_t* dst_ptr = new_pixel_data;

                                        for (int y = 0; y < entry->height; ++y)
                                        {
                                            memcpy(dst_ptr, src_ptr, copy_bytes);
                                            src_ptr += entry->rowbytes;
                                            dst_ptr += new_rowbytes;
                                        }

                                        if (entry->has_mask && new_mask_data)
                                        {
                                            dst_ptr = new_mask_data;
                                            for (int y = 0; y < entry->height; ++y)
                                            {
                                                memcpy(dst_ptr, src_ptr, copy_bytes);
                                                src_ptr += entry->rowbytes;
                                                dst_ptr += new_rowbytes;
                                            }
                                        }

                                        CB_App->coverArtCache.art.bitmap = new_bitmap;
                                        CB_App->coverArtCache.art.original_width = entry->width;
                                        CB_App->coverArtCache.art.original_height = entry->height;
                                        CB_App->coverArtCache.art.scaled_width = entry->width;
                                        CB_App->coverArtCache.art.scaled_height = entry->height;
                                        CB_App->coverArtCache.art.status = CB_COVER_ART_SUCCESS;
                                        CB_App->coverArtCache.rom_path =
                                            cb_strdup(selectedGame->fullpath);
                                        foundInCache = true;
                                    }
                                }
                                else
                                {
                                    playdate->system->logToConsole(
                                        "LZ4 decompression failed for %s", entry->rom_path
                                    );
                                }
                            }

                            if (foundInCache)
                                break;
                        }
                    }
                }

                if (!foundInCache && selectedGame->coverPath != NULL)
                {
                    CB_App->coverArtCache.art = cb_load_and_scale_cover_art_from_path(
                        selectedGame->coverPath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT
                    );
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

        libraryScene->listView->needsDisplay = libraryScene->listView->needsDisplay || needsDisplay;
        libraryScene->listView->frame = PDRectMake(0, 0, leftPanelWidth, screenHeight);

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

        if (needsDisplay || libraryScene->listView->needsDisplay || selectionChanged)
        {
            libraryScene->lastSelectedItem = selectedIndex;

            playdate->graphics->fillRect(
                leftPanelWidth + 1, 0, rightPanelWidth - 1, screenHeight, kColorWhite
            );

            if (selectedIndex >= 0 && selectedIndex < libraryScene->games->length)
            {
                if (CB_App->coverArtCache.art.status == CB_COVER_ART_SUCCESS &&
                    CB_App->coverArtCache.art.bitmap != NULL)
                {
                    CB_Game* selectedGame = libraryScene->games->items[selectedIndex];
                    playdate->graphics->setFont(CB_App->bodyFont);

                    if (libraryScene->showCrc)
                    {
                        char message[32];
                        if (selectedGame->names->crc32 != 0)
                        {
                            snprintf(
                                message, sizeof(message), "%08lX",
                                (unsigned long)selectedGame->names->crc32
                            );
                        }
                        else
                        {
                            snprintf(message, sizeof(message), "No CRC found");
                        }

                        int panel_content_width = rightPanelWidth - 1;
                        int textWidth = playdate->graphics->getTextWidth(
                            CB_App->bodyFont, message, strlen(message), kUTF8Encoding, 0
                        );
                        int textX = leftPanelWidth + 1 + (panel_content_width - textWidth) / 2;
                        int textY =
                            (screenHeight - playdate->graphics->getFontHeight(CB_App->bodyFont)) /
                            2;

                        playdate->graphics->fillRect(
                            leftPanelWidth + 1, 0, rightPanelWidth - 1, screenHeight, kColorWhite
                        );
                        playdate->graphics->setDrawMode(kDrawModeFillBlack);
                        playdate->graphics->drawText(
                            message, strlen(message), kUTF8Encoding, textX, textY
                        );
                    }
                    else
                    {
                        int panel_content_width = rightPanelWidth - 1;
                        int coverX =
                            leftPanelWidth + 1 +
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
                }
                else
                {
                    bool had_error_loading =
                        CB_App->coverArtCache.art.status != CB_COVER_ART_FILE_NOT_FOUND;

                    if (had_error_loading)
                    {
                        const char* message = "Error";
                        if (CB_App->coverArtCache.art.status == CB_COVER_ART_ERROR_LOADING)
                        {
                            message = "Error loading image";
                        }
                        else if (CB_App->coverArtCache.art.status == CB_COVER_ART_INVALID_IMAGE)
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
                            else if (libraryScene->coverDownloadState ==
                                         COVER_DOWNLOAD_NO_GAME_IN_DB &&
                                     libraryScene->showCrc)
                            {
                                CB_Game* selectedGame = libraryScene->games->items[selectedIndex];
                                if (selectedGame->names->crc32 != 0)
                                {
                                    snprintf(
                                        message, sizeof(message), "%08lX",
                                        (unsigned long)selectedGame->names->crc32
                                    );
                                }
                                else
                                {
                                    snprintf(message, sizeof(message), "No CRC found");
                                }
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
                                if (libraryScene->showCrc)
                                {
                                    if (selectedGame->names->crc32 != 0)
                                    {
                                        snprintf(
                                            middle_message, sizeof(middle_message), "%08lX",
                                            (unsigned long)selectedGame->names->crc32
                                        );
                                    }
                                    else
                                    {
                                        snprintf(
                                            middle_message, sizeof(middle_message), "No CRC found"
                                        );
                                    }
                                }
                                else
                                {
                                    snprintf(
                                        middle_message, sizeof(middle_message), "No database match"
                                    );
                                }
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

                // Draw separator line
                playdate->graphics->drawLine(
                    leftPanelWidth, 0, leftPanelWidth, screenHeight, 1, kColorBlack
                );
            }
        }
    }
    else if (libraryScene->tab == CB_LibrarySceneTabEmpty)
    {
        if (needsDisplay)
        {
            static const char* title = "CrankBoy";
            static const char* message1 = "To add games:";

            static const char* message2_num = "1.";
            static const char* message2_text = "Connect to a computer via USB";

            static const char* message3_num = "2.";
            static const char* message3_text1 = "For about 10s, hold ";
            static const char* message3_text2 = "LEFT + MENU + POWER";

            static const char* message4_num = "3.";
            static const char* message4_text1 = "Copy games to ";
            const char* message4_text2 = cb_gb_directory_path(CB_gamesPath);

            static const char* message5_text = "(Filenames must end with .gb or .gbc)";

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
            playdate->graphics->drawText(
                message1, strlen(message1), kUTF8Encoding, blockAnchorX, message1_Y
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

    // display errors to user if needed
    if (getSpooledErrors() > 0)
    {
        const char* spool = getSpooledErrorMessage();
        if (spool)
        {
            CB_InfoScene* infoScene = CB_InfoScene_new(NULL, NULL);
            if (!infoScene)
            {
                freeSpool();
                playdate->system->error("Fatal: Out of memory");
                return;
            }

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

static void CB_LibraryScene_showSettings(void* userdata)
{
    CB_SettingsScene* settingsScene = CB_SettingsScene_new(NULL, userdata);
    CB_presentModal(settingsScene->scene);
}

static void CB_LibraryScene_menu(void* object)
{
    playdate->system->addMenuItem("Credits", CB_showCredits, object);
    playdate->system->addMenuItem("Help", (void*)CB_showHelp, 0);
    playdate->system->addMenuItem("Settings", CB_LibraryScene_showSettings, object);
}

static void CB_LibraryScene_free(void* object)
{
    CB_LibraryScene* libraryScene = object;

    CB_Scene_free(libraryScene->scene);

    CB_ListView_free(libraryScene->listView);

    if (libraryScene->coverDownloadMessage)
    {
        cb_free(libraryScene->coverDownloadMessage);
    }

    if (libraryScene->activeCoverDownloadConnection)
    {
        http_cancel(libraryScene->activeCoverDownloadConnection);
        libraryScene->activeCoverDownloadConnection = 0;
    }

    if (libraryScene->decompression_buffer)
    {
        cb_free(libraryScene->decompression_buffer);
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

    char* fullpath_str;
    playdate->system->formatString(&fullpath_str, "%s/%s", cb_gb_directory_path(CB_gamesPath), cachedName->filename);
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
        playdate->system->formatString(
            &game->coverPath, "%s/%s.pdi", cb_gb_directory_path(CB_coversPath), found_cover_name
        );
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
    cb_free(game);
}
