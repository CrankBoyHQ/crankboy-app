#pragma once

#include "pd_api.h"
#include "scene.h"
#include "utility.h"

#define MODAL_MAX_OPTIONS 3

// pop-up boxes and such

struct CB_Modal;

// option is -1 if cancelled;
// otherwise, option is index in options[]
typedef void (*CB_ModalCallback)(void* ud, int option);

typedef struct CB_Modal
{
    CB_Scene* scene;
    void* ud;

    int cursorIndex;
    uint8_t lcd[LCD_ROWS * LCD_ROWSIZE];

    char* text;
    int options_count;
    int option_selected;
    int width, height, margin;
    char* options[MODAL_MAX_OPTIONS];
    CB_ModalCallback callback;
    int timer;
    int droptimer;
    unsigned master_timer; // ticks ever frame from modal start
    bool exit : 1;
    bool setup : 1;
    bool accept_on_dock : 1;
    bool warning : 1;
    int result;

    LCDBitmap* dissolveMask;
    LCDBitmap* icon;
} CB_Modal;

CB_Modal* CB_Modal_new(char* text, char const* const* options, CB_ModalCallback callback, void* ud);