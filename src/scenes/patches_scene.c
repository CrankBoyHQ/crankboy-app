#include "patches_scene.h"

#include "../userstack.h"
#include "info_scene.h"

#define MAX_DISP 5
#define MARGIN 4
#define BOX_SIZE 28
#define ROW_HEIGHT 32
#define BOX_SELECTED_PADDING 6
#define ROW_HEIGHT_TEXT_OFFSET 6

#define HEADER_HEIGHT 18

#define INFO_Y (HEADER_HEIGHT + 2 * MARGIN + ROW_HEIGHT * MAX_DISP)

#define INFO                                                                                       \
    "Press Ⓐ to toggle patches.\n \nHold Ⓐ to rearrange patches. Enabled patches will be applied " \
    "in the order listed."

extern const uint8_t lcdp_50[16];

static void CB_PatchesScene_update(void* object, uint32_t u32enc_dt)
{
    CB_PatchesScene* patchesScene = object;
    float dt = UINT32_AS_FLOAT(u32enc_dt);
    size_t len = 0;
    for (SoftPatch* patch = patchesScene->patches; patch->fullpath; ++patch, ++len)
        ;

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

    bool held = !!(CB_App->buttons_down & kButtonA);

    // menu movement
    int ydir = !!(CB_App->buttons_pressed & kButtonDown) - !!(CB_App->buttons_pressed & kButtonUp);
    if ((ydir < 0 && patchesScene->selected > 0) || (ydir > 0 && patchesScene->selected < len - 1))
    {
        if (held)
        {
            // reorder patches
            memswap(
                &patchesScene->patches[patchesScene->selected],
                &patchesScene->patches[patchesScene->selected + ydir], sizeof(SoftPatch)
            );
            patchesScene->didDrag = true;
        }

        patchesScene->selected += ydir;
        cb_play_ui_sound(CB_UISound_Navigate);
    }

    unsigned scroll = 0;
    if (patchesScene->selected >= MAX_DISP / 2)
    {
        scroll = patchesScene->selected - MAX_DISP / 2;
    }
    if (scroll + MAX_DISP > len && len >= MAX_DISP)
    {
        scroll = len - MAX_DISP;
    }

    SoftPatch* selectedPatch = &patchesScene->patches[patchesScene->selected];

    if (CB_App->buttons_released & kButtonA)
    {
        if (!patchesScene->didDrag)
        {
            if (selectedPatch->state == PATCH_ENABLED)
            {
                selectedPatch->state = PATCH_DISABLED;
            }
            else
            {
                selectedPatch->state = PATCH_ENABLED;
            }
            cb_play_ui_sound(CB_UISound_Confirm);
        }

        patchesScene->didDrag = false;
    }
    else if (CB_App->buttons_pressed & kButtonB)
    {
        patchesScene->dismiss = true;
    }

    // menu
    LCDFont* font = CB_App->bodyFont;
    playdate->graphics->setFont(font);
    playdate->graphics->setDrawMode(kDrawModeFillBlack);

    for (size_t i = 0; scroll + i < len && i < MAX_DISP; ++i)
    {
        size_t index = scroll + i;
        SoftPatch* patch = &patchesScene->patches[index];

        const bool thisHeld = patchesScene->selected == index && held;

        int y = MARGIN + ROW_HEIGHT * i + HEADER_HEIGHT;

        playdate->graphics->drawRect(
            MARGIN, y + ROW_HEIGHT / 2 - BOX_SIZE / 2, BOX_SIZE, BOX_SIZE, kColorBlack
        );
        playdate->graphics->drawRect(
            MARGIN + 1, y + ROW_HEIGHT / 2 - BOX_SIZE / 2 + 1, BOX_SIZE - 2, BOX_SIZE - 2,
            kColorBlack
        );

        if (patch->state == PATCH_ENABLED || thisHeld)
        {
            LCDColor col = thisHeld ? (uintptr_t)&lcdp_50 : kColorBlack;
            playdate->graphics->fillRect(
                MARGIN + BOX_SELECTED_PADDING,
                y + ROW_HEIGHT / 2 - BOX_SIZE / 2 + BOX_SELECTED_PADDING,
                BOX_SIZE - 2 * BOX_SELECTED_PADDING, BOX_SIZE - 2 * BOX_SELECTED_PADDING, col
            );
        }

        // TODO: display "new" patches slightly differently

        playdate->graphics->drawText(
            patch->basename, strlen(patch->basename), kUTF8Encoding, MARGIN * 2 + BOX_SIZE,
            y + ROW_HEIGHT_TEXT_OFFSET
        );

        if (index == patchesScene->selected)
        {
            playdate->graphics->fillRect(0, y, LCD_COLUMNS, ROW_HEIGHT, kColorXOR);
        }
    }

    playdate->graphics->setFont(CB_App->labelFont);

    const char* info = INFO;
    playdate->graphics->drawTextInRect(
        info, strlen(info), kUTF8Encoding, MARGIN, INFO_Y, LCD_COLUMNS - 2 * MARGIN, 200, kWrapWord,
        kAlignTextLeft
    );

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

    cb_free(patchesScene->patches_dir);
    free_patches(patchesScene->patches);
    cb_free(patchesScene);
}

static void CB_PatchesScene_menu(void* object)
{
    // TODO -- "return" button
}

CB_PatchesScene* CB_PatchesScene_new(CB_Game* game)
{
    char* patches_dir_path = get_patches_directory(game->fullpath);

    // We must run this on the main stack, otherwise it can fail
    // in unpredictable ways, like truncated paths.
    call_with_main_stack_1(playdate->file->mkdir, patches_dir_path);

    SoftPatch* patches = call_with_main_stack_2(list_patches, game->fullpath, NULL);

    // If no patches are found, display an informational scene.
    if (!patches || !patches[0].fullpath)
    {
        // For the help message, get the ROM's basename to show the user.
        char* rom_basename = cb_basename(game->fullpath, true);

        char* msg = aprintf(
            "No patches found, add them by:\n\n"
            "1. Place your Playdate in disk mode by holding LEFT+MENU+LOCK for ten seconds.\n"
            "2. Via your computer, add patch files to: Data/*crankboy/patches/%s\n"
            "3. Finally, enable them from this screen (settings > Patches).\n\n"
            "You may find patches on romhacking.net or romhack.ing",
            rom_basename
        );

        cb_free(rom_basename);
        cb_free(patches_dir_path);
        if (patches)
        {
            free_patches(patches);
        }

        return (void*)CB_InfoScene_new(game->names->name_short_leading_article, msg);
    }

    // If patches were found, create the patches scene.
    CB_Scene* scene = CB_Scene_new();
    CB_PatchesScene* patchesScene = allocz(CB_PatchesScene);
    patchesScene->scene = scene;
    scene->managedObject = patchesScene;

    patchesScene->game = game;
    patchesScene->patches = patches;
    patchesScene->patches_dir = patches_dir_path;

    scene->update = CB_PatchesScene_update;
    scene->free = CB_PatchesScene_free;
    scene->menu = CB_PatchesScene_menu;

    // Set selected to first enabled patch, or default to the first item.
    patchesScene->selected = 0;
    for (int i = 0; patches[i].fullpath; ++i)
    {
        if (patches[i].state == PATCH_ENABLED)
        {
            patchesScene->selected = i;
            break;
        }
    }

    return patchesScene;
}
