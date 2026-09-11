#pragma once

#include "../listview.h"
#include "../softpatch.h"
#include "library_scene.h"

typedef struct CB_PatchesScene
{
    CB_Scene* scene;
    CB_Game* game;
    SoftPatch* patches;
    char* patches_dir;
    CB_ListView* listView;
    bool dismiss : 1;
    float holdTime;
} CB_PatchesScene;

CB_PatchesScene* CB_PatchesScene_new(CB_Game* game);
