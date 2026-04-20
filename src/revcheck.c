#include "revcheck.h"

#include <stdint.h>

int pd_rev = PD_REV_UNDEFINED;
const char* pd_rev_description = "Undefined";

int rev_bss[] = {
    0x60,  // rev A
    0x90,  // rev B
    // TODO
};

int rev_stack[] = {
    0x20,  // rev A
    0x20,  // rev B
    // TODO
};

__attribute__((constructor)) void pd_revcheck(void)
{
#ifdef TARGET_SIMULATOR
    pd_rev = PD_REV_SIMULATOR;
#else
    volatile uintptr_t bss = (uintptr_t)(void*)&pd_rev;
    uintptr_t stack = (uintptr_t)(void*)&bss;

    int i = 0;
    for (i = 0; i < sizeof(rev_bss) / sizeof(rev_bss[0]); ++i)
    {
        if ((bss >> 24) == rev_bss[i] && (stack >> 24) == rev_stack[i])
        {
            pd_rev = i + 1;
            goto set_description;
        }
    }
    pd_rev = PD_REV_UNKNOWN;
#endif

set_description:
    switch (pd_rev)
    {
    case PD_REV_A:
        pd_rev_description = "Rev A";
        break;
    case PD_REV_B:
        pd_rev_description = "Rev B";
        break;
    case PD_REV_SIMULATOR:
        pd_rev_description = "Simulator";
        break;
    case PD_REV_UNKNOWN:
        pd_rev_description = "Unknown";
        break;
    default:
        pd_rev_description = "Undefined";
        break;
    }
}
