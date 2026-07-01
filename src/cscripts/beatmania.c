#include "../scriptutil.h"

#define DESCRIPTION "Forces CGB mode. Game header claims GBC support but ROM is not DMG-compatible."

typedef struct
{
    int dummy;
} ScriptData;

static void* on_begin(gb_s* gb, const char* header_name)
{
    (void)gb;
    (void)header_name;
    return allocz(ScriptData);
}

static void on_end(gb_s* gb, void* data)
{
    (void)gb;
    cb_free(data);
}

C_SCRIPT{
    .rom_name = "BEAT MANIA GB",
    .description = DESCRIPTION,
    .launch_system = ScriptPreferredLaunchSystem_CGB,
    .on_begin = (CS_OnBegin)on_begin,
    .on_end = (CS_OnEnd)on_end,
};

C_SCRIPT{
    .rom_name = "BEAT MANIA2A2GJ",
    .description = DESCRIPTION,
    .launch_system = ScriptPreferredLaunchSystem_CGB,
    .on_begin = (CS_OnBegin)on_begin,
    .on_end = (CS_OnEnd)on_end,
};

C_SCRIPT{
    .rom_name = "BEAT MANIA3B3GJ",
    .description = DESCRIPTION,
    .launch_system = ScriptPreferredLaunchSystem_CGB,
    .on_begin = (CS_OnBegin)on_begin,
    .on_end = (CS_OnEnd)on_end,
};
