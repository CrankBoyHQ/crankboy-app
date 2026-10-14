//
//  game.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef game_h
#define game_h

#include "array.h"

#include <stdbool.h>
#include <stdint.h>

struct CB_GameName;

typedef struct CB_Game
{
    char* fullpath;
    char* coverPath;

    const struct CB_GameName* names;

    char* displayName;
    char* sortName;

    void* cover_compressed_data;
    int cover_compressed_size;
    int cover_width;
    int cover_height;
    int cover_rowbytes;
    bool cover_has_mask;
    uint32_t cover_access_counter;
} CB_Game;

CB_Game* CB_Game_new(struct CB_GameName* cachedName, CB_Array* available_covers);
void CB_Game_free(CB_Game* game);

void CB_Game_set_display_and_sort_name(CB_Game* game);

#endif /* game_h */
