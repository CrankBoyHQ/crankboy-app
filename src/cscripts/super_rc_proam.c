#include "../scriptutil.h"

#define DESCRIPTION                                                 \
    "- Disables batching during boot to fix title screen freeze.\n" \
    "- Enables batching for smooth gameplay.\n"                     \
    "\nCreated by: stonerl"

#define TITLE_TILE_ADDR 0x92F0
#define TITLE_TILE_VALUE 0x2F

typedef struct
{
    bool batching_enabled;
} SuperRCState;

static void* on_begin(gb_s* gb, const char* header_name)
{
    force_pref(batching, 0);

    SuperRCState* state = allocz(SuperRCState);
    return state;
}

static void on_end(gb_s* gb, SuperRCState* state)
{
    cb_free(state);
}

static void on_tick(gb_s* gb, SuperRCState* state, int frames_elapsed)
{
    if (state->batching_enabled)
        return;

    if (ram_peek(TITLE_TILE_ADDR) == TITLE_TILE_VALUE)
    {
        force_pref(batching, 1);
        state->batching_enabled = true;
    }
}

C_SCRIPT{
    .rom_name = "SUPER RC PRO-AM",
    .description = DESCRIPTION,
    .experimental = false,
    .launch_system = ScriptPreferredLaunchSystem_DMG,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_end = (CS_OnEnd)on_end,
};
