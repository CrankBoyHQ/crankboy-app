#include "../scriptutil.h"

#define DESCRIPTION \
    "- Crank aims the turret in 8 directions.\n" \
    "- Press Ⓐ in-game to toggle autofire.\n" \
    "- Crank docked falls back to default controls.\n" \
    "\nCreated by: stonerl"

static const uint16_t DIR_ADDRS[] = {
    0xC0ED, 0xC802, 0xC804, 0xC843, 0xC93E, 0xC952, 0xCB76,
};

// Values for angles 0,45,90,135,180,225,270,315 respectively.
static const uint8_t DIR_VALUES[][8] = {
    {0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},  // 0xC0ED
    {0x8A, 0xC5, 0x8E, 0xEA, 0x7D, 0x69, 0x30, 0xAE},  // 0xC802
    {0x6D, 0xBA, 0x78, 0xDB, 0x5C, 0x44, 0xF3, 0xA0},  // 0xC804
    {0x09, 0x05, 0x08, 0x03, 0x0A, 0x0B, 0x00, 0x06},  // 0xC843
    {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},  // 0xC93E
    {0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},  // 0xC952
    {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38},  // 0xCB76
};

static const size_t DIR_SLOT_COUNT = sizeof(DIR_ADDRS) / sizeof(DIR_ADDRS[0]);

typedef struct TraxState
{
    uint8_t last_vals[7];
    uint8_t pending_sector;
    bool in_game;
    bool autofire_enabled;
    uint8_t autofire_counter;
} TraxState;

static uint8_t sector_from_angle(float angle)
{
    while (angle < 0.0f)
        angle += 360.0f;
    while (angle >= 360.0f)
        angle -= 360.0f;

    // Midpoint snapping: [-22.5..22.5) -> sector 0, etc.
    int sector = (int)((angle + 22.5f) / 45.0f) & 7;
    return (uint8_t)sector;
}

static bool trax_in_game(void)
{
    uint8_t v1 = ram_peek(0x9C20);
    uint8_t v2 = ram_peek(0x9C40);
    bool ok1 = (v1 == 0x82 || v1 == 0x84);
    bool ok2 = (v2 == 0x83 || v2 == 0x85);
    uint8_t go1 = ram_peek(0x9C60);
    uint8_t go2 = ram_peek(0x9E20);
    bool is_game_over = (go1 == 0x86 && go2 == 0xFF);
    return ok1 && ok2 && !is_game_over;
}

static void apply_sector(TraxState* state, uint8_t sector)
{
    for (size_t i = 0; i < DIR_SLOT_COUNT; ++i)
    {
        uint8_t v = DIR_VALUES[i][sector];
        if (state->last_vals[i] != v)
        {
            ram_poke(DIR_ADDRS[i], v);
            state->last_vals[i] = v;
        }
    }
}

static TraxState* on_begin(gb_s* gb, const char* header_name)
{
    force_pref(crank_mode, CRANK_MODE_OFF);
    force_pref(crank_dock_button, PREF_BUTTON_NONE);
    force_pref(crank_undock_button, PREF_BUTTON_NONE);

    return allocz(TraxState);
}

static void on_end(gb_s* gb, TraxState* state)
{
    cb_free(state);
}

static void on_tick(gb_s* gb, TraxState* state, int frames_elapsed)
{
    if (!state || playdate->system->isCrankDocked())
    {
        state->autofire_enabled = false;
        state->autofire_counter = 0;
        return;
    }

    state->in_game = trax_in_game();

    PDButtons current, pushed, released;
    playdate->system->getButtonState(&current, &pushed, &released);
    if (state->in_game && (pushed & kButtonA))
    {
        state->autofire_enabled = !state->autofire_enabled;
        script_gb->direct.joypad_bits.a = 1;
    }

    state->pending_sector = sector_from_angle(playdate->system->getCrankAngle());

    if (state->in_game)
    {
        script_gb->direct.joypad_bits.a = 1;

        apply_sector(state, state->pending_sector);

        if (state->autofire_enabled)
        {
            const uint8_t HOLD_FRAMES = 10;
            const uint8_t GAP_FRAMES = 5;
            uint8_t phase = state->autofire_counter % (HOLD_FRAMES + GAP_FRAMES);
            script_gb->direct.joypad_bits.b = (phase < HOLD_FRAMES) ? 0 : 1;
            state->autofire_counter++;
        }
    }
    else
    {
        state->autofire_counter = 0;
    }
}

C_SCRIPT{
    .rom_name = "TRAX",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_end = (CS_OnEnd)on_end,
};
