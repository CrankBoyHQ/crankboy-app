//
//  scene.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "scene.h"

#include "app.h"
#include "utility.h"

static void CB_Scene_menu_callback(void* object);
static void CB_Scene_event(void* object, PDSystemEvent event, uint32_t arg);

CB_Scene* CB_Scene_new(void)
{
    CB_Scene* scene = cb_malloc(sizeof(CB_Scene));
    memset(scene, 0, sizeof(CB_Scene));

    scene->update = CB_Scene_update;
    scene->menu = CB_Scene_menu_callback;
    scene->free = CB_Scene_free;
    scene->event = CB_Scene_event;

    scene->preferredRefreshRate = 30;
    scene->forceFullRefresh = false;

    // extra stack space, to avoid stack overflow.
    // (We use most of the normal stack for DTCM.)
    scene->use_user_stack = 1;

    return scene;
}

void CB_Scene_update(void* object, uint32_t u32enc_dt)
{
}

static void CB_Scene_menu_callback(void* object)
{
}

static void CB_Scene_event(void* object, PDSystemEvent event, uint32_t arg)
{
}

void CB_Scene_refreshMenu(CB_Scene* scene)
{
    playdate->system->removeAllMenuItems();
    scene->menu(CB_App->scene->managedObject);
}

void CB_Scene_free(void* object)
{
    CB_Scene* scene = object;
    cb_free(scene);
}
