//
//  listview.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 16/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "listview.h"

#include "app.h"

static CB_ListItem* CB_ListItem_new(void);
static void CB_ListView_selectItem(CB_ListView* listView, unsigned int index, bool animated);
static void CB_ListItem_super_free(CB_ListItem* item);

static int CB_ListView_rowHeight = 32;
static int CB_ListView_inset = 4;
static int CB_ListView_scrollInset = 2;
static int CB_ListView_scrollIndicatorWidth = 2;
static int CB_ListView_scrollIndicatorMinHeight = 40;

static float CB_ListView_repeatInterval1 = 0.15f;
static float CB_ListView_repeatInterval2 = 2.0f;

static float CB_ListView_crankResetMinTime = 2.0f;
static float CB_ListView_crankMinChange = 30.0f;

CB_ListView* CB_ListView_new(void)
{
    CB_ListView* listView = cb_malloc(sizeof(CB_ListView));
    listView->items = array_new();
    listView->frame = PDRectMake(0, 0, 200, 200);

    listView->contentSize = 0;
    listView->contentOffset = 0;

    listView->scroll = (CB_ListViewScroll){.active = false,
                                           .start = 0,
                                           .end = 0,
                                           .time = 0,
                                           .duration = 0.15,
                                           .indicatorVisible = false,
                                           .indicatorOffset = 0,
                                           .indicatorHeight = 0};

    listView->selectedItem = -1;

    listView->direction = CB_ListViewDirectionNone;

    listView->repeatLevel = 0;
    listView->repeatIncrementTime = 0;
    listView->repeatTime = 0;

    listView->crankChange = 0;
    listView->crankResetTime = 0;

    listView->model = (CB_ListViewModel){.selectedItem = -1,
                                         .contentOffset = 0,
                                         .empty = true,
                                         .scrollIndicatorHeight = 0,
                                         .scrollIndicatorOffset = 0,
                                         .scrollIndicatorVisible = false};

    listView->textScrollTime = 0;
    listView->textScrollPause = 0;

    return listView;
}

void CB_ListView_invalidateLayout(CB_ListView* listView)
{

    int y = 0;

    for (int i = 0; i < listView->items->length; i++)
    {
        CB_ListItemButton* button = listView->items->items[i];
        button->item.offsetY = y;
        y += button->item.height;
    }

    listView->contentSize = y;

    int scrollHeight = listView->frame.height - CB_ListView_scrollInset * 2;

    bool indicatorVisible = false;
    if (listView->contentSize > listView->frame.height)
    {
        indicatorVisible = true;
    }
    listView->scroll.indicatorVisible = indicatorVisible;

    float indicatorHeight = 0.0f;
    if (listView->contentSize > listView->frame.height && listView->frame.height != 0)
    {
        indicatorHeight = CB_MAX(
            scrollHeight * (listView->frame.height / listView->contentSize),
            CB_ListView_scrollIndicatorMinHeight
        );
    }
    listView->scroll.indicatorHeight = indicatorHeight;
}

void CB_ListView_reload(CB_ListView* listView)
{

    CB_ListView_invalidateLayout(listView);

    int numberOfItems = listView->items->length;

    if (numberOfItems > 0)
    {
        if (listView->selectedItem < 0)
        {
            CB_ListView_selectItem(listView, 0, false);
        }
        else if (listView->selectedItem >= numberOfItems)
        {
            CB_ListView_selectItem(listView, numberOfItems - 1, false);
        }
        else
        {
            CB_ListView_selectItem(listView, listView->selectedItem, false);
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

void CB_ListView_update(CB_ListView* listView)
{
    PDButtons pushed = CB_App->buttons_pressed;
    PDButtons pressed = CB_App->buttons_down;

    if (pushed & kButtonDown)
    {
        if (listView->items->length > 0)
        {
            int nextIndex = listView->selectedItem + 1;
            if (nextIndex >= listView->items->length)
            {
                nextIndex = 0;
            }
            CB_ListView_selectItem(listView, nextIndex, true);
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
            CB_ListView_selectItem(listView, prevIndex, true);
        }
    }

    listView->crankChange += CB_App->crankChange;

    if (listView->crankChange != 0)
    {
        listView->crankResetTime += CB_App->dt;
    }
    else
    {
        listView->crankResetTime = 0;
    }

    if (listView->crankChange > 0 && listView->crankChange >= CB_ListView_crankMinChange)
    {
        if (listView->items->length > 0)
        {
            int nextIndex = listView->selectedItem + 1;
            if (nextIndex >= listView->items->length)
            {
                nextIndex = 0;
            }
            CB_ListView_selectItem(listView, nextIndex, true);
            listView->crankChange = 0;
        }
    }
    else if (listView->crankChange < 0 && listView->crankChange <= (-CB_ListView_crankMinChange))
    {
        if (listView->items->length > 0)
        {
            int prevIndex = listView->selectedItem - 1;
            if (prevIndex < 0)
            {
                prevIndex = listView->items->length - 1;
            }
            CB_ListView_selectItem(listView, prevIndex, true);
            listView->crankChange = 0;
        }
    }

    if (listView->crankResetTime > CB_ListView_crankResetMinTime)
    {
        listView->crankResetTime = 0;
        listView->crankChange = 0;
    }

    CB_ListViewDirection old_direction = listView->direction;
    listView->direction = CB_ListViewDirectionNone;

    if (pressed & kButtonUp)
    {
        listView->direction = CB_ListViewDirectionUp;
    }
    else if (pressed & kButtonDown)
    {
        listView->direction = CB_ListViewDirectionDown;
    }

    if (listView->direction == CB_ListViewDirectionNone || listView->direction != old_direction)
    {
        listView->repeatIncrementTime = 0;
        listView->repeatLevel = 0;
        listView->repeatTime = 0;
    }
    else
    {
        listView->repeatIncrementTime += CB_App->dt;

        float repeatInterval = CB_ListView_repeatInterval1;
        if (listView->repeatLevel > 0)
        {
            repeatInterval = CB_ListView_repeatInterval2;
        }

        if (listView->repeatIncrementTime >= repeatInterval)
        {
            listView->repeatLevel = CB_MIN(3, listView->repeatLevel + 1);
            listView->repeatIncrementTime = fmodf(listView->repeatIncrementTime, repeatInterval);
        }

        if (listView->repeatLevel > 0)
        {
            listView->repeatTime += CB_App->dt;

            float repeatRate = 0.16f;

            if (listView->repeatLevel == 2)
            {
                repeatRate = 0.1f;
            }
            else if (listView->repeatLevel == 3)
            {
                repeatRate = 0.05f;
            }

            if (listView->repeatTime >= repeatRate)
            {
                listView->repeatTime = fmodf(listView->repeatTime, repeatRate);

                if (listView->direction == CB_ListViewDirectionUp)
                {
                    if (listView->items->length > 0)
                    {
                        int prevIndex = listView->selectedItem - 1;
                        if (prevIndex < 0)
                        {
                            prevIndex = listView->items->length - 1;
                        }
                        CB_ListView_selectItem(listView, prevIndex, true);
                    }
                }
                else if (listView->direction == CB_ListViewDirectionDown)
                {
                    if (listView->items->length > 0)
                    {
                        int nextIndex = listView->selectedItem + 1;
                        if (nextIndex >= listView->items->length)
                        {
                            nextIndex = 0;
                        }
                        CB_ListView_selectItem(listView, nextIndex, true);
                    }
                }
            }
        }
    }

    if (listView->scroll.active)
    {
        listView->scroll.time += CB_App->dt;

        float progress =
            cb_easeInOutQuad(fminf(1.0f, listView->scroll.time / listView->scroll.duration));
        listView->contentOffset =
            listView->scroll.start + (listView->scroll.end - listView->scroll.start) * progress;

        if (listView->scroll.time >= listView->scroll.duration)
        {
            listView->scroll.time = 0;
            listView->scroll.active = false;
        }
    }

    float indicatorOffset = CB_ListView_scrollInset;
    if (listView->contentSize > listView->frame.height)
    {
        int scrollHeight = listView->frame.height -
                           (CB_ListView_scrollInset * 2 + listView->scroll.indicatorHeight);
        indicatorOffset =
            CB_ListView_scrollInset +
            (listView->contentOffset / (listView->contentSize - listView->frame.height)) *
                scrollHeight;
    }
    listView->scroll.indicatorOffset = indicatorOffset;

    if (listView->selectedItem >= 0 && listView->selectedItem < listView->items->length)
    {
        CB_ListItemButton* button = listView->items->items[listView->selectedItem];

        if (button->item.type == CB_ListViewItemTypeButton)
        {
            playdate->graphics->setFont(CB_App->subheadFont);
            int textWidth = playdate->graphics->getTextWidth(
                CB_App->subheadFont, button->title, strlen(button->title), kUTF8Encoding, 0
            );
            int availableWidth = listView->scroll.active
                                     ? listView->frame.width - (CB_ListView_inset * 2)
                                     : listView->frame.width - CB_ListView_inset -
                                           (CB_ListView_scrollInset * 2) -
                                           (CB_ListView_scrollIndicatorWidth * 2);

            button->needsTextScroll = (textWidth > availableWidth);

            if (button->needsTextScroll)
            {
                listView->textScrollTime += CB_App->dt;

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
                            cb_easeInOutQuad(normalizedScrollProgress) * maxOffset;
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
                            (1.0f - cb_easeInOutQuad(normalizedScrollProgress)) * maxOffset;
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

void CB_ListView_draw(CB_ListView* listView)
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
            CB_ListItemButton* button = listView->items->items[i];
            CB_ListItem* item = &button->item;

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

            if (item->type == CB_ListViewItemTypeButton)
            {
                if (selected)
                {
                    playdate->graphics->setDrawMode(kDrawModeFillWhite);
                }
                else
                {
                    playdate->graphics->setDrawMode(kDrawModeFillBlack);
                }

                int textX = listX + CB_ListView_inset;
                int textY = rowY + (float)(item->height -
                                           playdate->graphics->getFontHeight(CB_App->subheadFont)) /
                                       2;

                playdate->graphics->setFont(CB_App->subheadFont);

                int rightSidePadding;

                if (listView->scroll.indicatorVisible)
                {
                    // If the scrollbar is visible, the padding must be wide enough
                    // to contain the scrollbar itself plus its inset.
                    rightSidePadding = CB_ListView_scrollIndicatorWidth + CB_ListView_scrollInset;
                }
                else
                {
                    // If no scrollbar, we just need a 1-pixel gap to avoid
                    // text touching the divider line on the right.
                    rightSidePadding = 1;
                }

                int maxTextWidth = listView->frame.width - CB_ListView_inset - rightSidePadding;

                if (maxTextWidth < 0)
                {
                    maxTextWidth = 0;
                }

                playdate->graphics->setClipRect(textX, rowY, maxTextWidth, item->height);

                if (selected && button->needsTextScroll)
                {
                    int scrolledX = textX - (int)button->textScrollOffset;
                    playdate->graphics->drawText(
                        button->title, strlen(button->title), kUTF8Encoding, scrolledX, textY
                    );
                }
                else
                {
                    playdate->graphics->drawText(
                        button->title, strlen(button->title), kUTF8Encoding, textX, textY
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
                listView->frame.width - CB_ListView_scrollInset - CB_ListView_scrollIndicatorWidth,
                listView->scroll.indicatorOffset, CB_ListView_scrollIndicatorWidth,
                listView->scroll.indicatorHeight
            );
            PDRect indicatorBorderRect = PDRectMake(
                indicatorFillRect.x - indicatorLineWidth, indicatorFillRect.y - indicatorLineWidth,
                indicatorFillRect.width + indicatorLineWidth * 2,
                indicatorFillRect.height + indicatorLineWidth * 2
            );

            cb_drawRoundRect(indicatorBorderRect, 2, indicatorLineWidth, kColorWhite);
            cb_fillRoundRect(indicatorFillRect, 2, kColorBlack);
        }
    }
}

static void CB_ListView_selectItem(CB_ListView* listView, unsigned int index, bool animated)
{

    CB_ListItemButton* button = listView->items->items[index];
    CB_ListItem* item = &button->item;

    int listHeight = listView->frame.height;

    int centeredOffset = 0;

    if (listView->contentSize > listHeight)
    {
        centeredOffset = item->offsetY - ((float)listHeight / 2 - (float)CB_ListView_rowHeight / 2);
        centeredOffset = CB_MAX(0, centeredOffset);
        centeredOffset = CB_MIN(centeredOffset, listView->contentSize - listHeight);
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
        CB_ListItemButton* oldButton = listView->items->items[listView->selectedItem];
        if (oldButton->item.type == CB_ListViewItemTypeButton)
        {
            oldButton->textScrollOffset = 0;
        }
    }

    listView->selectedItem = index;
}

void CB_ListView_free(CB_ListView* listView)
{
    if (!listView)
        return;

    if (listView->items)
    {
        for (int i = 0; i < listView->items->length; i++)
        {
            CB_ListItemButton_free(listView->items->items[i]);
        }
        array_free(listView->items);
    }

    cb_free(listView);
}

static CB_ListItem* CB_ListItem_new(void)
{
    CB_ListItem* item = cb_malloc(sizeof(CB_ListItem));
    return item;
}

CB_ListItemButton* CB_ListItemButton_new(char* title)
{
    CB_ListItemButton* button = cb_malloc(sizeof(CB_ListItemButton));
    memset(button, 0, sizeof(CB_ListItemButton));

    button->item.type = CB_ListViewItemTypeButton;
    button->item.height = CB_ListView_rowHeight;
    button->item.object = NULL;

    button->title = cb_strdup(title ? title : "There be dragons...");
    button->coverImage = NULL;
    button->textScrollOffset = 0.0f;
    button->needsTextScroll = false;

    return button;
}

void CB_ListItemButton_free(CB_ListItemButton* itemButton)
{
    if (!itemButton)
    {
        return;
    }

    cb_free(itemButton->title);

    if (itemButton->coverImage != NULL)
    {
        playdate->graphics->freeBitmap(itemButton->coverImage);
    }

    cb_free(itemButton);
}

void CB_ListItem_free(CB_ListItem* item)
{
    if (!item)
    {
        return;
    }

    if (item->type == CB_ListViewItemTypeButton)
    {
        CB_ListItemButton_free((CB_ListItemButton*)item);
    }
}
