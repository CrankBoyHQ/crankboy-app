//
//  covers.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "covers.h"

#include "app.h"
#include "cvi.h"
#include "utility.h"

#include <string.h>

bool CB_cover_is_loaded(const CB_Game* game)
{
    return game->cvi != NULL;
}

size_t CB_cover_load(CB_Game* game)
{
    // failed-before files stay skipped until CB_cover_mark_changed says the file changed
    if (game->cvi || !game->coverPath || game->cover_load_failed)
        return 0;
    CB_CVI cvi;
    // bundled-catalog covers live under packed/ and need the forwarded path
    if (!CB_cvi_load(&cvi, CB_get_forwarded_path(game->coverPath)))
    {
        game->cover_load_failed = true;
        return 0;
    }
    game->cvi = (CB_CVI*)cb_malloc(sizeof(CB_CVI));
    if (!game->cvi)
    {
        CB_cvi_free(&cvi);
        return 0;
    }
    *game->cvi = cvi;
    return game->cvi->data_size;
}

void CB_cover_mark_changed(CB_Game* game)
{
    game->cover_load_failed = false;
}

size_t CB_cover_free(CB_Game* game)
{
    if (!game->cvi)
        return 0;
    size_t bytes = game->cvi->data_size;
    CB_cvi_free(game->cvi);
    cb_free(game->cvi);
    game->cvi = NULL;
    return bytes;
}

size_t CB_cover_bytes(const CB_Game* game)
{
    return game->cvi ? game->cvi->data_size : 0;
}

LCDBitmap* CB_cover_decode(
    const CB_Game* game, int max_w, int max_h, void** io_buffer, size_t* io_buffer_size
)
{
    if (!game->cvi)
        return NULL;
    const CB_CVIVariant* variant = CVI_pick_variant(game->cvi, max_w, max_h);
    if (!variant)
        return NULL;
    return CVI_decode_variant(variant, io_buffer, io_buffer_size);
}

bool CB_cover_fill_art_cache(CB_Game* game, void** io_buffer, size_t* io_buffer_size)
{
    cb_clear_global_cover_cache();

    LCDBitmap* new_bitmap =
        CB_cover_decode(game, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, io_buffer, io_buffer_size);
    if (new_bitmap)
    {
        int w, h;
        playdate->graphics->getBitmapData(new_bitmap, &w, &h, NULL, NULL, NULL);
        CB_App->coverArtCache.art.bitmap = new_bitmap;
        CB_App->coverArtCache.art.original_width = w;
        CB_App->coverArtCache.art.original_height = h;
        CB_App->coverArtCache.art.scaled_width = w;
        CB_App->coverArtCache.art.scaled_height = h;
        CB_App->coverArtCache.art.status = CB_COVER_ART_SUCCESS;
    }
    else
    {
        CB_App->coverArtCache.art.status = CB_COVER_ART_ERROR_LOADING;
    }
    CB_App->coverArtCache.rom_path = cb_strdup(game->fullpath);
    return new_bitmap != NULL;
}

CB_Game* CB_cover_game_by_rom_filename(const char* rom_filename)
{
    CB_Array* games = CB_App->gameListCache;
    if (!games || !rom_filename)
        return NULL;
    for (int i = 0; i < games->length; ++i)
    {
        CB_Game* game = games->items[i];
        if (game && strcmp(game->fullpath, rom_filename) == 0)
            return game;
    }
    return NULL;
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
