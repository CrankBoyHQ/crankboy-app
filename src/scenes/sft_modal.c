/*
 * sft_modal.c
 * CrankBoy - Serial File Transfer Modal
 * Shows a fullscreen modal indicating serial file transfer is active
 */

#include "sft_modal.h"

#include "../app.h"
#include "../utility.h"

static CB_SFTModal* sft_modal_instance = NULL;

CB_SFTModal* CB_SFTModal_new(void)
{
    CB_Scene* scene = CB_Scene_new();
    if (!scene)
    {
        return NULL;
    }
    scene->type = CB_SCENE_TYPE_SFT_MODAL;

    CB_SFTModal* sftModal = allocz(CB_SFTModal);
    if (!sftModal)
    {
        CB_Scene_free(scene);
        return NULL;
    }

    sftModal->scene = scene;
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

    cb_free(sftModal);
}

void CB_SFTModal_update(void* object, uint32_t u32float_dt)
{
    (void)object;
    (void)u32float_dt;
    cb_draw_logo_screen_to_buffer(CB_App->subheadFont, "File Transfer Mode");
}

// Getter for the global instance (used by serial.c to check if modal is active)
CB_SFTModal* CB_SFTModal_get_instance(void)
{
    return sft_modal_instance;
}
