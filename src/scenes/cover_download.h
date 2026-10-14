//
//  cover_download.h
//  CrankBoy
//
//  Cover download pipeline for the library scene: GitHub-based cover art
//  fetch, status tracking, and save-to-disk handling.
//
//  Maintained and developed by the CrankBoy dev team.
//

#pragma once

#include "../game.h"

struct CB_LibraryScene;

typedef enum
{
    COVER_DOWNLOAD_IDLE,
    COVER_DOWNLOAD_SEARCHING,
    COVER_DOWNLOAD_DOWNLOADING,
    COVER_DOWNLOAD_FAILED,
    COVER_DOWNLOAD_NO_GAME_IN_DB,
    COVER_DOWNLOAD_COMPLETE
} CoverDownloadState;

// download the cover for `game` (retry pass `retries`; first pass is 0)
void cover_download_issue(struct CB_LibraryScene* libraryScene, CB_Game* game, int retries);

void cover_download_start_selected(struct CB_LibraryScene* libraryScene);

// download dot animation clocks, driven by the host scene's update loop
extern float cover_download_anim_timer;
extern int cover_download_anim_step;
