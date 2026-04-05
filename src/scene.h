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
#include "utility.h"

#include <stdio.h>

typedef enum
{
    CB_SCENE_TYPE_UNKNOWN = 0,
    CB_SCENE_TYPE_LIBRARY,
    CB_SCENE_TYPE_GAME,
    CB_SCENE_TYPE_SETTINGS,
    CB_SCENE_TYPE_INFO,
    CB_SCENE_TYPE_FILE_COPYING,
    CB_SCENE_TYPE_PATCH_DOWNLOAD,
    CB_SCENE_TYPE_HOMEBREW_HUB,
    CB_SCENE_TYPE_MODAL,
    CB_SCENE_TYPE_PARENTAL_LOCK,
    CB_SCENE_TYPE_SFT_MODAL,
    CB_SCENE_TYPE_COVER_CACHE,
    CB_SCENE_TYPE_GAME_SCANNING,
    CB_SCENE_TYPE_CREDITS,
    CB_SCENE_TYPE_IMAGE_CONVERSION,
    CB_SCENE_TYPE_PATCHES
} CB_SceneType;

typedef struct CB_Scene
{
    void* managedObject;
    struct CB_Scene* parentScene;
    CB_SceneType type;

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
