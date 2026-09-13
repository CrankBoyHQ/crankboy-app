#pragma once

#include "../scene.h"
#include "../listview.h"
#include "../app.h"
#include "../romcategory.h"

typedef enum
{
    CATSCENE_LIST,
    CATSCENE_EDIT
} CB_CategoriesSceneState;

typedef struct CB_CategoriesScene
{
    CB_Scene* scene;
    CB_ListView* listView;

    CB_CategoriesSceneState state;

    RomCategory* editing;

#ifdef CRANKBOY_PDKEYBOARD
    PDKeyboard* keyboard;
#endif

    bool dirty : 1;
    bool needs_rebuild : 1;
    bool dismiss : 1;
} CB_CategoriesScene;

CB_CategoriesScene* CB_CategoriesScene_new(void);
