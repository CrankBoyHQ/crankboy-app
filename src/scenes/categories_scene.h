#pragma once

#include "../app.h"
#include "../listview.h"
#include "../romcategory.h"
#include "../scene.h"

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
    bool keyboard_result_handled : 1;
#endif

    bool dirty : 1;
    bool needs_rebuild : 1;
    bool dismiss : 1;
} CB_CategoriesScene;

CB_CategoriesScene* CB_CategoriesScene_new(void);
