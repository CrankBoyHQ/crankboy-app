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
    bool canClose;
    bool textIsStatic;
} CB_InfoScene;

CB_InfoScene* CB_InfoScene_new(const char* title, const char* text);
