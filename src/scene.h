//
//  scene.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef scene_h
#define scene_h

#include "pd_api.h"

typedef struct CB_Scene
{
    void* managedObject;
    struct CB_Scene* parentScene;

    // e.g. "library", "modal"
    const char* id;

    float preferredRefreshRate;

    bool forceFullRefresh;
    bool use_user_stack;

    void (*update)(void* object, uint32_t u32float_dt);
    void (*menu)(void* object);
    void (*free)(void* object);
    void (*event)(void* object, PDSystemEvent event, uint32_t arg);
    bool (*lock)(void* object);
} CB_Scene;

CB_Scene* CB_Scene_new(void);

void CB_Scene_refreshMenu(CB_Scene* scene);

void CB_Scene_update(void* scene, uint32_t u32enc_dt);
void CB_Scene_free(void* scene);

#endif /* scene_h */
