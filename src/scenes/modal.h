#pragma once

#include "../scene.h"

#define MODAL_MAX_OPTIONS 3

// pop-up boxes and such

typedef enum
{
    CB_MODAL_WARNING_NONE = 0,
    CB_MODAL_WARNING_TOP,
    CB_MODAL_WARNING_BOTTOM_LEFT,
    CB_MODAL_WARNING_BOTTOM_RIGHT,
    CB_MODAL_WARNING_BOTTOM_LR  // Both left and right
} CB_ModalWarningPosition;

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
    unsigned master_timer;  // ticks ever frame from modal start
    bool exit : 1;
    bool setup : 1;
    bool accept_on_dock : 1;
    bool cannot_dismiss : 1;  // can't press B to cancel
    int result;

    CB_ModalWarningPosition warning;

    LCDBitmap* dissolveMask;
    LCDBitmap* icon;
    bool icon_flashing : 1;
} CB_Modal;

CB_Modal* CB_Modal_new(char* text, char const* const* options, CB_ModalCallback callback, void* ud);
