#include "patches_scene.h"

#include "../userstack.h"

#define HEADER_HEIGHT 18
#define kDividerX 240
#define kRightPanePadding 10
#define DRAG_HOLD_TIME 0.25f

static void CB_PatchesScene_update(void* object, uint32_t u32enc_dt)
{
    CB_PatchesScene* patchesScene = object;
    float dt = UINT32_AS_FLOAT(u32enc_dt);

    if (patchesScene->dismiss)
    {
        CB_dismiss(patchesScene->scene);
        return;
    }

    playdate->graphics->clear(kColorWhite);

    // header
    {
        const char* name = patchesScene->game->names->name_short_leading_article;
        playdate->graphics->setFont(CB_App->labelFont);
        int nameWidth = playdate->graphics->getTextWidth(
            CB_App->labelFont, name, strlen(name), kUTF8Encoding, 0
        );
        int textX = LCD_COLUMNS / 2 - nameWidth / 2;
        int fontHeight = playdate->graphics->getFontHeight(CB_App->labelFont);

        int vertical_offset = string_has_descenders(name) ? 1 : 2;
        int textY = ((HEADER_HEIGHT - fontHeight) / 2) + vertical_offset;

        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, HEADER_HEIGHT, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeFillWhite);

        playdate->graphics->drawText(name, strlen(name), kUTF8Encoding, textX, textY);
    }

    CB_ListView* listView = patchesScene->listView;
    bool held = !!(CB_App->buttons_down & kButtonA);
    bool releasedA = !!(CB_App->buttons_released & kButtonA);

    if (held)
        patchesScene->holdTime += dt;

    bool dragging = held && patchesScene->holdTime >= DRAG_HOLD_TIME;
    listView->ignoreButtons = dragging;
    listView->checkboxDrag = dragging;

    int sel = listView->selectedItem;
    int len = listView->items->length;

    int ydir = !!(CB_App->buttons_pressed & kButtonDown) - !!(CB_App->buttons_pressed & kButtonUp);

    if (dragging && ydir != 0 && sel >= 0 && ((ydir < 0 && sel > 0) || (ydir > 0 && sel < len - 1)))
    {
        int other = sel + ydir;
        memswap(&patchesScene->patches[sel], &patchesScene->patches[other], sizeof(SoftPatch));

        CB_ListItemCheckbox* a = listView->items->items[sel];
        CB_ListItemCheckbox* b = listView->items->items[other];
        char* title = a->title;
        a->title = b->title;
        b->title = title;
        bool checked = a->checked;
        a->checked = b->checked;
        b->checked = checked;

        CB_ListView_selectItem(listView, other, true);
        cb_play_ui_sound(CB_UISound_Navigate);
    }
    else if (releasedA)
    {
        if (patchesScene->holdTime < DRAG_HOLD_TIME && sel >= 0 && sel < len)
        {
            SoftPatch* patch = &patchesScene->patches[sel];
            patch->state = (patch->state == PATCH_ENABLED) ? PATCH_DISABLED : PATCH_ENABLED;
            CB_ListItemCheckbox* checkbox = listView->items->items[sel];
            checkbox->checked = (patch->state == PATCH_ENABLED);
            listView->needsDisplay = true;
            cb_play_ui_sound(CB_UISound_Confirm);
        }
    }
    else if (CB_App->buttons_pressed & kButtonB)
    {
        patchesScene->dismiss = true;
    }

    if (!held)
        patchesScene->holdTime = 0;

    CB_ListView_update(listView);

    listView->needsDisplay = true;
    CB_ListView_draw(listView);

    playdate->graphics->fillRect(
        kDividerX, HEADER_HEIGHT, LCD_COLUMNS - kDividerX, LCD_ROWS - HEADER_HEIGHT, kColorWhite
    );

    playdate->graphics->setFont(CB_App->labelFont);
    playdate->graphics->setDrawMode(kDrawModeFillBlack);

    const char* info = T(patches_help);
    int rightPaneX = kDividerX + kRightPanePadding;
    int rightPaneY = HEADER_HEIGHT + 20;
    int rightPaneWidth = LCD_COLUMNS - kDividerX - (kRightPanePadding * 2);
    playdate->graphics->drawTextInRect(
        info, strlen(info), kUTF8Encoding, rightPaneX, rightPaneY, rightPaneWidth,
        LCD_ROWS - rightPaneY, kWrapWord, kAlignTextLeft
    );

    playdate->graphics->drawLine(kDividerX, HEADER_HEIGHT, kDividerX, LCD_ROWS, 1, kColorBlack);

    playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}

static void CB_PatchesScene_free(void* object)
{
    CB_PatchesScene* patchesScene = object;
    CB_Scene_free(patchesScene->scene);

    // remove 'unknown'/'new' marker for patches

    for (SoftPatch* patch = patchesScene->patches; patch->fullpath; ++patch)
    {
        if (patch->state < 0)
            patch->state = PATCH_DISABLED;
    }

    // save patches
    call_with_main_stack_2(save_patches_state, patchesScene->game->fullpath, patchesScene->patches);

    CB_ListView_free(patchesScene->listView);

    cb_free(patchesScene->patches_dir);
    free_patches(patchesScene->patches);
    cb_free(patchesScene);
}

static void CB_PatchesScene_didSelectBack(void* userdata)
{
    CB_PatchesScene* patchesScene = userdata;
    patchesScene->dismiss = true;
}

static void CB_PatchesScene_menu(void* object)
{
    CB_PatchesScene* patchesScene = object;
    playdate->system->removeAllMenuItems();
    playdate->system->addMenuItem(T(pdmenu_back), CB_PatchesScene_didSelectBack, patchesScene);
}

CB_PatchesScene* CB_PatchesScene_new(CB_Game* game)
{
    char* patches_dir_path = get_patches_directory(game->fullpath);
    SoftPatch* patches = call_with_main_stack_2(list_patches, game->fullpath, NULL);

    CB_Scene* scene = CB_Scene_new();
    CB_PatchesScene* patchesScene = allocz(CB_PatchesScene);
    scene->id = "patches";
    patchesScene->scene = scene;
    scene->managedObject = patchesScene;

    patchesScene->game = game;
    patchesScene->patches = patches;
    patchesScene->patches_dir = patches_dir_path;

    CB_ListView* listView = CB_ListView_new();
    listView->font = CB_App->bodyFont;
    listView->frame = PDRectMake(0, HEADER_HEIGHT, kDividerX, LCD_ROWS - HEADER_HEIGHT);
    listView->paddingTop = 4;
    listView->paddingBottom = 4;
    patchesScene->listView = listView;

    for (int i = 0; patches[i].fullpath; ++i)
    {
        CB_ListItemCheckbox* item = CB_ListItemCheckbox_new(patches[i].basename);
        item->checked = (patches[i].state == PATCH_ENABLED);
        array_push(listView->items, item);
    }

    int selectedIndex = 0;
    for (int i = 0; patches[i].fullpath; ++i)
    {
        if (patches[i].state == PATCH_ENABLED)
        {
            selectedIndex = i;
            break;
        }
    }
    listView->selectedItem = selectedIndex;
    CB_ListView_reload(listView);

    scene->update = CB_PatchesScene_update;
    scene->free = CB_PatchesScene_free;
    scene->menu = CB_PatchesScene_menu;

    return patchesScene;
}
