#include "parental_lock_scene.h"

#include "../app.h"
#include "../utility.h"
#include "modal.h"

#define XORKEY 0xBEA03F8E

void check_for_parental_lock(void)
{
    CB_App->parentalLockEngaged = cb_file_exists(PARENTAL_LOCK_FILE, kFileReadData | kFileRead);
}

static void parental_lock_engage(CB_ParentalLockScene* parentalLockScene, int opt)
{
    if (opt == 1)
    {
        parentalLockScene->dismiss = true;

        uint32_t v = 0;
        v |= parentalLockScene->lock_value[0] << 24;
        v |= parentalLockScene->lock_value[1] << 16;
        v |= parentalLockScene->lock_value[2] << 8;
        v |= parentalLockScene->lock_value[3] << 0;

        v = (v >> 16) | (v << 16);
        v ^= XORKEY;

        if (!cb_write_entire_file(PARENTAL_LOCK_FILE, &v, sizeof(v)))
        {
            playdate->system->error("Failed to set parental lock.");
        }

        CB_App->parentalLockEngaged = true;
    }
}

static void parental_lock_disengage(CB_ParentalLockScene* parentalLockScene, int opt)
{
    CB_App->parentalLockEngaged = false;
    parentalLockScene->dismiss = true;
    if (opt == 1)
    {
        if (playdate->file->unlink(PARENTAL_LOCK_FILE, false) != 0)
        {
            playdate->system->error("Failed to remove parental lock.");
        }
    }
}

static void CB_ParentalLockScene_update(CB_ParentalLockScene* parentalLockScene, uint32_t u32enc_dt)
{
    if (CB_App->buttons_pressed & kButtonB)
        parentalLockScene->dismiss = true;

    if (CB_App->pendingScene || parentalLockScene->dismiss)
    {
        CB_dismiss(parentalLockScene->scene);
        return;
    }

    if (parentalLockScene->sel < 4)
    {
        if (CB_App->buttons_pressed & kButtonUp)
        {
            parentalLockScene->lock_value[parentalLockScene->sel] += 1;
            parentalLockScene->lock_value[parentalLockScene->sel] %= 10;
            cb_play_ui_sound(CB_UISound_Navigate);
        }

        if (CB_App->buttons_pressed & kButtonDown)
        {
            parentalLockScene->lock_value[parentalLockScene->sel] += 9;
            parentalLockScene->lock_value[parentalLockScene->sel] %= 10;
            cb_play_ui_sound(CB_UISound_Navigate);
        }

        if (CB_App->buttons_pressed & (kButtonRight | kButtonA))
        {
            parentalLockScene->sel++;
            cb_play_ui_sound(CB_UISound_Navigate);
            return;
        }
    }

    if (parentalLockScene->sel > 0)
    {
        if (CB_App->buttons_pressed & kButtonLeft)
        {
            parentalLockScene->sel--;
            cb_play_ui_sound(CB_UISound_Navigate);
        }
    }

    if (parentalLockScene->sel == 4)
    {
        if (CB_App->buttons_pressed & kButtonA)
        {
            if (parentalLockScene->unlocking)
            {
                if (memcmp(
                        parentalLockScene->lock_compare, parentalLockScene->lock_value,
                        4 * sizeof(unsigned)
                    ) != 0)
                {
                    CB_Modal* modal = CB_Modal_new("Incorrect password.", NULL, NULL, NULL);
                    modal->height = 90;
                    CB_presentModal(modal->scene);
                }
                else
                {
                    const char* options[] = {"Temporary", "Permanent", NULL};
                    CB_Modal* modal = CB_Modal_new(
                        "Unlock temporarily or permanently?", options,
                        (void*)parental_lock_disengage, parentalLockScene
                    );
                    modal->width = 330;
                    modal->height += 20;
                    modal->cannot_dismiss = true;
                    CB_presentModal(modal->scene);
                }
            }
            else
            {
                const char* options[] = {"Cancel", "Yes", NULL};
                CB_Modal* modal = CB_Modal_new(
                    "Really set parental lock? Internet features will be restricted until the lock "
                    "is disengaged. If you forget the password, you can simply "
                    "delete " PARENTAL_LOCK_FILE " as a fallback.",
                    options, (void*)parental_lock_engage, parentalLockScene
                );
                modal->width = 350;
                modal->height = 200;
                CB_presentModal(modal->scene);
            }
        }
    }

    LCDFont* font = CB_App->bodyFont;
    playdate->graphics->setFont(font);

    playdate->graphics->clear(kColorWhite);

    playdate->graphics->setDrawMode(kDrawModeCopy);
    int y = 128;
    for (int i = 0; i < 4; ++i)
    {
        char text[] = {parentalLockScene->lock_value[i] + '0', 0};
        int x = 64 + 24 * i;
        playdate->graphics->drawText(text, 1, kASCIIEncoding, x, y);
        if (parentalLockScene->sel == i)
        {
            playdate->graphics->fillRect(x - 4, y - 4, 20, 24, kColorXOR);
        }
    }

    playdate->graphics->drawText("Confirm", 8, kASCIIEncoding, 220, y);
    if (parentalLockScene->sel == 4)
    {
        playdate->graphics->fillRect(220 - 4, y - 4, 90, 24, kColorXOR);
    }

    if (parentalLockScene->unlocking)
    {
        playdate->graphics->drawText(
            "Please enter the code, or Ⓑ to cancel.", 50, kUTF8Encoding, 50, 50
        );
    }
    else
    {
        playdate->graphics->drawText(
            "Please enter a code, or Ⓑ to cancel.", 50, kUTF8Encoding, 50, 50
        );
    }
}

CB_ParentalLockScene* CB_ParentalLockScene_new(void)
{
    CB_ParentalLockScene* parentalLockScene = allocz(CB_ParentalLockScene);
    if (!parentalLockScene)
        return NULL;

    CB_Scene* scene = CB_Scene_new();
    scene->id = "parental-lock";
    parentalLockScene->scene = scene;
    scene->managedObject = parentalLockScene;

    scene->update = (void*)CB_ParentalLockScene_update;

    if (CB_App->parentalLockEngaged)
    {
        size_t size;
        char* dat = cb_read_entire_file(PARENTAL_LOCK_FILE, &size, kFileReadData);

        if (size == 4)
        {
            uint32_t v = *(uint32_t*)(void*)dat;
            v ^= XORKEY;
            v = (v >> 16) | (v << 16);

            parentalLockScene->lock_compare[0] = v >> 24;
            parentalLockScene->lock_compare[1] = (v >> 16) & 0xFF;
            parentalLockScene->lock_compare[2] = (v >> 8) & 0xFF;
            parentalLockScene->lock_compare[3] = (v >> 0) & 0xFF;
        }
        else
        {
            playdate->system->logToConsole("Failed to load parental lock");
            CB_App->parentalLockEngaged = false;
            playdate->file->unlink(PARENTAL_LOCK_FILE, false);
        }

        cb_free(dat);
    }

    parentalLockScene->unlocking = CB_App->parentalLockEngaged;

    return parentalLockScene;
}
