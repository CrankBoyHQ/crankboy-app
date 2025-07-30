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
#include "utility.h"

#include <stdio.h>

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
    float indicatorOffset;
    float indicatorHeight;
} CB_ListViewScroll;

typedef enum
{
    CB_ListViewItemTypeButton,
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
    LCDBitmap* coverImage;
    float textScrollOffset;
    bool needsTextScroll;
} CB_ListItemButton;

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

    float textScrollTime;
    float textScrollPause;
} CB_ListView;

CB_ListView* CB_ListView_new(void);

void CB_ListView_update(CB_ListView* listView);
void CB_ListView_draw(CB_ListView* listView);

void CB_ListView_reload(CB_ListView* listView);

void CB_ListView_free(CB_ListView* listView);

CB_ListItemButton* CB_ListItemButton_new(char* title);

void CB_ListItem_free(CB_ListItem* item);
void CB_ListItemButton_free(CB_ListItemButton* itemButton);

#endif /* listview_h */
