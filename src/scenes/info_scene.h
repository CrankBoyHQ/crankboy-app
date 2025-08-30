#pragma once

#include "../scene.h"
#include "pd_api.h"

// Just displays some text. Plain and simple.

typedef struct CB_InfoScene
{
    CB_Scene* scene;
    char* title;
    char* text;
    float scroll;
    bool dismiss : 1;
    bool canClose : 1;
    bool textIsStatic : 1;
} CB_InfoScene;

CB_InfoScene* CB_InfoScene_new(const char* title, const char* text);
