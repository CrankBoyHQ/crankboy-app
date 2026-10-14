//
//  cover_download.c
//  CrankBoy
//
//  Cover download pipeline for the library scene: GitHub-based cover art
//  fetch, status tracking, and save-to-disk handling.
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "cover_download.h"

#include "../app.h"
#include "../http_safe.h"
#include "../utility.h"
#include "library_scene.h"

#include <string.h>

float cover_download_anim_timer = 0.0f;
int cover_download_anim_step = 0;

#define COVER_DOWNLOAD_MAX_RETRIES 3

typedef struct
{
    CB_LibraryScene* libraryScene;
    CB_Game* game;
    int retries;
} CoverDownloadUserdata;

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
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(net_wifi_unavailable));
        }
        goto cleanup;
    }

    if (flags & HTTP_NOT_FOUND)
    {
        if (stillOnSameGame)
        {
            set_download_status(libraryScene, COVER_DOWNLOAD_NO_GAME_IN_DB, T(cover_no_cover));
        }
        goto cleanup;
    }
    else if ((flags & ~HTTP_ENABLE_ASKED) != 0 || data == NULL || data_len == 0)
    {
        if (stillOnSameGame)
        {
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_download_failed));
        }
        goto cleanup;
    }

    const char* pdi_header = "Playdate IMG";
    char* actual_data_start = strstr(data, pdi_header);

    if (actual_data_start == NULL)
    {
        if (stillOnSameGame)
        {
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_invalid_file));
        }
        goto cleanup;
    }

    size_t new_data_len = data_len - (actual_data_start - data);

    rom_basename_no_ext = cb_basename(game->names->filename, true);
    if (!rom_basename_no_ext)
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_internal_error));
        goto cleanup;
    }

    playdate->system->formatString(
        &cover_dest_path, "%s/%s.pdi", cb_gb_directory_path(CB_coversPath), rom_basename_no_ext
    );

    if (!cover_dest_path)
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_internal_error));
        goto cleanup;
    }

    // Safety: Ensure previous file is gone before writing
    playdate->file->unlink(cover_dest_path, 0);

    if (cb_write_entire_file(cover_dest_path, actual_data_start, new_data_len))
    {
        // Validate the downloaded PDI before committing it.
        const char* verify_error = NULL;
        LCDBitmap* verify_bitmap =
            playdate->graphics->loadBitmap(CB_get_forwarded_path(cover_dest_path), &verify_error);

        if (!verify_bitmap)
        {
            if (verify_error)
            {
                playdate->system->logToConsole(
                    "Downloaded cover failed to load, unlinking %s: %s", cover_dest_path,
                    verify_error
                );
            }

            playdate->file->unlink(cover_dest_path, 0);

            if (userdata->retries < COVER_DOWNLOAD_MAX_RETRIES && stillOnSameGame)
            {
                cover_download_issue(libraryScene, game, userdata->retries + 1);
            }
            else if (stillOnSameGame)
            {
                set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_invalid_file));
            }
            goto cleanup;
        }
        playdate->graphics->freeBitmap(verify_bitmap);

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

            CB_CoverFlow_invalidateAll(libraryScene->coverFlow);

            set_download_status(libraryScene, COVER_DOWNLOAD_IDLE, NULL);
            CB_ListView_reload(libraryScene->listView);
        }
    }
    else
    {
        if (stillOnSameGame)
            set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_failed_save));
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

void cover_download_issue(CB_LibraryScene* libraryScene, CB_Game* game, int retries)
{
    set_download_status(libraryScene, COVER_DOWNLOAD_SEARCHING, T(cover_searching));

    if (game->names->name_database == NULL)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_NO_GAME_IN_DB, T(cover_no_cover_found));
        return;
    }

    char* encoded_name = cb_url_encode_for_github_raw(game->names->name_database);
    if (!encoded_name)
    {
        set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_internal_error));
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
        set_download_status(libraryScene, COVER_DOWNLOAD_FAILED, T(cover_internal_error));
        return;
    }

    set_download_status(libraryScene, COVER_DOWNLOAD_DOWNLOADING, T(cover_downloading_long));

    cover_download_anim_timer = 0.0f;
    cover_download_anim_step = 0;
    libraryScene->scene->forceFullRefresh = true;

    CoverDownloadUserdata* userdata = cb_malloc(sizeof(CoverDownloadUserdata));
    userdata->libraryScene = libraryScene;
    userdata->game = game;
    userdata->retries = retries;

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
        T(net_cover_art_reason), on_cover_download_finished, 15000, userdata
    );

    cb_free(url_path);
}

void cover_download_start_selected(CB_LibraryScene* libraryScene)
{
    int selectedIndex = libraryScene->listView->selectedItem;

    if (selectedIndex < 0 || selectedIndex >= libraryScene->games->length)
    {
        return;
    }

    CB_Game* game = libraryScene->games->items[selectedIndex];

    cover_download_issue(libraryScene, game, 0);
}
