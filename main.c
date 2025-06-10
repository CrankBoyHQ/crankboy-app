//
//  main.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include <stdio.h>

#include "./src/app.h"
#include "app.h"
#include "dtcm.h"
#include "pd_api.h"
#include "revcheck.h"
#include "userstack.h"
#include "version.h"

#ifdef _WINDLL
#define DllExport __declspec(dllexport)
#else
#define DllExport
#endif

static int update(void *userdata);
int eventHandler_pdnewlib(PlaydateAPI *, PDSystemEvent event, uint32_t arg);

__section__(".rare") static void *user_stack_test(void *p)
{
    if (p == (void *)(uintptr_t)0x103)
        playdate->system->logToConsole("User stack accessible (%p)",
                                       __builtin_frame_address(0));
    else
        playdate->system->error("Error from user stack: unexpected arg p=%p",
                                p);
    return (void *)0x784;
}

int eventHandlerShim(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg);

__section__(".text.main") DllExport
    int eventHandler(PlaydateAPI *pd, PDSystemEvent event, uint32_t arg)
{
    eventHandler_pdnewlib(pd, event, arg);

    DTCM_VERIFY_DEBUG();

    if (event != kEventInit)
    {
        PGB_event(event, arg);
    }

    if (event == kEventInit)
    {
        init_user_stack();
        pd_revcheck();
        playdate = pd;
        playdate->system->logToConsole("Device: %s", pd_rev_description);
        check_for_updates(NULL);

#ifdef TARGET_PLAYDATE
        playdate->system->logToConsole("Test user stack");
        void *result =
            call_with_user_stack_1(user_stack_test, (void *)(uintptr_t)0x103);
        PGB_ASSERT(result == 0x784);
        playdate->system->logToConsole("User stack validated");
#endif

        dtcm_set_mempool(__builtin_frame_address(0) - PLAYDATE_STACK_SIZE);

        PGB_init();

        pd->system->setUpdateCallback(update, pd);
    }
    else if (event == kEventTerminate)
    {
        PGB_quit();
    }

    DTCM_VERIFY_DEBUG();

    return 0;
}

__section__(".text.main") int update(void *userdata)
{
    PlaydateAPI *pd = userdata;

#if DTCM_DEBUG
    const char *dtcm_verify_context = "main update";
#else
    const char *dtcm_verify_context = "main update (debug with -DDTCM_DEBUG=1)";
#endif

    if (!dtcm_verify(dtcm_verify_context))
        return 0;

    float dt = pd->system->getElapsedTime();
    pd->system->resetElapsedTime();

    PGB_update(dt);

    DTCM_VERIFY_DEBUG();

    return 1;
}
