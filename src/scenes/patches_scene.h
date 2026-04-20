#pragma once

#include "../softpatch.h"
#include "library_scene.h"

typedef struct CB_PatchesScene
{
    CB_Scene* scene;
    CB_Game* game;
    SoftPatch* patches;
    char* patches_dir;
    bool dismiss : 1;
    bool didDrag : 1;
    unsigned selected;
} CB_PatchesScene;

CB_PatchesScene* CB_PatchesScene_new(CB_Game* game);
