//
//  main.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "./src/app.h"
#include "./src/dtcm.h"
#include "./src/revcheck.h"
#include "./src/userstack.h"
#include "pd_api.h"

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

int eventHandlerShim(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg);

__section__(".text.main") DllExport
    int eventHandler(PlaydateAPI* pd, PDSystemEvent event, uint32_t arg)
{
    eventHandler_pdnewlib(pd, event, arg);

    DTCM_VERIFY_DEBUG();

    if (event != kEventInit)
    {
        CB_event(event, arg);
    }

    if (event == kEventInit)
    {
        playdate = pd;
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
__attribute__((section(".entry"))) __attribute__((naked)) void _entrypoint_(
    PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg
)
{
    asm volatile(
        "ldr r3, =eventHandlerShim\n\t"
        "bx r3\n\t"
    );
}
#endif
