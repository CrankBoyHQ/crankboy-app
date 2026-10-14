//
//  game.c
//  CrankBoy
//
//  Game model lifecycle: construction from cached name metadata, cover
//  path resolution, display/sort-name derivation, and teardown.
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "game.h"

#include "app.h"
#include "covers.h"
#include "preferences.h"
#include "utility.h"

#include <stdlib.h>
#include <string.h>

void CB_Game_set_display_and_sort_name(CB_Game* game)
{
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

    char* fullpath_str = NULL;

    if (cachedName->fullpath)
    {
        fullpath_str = cb_strdup(cachedName->fullpath);
    }
#ifdef CRANKBOY_OFFICIAL_CATALOG
    else if (CB_App->packed_filenames && CB_App->packed_filenames->length > 0)
    {
        char** found = (char**)bsearch(
            &cachedName->filename, CB_App->packed_filenames->items,
            CB_App->packed_filenames->length, sizeof(char*), cb_compare_strings
        );
        if (found)
        {
            fullpath_str = aprintf("packed/%s", cachedName->filename);
        }
    }
#endif

    if (!fullpath_str)
    {
        char* games_dir = cb_system_directory_path_for_slug(cachedName->system_slug, CB_gamesPath);
        playdate->system->formatString(&fullpath_str, "%s/%s", games_dir, cachedName->filename);
        cb_free(games_dir);
    }

    game->fullpath = fullpath_str;

    game->names = cachedName;
    CB_Game_set_display_and_sort_name(game);

    char* basename_no_ext = cb_rom_basename(cachedName->filename);

    // available_covers lists gb's covers only
    bool other_system = cachedName->system_slug && strcmp(cachedName->system_slug, GB_SYSTEM_SLUG);
    if (other_system)
    {
        char* covers_dir =
            cb_system_directory_path_for_slug(cachedName->system_slug, CB_coversPath);
        char* path = aprintf("%s/%s.pdi", covers_dir, basename_no_ext);
        cb_free(covers_dir);
        if (cb_file_exists(path, kFileReadData | kFileRead))
            game->coverPath = path;
        else
            cb_free(path);
        cb_free(basename_no_ext);
        return game;
    }

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

#ifdef CRANKBOY_OFFICIAL_CATALOG
        if (strncmp(game->fullpath, "packed/", 7) == 0)
        {
            game->coverPath = aprintf("packed/%s.pdi", found_cover_name);
        }
        else
#endif
        {
            char* covers_dir =
                cb_system_directory_path_for_slug(cachedName->system_slug, CB_coversPath);
            playdate->system->formatString(
                &game->coverPath, "%s/%s.pdi", covers_dir, found_cover_name
            );
            cb_free(covers_dir);
        }
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
    CB_cover_free_compressed(game);
    cb_free(game);
}
