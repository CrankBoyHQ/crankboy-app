//
//  main.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "pd_api.h"
#include "src/app.h"
#include "src/dtcm.h"
#include "src/revcheck.h"
#include "src/userstack.h"

#if __has_include("pdboot/pdboot.h")
#include "pdboot/pdboot.h"
#endif

#include <stdio.h>
#include <time.h>

#ifdef _WINDLL
#define DllExport __declspec(dllexport)
#else
#define DllExport
#endif

static int update(void* userdata);
int eventHandler_pdnewlib(PlaydateAPI*, PDSystemEvent event, uint32_t arg);

__section__(".rare") static void* user_stack_test(void* p)
{
    if (p == (void*)(uintptr_t)0x103)
        playdate->system->logToConsole("User stack accessible (%p)", __builtin_frame_address(0));
    else
        playdate->system->error("Error from user stack: unexpected arg p=%p", p);
    return (void*)0x784;
}

#if TARGET_PLAYDATE
typedef const void (*init_routine_t)(void);
extern init_routine_t __preinit_array_start, __preinit_array_end, __init_array_start,
    __init_array_end, __fini_array_start, __fini_array_end;
static PlaydateAPI* pd;

__section__(".rare") static void exec_array(init_routine_t* start, init_routine_t* end)
{
    while (start < end)
    {
        for (size_t i = 0; i < 58000; ++i)
            asm volatile("nop");
        if (*start)
            (*start)();
        ++start;
    }
}
#endif

#ifdef _PDBOOT_H
pdboot_data_t pdboot_data;

const char* get_pdboot_name_and_version(void)
{
    if (!memcmp(pdboot_data.magic, PDBOOT_MAGIC, sizeof(pdboot_data.magic)))
    {
        return pdboot_data.name_and_version;
    }
    else
        return NULL;
}
#else

const char* get_pdboot_name_and_version()
{
    return NULL;
}
#endif

static int setHasSystemPrivileges(lua_State* L)
{
    CB_App->hasSystemAccess = true;
    playdate->system->logToConsole("Launched with system privileges");
    return 0;
}

static int onSystemDeviceLock(lua_State* L)
{
    bool shouldAbortLock = false;
    
    if (!CB_App->currentlyPaused)
    {
        // check if scene wants to abort the lock
        if (CB_App->scene->lock && CB_App->scene->lock(CB_App->scene->managedObject))
        {
            shouldAbortLock = true;
        }
    }
    
    playdate->lua->pushBool(shouldAbortLock);
    return 1;
}

static bool useLua(void)
{
    if (!cb_file_exists("main.pdz", kFileRead))
    {
        playdate->system->logToConsole("main.pdz not found.");
        return false;
    }
    
    // attempt to access system file to determine if we have system access
    if (cb_file_exists("/System/Launcher.pdx/pdxinfo", kFileRead | kFileReadData))
    {
        return true;
    }
    if (cb_file_exists("/System/Data/global.settings", kFileRead | kFileReadData))
    {
        return true;
    }
    
    return false;
}

static void initLua(void)
{
    playdate->lua->addFunction(setHasSystemPrivileges, "crankboy.setHasSystemPrivileges", NULL);
    playdate->lua->addFunction(onSystemDeviceLock, "crankboy.onSystemDeviceLock", NULL);
}

int eventHandlerShim(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg);

__section__(".text.main") DllExport
    int eventHandler(PlaydateAPI* pd, PDSystemEvent event, uint32_t arg)
{
    eventHandler_pdnewlib(pd, event, arg);

    DTCM_VERIFY_DEBUG();
    
    if (event == kEventPause)
    {
        CB_App->currentlyPaused = true;
    }
    if (event == kEventResume)
    {
        CB_App->currentlyPaused = false;
    }

    if (event != kEventInit)
    {
        CB_event(event, arg);
    }

    if (event == kEventInit)
    {
        playdate = pd;

        int headphone, mic;
        playdate->sound->getHeadphoneState(&headphone, &mic, CB_headphone_state_changed);

#ifdef _PDBOOT_H
        memcpy(&pdboot_data, playdate->graphics->getFrame(), sizeof(pdboot_data));
#endif

        init_user_stack();
        srand(time(NULL));

#ifdef TARGET_PLAYDATE
        // support for attribute((constructor)),
        // and possibly future support for c++.
        exec_array(&__preinit_array_start, &__preinit_array_end);
        exec_array(&__init_array_start, &__init_array_end);
#endif

        pd_revcheck();
        playdate->system->logToConsole("Device: %s", pd_rev_description);

#ifdef TARGET_PLAYDATE
        playdate->system->logToConsole("Test user stack");
        void* result = call_with_user_stack_1(user_stack_test, (void*)(uintptr_t)0x103);
        CB_ASSERT(result == 0x784);
        playdate->system->logToConsole("User stack validated");
#endif

        dtcm_set_mempool(__builtin_frame_address(0) - PLAYDATE_STACK_SIZE);
        CB_init();
        
        if (!useLua())
        {
            pd->system->setUpdateCallback(update, pd);
        }
    }
    else if (event == kEventInitLua)
    {
        initLua();
        
        pd->system->setUpdateCallback(update, pd);
    }
    else if (event == kEventTerminate)
    {
#ifdef TARGET_PLAYDATE
        exec_array(&__fini_array_start, &__fini_array_end);
#endif

        CB_quit();
    }

    DTCM_VERIFY_DEBUG();

    return 0;
}

__section__(".text.main") int update(void* userdata)
{
    PlaydateAPI* pd = userdata;

#if DTCM_DEBUG
    const char* dtcm_verify_context = "main update";
#else
    const char* dtcm_verify_context = "main update (debug with -DDTCM_DEBUG=1)";
#endif

    if (!dtcm_verify(dtcm_verify_context))
        return 0;

    float dt = pd->system->getElapsedTime();
    pd->system->resetElapsedTime();

    CB_update(dt);

    DTCM_VERIFY_DEBUG();

    // we manually flush display in app.c
    return 0;
}

#if TARGET_PLAYDATE
int eventHandlerShim(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg);

// very short entrypoint function that pre-empts the eventHandlerShim.
// This must be located at exactly the segment start, so that it aligns with the
// entrypoint in the bootstrapper
__attribute__((section(".entry"))) __attribute__((naked)) int _entrypoint_(
    PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg
)
{
    asm volatile(
        "ldr r3, =eventHandlerShim\n\t"
        "bx r3\n\t"
    );
}
#endif
