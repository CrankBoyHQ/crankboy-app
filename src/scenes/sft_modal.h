/*
 * sft_modal.h
 * CrankBoy - Serial File Transfer Modal
 */

#ifndef sft_modal_h
#define sft_modal_h

#include "../scene.h"

typedef struct CB_SFTModal
{
    CB_Scene* scene;
} CB_SFTModal;

CB_SFTModal* CB_SFTModal_new(void);
void CB_SFTModal_free(void* object);
void CB_SFTModal_update(void* object, uint32_t u32float_dt);

// Getter for the global instance (used by serial.c)
CB_SFTModal* CB_SFTModal_get_instance(void);

#endif /* sft_modal_h */
