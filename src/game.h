//
//  game.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef game_h
#define game_h

#include "array.h"
#include "cvi.h"

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

    CB_CVI* cvi;             // container loaded on demand, NULL otherwise
    bool cover_load_failed;  // a load attempt failed; skip until file changes
    uint32_t cover_access_counter;
} CB_Game;

CB_Game* CB_Game_new(struct CB_GameName* cachedName, CB_Array* available_covers);
void CB_Game_free(CB_Game* game);

// resolves coverPath (must be NULL on entry); listing must be sorted
void CB_Game_assign_cover_path(
    CB_Game* game, const struct CB_GameName* cachedName, CB_Array* available_covers
);

void CB_Game_set_display_and_sort_name(CB_Game* game);

#endif /* game_h */
