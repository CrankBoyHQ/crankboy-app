#include "../scriptutil.h"

#include <stdlib.h>

#define DESCRIPTION                         \
    "- Use the crank to move the paddle.\n" \
    "- Press Ⓐ to start the game."

#define ADDR_PADDLE_ACTIVE_FLAG 0xC800
#define ADDR_PADDLE_SPRITE_X 0xC801
#define PADDLE_MIN_X_SPRITE 0x10  // Leftmost position
#define PADDLE_MAX_X_SPRITE 0x68  // Rightmost position

#define CRANK_SENSITIVITY 1.0f

typedef struct
{
    float paddle_x_pos;
    uint8_t last_known_x;
    bool initialized;
} AlleywayState;

static void* on_begin(gb_s* gb, char* header_name)
{
    AlleywayState* state = allocz(AlleywayState);

    force_pref(crank_mode, CRANK_MODE_OFF);
    force_pref(crank_dock_button, PREF_BUTTON_NONE);
    force_pref(crank_undock_button, PREF_BUTTON_NONE);

    return state;
}

static void on_end(gb_s* gb, void* data)
{
    cb_free(data);
}

static void on_tick(gb_s* gb, void* data)
{
    AlleywayState* state = (AlleywayState*)data;
    if (!state)
        return;

    if (ram_peek(ADDR_PADDLE_ACTIVE_FLAG) == 0x90)
    {
        uint8_t current_sprite_x = ram_peek(ADDR_PADDLE_SPRITE_X);

        if (!playdate->system->isCrankDocked())
        {

            script_gb->direct.joypad |= (PAD_LEFT | PAD_RIGHT);

            if (!state->initialized)
            {
                state->paddle_x_pos = (float)current_sprite_x;
                state->last_known_x = current_sprite_x;
                state->initialized = true;
            }

            if (current_sprite_x != state->last_known_x)
            {
                state->paddle_x_pos = (float)current_sprite_x;
            }

            float crank_change = playdate->system->getCrankChange();
            state->paddle_x_pos += crank_change * CRANK_SENSITIVITY;

            if (state->paddle_x_pos < PADDLE_MIN_X_SPRITE)
                state->paddle_x_pos = PADDLE_MIN_X_SPRITE;
            if (state->paddle_x_pos > PADDLE_MAX_X_SPRITE)
                state->paddle_x_pos = PADDLE_MAX_X_SPRITE;

            uint8_t desired_sprite_x = (uint8_t)(state->paddle_x_pos + 0.5f);
            if (desired_sprite_x > current_sprite_x)
            {
                script_gb->direct.joypad &= ~PAD_RIGHT;
            }
            else if (desired_sprite_x < current_sprite_x)
            {
                script_gb->direct.joypad &= ~PAD_LEFT;
            }

            state->last_known_x = current_sprite_x;
        }
        else
        {
            state->initialized = false;
        }
    }
    else
    {
        state->initialized = false;

        if ($JOYPAD & PAD_A)
        {
            script_gb->direct.joypad &= ~(1 << 3);
            script_gb->direct.joypad |= (1 << 0);
        }
    }
}

C_SCRIPT{
    .rom_name = "ALLEY WAY",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_end = (CS_OnEnd)on_end,
};
