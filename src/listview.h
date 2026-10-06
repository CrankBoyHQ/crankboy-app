//
//  listview.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 16/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef listview_h
#define listview_h

#include "array.h"

#include <pd_api.h>

typedef struct
{
    bool empty;
    int contentOffset;
    int selectedItem;
    bool scrollIndicatorVisible;
    int scrollIndicatorOffset;
    int scrollIndicatorHeight;
} CB_ListViewModel;

typedef struct
{
    bool active;
    int start;
    int end;
    float time;
    float duration;
    bool indicatorVisible;
    int indicatorOffset;
    int indicatorHeight;
} CB_ListViewScroll;

typedef enum
{
    CB_ListViewItemTypeButton,
    CB_ListViewItemTypeCheckbox,
    CB_ListViewItemTypeSwitch
} CB_ListItemType;

typedef enum
{
    CB_ListViewDirectionNone,
    CB_ListViewDirectionUp,
    CB_ListViewDirectionDown
} CB_ListViewDirection;

typedef struct
{
    CB_ListItemType type;
    void* object;
    int height;
    int offsetY;
} CB_ListItem;

typedef struct
{
    CB_ListItem item;
    char* title;
    float textScrollOffset;
    bool needsTextScroll : 1;
    bool is_header : 1;
    bool unselectable : 1;
    bool disabled : 1;
    union
    {
        void* ptr;
        uintptr_t uint;
    } ud;
} CB_ListItemButton;

typedef struct
{
    CB_ListItem item;
    char* title;
    float textScrollOffset;
    bool needsTextScroll;
    bool checked;
    union
    {
        void* ptr;
        uintptr_t uint;
    } ud;
} CB_ListItemCheckbox;

typedef struct
{
    CB_Array* items;
    CB_ListViewModel model;
    int selectedItem;

    int contentOffset;
    int contentSize;

    CB_ListViewScroll scroll;
    CB_ListViewDirection direction;
    int repeatLevel;
    float repeatIncrementTime;
    float repeatTime;
    float crankChange;
    float crankResetTime;
    bool needsDisplay;
    PDRect frame;

    int paddingTop;
    int paddingBottom;
    int textInset;

    float textScrollTime;
    float textScrollPause;

    bool hideScrollIndicator;
    bool ignoreButtons;
    bool checkboxDrag;
    LCDFont* font;
} CB_ListView;

// hold-A drag reorder state (per scene instance)
#define CB_ListView_DRAG_HOLD_TIME 0.25f

typedef struct
{
    float hold_time;
    bool dragging;
} CB_ListViewDragState;

CB_ListView* CB_ListView_new(void);

void CB_ListView_update(CB_ListView* listView);
void CB_ListView_draw(CB_ListView* listView);

void CB_ListView_invalidateLayout(CB_ListView* listView);

void CB_ListView_reload(CB_ListView* listView);

void CB_ListView_selectItem(CB_ListView* listView, int index, bool animated);

void CB_ListView_free(CB_ListView* listView);

void CB_ListView_clear(CB_ListView* listView);
// shared hold-A reorder skeleton; sets ignoreButtons/checkboxDrag,
// *ydir = -1/0/+1. Returns true on a short A tap (released early).
bool CB_ListView_drag_update(
    CB_ListView* listView, CB_ListViewDragState* state, float dt, uint32_t buttons_down,
    uint32_t buttons_pressed, uint32_t buttons_released, int* ydir
);

CB_ListItemButton* CB_ListItemButton_new(const char* title);
CB_ListItemCheckbox* CB_ListItemCheckbox_new(const char* title);

void CB_ListItemCheckbox_swap(CB_ListItemCheckbox* a, CB_ListItemCheckbox* b);

void CB_ListItem_free(CB_ListItem* item);
void CB_ListItemButton_free(CB_ListItemButton* itemButton);

#endif /* listview_h */
