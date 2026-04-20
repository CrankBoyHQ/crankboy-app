//
//  settings_scene.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#pragma once

#include "../scene.h"

typedef struct CB_CreditsScene
{
    CB_Scene* scene;

    json_value jcred;
    int* y_advance_by_item;
    float scroll;
    float time;
    float initial_wait;
    bool shouldDismiss;
    LCDBitmap* logo;
} CB_CreditsScene;

CB_CreditsScene* CB_CreditsScene_new(void);

void CB_showCredits(void* userdata);
