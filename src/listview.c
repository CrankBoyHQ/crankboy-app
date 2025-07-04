//
//  listview.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 16/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "listview.h"

#include "app.h"

static PGB_ListItem* PGB_ListItem_new(void);
static void PGB_ListView_selectItem(PGB_ListView* listView, unsigned int index, bool animated);
static void PGB_ListItem_super_free(PGB_ListItem* item);

static int PGB_ListView_rowHeight = 32;
static int PGB_ListView_inset = 4;
static int PGB_ListView_scrollInset = 2;
static int PGB_ListView_scrollIndicatorWidth = 2;
static int PGB_ListView_scrollIndicatorMinHeight = 40;

static float PGB_ListView_repeatInterval1 = 0.15;
static float PGB_ListView_repeatInterval2 = 2;

static float PGB_ListView_crankResetMinTime = 2;
static float PGB_ListView_crankMinChange = 30;

PGB_ListView* PGB_ListView_new(void)
{
    PGB_ListView* listView = pgb_malloc(sizeof(PGB_ListView));
    listView->items = array_new();
    listView->frame = PDRectMake(0, 0, 200, 200);

    listView->contentSize = 0;
    listView->contentOffset = 0;

    listView->scroll = (PGB_ListViewScroll){.active = false,
                                            .start = 0,
                                            .end = 0,
                                            .time = 0,
                                            .duration = 0.15,
                                            .indicatorVisible = false,
                                            .indicatorOffset = 0,
                                            .indicatorHeight = 0};

    listView->selectedItem = -1;

    listView->direction = PGB_ListViewDirectionNone;

    listView->repeatLevel = 0;
    listView->repeatIncrementTime = 0;
    listView->repeatTime = 0;

    listView->crankChange = 0;
    listView->crankResetTime = 0;

    listView->model = (PGB_ListViewModel){.selectedItem = -1,
                                          .contentOffset = 0,
                                          .empty = true,
                                          .scrollIndicatorHeight = 0,
                                          .scrollIndicatorOffset = 0,
                                          .scrollIndicatorVisible = false};

    listView->textScrollTime = 0;
    listView->textScrollPause = 0;

    return listView;
}

void PGB_ListView_invalidateLayout(PGB_ListView* listView)
{

    int y = 0;

    for (int i = 0; i < listView->items->length; i++)
    {
        PGB_ListItem* item = listView->items->items[i];
        item->offsetY = y;
        y += item->height;
    }

    listView->contentSize = y;

    int scrollHeight = listView->frame.height - PGB_ListView_scrollInset * 2;

    bool indicatorVisible = false;
    if (listView->contentSize > listView->frame.height)
    {
        indicatorVisible = true;
    }
    listView->scroll.indicatorVisible = indicatorVisible;

    float indicatorHeight = 0;
    if (listView->contentSize > listView->frame.height && listView->frame.height != 0)
    {
        indicatorHeight = PGB_MAX(
            scrollHeight * (listView->frame.height / listView->contentSize),
            PGB_ListView_scrollIndicatorMinHeight
        );
    }
    listView->scroll.indicatorHeight = indicatorHeight;
}

void PGB_ListView_reload(PGB_ListView* listView)
{

    PGB_ListView_invalidateLayout(listView);

    int numberOfItems = listView->items->length;

    if (numberOfItems > 0)
    {
        if (listView->selectedItem < 0)
        {
            PGB_ListView_selectItem(listView, 0, false);
        }
        else if (listView->selectedItem >= numberOfItems)
        {
            PGB_ListView_selectItem(listView, numberOfItems - 1, false);
        }
        else
        {
            PGB_ListView_selectItem(listView, listView->selectedItem, false);
        }
    }
    else
    {
        listView->scroll.active = false;
        listView->contentOffset = 0;
        listView->selectedItem = -1;
    }

    listView->needsDisplay = true;
}

void PGB_ListView_update(PGB_ListView* listView)
{
    PDButtons pushed = PGB_App->buttons_pressed;
    PDButtons pressed = PGB_App->buttons_down;

    if (pushed & kButtonDown)
    {
        if (listView->items->length > 0)
        {
            int nextIndex = listView->selectedItem + 1;
            if (nextIndex >= listView->items->length)
            {
                nextIndex = 0;
            }
            PGB_ListView_selectItem(listView, nextIndex, true);
        }
    }
    else if (pushed & kButtonUp)
    {
        if (listView->items->length > 0)
        {
            int prevIndex = listView->selectedItem - 1;
            if (prevIndex < 0)
            {
                prevIndex = listView->items->length - 1;
            }
            PGB_ListView_selectItem(listView, prevIndex, true);
        }
    }

    listView->crankChange += PGB_App->crankChange;

    if (listView->crankChange != 0)
    {
        listView->crankResetTime += PGB_App->dt;
    }
    else
    {
        listView->crankResetTime = 0;
    }

    if (listView->crankChange > 0 && listView->crankChange >= PGB_ListView_crankMinChange)
    {
        if (listView->items->length > 0)
        {
            int nextIndex = listView->selectedItem + 1;
            if (nextIndex >= listView->items->length)
            {
                nextIndex = 0;
            }
            PGB_ListView_selectItem(listView, nextIndex, true);
            listView->crankChange = 0;
        }
    }
    else if (listView->crankChange < 0 && listView->crankChange <= (-PGB_ListView_crankMinChange))
    {
        if (listView->items->length > 0)
        {
            int prevIndex = listView->selectedItem - 1;
            if (prevIndex < 0)
            {
                prevIndex = listView->items->length - 1;
            }
            PGB_ListView_selectItem(listView, prevIndex, true);
            listView->crankChange = 0;
        }
    }

    if (listView->crankResetTime > PGB_ListView_crankResetMinTime)
    {
        listView->crankResetTime = 0;
        listView->crankChange = 0;
    }

    PGB_ListViewDirection old_direction = listView->direction;
    listView->direction = PGB_ListViewDirectionNone;

    if (pressed & kButtonUp)
    {
        listView->direction = PGB_ListViewDirectionUp;
    }
    else if (pressed & kButtonDown)
    {
        listView->direction = PGB_ListViewDirectionDown;
    }

    if (listView->direction == PGB_ListViewDirectionNone || listView->direction != old_direction)
    {
        listView->repeatIncrementTime = 0;
        listView->repeatLevel = 0;
        listView->repeatTime = 0;
    }
    else
    {
        listView->repeatIncrementTime += PGB_App->dt;

        float repeatInterval = PGB_ListView_repeatInterval1;
        if (listView->repeatLevel > 0)
        {
            repeatInterval = PGB_ListView_repeatInterval2;
        }

        if (listView->repeatIncrementTime >= repeatInterval)
        {
            listView->repeatLevel = PGB_MIN(3, listView->repeatLevel + 1);
            listView->repeatIncrementTime = fmodf(listView->repeatIncrementTime, repeatInterval);
        }

        if (listView->repeatLevel > 0)
        {
            listView->repeatTime += PGB_App->dt;

            float repeatRate = 0.16;

            if (listView->repeatLevel == 2)
            {
                repeatRate = 0.1;
            }
            else if (listView->repeatLevel == 3)
            {
                repeatRate = 0.05;
            }

            if (listView->repeatTime >= repeatRate)
            {
                listView->repeatTime = fmodf(listView->repeatTime, repeatRate);

                if (listView->direction == PGB_ListViewDirectionUp)
                {
                    if (listView->items->length > 0)
                    {
                        int prevIndex = listView->selectedItem - 1;
                        if (prevIndex < 0)
                        {
                            prevIndex = listView->items->length - 1;
                        }
                        PGB_ListView_selectItem(listView, prevIndex, true);
                    }
                }
                else if (listView->direction == PGB_ListViewDirectionDown)
                {
                    if (listView->items->length > 0)
                    {
                        int nextIndex = listView->selectedItem + 1;
                        if (nextIndex >= listView->items->length)
                        {
                            nextIndex = 0;
                        }
                        PGB_ListView_selectItem(listView, nextIndex, true);
                    }
                }
            }
        }
    }

    if (listView->scroll.active)
    {
        listView->scroll.time += PGB_App->dt;

        float progress =
            pgb_easeInOutQuad(fminf(1, listView->scroll.time / listView->scroll.duration));
        listView->contentOffset =
            listView->scroll.start + (listView->scroll.end - listView->scroll.start) * progress;

        if (listView->scroll.time >= listView->scroll.duration)
        {
            listView->scroll.time = 0;
            listView->scroll.active = false;
        }
    }

    float indicatorOffset = PGB_ListView_scrollInset;
    if (listView->contentSize > listView->frame.height)
    {
        int scrollHeight = listView->frame.height -
                           (PGB_ListView_scrollInset * 2 + listView->scroll.indicatorHeight);
        indicatorOffset =
            PGB_ListView_scrollInset +
            (listView->contentOffset / (listView->contentSize - listView->frame.height)) *
                scrollHeight;
    }
    listView->scroll.indicatorOffset = indicatorOffset;

    if (listView->selectedItem >= 0 && listView->selectedItem < listView->items->length)
    {
        PGB_ListItem* item = listView->items->items[listView->selectedItem];
        if (item->type == PGB_ListViewItemTypeButton)
        {
            PGB_ListItemButton* button = item->object;

            playdate->graphics->setFont(PGB_App->subheadFont);
            int textWidth = playdate->graphics->getTextWidth(
                PGB_App->subheadFont, button->title, strlen(button->title), kUTF8Encoding, 0
            );
            int availableWidth = listView->scroll.active
                                     ? listView->frame.width - (PGB_ListView_inset * 2)
                                     : listView->frame.width - PGB_ListView_inset -
                                           (PGB_ListView_scrollInset * 2) -
                                           (PGB_ListView_scrollIndicatorWidth * 2);

            button->needsTextScroll = (textWidth > availableWidth);

            if (button->needsTextScroll)
            {
                listView->textScrollTime += PGB_App->dt;

                // Pixels per second for scroll-to-end
                const float TEXT_SCROLL_BASE_SPEED_PPS = 50.0f;

                // Prevents super-fast scrolls for tiny overflows.
                const float MIN_SCROLL_DURATION = 0.75f;

                // Makes scroll-back duration 2/3 of scroll-to-end duration
                const float SCROLL_BACK_DURATION_FACTOR = 2.0f / 3.0f;

                float pauseAtStartDuration = 1.5f;
                float pauseAtEndDuration = 2.0f;

                float maxOffset = textWidth - availableWidth;

                if (maxOffset <= 0)
                {
                    button->textScrollOffset = 0.0f;
                }
                else
                {
                    float dynamicScrollToEndDuration = maxOffset / TEXT_SCROLL_BASE_SPEED_PPS;
                    float dynamicScrollToStartDuration =
                        dynamicScrollToEndDuration * SCROLL_BACK_DURATION_FACTOR;
                    ;

                    if (dynamicScrollToEndDuration < MIN_SCROLL_DURATION)
                    {
                        dynamicScrollToEndDuration = MIN_SCROLL_DURATION;
                    }
                    if (dynamicScrollToStartDuration < MIN_SCROLL_DURATION)
                    {
                        dynamicScrollToStartDuration = MIN_SCROLL_DURATION;
                    }

                    float totalCycleDuration = pauseAtStartDuration + dynamicScrollToEndDuration +
                                               pauseAtEndDuration + dynamicScrollToStartDuration;

                    float currentTimeInCycle = fmodf(listView->textScrollTime, totalCycleDuration);

                    if (currentTimeInCycle < pauseAtStartDuration)
                    {
                        button->textScrollOffset = 0.0f;
                    }
                    else if (currentTimeInCycle <
                             (pauseAtStartDuration + dynamicScrollToEndDuration))
                    {
                        float timeIntoScrollToEnd = currentTimeInCycle - pauseAtStartDuration;
                        float normalizedScrollProgress = 0.0f;
                        if (dynamicScrollToEndDuration > 0)
                        {
                            normalizedScrollProgress =
                                timeIntoScrollToEnd / dynamicScrollToEndDuration;
                        }

                        button->textScrollOffset =
                            pgb_easeInOutQuad(normalizedScrollProgress) * maxOffset;
                    }
                    else if (currentTimeInCycle < (pauseAtStartDuration +
                                                   dynamicScrollToEndDuration + pauseAtEndDuration))
                    {
                        button->textScrollOffset = maxOffset;
                    }
                    else  // Scrolling to start
                    {
                        float timeIntoScrollToStart =
                            currentTimeInCycle - (pauseAtStartDuration +
                                                  dynamicScrollToEndDuration + pauseAtEndDuration);
                        float normalizedScrollProgress = 0.0f;
                        if (dynamicScrollToStartDuration > 0)
                        {
                            normalizedScrollProgress =
                                timeIntoScrollToStart / dynamicScrollToStartDuration;
                        }

                        button->textScrollOffset =
                            (1.0f - pgb_easeInOutQuad(normalizedScrollProgress)) * maxOffset;
                    }
                }
                listView->needsDisplay = true;
            }
            else
            {
                button->textScrollOffset = 0;
            }
        }
    }
}

void PGB_ListView_draw(PGB_ListView* listView)
{
    bool needsDisplay = false;

    if (listView->model.empty || listView->needsDisplay ||
        listView->model.selectedItem != listView->selectedItem ||
        listView->model.contentOffset != listView->contentOffset ||
        listView->model.scrollIndicatorVisible != listView->scroll.indicatorVisible ||
        listView->model.scrollIndicatorOffset != listView->scroll.indicatorOffset ||
        listView->scroll.indicatorHeight != listView->scroll.indicatorHeight)
    {
        needsDisplay = true;
    }

    listView->needsDisplay = false;

    listView->model.empty = false;
    listView->model.selectedItem = listView->selectedItem;
    listView->model.contentOffset = listView->contentOffset;
    listView->model.scrollIndicatorVisible = listView->scroll.indicatorVisible;
    listView->model.scrollIndicatorOffset = listView->scroll.indicatorOffset;
    listView->model.scrollIndicatorHeight = listView->scroll.indicatorHeight;

    if (needsDisplay)
    {
        int listX = listView->frame.x;
        int listY = listView->frame.y;

        int screenWidth = playdate->display->getWidth();
        int rightPanelWidth = 241;
        int leftPanelWidth = screenWidth - rightPanelWidth;

        playdate->graphics->fillRect(
            listX, listY, listView->frame.width, listView->frame.height, kColorWhite
        );

        for (int i = 0; i < listView->items->length; i++)
        {
            PGB_ListItem* item = listView->items->items[i];

            int rowY = listY + item->offsetY - listView->contentOffset;

            if (rowY + item->height < listY)
            {
                continue;
            }
            if (rowY > listY + listView->frame.height)
            {
                break;
            }

            bool selected = (i == listView->selectedItem);

            if (selected)
            {
                playdate->graphics->fillRect(
                    listX, rowY, listView->frame.width, item->height, kColorBlack
                );
            }

            if (item->type == PGB_ListViewItemTypeButton)
            {
                PGB_ListItemButton* itemButton = item->object;

                if (selected)
                {
                    playdate->graphics->setDrawMode(kDrawModeFillWhite);
                }
                else
                {
                    playdate->graphics->setDrawMode(kDrawModeFillBlack);
                }

                int textX = listX + PGB_ListView_inset;
                int textY =
                    rowY + (float)(item->height -
                                   playdate->graphics->getFontHeight(PGB_App->subheadFont)) /
                               2;

                playdate->graphics->setFont(PGB_App->subheadFont);

                int rightSidePadding;

                if (listView->scroll.indicatorVisible)
                {
                    // If the scrollbar is visible, the padding must be wide enough
                    // to contain the scrollbar itself plus its inset.
                    rightSidePadding = PGB_ListView_scrollIndicatorWidth + PGB_ListView_scrollInset;
                }
                else
                {
                    // If no scrollbar, we just need a 1-pixel gap to avoid
                    // text touching the divider line on the right.
                    rightSidePadding = 1;
                }

                int maxTextWidth = listView->frame.width - PGB_ListView_inset - rightSidePadding;

                if (maxTextWidth < 0)
                {
                    maxTextWidth = 0;
                }

                playdate->graphics->setClipRect(textX, rowY, maxTextWidth, item->height);

                if (selected && itemButton->needsTextScroll)
                {
                    int scrolledX = textX - (int)itemButton->textScrollOffset;
                    playdate->graphics->drawText(
                        itemButton->title, strlen(itemButton->title), kUTF8Encoding, scrolledX,
                        textY
                    );
                }
                else
                {
                    playdate->graphics->drawText(
                        itemButton->title, strlen(itemButton->title), kUTF8Encoding, textX, textY
                    );
                }

                playdate->graphics->clearClipRect();

                playdate->graphics->setDrawMode(kDrawModeCopy);
            }
        }

        if (listView->scroll.indicatorVisible)
        {
            int indicatorLineWidth = 1;

            PDRect indicatorFillRect = PDRectMake(
                listView->frame.width - PGB_ListView_scrollInset -
                    PGB_ListView_scrollIndicatorWidth,
                listView->scroll.indicatorOffset, PGB_ListView_scrollIndicatorWidth,
                listView->scroll.indicatorHeight
            );
            PDRect indicatorBorderRect = PDRectMake(
                indicatorFillRect.x - indicatorLineWidth, indicatorFillRect.y - indicatorLineWidth,
                indicatorFillRect.width + indicatorLineWidth * 2,
                indicatorFillRect.height + indicatorLineWidth * 2
            );

            pgb_drawRoundRect(indicatorBorderRect, 2, indicatorLineWidth, kColorWhite);
            pgb_fillRoundRect(indicatorFillRect, 2, kColorBlack);
        }
    }
}

static void PGB_ListView_selectItem(PGB_ListView* listView, unsigned int index, bool animated)
{

    PGB_ListItem* item = listView->items->items[index];

    int listHeight = listView->frame.height;

    int centeredOffset = 0;

    if (listView->contentSize > listHeight)
    {
        centeredOffset =
            item->offsetY - ((float)listHeight / 2 - (float)PGB_ListView_rowHeight / 2);
        centeredOffset = PGB_MAX(0, centeredOffset);
        centeredOffset = PGB_MIN(centeredOffset, listView->contentSize - listHeight);
    }

    if (animated)
    {
        listView->scroll.active = true;
        listView->scroll.start = listView->contentOffset;
        listView->scroll.end = centeredOffset;
        listView->scroll.time = 0;
    }
    else
    {
        listView->scroll.active = false;
        listView->contentOffset = centeredOffset;
    }

    listView->textScrollTime = 0;
    listView->textScrollPause = 0;

    if (listView->selectedItem >= 0 && listView->selectedItem < listView->items->length)
    {
        PGB_ListItem* oldItem = listView->items->items[listView->selectedItem];
        if (oldItem->type == PGB_ListViewItemTypeButton)
        {
            PGB_ListItemButton* button = oldItem->object;
            button->textScrollOffset = 0;
        }
    }

    listView->selectedItem = index;
}

void PGB_ListView_free(PGB_ListView* listView)
{

    array_free(listView->items);
    pgb_free(listView);
}

static PGB_ListItem* PGB_ListItem_new(void)
{
    PGB_ListItem* item = pgb_malloc(sizeof(PGB_ListItem));
    return item;
}

PGB_ListItemButton* PGB_ListItemButton_new(char* title)
{

    PGB_ListItem* item = PGB_ListItem_new();

    PGB_ListItemButton* buttonItem = pgb_malloc(sizeof(PGB_ListItemButton));
    buttonItem->item = item;

    item->type = PGB_ListViewItemTypeButton;
    item->object = buttonItem;

    item->height = PGB_ListView_rowHeight;

    buttonItem->title = string_copy(title);
    buttonItem->coverImage = NULL;
    buttonItem->textScrollOffset = 0.0f;
    buttonItem->needsTextScroll = false;

    return buttonItem;
}

static void PGB_ListItem_super_free(PGB_ListItem* item)
{
    pgb_free(item);
}

void PGB_ListItemButton_free(PGB_ListItemButton* itemButton)
{
    PGB_ListItem_super_free(itemButton->item);

    pgb_free(itemButton->title);

    if (itemButton->coverImage != NULL)
    {
        playdate->graphics->freeBitmap(itemButton->coverImage);
    }

    pgb_free(itemButton);
}

void PGB_ListItem_free(PGB_ListItem* item)
{
    if (item->type == PGB_ListViewItemTypeButton)
    {
        PGB_ListItemButton_free(item->object);
    }
}
