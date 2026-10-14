//
//  covers.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "covers.h"

#include "../../libs/lz4/lz4.h"
#include "app.h"
#include "utility.h"

#include <math.h>
#include <string.h>

CB_LoadedCoverArt cb_load_and_scale_cover_art_from_path(
    const char* cover_path, int max_target_width, int max_target_height
)
{
    CB_LoadedCoverArt result = {
        .bitmap = NULL,
        .original_width = 0,
        .original_height = 0,
        .scaled_width = 0,
        .scaled_height = 0,
        .status = CB_COVER_ART_FILE_NOT_FOUND
    };

    if (!cover_path)
    {
        result.status = CB_COVER_ART_FILE_NOT_FOUND;
        return result;
    }

    FileStat fileStatCheck;
    if (playdate->file->stat(cover_path, &fileStatCheck) != 0)
    {
        result.status = CB_COVER_ART_FILE_NOT_FOUND;
        return result;
    }

    const char* error_str = NULL;
    LCDBitmap* original_image =
        playdate->graphics->loadBitmap(CB_get_forwarded_path(cover_path), &error_str);

    if (error_str)
    {
        playdate->system->logToConsole(
            "Error string from loadBitmap for %s: %s", cover_path, error_str
        );
    }

    if (original_image == NULL)
    {
        result.status = CB_COVER_ART_ERROR_LOADING;
        playdate->system->logToConsole("Failed to load bitmap: %s", cover_path);
        return result;
    }

    playdate->graphics->getBitmapData(
        original_image, &result.original_width, &result.original_height, NULL, NULL, NULL
    );

    if (result.original_width <= 0 || result.original_height <= 0)
    {
        playdate->graphics->freeBitmap(original_image);
        result.status = CB_COVER_ART_INVALID_IMAGE;
        playdate->system->logToConsole(
            "Invalid image dimensions (%dx%d) for: %s", result.original_width,
            result.original_height, cover_path
        );
        return result;
    }

    float scale;
    float scaleX = (float)max_target_width / result.original_width;
    float scaleY = (float)max_target_height / result.original_height;
    scale = (scaleX < scaleY) ? scaleX : scaleY;

    result.scaled_width = (int)roundf(result.original_width * scale);
    result.scaled_height = (int)roundf(result.original_height * scale);

    if (result.scaled_width < 1 && result.original_width > 0)
        result.scaled_width = 1;
    if (result.scaled_height < 1 && result.original_height > 0)
        result.scaled_height = 1;

    bool perform_scaling_operation = false;
    if (result.scaled_width != result.original_width ||
        result.scaled_height != result.original_height)
    {
        perform_scaling_operation = true;
    }

    if (perform_scaling_operation)
    {
        if (result.scaled_width <= 0 || result.scaled_height <= 0)
        {
            playdate->system->logToConsole(
                "Error: Calculated scaled dimensions are zero or negative "
                "(%dx%d) for %s. Original: %dx%d, Scale: %f",
                result.scaled_width, result.scaled_height, cover_path, result.original_width,
                result.original_height, (double)scale
            );
            playdate->graphics->freeBitmap(original_image);
            result.status = CB_COVER_ART_INVALID_IMAGE;
            return result;
        }

        LCDBitmap* scaled_bitmap =
            playdate->graphics->newBitmap(result.scaled_width, result.scaled_height, kColorClear);
        if (scaled_bitmap == NULL)
        {
            playdate->graphics->freeBitmap(original_image);
            result.status = CB_COVER_ART_ERROR_LOADING;
            playdate->system->logToConsole(
                "Failed to create new scaled bitmap (%dx%d) for: %s", result.scaled_width,
                result.scaled_height, cover_path
            );
            return result;
        }

        playdate->graphics->pushContext(scaled_bitmap);
        playdate->graphics->setDrawMode(kDrawModeCopy);
        playdate->graphics->drawScaledBitmap(original_image, 0, 0, scale, scale);
        playdate->graphics->popContext();

        playdate->graphics->freeBitmap(original_image);
        result.bitmap = scaled_bitmap;
    }
    else
    {
        result.bitmap = original_image;
    }

    result.status = CB_COVER_ART_SUCCESS;
    return result;
}

void cb_free_loaded_cover_art_bitmap(CB_LoadedCoverArt* art_result)
{
    if (art_result && art_result->bitmap)
    {
        playdate->graphics->freeBitmap(art_result->bitmap);
        art_result->bitmap = NULL;
    }
}

void cb_clear_global_cover_cache(void)
{
    if (CB_App->coverArtCache.rom_path)
    {
        cb_free(CB_App->coverArtCache.rom_path);
        CB_App->coverArtCache.rom_path = NULL;
    }
    cb_free_loaded_cover_art_bitmap(&CB_App->coverArtCache.art);

    CB_App->coverArtCache.art.status = CB_COVER_ART_FILE_NOT_FOUND;
}

// compress a game's disk cover into an LZ4 snapshot (pixel + optional mask
// plane, row-wise) so views decode from RAM instead of disk
void CB_cover_compress(CB_Game* game, void* lz4_state, size_t* io_cache_bytes, int* io_cached_count)
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

void CB_cover_free_compressed(CB_Game* game)
{
    if (game->cover_compressed_data)
    {
        cb_free(game->cover_compressed_data);
        game->cover_compressed_data = NULL;
    }
}

// decompress a game's cached cover bitmap; io_buffer is a caller-owned
// scratch buffer reused across calls (grown on demand)
LCDBitmap* CB_decompress_game_cover(CB_Game* game, void** io_buffer, size_t* io_buffer_size)
{
    if (!game->cover_compressed_data || !io_buffer || !io_buffer_size)
        return NULL;

    int original_size = game->cover_rowbytes * game->cover_height;
    if (game->cover_has_mask)
        original_size *= 2;

    if (*io_buffer_size < (size_t)original_size)
    {
        char* new_buffer = cb_realloc(*io_buffer, original_size);
        if (!new_buffer)
            return NULL;
        *io_buffer = new_buffer;
        *io_buffer_size = original_size;
    }

    char* decompressed_buffer = *io_buffer;
    if (!decompressed_buffer)
        return NULL;

    int decompressed_size = LZ4_decompress_safe(
        game->cover_compressed_data, decompressed_buffer, game->cover_compressed_size, original_size
    );
    if (decompressed_size != original_size)
    {
        playdate->system->logToConsole("LZ4 decompression failed for %s", game->fullpath);
        return NULL;
    }

    LCDBitmap* new_bitmap = NULL;
    if (game->cover_has_mask)
    {
        new_bitmap =
            playdate->graphics->newBitmap(game->cover_width, game->cover_height, kColorClear);
    }
    else
    {
        new_bitmap =
            playdate->graphics->newBitmap(game->cover_width, game->cover_height, kColorWhite);
    }
    if (!new_bitmap)
        return NULL;

    int new_rowbytes;
    uint8_t *new_pixel_data, *new_mask_data;
    playdate->graphics->getBitmapData(
        new_bitmap, NULL, NULL, &new_rowbytes, &new_mask_data, &new_pixel_data
    );
    size_t copy_bytes = ((size_t)game->cover_rowbytes < (size_t)new_rowbytes)
                            ? game->cover_rowbytes
                            : (size_t)new_rowbytes;

    uint8_t* src_ptr = (uint8_t*)decompressed_buffer;
    uint8_t* dst_ptr = new_pixel_data;

    for (int y = 0; y < game->cover_height; ++y)
    {
        memcpy(dst_ptr, src_ptr, copy_bytes);
        src_ptr += game->cover_rowbytes;
        dst_ptr += new_rowbytes;
    }

    if (game->cover_has_mask && new_mask_data)
    {
        dst_ptr = new_mask_data;
        for (int y = 0; y < game->cover_height; ++y)
        {
            memcpy(dst_ptr, src_ptr, copy_bytes);
            src_ptr += game->cover_rowbytes;
            dst_ptr += new_rowbytes;
        }
    }

    return new_bitmap;
}
