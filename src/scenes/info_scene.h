#pragma once

#include "../scene.h"

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

    void (*complete_callback)(void);
    float min_dismiss_time;

    // one image per tagged line, in order.
    // TODO: expand this to support other kinds of images too
    LCDBitmap** qr_bitmaps;
    int qr_count;
} CB_InfoScene;

CB_InfoScene* CB_InfoScene_new(const char* title, const char* text);
