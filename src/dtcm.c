#include "dtcm.h"

#include "utility.h"

#define __dtcm_ctrl __section__(".text.dtcm_ctrl")

#ifdef DTCM_ALLOC
static uint32_t* dtcm_low_canary_addr = NULL;
#define DTCM_CANARY 0xDE0DCA94
#endif

bool is_dtcm_init = false;

// high address that's within stack region,
// can allocate global variables from here+
void* dtcm_mempool = NULL;
void* dtcm_mempool_start = NULL;

__dtcm_ctrl void* dtcm_alloc(size_t size)
{
#ifdef DTCM_ALLOC
    if (is_dtcm_init)
    {
        void* tmp = dtcm_mempool;
        *(uint32_t*)dtcm_mempool = 0;  // remove canary
        dtcm_mempool = (void*)(size + (uintptr_t)dtcm_mempool);
        // high canary
        *(uint32_t*)dtcm_mempool = DTCM_CANARY;
        return tmp;
    }
#endif

    return playdate->system->realloc(NULL, size);
}

__dtcm_ctrl void* dtcm_alloc_aligned(size_t size, size_t alignment)
{
#ifdef DTCM_ALLOC
    if (is_dtcm_init)
    {
        alignment %= 32;
        while ((uintptr_t)dtcm_mempool % 32 != alignment)
            dtcm_mempool = (void*)(dtcm_mempool + 1);
        void* tmp = dtcm_mempool;
        dtcm_mempool = (void*)(size + (uintptr_t)dtcm_mempool);
        *(uint32_t*)dtcm_mempool = DTCM_CANARY;
        return tmp;
    }
#endif

    void* original_ptr = cb_malloc(size + alignment - 1 + sizeof(void*));
    if (!original_ptr)
    {
        return NULL;
    }

    void* aligned_ptr =
        (void*)(((uintptr_t)original_ptr + sizeof(void*) + alignment - 1) & ~(alignment - 1));

    ((void**)aligned_ptr)[-1] = original_ptr;

    return aligned_ptr;
}

__dtcm_ctrl void dtcm_init(void)
{
    if (is_dtcm_init)
        return;
    is_dtcm_init = true;

    if (dtcm_mempool_start == NULL)
    {
        is_dtcm_init = false;
        playdate->system->error("Attempt to enable DTCM, but mempool region not set!");
        return;
    }

    dtcm_mempool = dtcm_mempool_start;

#ifdef DTCM_ALLOC
    *(uint32_t*)dtcm_mempool_start = DTCM_CANARY;
    dtcm_low_canary_addr = (uint32_t*)dtcm_alloc(sizeof(uint32_t));
    *dtcm_low_canary_addr = DTCM_CANARY;
    playdate->system->logToConsole("DTCM init");
#endif
}

__dtcm_ctrl void dtcm_deinit(void)
{
    is_dtcm_init = 0;
    dtcm_mempool = dtcm_mempool_start;
}

__dtcm_ctrl void dtcm_set_mempool(void* addr)
{
    if (dtcm_mempool_start != NULL)
    {
        playdate->system->error("Cannot set DTCM mempool twice.");
        return;
    }
    dtcm_mempool_start = addr;
#ifdef DTCM_ALLOC
    playdate->system->logToConsole("DTCM mempool: %p\n", dtcm_mempool_start);
#endif
}

__dtcm_ctrl bool dtcm_verify(const char* context)
{
    if (!is_dtcm_init)
        return true;

#ifdef DTCM_ALLOC
    if (dtcm_low_canary_addr)
    {
        if (*dtcm_low_canary_addr != DTCM_CANARY)
        {
            playdate->system->error(
                "ERROR %s: DTCM low canary broken (decrease "
                "PLAYDATE_STACK_SIZE?)",
                context
            );
            return false;
        }
        if (*(uint32_t*)dtcm_mempool != DTCM_CANARY)
        {
            playdate->system->error("ERROR %s: DTCM high canary broken (stack overflow?)", context);
            return false;
        }
    }
#endif
    return true;
}

struct dtcm_store_t
{
    uint32_t* dtcm_low;
    void* dtcm_mempool;
    char data[];
};

struct dtcm_store_t* dtcm_store(void)
{
#ifdef DTCM_ALLOC
    if (!is_dtcm_init)
        return NULL;

    size_t size = (uintptr_t)dtcm_mempool + 4 - (uintptr_t)dtcm_low_canary_addr;

    playdate->system->logToConsole("Storing DTCM (0x%x bytes)", size);
    struct dtcm_store_t* buff = (struct dtcm_store_t*)cb_malloc(sizeof(struct dtcm_store_t) + size);
    buff->dtcm_low = dtcm_low_canary_addr;
    buff->dtcm_mempool = dtcm_mempool;
    memcpy(buff->data, dtcm_low_canary_addr, size);
    return buff;
#else
    return NULL;
#endif
}

void dtcm_restore(struct dtcm_store_t* buff)
{
    if (!buff)
        return;
#ifdef DTCM_ALLOC
    playdate->system->logToConsole("Restoring DTCM");
    dtcm_low_canary_addr = buff->dtcm_low;
    dtcm_mempool = buff->dtcm_mempool;
    size_t size = (uintptr_t)dtcm_mempool + 4 - (uintptr_t)dtcm_low_canary_addr;
    playdate->system->logToConsole("-> restored DTCM is 0x%x bytes", size);
    memcpy(dtcm_low_canary_addr, buff->data, size);
    cb_free(buff);
    playdate->system->logToConsole("Restore complete.");
#endif
}

void dtcm_free(void* ptr)
{
    if (!ptr)
    {
        return;
    }

#ifdef DTCM_ALLOC
    uintptr_t p = (uintptr_t)ptr;
    if (p >= (uintptr_t)dtcm_mempool_start && p < (uintptr_t)dtcm_mempool)
    {
        return;
    }
#endif
    void* original_ptr = ((void**)ptr)[-1];
    cb_free(original_ptr);
}
