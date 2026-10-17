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

typedef struct
{
    CB_LoadedCoverArt art;
    char* rom_path;
} CB_GlobalCoverCache;

// loads the game's cover container from disk into memory (no-op if it is
// already loaded); returns byte size contributed to the cover cache
size_t CB_cover_load(CB_Game* game);

// drops any loaded container, returns its byte size (0 when not loaded)
size_t CB_cover_free(CB_Game* game);
size_t CB_cover_bytes(const CB_Game* game);
bool CB_cover_is_loaded(const CB_Game* game);

// decompresses the variant of choice into an LCDBitmap; io_buffer/size is
// a caller-owned scratch buffer reused across calls (grown on demand);
// returns NULL when nothing is loaded or decoding fails
LCDBitmap* CB_cover_decode(
    const CB_Game* game, int max_w, int max_h, void** io_buffer, size_t* io_buffer_size
);

// decodes the art at thumbnail size into the global art cache (clears the
// cache first, sets status + rom_path); returns decode success
bool CB_cover_fill_art_cache(CB_Game* game, void** io_buffer, size_t* io_buffer_size);

// finds <rom_filename>'s game (by full path) in the global game list;
// NULL when absent
CB_Game* CB_cover_game_by_rom_filename(const char* rom_filename);

// clears the failed-load marker after the cover file changed (download
// replace, cover delete, new game entry)
void CB_cover_mark_changed(CB_Game* game);

void cb_free_loaded_cover_art_bitmap(CB_LoadedCoverArt* art_result);
void cb_clear_global_cover_cache(void);

#endif /* covers_h */
