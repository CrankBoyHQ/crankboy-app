#include "cover_cache_scene.h"

#include "../../libs/lz4/lz4.h"
#include "../app.h"
#include "../utility.h"
#include "library_scene.h"

#define MAX_CACHE_SIZE_BYTES (3072 * 1024)  // 3MB

void CB_CoverCacheScene_update(void* object, uint32_t u32enc_dt);
void CB_CoverCacheScene_free(void* object);

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

CB_CoverCacheScene* CB_CoverCacheScene_new(void)
{
    CB_CoverCacheScene* cacheScene = cb_calloc(1, sizeof(CB_CoverCacheScene));

    cacheScene->scene = CB_Scene_new();
    cacheScene->scene->managedObject = cacheScene;
    cacheScene->scene->update = CB_CoverCacheScene_update;
    cacheScene->scene->free = CB_CoverCacheScene_free;
    cacheScene->scene->use_user_stack = false;

    cacheScene->state = kCoverCacheStateInit;
    cacheScene->current_index = 0;
    cacheScene->cache_size_bytes = 0;

    if (CB_App->coverCache == NULL)
    {
        CB_App->coverCache = array_new();
    }

    cacheScene->available_covers = array_new();
    cacheScene->games_with_covers = array_new();

    cacheScene->lz4_state = cb_malloc(LZ4_sizeofState());

    return cacheScene;
}

void CB_CoverCacheScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    CB_CoverCacheScene* cacheScene = object;
    float dt = UINT32_AS_FLOAT(u32enc_dt);

    switch (cacheScene->state)
    {
    case kCoverCacheStateInit:
    {
        playdate->file->listfiles(
            cb_gb_directory_path(CB_coversPath), collect_cover_filenames_callback, cacheScene->available_covers, 0
        );

        if (cacheScene->available_covers->length > 0)
        {
            qsort(
                cacheScene->available_covers->items, cacheScene->available_covers->length,
                sizeof(char*), cb_compare_strings
            );
        }

        if (CB_App->gameNameCache->length > 0)
        {
            cacheScene->progress_max_width =
                cb_calculate_progress_max_width(CB_App->subheadFont, PROGRESS_STYLE_PERCENT, 0);
            cacheScene->state = kCoverCacheStateBuildGameList;
        }
        else
        {
            cacheScene->state = kCoverCacheStateDone;
        }
        break;
    }

    case kCoverCacheStateBuildGameList:
    {
        if (cacheScene->current_index == 0)
        {
            array_reserve(CB_App->gameListCache, CB_App->gameNameCache->length);
        }

        if (cacheScene->current_index < CB_App->gameNameCache->length)
        {
            CB_GameName* cachedName = CB_App->gameNameCache->items[cacheScene->current_index];
            CB_Game* game = CB_Game_new(cachedName, cacheScene->available_covers);
            array_push(CB_App->gameListCache, game);

            char progress_suffix[20];
            int total = CB_App->gameNameCache->length;
            int percentage = (total > 0) ? ((float)cacheScene->current_index / total) * 100 : 99;

            if (percentage >= 100)
            {
                percentage = 99;
            }

            snprintf(progress_suffix, sizeof(progress_suffix), "%d%%", percentage);

            cb_draw_logo_screen_centered_split(
                CB_App->subheadFont, "Building Games List...", progress_suffix,
                cacheScene->progress_max_width
            );

            cacheScene->current_index++;
        }
        else
        {
            CB_App->gameListCacheIsSorted = false;
            cacheScene->state = kCoverCacheStateSort;
        }
        break;
    }

    case kCoverCacheStateSort:
    {
        cb_sort_games_array(CB_App->gameListCache);
        CB_App->gameListCacheIsSorted = true;
        cacheScene->current_index = 0;

        cacheScene->start_time_ms = playdate->system->getCurrentTimeMilliseconds();

        array_clear(cacheScene->games_with_covers);
        array_reserve(cacheScene->games_with_covers, CB_App->gameListCache->length);

        for (int i = 0; i < CB_App->gameListCache->length; ++i)
        {
            CB_Game* game = CB_App->gameListCache->items[i];
            if (game->coverPath)
            {
                array_push(cacheScene->games_with_covers, game);
            }
        }

        if (cacheScene->games_with_covers->length > 0)
        {
            cacheScene->state = kCoverCacheStateCaching;
        }
        else
        {
            cacheScene->state = kCoverCacheStateDone;
        }

        break;
    }

    case kCoverCacheStateCaching:
    {
        if (cacheScene->current_index < cacheScene->games_with_covers->length &&
            cacheScene->cache_size_bytes < MAX_CACHE_SIZE_BYTES)
        {
            CB_Game* game = cacheScene->games_with_covers->items[cacheScene->current_index];

            char progress_suffix[20];
            int total = cacheScene->games_with_covers->length;
            int percentage = (total > 0) ? ((float)cacheScene->current_index / total) * 100 : 99;

            if (percentage >= 100)
            {
                percentage = 99;
            }

            snprintf(progress_suffix, sizeof(progress_suffix), "%d%%", percentage);

            cb_draw_logo_screen_centered_split(
                CB_App->subheadFont, "Caching Covers...", progress_suffix,
                cacheScene->progress_max_width
            );

            const char* error = NULL;
            LCDBitmap* coverBitmap = playdate->graphics->loadBitmap(game->coverPath, &error);

            if (coverBitmap)
            {
                int width, height, rowbytes;
                uint8_t *mask_data, *pixel_data;
                playdate->graphics->getBitmapData(
                    coverBitmap, &width, &height, &rowbytes, &mask_data, &pixel_data
                );

                bool has_mask = (mask_data != NULL);
                size_t original_size = rowbytes * height;
                if (has_mask)
                {
                    original_size *= 2;
                }

                int max_dst_size = LZ4_compressBound(original_size);
                char* temp_compressed_buffer = cb_malloc(max_dst_size);

                if (temp_compressed_buffer)
                {
                    uint8_t* uncompressed_buffer = cb_malloc(original_size);
                    if (uncompressed_buffer)
                    {
                        memcpy(uncompressed_buffer, pixel_data, rowbytes * height);
                        if (has_mask)
                        {
                            memcpy(
                                uncompressed_buffer + (rowbytes * height), mask_data,
                                rowbytes * height
                            );
                        }

                        int compressed_size = LZ4_compress_fast_extState(
                            cacheScene->lz4_state, (const char*)uncompressed_buffer,
                            temp_compressed_buffer, original_size, max_dst_size, 1
                        );

                        cb_free(uncompressed_buffer);

                        if (compressed_size > 0 &&
                            (cacheScene->cache_size_bytes + compressed_size <=
                             MAX_CACHE_SIZE_BYTES))
                        {
                            char* final_buffer = cb_malloc(compressed_size);
                            if (final_buffer)
                            {
                                memcpy(final_buffer, temp_compressed_buffer, compressed_size);

                                CB_CoverCacheEntry* entry = cb_malloc(sizeof(CB_CoverCacheEntry));
                                if (!entry)
                                {
                                    cb_free(final_buffer);
                                }
                                else
                                {
                                    entry->rom_path = cb_strdup(game->fullpath);
                                    if (!entry->rom_path)
                                    {
                                        cb_free(entry);
                                        cb_free(final_buffer);
                                    }
                                    else
                                    {
                                        entry->compressed_data = final_buffer;
                                        entry->compressed_size = compressed_size;
                                        entry->original_size = original_size;
                                        entry->width = width;
                                        entry->height = height;
                                        entry->rowbytes = rowbytes;
                                        entry->has_mask = has_mask;

                                        array_push(CB_App->coverCache, entry);
                                        cacheScene->cache_size_bytes += compressed_size;
                                    }
                                }
                            }
                        }

                        cb_free(temp_compressed_buffer);
                    }
                    else
                    {
                        cb_free(temp_compressed_buffer);
                    }
                }

                playdate->graphics->freeBitmap(coverBitmap);
            }

            cacheScene->current_index++;
        }
        else
        {
            cacheScene->state = kCoverCacheStateDone;
        }
        break;
    }

    case kCoverCacheStateDone:
    {
        uint32_t end_time_ms = playdate->system->getCurrentTimeMilliseconds();
        float duration = (end_time_ms - cacheScene->start_time_ms) / 1000.0f;

        playdate->system->logToConsole(
            "Cover Caching Complete: %d covers cached, size: %lu bytes, took %.2f seconds.",
            CB_App->coverCache->length, (unsigned long)cacheScene->cache_size_bytes,
            (double)duration
        );

        CB_LibraryScene* libraryScene = CB_LibraryScene_new();
        CB_present(libraryScene->scene);
        break;
    }
    }
}

void CB_CoverCacheScene_free(void* object)
{
    CB_CoverCacheScene* cacheScene = object;

    if (cacheScene->available_covers)
    {
        for (int i = 0; i < cacheScene->available_covers->length; i++)
        {
            cb_free(cacheScene->available_covers->items[i]);
        }
        array_free(cacheScene->available_covers);
    }

    if (cacheScene->games_with_covers)
    {
        array_free(cacheScene->games_with_covers);
    }

    if (cacheScene->lz4_state)
    {
        cb_free(cacheScene->lz4_state);
    }

    CB_Scene_free(cacheScene->scene);
    cb_free(cacheScene);
}
