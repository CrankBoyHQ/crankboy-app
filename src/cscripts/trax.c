#include "../scriptutil.h"

#define DESCRIPTION                                    \
    "- Crank aims the turret in 8 directions.\n"       \
    "- Press Ⓐ in-game to toggle autofire.\n"          \
    "- Crank docked falls back to default controls.\n" \
    "\nCreated by: stonerl"

static const uint16_t DIR_ADDR = 0xC93E;

// Values for angles 0,45,90,135,180,225,270,315 respectively.
static const uint8_t DIR_VALUES[8] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
};

typedef struct TraxState
{
    uint8_t last_val;
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
    uint8_t v = DIR_VALUES[sector];
    if (state->last_val != v)
    {
        ram_poke(DIR_ADDR, v);
        state->last_val = v;
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
            const uint8_t HOLD_FRAMES = 15;
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
