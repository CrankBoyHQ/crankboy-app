//
//  library_scene.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 15/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "library_scene.h"

#include "../lz4/lz4.h"
#include "../minigb_apu/minigb_apu.h"
#include "app.h"
#include "credits_scene.h"
#include "dtcm.h"
#include "game_scene.h"
#include "http.h"
#include "info_scene.h"
#include "jparse.h"
#include "modal.h"
#include "preferences.h"
#include "script.h"
#include "settings_scene.h"
#include "userstack.h"
#include "utility.h"
#include "version.h"

#define LAST_SELECTED_PATH "library_last_selected.txt"

static void PGB_LibraryScene_update(void* object, uint32_t u32enc_dt);
static void PGB_LibraryScene_free(void* object);
static void PGB_LibraryScene_reloadList(PGB_LibraryScene* libraryScene);
static void PGB_LibraryScene_menu(void* object);
static int last_selected_game_index = 0;
static bool has_loaded_initial_index = false;
static bool has_checked_for_update = false;
static bool library_was_initialized_once = false;

typedef struct
{
    PGB_LibraryScene* libraryScene;
    PGB_Game* game;
} CoverDownloadUserdata;

static void save_last_selected_index(const char* rompath)
{
    pgb_write_entire_file(LAST_SELECTED_PATH, rompath, strlen(rompath));
    return;
}

static intptr_t load_last_selected_index(PGB_Array* games)
{
    const char* path = LAST_SELECTED_PATH;
    char* content = pgb_read_entire_file(LAST_SELECTED_PATH, NULL, kFileReadData);
    if (content)
    {
        // first, try searching for a rom whose path matches the given name
        for (int i = 0; i < games->length; ++i)
        {
            PGB_Game* game = games->items[i];
            if (!strcmp(game->fullpath, content))
            {
                return i;
            }
        }
        
        // failing that, convert the value to an integer.
        int index = atoi(content);
        if (index < games->length)
        {
            return index;
        }
    }
    
    // default -- top of list
    return 0;
}

static unsigned combined_display_mode(void)
{
    return preferences_display_name_mode | (preferences_display_article << 3) |
           (preferences_display_sort << 6);
}

static void set_download_status(
    PGB_LibraryScene* self, CoverDownloadState state, const char* message
)
{
    self->coverDownloadState = state;
    if (self->coverDownloadMessage)
    {
        pgb_free(self->coverDownloadMessage);
    }
    self->coverDownloadMessage = message ? string_copy(message) : NULL;
    self->scene->forceFullRefresh = true;
}

static void on_cover_download_finished(unsigned flags, char* data, size_t data_len, void* ud)
{
    CoverDownloadUserdata* userdata = ud;
    PGB_LibraryScene* libraryScene = userdata->libraryScene;
    PGB_Game* game = userdata->game;

    int currentSelectedIndex = libraryScene->listView->selectedItem;
    PGB_Game* currentlySelectedGame = NULL;
    if (currentSelectedIndex >= 0 && currentSelectedIndex < libraryScene->games->length)
    {
        currentlySelectedGame = libraryScene->games->items[currentSelectedIndex];
    }

    bool stillOnSameGame = (currentlySelectedGame == game);
    char* rom_basename_no_ext = NULL;
    char* cover_dest_path = NULL;

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

    rom_basename_no_ext = pgb_basename(game->names->filename, true);
    if (!rom_basename_no_ext)
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        goto cleanup;
    }

    playdate->system->formatString(
        &cover_dest_path, "%s/%s.pdi", PGB_coversPath, rom_basename_no_ext
    );

    if (!cover_dest_path)
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        goto cleanup;
    }

    if (pgb_write_entire_file(cover_dest_path, actual_data_start, new_data_len))
    {
        if (game->coverPath)
        {
            pgb_free(game->coverPath);
        }
        game->coverPath = string_copy(cover_dest_path);

        if (stillOnSameGame)
        {
            pgb_clear_global_cover_cache();

            PGB_App->coverArtCache.art = pgb_load_and_scale_cover_art_from_path(
                game->coverPath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT
            );
            PGB_App->coverArtCache.rom_path = string_copy(game->fullpath);

            set_download_status(libraryScene, COVER_DOWNLOAD_IDLE, NULL);
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
        pgb_free(cover_dest_path);
    }
    if (rom_basename_no_ext)
    {
        pgb_free(rom_basename_no_ext);
    }

    libraryScene->activeCoverDownloadConnection = NULL;

    pgb_free(userdata);
}

static void PGB_LibraryScene_startCoverDownload(PGB_LibraryScene* libraryScene)
{
    int selectedIndex = libraryScene->listView->selectedItem;
    if (selectedIndex < 0 || selectedIndex >= libraryScene->games->length)
    {
        return;
    }

    PGB_Game* game = libraryScene->games->items[selectedIndex];

    set_download_status(libraryScene, COVER_DOWNLOAD_SEARCHING, "Searching for missing Cover...");

    if (game->names->name_database == NULL)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_NO_GAME_IN_DB, "No Cover found.");
        return;
    }

    char* encoded_name = pgb_url_encode_for_github_raw(game->names->name_database);
    if (!encoded_name)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        return;
    }

    for (char* p = encoded_name; *p; ++p)
    {
        if (*p == '&' || *p == ':')
        {
            *p = '_';
        }
    }

    char* url_path;
    playdate->system->formatString(
        &url_path, "/CrankBoyHQ/crankboy-covers/raw/refs/heads/main/Combined_Boxarts/%s.pdi",
        encoded_name
    );

    pgb_free(encoded_name);

    if (!url_path)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, "Internal error.");
        return;
    }

    set_download_status(libraryScene, COVER_DOWNLOAD_DOWNLOADING, "Downloading cover...");

    CoverDownloadUserdata* userdata = pgb_malloc(sizeof(CoverDownloadUserdata));
    userdata->libraryScene = libraryScene;
    userdata->game = game;

    http_get(
        "github.com", url_path, "to download missing cover art", on_cover_download_finished, 15000,
        userdata, &libraryScene->activeCoverDownloadConnection
    );

    pgb_free(url_path);
}

static void load_game_prefs(const char* game_path, bool onlyIfPerGameEnabled)
{
    void* stored = preferences_store_subset(-1);
    bool useGame = false;
    char* settings_path = pgb_game_config_path(game_path);
    if (settings_path)
    {
        call_with_main_stack_1(preferences_merge_from_disk, settings_path);
        pgb_free(settings_path);

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
    pgb_free(stored);
}

static void launch_game(void* ud, int option)
{
    PGB_Game* game = ud;
    switch (option)
    {
    case 0:  // launch w/ scripts enabled
    {
        char* settings_path = pgb_game_config_path(game->fullpath);
        if (settings_path)
        {
            void* prefs = preferences_store_subset(-1);

            load_game_prefs(game->fullpath, false);

            // enable script and per-game support
            preferences_script_support = 1;
            preferences_per_game = 1;
            preferences_script_has_prompted = 1;

            call_with_user_stack_2(
                preferences_save_to_disk, settings_path,
                ~(PREFBIT_script_has_prompted | PREFBIT_script_support | PREFBIT_per_game)
            );

            preferences_restore_subset(prefs);
            if (prefs)
                pgb_free(prefs);
            pgb_free(settings_path);
        }
    }
        goto launch_normal;

    case 1:  // launch w/ scripts disabled
    {
        char* settings_path = pgb_game_config_path(game->fullpath);
        if (settings_path)
        {
            void* prefs = preferences_store_subset(-1);

            load_game_prefs(game->fullpath, false);

            // disable lua script and enable per-game support
            preferences_script_support = 0;
            preferences_per_game = 1;
            preferences_script_has_prompted = 1;

            call_with_user_stack_2(
                preferences_save_to_disk, settings_path,
                ~(PREFBIT_script_has_prompted | PREFBIT_script_support | PREFBIT_per_game)
            );

            preferences_restore_subset(prefs);
            if (prefs)
                pgb_free(prefs);
            pgb_free(settings_path);
        }
    }
        goto launch_normal;

    case 2:
        // display information
        {
            show_game_script_info(game->fullpath);
        }
        break;

    case 3:  // launch game
    launch_normal:
    {
        PGB_GameScene* gameScene =
            PGB_GameScene_new(game->fullpath, game->names->name_short_leading_article);
        if (gameScene)
        {
            PGB_present(gameScene->scene);
        }

        playdate->system->logToConsole("Present gameScene");
    }
    break;

    default:
        // do nothing
        break;
    }
}

static void CB_updatecheck(int code, const char* text, void* ud)
{
    playdate->system->logToConsole("UPDATE RESULT %d: %s\n", code, text);

    char* modal_result = NULL;

    if (code == ERR_PERMISSION_ASKED_DENIED)
    {
        modal_result = aprintf(
            "You can enable checking for updates at any time by adjusting CrankBoy's permissions "
            "in your Playdate's settings."
        );
    }
    else if (code == 2)
    {
        modal_result = aprintf(
            "Update available: %s\n\n(Your version: %s)\n\nPlease download it manually.", text,
            get_current_version()
        );
    }

    if (modal_result)
    {
        PGB_Modal* modal = PGB_Modal_new(modal_result, NULL, NULL, NULL);
        pgb_free(modal_result);

        modal->width = 300;
        modal->height = 180;

        PGB_presentModal(modal->scene);
    }
}

static int page_advance = 0;

__section__(".rare") static void PGB_LibraryScene_event(
    void* object, PDSystemEvent event, uint32_t arg
)
{
    PGB_LibraryScene* libraryScene = object;

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

PGB_LibraryScene* PGB_LibraryScene_new(void)
{
    setCrankSoundsEnabled(true);

    if (!has_loaded_initial_index)
    {
        last_selected_game_index =
            (int)(intptr_t)call_with_user_stack_1(load_last_selected_index, PGB_App->gameListCache);
        has_loaded_initial_index = true;
    }

    PGB_Scene* scene = PGB_Scene_new();

    PGB_LibraryScene* libraryScene = pgb_calloc(1, sizeof(PGB_LibraryScene));

    libraryScene->state = kLibraryStateInit;
    libraryScene->build_index = 0;

    libraryScene->scene = scene;
    scene->managedObject = libraryScene;

    scene->update = PGB_LibraryScene_update;
    scene->free = PGB_LibraryScene_free;
    scene->menu = PGB_LibraryScene_menu;
    scene->event = PGB_LibraryScene_event;

    libraryScene->model = (PGB_LibrarySceneModel){.empty = true, .tab = PGB_LibrarySceneTabList};

    libraryScene->games = PGB_App->gameListCache;
    libraryScene->listView = PGB_ListView_new();

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

    libraryScene->listView->selectedItem =
        (preferences_library_remember_selection) ? last_selected_game_index : 0;
    libraryScene->tab = PGB_LibrarySceneTabList;
    libraryScene->lastSelectedItem = -1;
    libraryScene->last_display_name_mode = combined_display_mode();
    libraryScene->initialLoadComplete = false;
    libraryScene->coverDownloadState = COVER_DOWNLOAD_IDLE;
    libraryScene->showCrc = false;
    libraryScene->isReloading = library_was_initialized_once;
    library_was_initialized_once = true;

    pgb_clear_global_cover_cache();

    return libraryScene;
}

static void set_display_and_sort_name(PGB_Game* game);
static void PGB_LibraryScene_updateDisplayNames(PGB_LibraryScene* libraryScene)
{
    char* selectedFilename = NULL;
    if (libraryScene->listView->selectedItem >= 0 &&
        libraryScene->listView->selectedItem < libraryScene->games->length)
    {
        PGB_Game* selectedGameBefore =
            libraryScene->games->items[libraryScene->listView->selectedItem];
        selectedFilename = string_copy(selectedGameBefore->names->filename);
    }

    for (int i = 0; i < libraryScene->games->length; i++)
    {
        PGB_Game* game = libraryScene->games->items[i];
        set_display_and_sort_name(game);
    }

    pgb_sort_games_array(libraryScene->games);
    PGB_App->gameListCacheIsSorted = true;

    int newSelectedIndex = 0;
    if (selectedFilename)
    {
        for (int i = 0; i < libraryScene->games->length; i++)
        {
            PGB_Game* currentGame = libraryScene->games->items[i];
            if (strcmp(currentGame->names->filename, selectedFilename) == 0)
            {
                newSelectedIndex = i;
                break;
            }
        }
        pgb_free(selectedFilename);
    }

    libraryScene->listView->selectedItem = newSelectedIndex;

    PGB_Array* items = libraryScene->listView->items;
    for (int i = 0; i < items->length; i++)
    {
        PGB_ListItem* item = items->items[i];
        PGB_ListItem_free(item);
    }
    array_clear(items);
    array_reserve(items, libraryScene->games->length);

    for (int i = 0; i < libraryScene->games->length; i++)
    {
        PGB_Game* game = libraryScene->games->items[i];
        PGB_ListItemButton* itemButton = PGB_ListItemButton_new(game->displayName);
        array_push(items, itemButton->item);
    }

    PGB_ListView_reload(libraryScene->listView);
}

static void PGB_LibraryScene_update(void* object, uint32_t u32enc_dt)
{
    if (PGB_App->pendingScene)
    {
        return;
    }

    // display errors to user if needed
    if (getSpooledErrors() > 0)
    {
        const char* spool = getSpooledErrorMessage();
        if (spool)
        {
            PGB_InfoScene* infoScene = PGB_InfoScene_new(NULL);
            if (!infoScene)
            {
                freeSpool();
                goto out_of_memory_error;
            }

            char* spooldup = string_copy(spool);
            if (spooldup)
            {
                infoScene->text = spooldup;
                freeSpool();
            }
            else
            {
                // this is not safe, but we need to show the error message.
                // can force user to quit afterward to recover memory.
                infoScene->text = (char*)spool;
                infoScene->canClose = false;
            }
            PGB_presentModal(infoScene->scene);
        }
        else
        {
        out_of_memory_error:
            playdate->system->error("Out of memory -- unable to list errors.");
        }
        return;
    }

    PGB_LibraryScene* libraryScene = object;

    if (libraryScene->state != kLibraryStateDone)
    {
        switch (libraryScene->state)
        {
        case kLibraryStateInit:
        {
            libraryScene->build_index = 0;
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
                    PGB_Game* game = libraryScene->games->items[libraryScene->build_index];
                    PGB_ListItemButton* itemButton = PGB_ListItemButton_new(game->displayName);
                    array_push(libraryScene->listView->items, itemButton->item);
                    libraryScene->build_index++;
                }

                char progress_message[100];
                int total = libraryScene->games->length;
                int percentage =
                    (total > 0) ? ((float)libraryScene->build_index / total) * 100 : 100;
                snprintf(
                    progress_message, sizeof(progress_message), "Loading Library… %d%%", percentage
                );

                if (!libraryScene->isReloading)
                {
                    pgb_draw_logo_screen_to_buffer(progress_message);
                }
            }
            else
            {
                if (libraryScene->listView->items->length > 0)
                {
                    libraryScene->tab = PGB_LibrarySceneTabList;
                }
                else
                {
                    libraryScene->tab = PGB_LibrarySceneTabEmpty;
                }

                libraryScene->listView->frame.height = playdate->display->getHeight();
                PGB_ListView_reload(libraryScene->listView);
                libraryScene->state = kLibraryStateDone;
            }
            return;
        }
        case kLibraryStateDone:
            break;
        }
    }

    if (libraryScene->last_display_name_mode != combined_display_mode())
    {
        libraryScene->last_display_name_mode = combined_display_mode();
        PGB_LibraryScene_updateDisplayNames(libraryScene);
    }

    float dt = UINT32_AS_FLOAT(u32enc_dt);

    if (!has_checked_for_update)
    {
        has_checked_for_update = true;
        possibly_check_for_updates(CB_updatecheck, NULL);
    }

    PGB_Scene_update(libraryScene->scene, dt);

    PDButtons pressed = PGB_App->buttons_pressed;

    if (pressed & kButtonA)
    {
        int selectedItem = libraryScene->listView->selectedItem;
        if (selectedItem >= 0 && selectedItem < libraryScene->listView->items->length)
        {
            pgb_play_ui_sound(PGB_UISound_Confirm);
            last_selected_game_index = selectedItem;
            PGB_Game* game = libraryScene->games->items[selectedItem];

            if (preferences_library_remember_selection)
            {
                call_with_user_stack_1(save_last_selected_index, game->fullpath);
            }

            bool launch = true;

#ifndef NOLUA
            // Prompt for use game script

            // check if user has already accepted/rejected script prompt for this game before
            void* prefs = preferences_store_subset(-1);
            preferences_script_has_prompted = 0;
            load_game_prefs(game->fullpath, false);
            int has_prompted = preferences_script_has_prompted;
            preferences_restore_subset(prefs);
            pgb_free(prefs);

            if (!has_prompted)
            {
                ScriptInfo* info = script_get_info_by_rom_path(game->fullpath);
                if (info && !info->experimental)
                {
                    const char* options[] = {"Yes", "No", "About", NULL};
                    if (!info->info)
                        options[2] = NULL;
                    PGB_Modal* modal = PGB_Modal_new(
                        "There is native Playdate support for this game.\nWould you like to enable "
                        "it?",
                        options, launch_game, game
                    );

                    script_info_free(info);

                    modal->width = 290;
                    modal->height = 152;

                    PGB_presentModal(modal->scene);
                    launch = false;
                }
            }
#endif

            if (launch)
            {
                launch_game(game, 3);
            }
        }
    }
    else if (pressed & kButtonB)
    {
        int selectedItem = libraryScene->listView->selectedItem;
        if (selectedItem >= 0 && selectedItem < libraryScene->games->length)
        {
            PGB_Game* selectedGame = libraryScene->games->items[selectedItem];
            bool hasDBMatch = (selectedGame->names->name_database != NULL);

            // Only allow download if a cover is missing, a DB match exists,
            // and no download is already in progress.
            if (PGB_App->coverArtCache.art.status != PGB_COVER_ART_SUCCESS &&
                libraryScene->coverDownloadState == COVER_DOWNLOAD_IDLE && hasDBMatch)
            {
                pgb_play_ui_sound(PGB_UISound_Confirm);
                PGB_LibraryScene_startCoverDownload(libraryScene);
            }
            else if ((PGB_App->coverArtCache.art.status != PGB_COVER_ART_SUCCESS && !hasDBMatch) ||
                     libraryScene->coverDownloadState == COVER_DOWNLOAD_NO_GAME_IN_DB)
            {
                libraryScene->showCrc = !libraryScene->showCrc;
                libraryScene->scene->forceFullRefresh = true;
                pgb_play_ui_sound(PGB_UISound_Navigate);
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

    if (libraryScene->tab == PGB_LibrarySceneTabList)
    {
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
                http_cancel_and_cleanup(libraryScene->activeCoverDownloadConnection);
                libraryScene->activeCoverDownloadConnection = NULL;
            }

            if (libraryScene->coverDownloadState != COVER_DOWNLOAD_IDLE)
            {
                libraryScene->coverDownloadState = COVER_DOWNLOAD_IDLE;
                if (libraryScene->coverDownloadMessage)
                {
                    pgb_free(libraryScene->coverDownloadMessage);
                    libraryScene->coverDownloadMessage = NULL;
                }
            }
            pgb_clear_global_cover_cache();

            if (libraryScene->initialLoadComplete)
            {
                pgb_play_ui_sound(PGB_UISound_Navigate);
            }

            if (selectedIndex >= 0 && selectedIndex < libraryScene->games->length)
            {
                PGB_Game* selectedGame = libraryScene->games->items[selectedIndex];

                bool foundInCache = false;
                if (PGB_App->coverCache)
                {
                    for (int i = 0; i < PGB_App->coverCache->length; i++)
                    {
                        PGB_CoverCacheEntry* entry = PGB_App->coverCache->items[i];
                        if (strcmp(entry->rom_path, selectedGame->fullpath) == 0)
                        {
                            char* decompressed_buffer = pgb_malloc(entry->original_size);
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

                                        uint8_t* src_ptr = (uint8_t*)decompressed_buffer;
                                        uint8_t* dst_ptr = new_pixel_data;

                                        for (int y = 0; y < entry->height; ++y)
                                        {
                                            memcpy(dst_ptr, src_ptr, entry->rowbytes);
                                            src_ptr += entry->rowbytes;
                                            dst_ptr += new_rowbytes;
                                        }

                                        if (entry->has_mask && new_mask_data)
                                        {
                                            dst_ptr = new_mask_data;
                                            for (int y = 0; y < entry->height; ++y)
                                            {
                                                memcpy(dst_ptr, src_ptr, entry->rowbytes);
                                                src_ptr += entry->rowbytes;
                                                dst_ptr += new_rowbytes;
                                            }
                                        }

                                        PGB_App->coverArtCache.art.bitmap = new_bitmap;
                                        PGB_App->coverArtCache.art.original_width = entry->width;
                                        PGB_App->coverArtCache.art.original_height = entry->height;
                                        PGB_App->coverArtCache.art.scaled_width = entry->width;
                                        PGB_App->coverArtCache.art.scaled_height = entry->height;
                                        PGB_App->coverArtCache.art.status = PGB_COVER_ART_SUCCESS;
                                        PGB_App->coverArtCache.rom_path =
                                            string_copy(selectedGame->fullpath);
                                        foundInCache = true;
                                    }
                                }
                                else
                                {
                                    playdate->system->logToConsole(
                                        "LZ4 decompression failed for %s", entry->rom_path
                                    );
                                }
                                pgb_free(decompressed_buffer);
                            }

                            if (foundInCache)
                                break;
                        }
                    }
                }

                if (!foundInCache && selectedGame->coverPath != NULL)
                {
                    PGB_App->coverArtCache.art = pgb_load_and_scale_cover_art_from_path(
                        selectedGame->coverPath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT
                    );
                    PGB_App->coverArtCache.rom_path = string_copy(selectedGame->fullpath);
                }
            }
        }

        int screenWidth = playdate->display->getWidth();
        int screenHeight = playdate->display->getHeight();

        int rightPanelWidth = THUMBNAIL_WIDTH + 1;

        // use actual thumbnail width if possible
        if (PGB_App->coverArtCache.art.status == PGB_COVER_ART_SUCCESS &&
            PGB_App->coverArtCache.art.bitmap != NULL)
        {
            playdate->graphics->getBitmapData(
                PGB_App->coverArtCache.art.bitmap, &rightPanelWidth, NULL, NULL, NULL, NULL
            );
            if (rightPanelWidth >= THUMBNAIL_WIDTH - 1)
                rightPanelWidth = THUMBNAIL_WIDTH;
            rightPanelWidth++;
        }

        int leftPanelWidth = screenWidth - rightPanelWidth;

        libraryScene->listView->needsDisplay = needsDisplay;
        libraryScene->listView->frame = PDRectMake(0, 0, leftPanelWidth, screenHeight);

        PGB_ListView_update(libraryScene->listView);

#ifdef TARGET_SIMULATOR
        while (page_advance > 0)
        {
            --page_advance;
            PGB_App->buttons_pressed = kButtonDown;
            PGB_ListView_update(libraryScene->listView);
        }
        while (page_advance < 0)
        {
            ++page_advance;
            PGB_App->buttons_pressed = kButtonUp;
            PGB_ListView_update(libraryScene->listView);
        }
#endif

        PGB_ListView_draw(libraryScene->listView);

        if (needsDisplay || libraryScene->listView->needsDisplay || selectionChanged)
        {
            libraryScene->lastSelectedItem = selectedIndex;

            playdate->graphics->fillRect(
                leftPanelWidth + 1, 0, rightPanelWidth - 1, screenHeight, kColorWhite
            );

            if (selectedIndex >= 0 && selectedIndex < libraryScene->games->length)
            {
                if (PGB_App->coverArtCache.art.status == PGB_COVER_ART_SUCCESS &&
                    PGB_App->coverArtCache.art.bitmap != NULL)
                {
                    int panel_content_width = rightPanelWidth - 1;
                    int coverX =
                        leftPanelWidth + 1 +
                        (panel_content_width - PGB_App->coverArtCache.art.scaled_width) / 2;
                    int coverY = (screenHeight - PGB_App->coverArtCache.art.scaled_height) / 2;

                    playdate->graphics->fillRect(
                        leftPanelWidth + 1, 0, rightPanelWidth - 1, screenHeight, kColorBlack
                    );
                    playdate->graphics->setDrawMode(kDrawModeCopy);
                    playdate->graphics->drawBitmap(
                        PGB_App->coverArtCache.art.bitmap, coverX, coverY, kBitmapUnflipped
                    );
                }
                else
                {
                    PGB_Game* selectedGame = libraryScene->games->items[selectedIndex];
                    bool had_error_loading =
                        PGB_App->coverArtCache.art.status != PGB_COVER_ART_FILE_NOT_FOUND;

                    if (had_error_loading)
                    {
                        const char* message = "Error";
                        if (PGB_App->coverArtCache.art.status == PGB_COVER_ART_ERROR_LOADING)
                        {
                            message = "Error loading image";
                        }
                        else if (PGB_App->coverArtCache.art.status == PGB_COVER_ART_INVALID_IMAGE)
                        {
                            message = "Invalid image";
                        }

                        playdate->graphics->setFont(PGB_App->bodyFont);
                        int textWidth = playdate->graphics->getTextWidth(
                            PGB_App->bodyFont, message, pgb_strlen(message), kUTF8Encoding, 0
                        );
                        int panel_content_width = rightPanelWidth - 1;
                        int textX = leftPanelWidth + 1 + (panel_content_width - textWidth) / 2;
                        int textY =
                            (screenHeight - playdate->graphics->getFontHeight(PGB_App->bodyFont)) /
                            2;

                        playdate->graphics->setDrawMode(kDrawModeFillBlack);
                        playdate->graphics->drawText(
                            message, pgb_strlen(message), kUTF8Encoding, textX, textY
                        );
                    }
                    else
                    {
                        if (libraryScene->coverDownloadState != COVER_DOWNLOAD_IDLE &&
                            libraryScene->coverDownloadState != COVER_DOWNLOAD_COMPLETE)
                        {
                            char message[32];

                            if (libraryScene->coverDownloadState == COVER_DOWNLOAD_NO_GAME_IN_DB &&
                                libraryScene->showCrc)
                            {
                                PGB_Game* selectedGame = libraryScene->games->items[selectedIndex];
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

                            playdate->graphics->setFont(PGB_App->bodyFont);
                            int textWidth = playdate->graphics->getTextWidth(
                                PGB_App->bodyFont, message, strlen(message), kUTF8Encoding, 0
                            );
                            int panel_content_width = rightPanelWidth - 1;
                            int textX = leftPanelWidth + 1 + (panel_content_width - textWidth) / 2;
                            int textY = (screenHeight -
                                         playdate->graphics->getFontHeight(PGB_App->bodyFont)) /
                                        2;
                            playdate->graphics->setDrawMode(kDrawModeFillBlack);
                            playdate->graphics->drawText(
                                message, strlen(message), kUTF8Encoding, textX, textY
                            );
                        }
                        else
                        {
                            PGB_Game* selectedGame = libraryScene->games->items[selectedIndex];
                            bool hasDBMatch = (selectedGame->names->name_database != NULL);

                            if (hasDBMatch)
                            {
                                static const char* title = "Missing Cover";
                                static const char* message1 = "Press Ⓑ to download.";
                                static const char* message2 = "- or -";
                                static const char* message3 = "Connect to a computer";
                                static const char* message4 = "and copy cover to:";
                                static const char* message5 = "Data/*crankboy/covers";

                                LCDFont* titleFont = PGB_App->bodyFont;
                                LCDFont* bodyFont = PGB_App->subheadFont;
                                int large_gap = 12;
                                int small_gap = 3;
                                int titleHeight = playdate->graphics->getFontHeight(titleFont);
                                int messageHeight = playdate->graphics->getFontHeight(bodyFont);
                                int containerHeight = titleHeight + large_gap + messageHeight +
                                                      large_gap + messageHeight + large_gap +
                                                      messageHeight + small_gap + messageHeight +
                                                      small_gap + messageHeight;
                                int containerY_start = (screenHeight - containerHeight) / 2;
                                int panel_content_width = rightPanelWidth - 1;

                                int titleX = leftPanelWidth + 1 +
                                             (panel_content_width -
                                              playdate->graphics->getTextWidth(
                                                  titleFont, title, strlen(title), kUTF8Encoding, 0
                                              )) /
                                                 2;
                                int message1_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message1, strlen(message1), kUTF8Encoding, 0
                                     )) /
                                        2;
                                int message2_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message2, strlen(message2), kUTF8Encoding, 0
                                     )) /
                                        2;
                                int message3_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message3, strlen(message3), kUTF8Encoding, 0
                                     )) /
                                        2;
                                int message4_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message4, strlen(message4), kUTF8Encoding, 0
                                     )) /
                                        2;
                                int message5_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message5, strlen(message5), kUTF8Encoding, 0
                                     )) /
                                        2;

                                int currentY = containerY_start;
                                playdate->graphics->setDrawMode(kDrawModeFillBlack);
                                playdate->graphics->setFont(titleFont);
                                playdate->graphics->drawText(
                                    title, strlen(title), kUTF8Encoding, titleX, currentY
                                );
                                currentY += titleHeight + large_gap;
                                playdate->graphics->setFont(bodyFont);
                                playdate->graphics->drawText(
                                    message1, strlen(message1), kUTF8Encoding, message1_X, currentY
                                );
                                currentY += messageHeight + large_gap;
                                playdate->graphics->drawText(
                                    message2, strlen(message2), kUTF8Encoding, message2_X, currentY
                                );
                                currentY += messageHeight + large_gap;
                                playdate->graphics->drawText(
                                    message3, strlen(message3), kUTF8Encoding, message3_X, currentY
                                );
                                currentY += messageHeight + small_gap;
                                playdate->graphics->drawText(
                                    message4, strlen(message4), kUTF8Encoding, message4_X, currentY
                                );
                                currentY += messageHeight + small_gap;
                                playdate->graphics->drawText(
                                    message5, strlen(message5), kUTF8Encoding, message5_X, currentY
                                );
                            }
                            else
                            {
                                static const char* title = "Missing Cover";
                                char message1[32];

                                if (libraryScene->showCrc)
                                {
                                    PGB_Game* selectedGame =
                                        libraryScene->games->items[selectedIndex];
                                    if (selectedGame->names->crc32 != 0)
                                    {
                                        snprintf(
                                            message1, sizeof(message1), "%08lX",
                                            (unsigned long)selectedGame->names->crc32
                                        );
                                    }
                                    else
                                    {
                                        snprintf(message1, sizeof(message1), "No CRC found");
                                    }
                                }
                                else
                                {
                                    snprintf(message1, sizeof(message1), "No database match");
                                }
                                static const char* message2 = "Connect to a computer";
                                static const char* message3 = "and copy cover to:";
                                static const char* message4 = "Data/*crankboy/covers";

                                LCDFont* titleFont = PGB_App->bodyFont;
                                LCDFont* bodyFont = PGB_App->subheadFont;
                                int large_gap = 12;
                                int small_gap = 3;
                                int titleHeight = playdate->graphics->getFontHeight(titleFont);
                                int messageHeight = playdate->graphics->getFontHeight(bodyFont);

                                int containerHeight = titleHeight + large_gap + messageHeight +
                                                      large_gap + messageHeight + small_gap +
                                                      messageHeight + small_gap + messageHeight;
                                int containerY_start = (screenHeight - containerHeight) / 2;
                                int panel_content_width = rightPanelWidth - 1;

                                int titleX = leftPanelWidth + 1 +
                                             (panel_content_width -
                                              playdate->graphics->getTextWidth(
                                                  titleFont, title, strlen(title), kUTF8Encoding, 0
                                              )) /
                                                 2;
                                int message1_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message1, strlen(message1), kUTF8Encoding, 0
                                     )) /
                                        2;
                                int message2_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message2, strlen(message2), kUTF8Encoding, 0
                                     )) /
                                        2;
                                int message3_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message3, strlen(message3), kUTF8Encoding, 0
                                     )) /
                                        2;
                                int message4_X =
                                    leftPanelWidth + 1 +
                                    (panel_content_width -
                                     playdate->graphics->getTextWidth(
                                         bodyFont, message4, strlen(message4), kUTF8Encoding, 0
                                     )) /
                                        2;

                                int currentY = containerY_start;
                                playdate->graphics->setDrawMode(kDrawModeFillBlack);
                                playdate->graphics->setFont(titleFont);
                                playdate->graphics->drawText(
                                    title, strlen(title), kUTF8Encoding, titleX, currentY
                                );
                                currentY += titleHeight + large_gap;
                                playdate->graphics->setFont(bodyFont);
                                playdate->graphics->drawText(
                                    message1, strlen(message1), kUTF8Encoding, message1_X, currentY
                                );
                                currentY += messageHeight + large_gap;
                                playdate->graphics->drawText(
                                    message2, strlen(message2), kUTF8Encoding, message2_X, currentY
                                );
                                currentY += messageHeight + small_gap;
                                playdate->graphics->drawText(
                                    message3, strlen(message3), kUTF8Encoding, message3_X, currentY
                                );
                                currentY += messageHeight + small_gap;
                                playdate->graphics->drawText(
                                    message4, strlen(message4), kUTF8Encoding, message4_X, currentY
                                );
                            }
                        }
                    }
                }

                int screenWidth = playdate->display->getWidth();
                int screenHeight = playdate->display->getHeight();
                int rightPanelWidth = THUMBNAIL_WIDTH + 1;
                if (PGB_App->coverArtCache.art.status == PGB_COVER_ART_SUCCESS &&
                    PGB_App->coverArtCache.art.bitmap != NULL)
                {
                    playdate->graphics->getBitmapData(
                        PGB_App->coverArtCache.art.bitmap, &rightPanelWidth, NULL, NULL, NULL, NULL
                    );
                    if (rightPanelWidth >= THUMBNAIL_WIDTH - 1)
                        rightPanelWidth = THUMBNAIL_WIDTH;
                    rightPanelWidth++;
                }
                int leftPanelWidth = screenWidth - rightPanelWidth;

                // Draw separator line
                playdate->graphics->drawLine(
                    leftPanelWidth, 0, leftPanelWidth, screenHeight, 1, kColorBlack
                );
            }
        }
    }
    else if (libraryScene->tab == PGB_LibrarySceneTabEmpty)
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
            static const char* message4_text2 = "Data/*.crankboy/games";

            static const char* message5_text = "(Filenames must end with .gb or .gbc)";

            playdate->graphics->clear(kColorWhite);

            int titleToMessageSpacing = 8;
            int messageLineSpacing = 4;
            int verticalOffset = 2;
            int textPartSpacing = 5;

            int titleHeight = playdate->graphics->getFontHeight(PGB_App->titleFont);
            int subheadHeight = playdate->graphics->getFontHeight(PGB_App->subheadFont);
            int messageHeight = playdate->graphics->getFontHeight(PGB_App->bodyFont);
            int compositeLineHeight = (subheadHeight + verticalOffset > messageHeight)
                                          ? (subheadHeight + verticalOffset)
                                          : messageHeight;

            int numWidth1 = playdate->graphics->getTextWidth(
                PGB_App->bodyFont, message2_num, strlen(message2_num), kUTF8Encoding, 0
            );
            int numWidth2 = playdate->graphics->getTextWidth(
                PGB_App->bodyFont, message3_num, strlen(message3_num), kUTF8Encoding, 0
            );
            int numWidth3 = playdate->graphics->getTextWidth(
                PGB_App->bodyFont, message4_num, strlen(message4_num), kUTF8Encoding, 0
            );
            int maxNumWidth = (numWidth1 > numWidth2) ? numWidth1 : numWidth2;
            maxNumWidth = (numWidth3 > maxNumWidth) ? numWidth3 : maxNumWidth;

            int textWidth4_part1 = playdate->graphics->getTextWidth(
                PGB_App->bodyFont, message4_text1, strlen(message4_text1), kUTF8Encoding, 0
            );
            int textWidth4_part2 = playdate->graphics->getTextWidth(
                PGB_App->subheadFont, message4_text2, strlen(message4_text2), kUTF8Encoding, 0
            );
            int totalInstructionWidth =
                maxNumWidth + 4 + textWidth4_part1 + textPartSpacing + textWidth4_part2;

            int titleX = (playdate->display->getWidth() -
                          playdate->graphics->getTextWidth(
                              PGB_App->titleFont, title, strlen(title), kUTF8Encoding, 0
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

            playdate->graphics->setFont(PGB_App->titleFont);
            playdate->graphics->drawText(title, strlen(title), kUTF8Encoding, titleX, titleY);

            playdate->graphics->setFont(PGB_App->bodyFont);
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
            playdate->graphics->setFont(PGB_App->subheadFont);
            int message3_text1_width = playdate->graphics->getTextWidth(
                PGB_App->bodyFont, message3_text1, strlen(message3_text1), kUTF8Encoding, 0
            );
            playdate->graphics->drawText(
                message3_text2, strlen(message3_text2), kUTF8Encoding,
                textColX + message3_text1_width + textPartSpacing, message3_Y + verticalOffset
            );

            playdate->graphics->setFont(PGB_App->bodyFont);
            playdate->graphics->drawText(
                message4_num, strlen(message4_num), kUTF8Encoding, numColX, message4_Y
            );
            playdate->graphics->drawText(
                message4_text1, strlen(message4_text1), kUTF8Encoding, textColX, message4_Y
            );
            playdate->graphics->setFont(PGB_App->subheadFont);
            int message4_text1_width = playdate->graphics->getTextWidth(
                PGB_App->bodyFont, message4_text1, strlen(message4_text1), kUTF8Encoding, 0
            );
            playdate->graphics->drawText(
                message4_text2, strlen(message4_text2), kUTF8Encoding,
                textColX + message4_text1_width + textPartSpacing, message4_Y + verticalOffset
            );

            playdate->graphics->setFont(PGB_App->bodyFont);
            playdate->graphics->drawText(
                message5_text, strlen(message5_text), kUTF8Encoding, textColX, message5_Y
            );
        }
    }
    libraryScene->initialLoadComplete = true;
}

static void PGB_LibraryScene_showSettings(void* userdata)
{
    PGB_SettingsScene* settingsScene = PGB_SettingsScene_new(NULL);
    PGB_presentModal(settingsScene->scene);
}

static void PGB_LibraryScene_menu(void* object)
{
    playdate->system->addMenuItem("Credits", PGB_showCredits, object);

    playdate->system->addMenuItem("Settings", PGB_LibraryScene_showSettings, object);
}

static void PGB_LibraryScene_free(void* object)
{
    PGB_LibraryScene* libraryScene = object;

    PGB_Scene_free(libraryScene->scene);

    PGB_ListView_free(libraryScene->listView);

    if (libraryScene->coverDownloadMessage)
    {
        pgb_free(libraryScene->coverDownloadMessage);
    }

    if (libraryScene->activeCoverDownloadConnection)
    {
        http_cancel_and_cleanup(libraryScene->activeCoverDownloadConnection);
        libraryScene->activeCoverDownloadConnection = NULL;
    }

    pgb_free(libraryScene);
}

static void set_display_and_sort_name(PGB_Game* game)
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

PGB_Game* PGB_Game_new(PGB_GameName* cachedName, PGB_Array* available_covers)
{
    PGB_Game* game = pgb_malloc(sizeof(PGB_Game));
    memset(game, 0, sizeof(PGB_Game));

    char* fullpath_str;
    playdate->system->formatString(&fullpath_str, "%s/%s", PGB_gamesPath, cachedName->filename);
    game->fullpath = fullpath_str;

    game->names = cachedName;
    set_display_and_sort_name(game);

    char* basename_no_ext = pgb_basename(cachedName->filename, true);

    char** found_cover_name_ptr = (char**)bsearch(
        &basename_no_ext, available_covers->items, available_covers->length, sizeof(char*),
        pgb_compare_strings
    );

    if (found_cover_name_ptr == NULL)
    {
        char* cleanName_no_ext = string_copy(basename_no_ext);
        pgb_sanitize_string_for_filename(cleanName_no_ext);
        found_cover_name_ptr = (char**)bsearch(
            &cleanName_no_ext, available_covers->items, available_covers->length, sizeof(char*),
            pgb_compare_strings
        );
        pgb_free(cleanName_no_ext);
    }

    if (found_cover_name_ptr)
    {
        const char* found_cover_name = *found_cover_name_ptr;
        playdate->system->formatString(
            &game->coverPath, "%s/%s.pdi", PGB_coversPath, found_cover_name
        );
    }
    else
    {
        game->coverPath = NULL;
    }

    pgb_free(basename_no_ext);

    return game;
}

void PGB_Game_free(PGB_Game* game)
{
    pgb_free(game->fullpath);
    pgb_free(game->coverPath);
    pgb_free(game);
}
