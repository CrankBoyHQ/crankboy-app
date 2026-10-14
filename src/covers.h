//
//  covers.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef covers_h
#define covers_h

#include "game.h"

#include <pd_api.h>

typedef enum
{
    CB_COVER_ART_SUCCESS,
    CB_COVER_ART_ERROR_LOADING,
    CB_COVER_ART_INVALID_IMAGE,
    CB_COVER_ART_FILE_NOT_FOUND
} CB_CoverArtStatus;

typedef struct
{
    LCDBitmap* bitmap;
    int original_width;
    int original_height;
    int scaled_width;
    int scaled_height;
    CB_CoverArtStatus status;
} CB_LoadedCoverArt;

// cache for the currently selected game's cover
typedef struct
{
    CB_LoadedCoverArt art;
    char* rom_path;
} CB_GlobalCoverCache;

void CB_cover_compress(
    CB_Game* game, void* lz4_state, size_t* io_cache_bytes, int* io_cached_count
);
void CB_cover_free_compressed(CB_Game* game);

LCDBitmap* CB_decompress_game_cover(CB_Game* game, void** io_buffer, size_t* io_buffer_size);

CB_LoadedCoverArt cb_load_and_scale_cover_art_from_path(
    const char* cover_path, int max_target_width, int max_target_height
);

void cb_free_loaded_cover_art_bitmap(CB_LoadedCoverArt* art_result);
void cb_clear_global_cover_cache(void);

#endif /* covers_h */
