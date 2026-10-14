#pragma once

#include "../game.h"
#include "../listview.h"
#include "../scene.h"
#include "../softpatch.h"

typedef struct CB_PatchesScene
{
    CB_Scene* scene;
    CB_Game* game;
    SoftPatch* patches;
    char* patches_dir;
    CB_ListView* listView;
    bool dismiss : 1;
    CB_ListViewDragState drag;
} CB_PatchesScene;

CB_PatchesScene* CB_PatchesScene_new(CB_Game* game);
