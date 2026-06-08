#pragma once

#include "../scene.h"
#include "library_scene.h"

typedef struct CB_ManageRomScene
{
    CB_Scene* scene;
    CB_Game* game;

    float header_animation_p;
    bool started_without_header;
    bool is_dismissing;
    char header_name[64];

    int cursorIndex;
    int actionCount;
    int save_slot_at_open;
    bool dismiss;
    float filename_scroll_time;

    uint8_t mapper_byte;
    uint8_t cgb_flag;
    bool compressed;
    bool header_ok;
    char* basename;  // (with extension)

    // for emucore games
    char* core_info_text;
    char* core_header_name;
} CB_ManageRomScene;

CB_ManageRomScene* CB_ManageRomScene_new(CB_Game* game, float initial_header_p);