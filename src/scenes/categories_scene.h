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

    // category row to restore after the next rebuild
    RomCategory* select_on_rebuild;

    // adjusted name shown in a modal once the keyboard closes
    char* pending_name_notice;

#ifdef CRANKBOY_PDKEYBOARD
    PDKeyboard* keyboard;
    bool keyboard_result_handled : 1;
#endif

    bool dirty : 1;
    bool needs_rebuild : 1;
    bool dismiss : 1;
    bool modal_input_guard : 1;

    // hold-A drag reorder in list state
    CB_ListViewDragState drag;
} CB_CategoriesScene;

CB_CategoriesScene* CB_CategoriesScene_new(void);
