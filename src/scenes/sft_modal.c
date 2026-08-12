/*
 * sft_modal.c
 * CrankBoy - Serial File Transfer Modal
 * Shows a fullscreen modal indicating serial file transfer is active
 */

#include "sft_modal.h"

#include "../app.h"
#include "../serial.h"
#include "../utility.h"

// Restart the app if no serial data arrives while the modal is up for this long.
#define SFT_WATCHDOG_TIMEOUT_MS 10000

static CB_SFTModal* sft_modal_instance = NULL;

CB_SFTModal* CB_SFTModal_new(void)
{
    CB_Scene* scene = CB_Scene_new();
    if (!scene)
    {
        return NULL;
    }
    scene->id = "sft-modal";

    CB_SFTModal* sftModal = allocz(CB_SFTModal);
    if (!sftModal)
    {
        CB_Scene_free(scene);
        return NULL;
    }

    sftModal->scene = scene;
    sftModal->shown_at_ms = playdate->system->getCurrentTimeMilliseconds();
    scene->managedObject = sftModal;

    scene->update = CB_SFTModal_update;
    scene->free = CB_SFTModal_free;
    // No menu or event handlers needed - this is a non-interactive modal

    sft_modal_instance = sftModal;

    return sftModal;
}

void CB_SFTModal_free(void* object)
{
    CB_SFTModal* sftModal = (CB_SFTModal*)object;
    if (!sftModal)
    {
        return;
    }

    if (sft_modal_instance == sftModal)
    {
        sft_modal_instance = NULL;
    }

    CB_Scene_free(sftModal->scene);
    cb_free(sftModal);
}

void CB_SFTModal_update(void* object, uint32_t u32float_dt)
{
    CB_SFTModal* sftModal = (CB_SFTModal*)object;
    (void)u32float_dt;

    // Watchdog: if the host went silent while the modal is up, the transfer
    // is dead (never started, stalled, or ft:e lost) -> restart the app.
    uint32_t now = playdate->system->getCurrentTimeMilliseconds();
    uint32_t last = serial_get_last_activity_ms();
    uint32_t base = (last > sftModal->shown_at_ms) ? last : sftModal->shown_at_ms;
    if (now - base > SFT_WATCHDOG_TIMEOUT_MS)
    {
        playdate->system->restartGame(playdate->system->getLaunchArgs(NULL));
        return;  // never reached
    }

    cb_draw_logo_screen_to_buffer(CB_App->subheadFont, T(status_file_transfer));
}

// Getter for the global instance (used by serial.c to check if modal is active)
CB_SFTModal* CB_SFTModal_get_instance(void)
{
    return sft_modal_instance;
}
