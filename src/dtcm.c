#include "dtcm.h"

#include "utility.h"

#define __dtcm_ctrl __section__(".text.dtcm_ctrl")

#ifdef DTCM_ALLOC
static uint32_t *dtcm_low_canary_addr = NULL;
#define DTCM_CANARY 0xDE0DCA94
#endif

static bool is_dtcm_init = false;

// low address that's within stack region,
// can allocate global variables from here+
void *dtcm_mempool = NULL;

__dtcm_ctrl void *dtcm_alloc(size_t size)
{
#ifdef DTCM_ALLOC
    if (is_dtcm_init)
    {
        void *tmp = dtcm_mempool;
        *(uint32_t *)dtcm_mempool = 0;
        dtcm_mempool = (void *)(size + (uintptr_t)dtcm_mempool);
        // high canary
        *(uint32_t *)dtcm_mempool = DTCM_CANARY;
        return tmp;
    }
#endif

    return playdate->system->realloc(NULL, size);
}

__dtcm_ctrl void *dtcm_alloc_aligned(size_t size, size_t offset)
{
    offset %= 32;
    uintptr_t u = (uintptr_t)dtcm_alloc(size + 32);
    
    // smallest integer n >= u, s.t. n % 32 == offset
    return (void*)(
        u + ((offset - (u % 32) + 32) % 32)
    );
}

__dtcm_ctrl void dtcm_init(void)
{
    if (is_dtcm_init)
        return;
    is_dtcm_init = true;

    if (dtcm_mempool == NULL)
    {
        is_dtcm_init = false;
        playdate->system->error(
            "Attempt to enable DTCM, but mempool region not set!");
        return;
    }

#ifdef DTCM_ALLOC
    *(uint32_t *)dtcm_mempool = DTCM_CANARY;
    dtcm_low_canary_addr = (uint32_t *)dtcm_alloc(sizeof(uint32_t));
    *dtcm_low_canary_addr = DTCM_CANARY;
    playdate->system->logToConsole("DTCM init");
#endif
}

__dtcm_ctrl void dtcm_set_mempool(void *addr)
{
    if (dtcm_mempool != NULL)
    {
        playdate->system->error("Cannot set DTCM mempool twice.");
        return;
    }
    dtcm_mempool = addr;
#ifdef DTCM_ALLOC
    playdate->system->logToConsole("DTCM mempool: %p\n", dtcm_mempool);
#endif
}

__dtcm_ctrl bool dtcm_verify(void)
{
    if (!is_dtcm_init)
        return true;

#ifdef DTCM_ALLOC
    if (dtcm_low_canary_addr)
    {
        if (*dtcm_low_canary_addr != DTCM_CANARY)
        {
            playdate->system->error(
                "DTCM low canary broken (decrease PLAYDATE_STACK_SIZE?)");
            return false;
        }
        if (*(uint32_t *)dtcm_mempool != DTCM_CANARY)
        {
            playdate->system->error(
                "DTCM high canary broken (stack overflow?)");
            return false;
        }
    }
#endif
    return true;
}

bool dtcm_enabled(void)
{
#ifndef DTCM_ALLOC
    return false;
#endif
    return is_dtcm_init;
}