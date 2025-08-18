#include "../scriptutil.h"

#include <stdlib.h>

#define DESCRIPTION                                        \
    "- On the start screen, press A to start the game.\\n" \
    "- In-game, use the crank to move the paddle."

/* --- Game-specific RAM Addresses --- */
#define ADDR_PADDLE_ACTIVE_FLAG 0xC800
#define ADDR_PADDLE_LOGIC_X 0xFFC0

// The game writes the paddle's sprite data to three distinct locations in memory.
// We must write to all three to ensure perfect game compatibility.
#define ADDR_PADDLE_SHADOW_OAM_1 0xC801  // A copy of sprite data in WRAM
#define ADDR_PADDLE_SHADOW_OAM_2 0xE801  // A second copy in HRAM
#define ADDR_PADDLE_HW_OAM 0xFE01        // The actual hardware OAM sprite data

/* --- Game-specific ROM Addresses (for patching) --- */
#define PATCH_ADDR_MOVE_LEFT 0x1805
#define PATCH_ADDR_MOVE_RIGHT 0x1811

/* --- Paddle Physics & Control (Game's Native Range) --- */
#define PADDLE_MIN_X 0x10
#define PADDLE_MAX_X 0x68
#define PADDLE_LOGIC_OFFSET -1
#define CRANK_SENSITIVITY 0.60f

typedef enum
{
    CONTROL_MODE_NORMAL,
    CONTROL_MODE_LOCKED_LEFT,
    CONTROL_MODE_LOCKED_RIGHT
} ControlMode;

typedef struct
{
    float paddle_x_pos;
    uint8_t original_move_left_byte;
    uint8_t original_move_right_byte;
    bool dpad_logic_is_disabled;
    ControlMode control_mode;
    float last_angle;
} AlleywayState;

static void write_paddle_sprite_data(uint16_t base_addr, uint8_t x_pos)
{
    ram_poke(base_addr, x_pos);
    ram_poke(base_addr + 4, x_pos + 8);
    ram_poke(base_addr + 8, x_pos + 16);
}

static void* on_begin(gb_s* gb, char* header_name)
{
    force_pref(crank_mode, CRANK_MODE_OFF);

    AlleywayState* state = allocz(AlleywayState);

    state->paddle_x_pos = 0.0f;
    state->control_mode = CONTROL_MODE_NORMAL;
    state->last_angle = -1.0f;

    state->original_move_left_byte = rom_peek(PATCH_ADDR_MOVE_LEFT);
    state->original_move_right_byte = rom_peek(PATCH_ADDR_MOVE_RIGHT);
    rom_poke(PATCH_ADDR_MOVE_LEFT, 0x00);
    rom_poke(PATCH_ADDR_MOVE_RIGHT, 0x00);
    state->dpad_logic_is_disabled = true;

    return state;
}

static void on_end(gb_s* gb, void* data)
{
    AlleywayState* state = (AlleywayState*)data;
    if (!state)
        return;

    rom_poke(PATCH_ADDR_MOVE_LEFT, state->original_move_left_byte);
    rom_poke(PATCH_ADDR_MOVE_RIGHT, state->original_move_right_byte);

    cb_free(state);
}

static void on_tick(gb_s* gb, void* data)
{
    AlleywayState* state = (AlleywayState*)data;
    if (!state)
        return;

    if (ram_peek(ADDR_PADDLE_ACTIVE_FLAG) == 0x90)
    {
        if (playdate->system->isCrankDocked())
        {
            if (state->dpad_logic_is_disabled)
            {
                rom_poke(PATCH_ADDR_MOVE_LEFT, state->original_move_left_byte);
                rom_poke(PATCH_ADDR_MOVE_RIGHT, state->original_move_right_byte);
                state->dpad_logic_is_disabled = false;
            }
            state->control_mode = CONTROL_MODE_NORMAL;
            state->last_angle = -1.0f;
            return;
        }
        else
        {
            if (!state->dpad_logic_is_disabled)
            {
                rom_poke(PATCH_ADDR_MOVE_LEFT, 0x00);
                rom_poke(PATCH_ADDR_MOVE_RIGHT, 0x00);
                state->dpad_logic_is_disabled = true;
            }

            script_gb->direct.joypad |= (1 << 1) | (1 << 0);

            PDButtons current, pushed, released;
            playdate->system->getButtonState(&current, &pushed, &released);

            if (pushed & kButtonA)
            {
                script_gb->direct.joypad_bits.a = 0;
            }

            float current_angle = playdate->system->getCrankAngle();
            if (state->last_angle < 0)
            {
                state->last_angle = current_angle;
            }

            float new_paddle_pos = state->paddle_x_pos;
            const float paddle_range = (float)(PADDLE_MAX_X - PADDLE_MIN_X);

            switch (state->control_mode)
            {
            case CONTROL_MODE_NORMAL:
                if (current_angle >= 270.0f || current_angle <= 90.0f)
                {
                    float crank_progress = (current_angle >= 270.0f) ? (current_angle - 270.0f)
                                                                     : (current_angle + 90.0f);
                    new_paddle_pos = PADDLE_MIN_X + (paddle_range * (crank_progress / 180.0f));
                }
                else if (current_angle > 90.0f && current_angle < 180.0f)
                {
                    state->control_mode = CONTROL_MODE_LOCKED_RIGHT;
                    new_paddle_pos = (float)PADDLE_MAX_X;
                }
                else
                {  // >= 180 and < 270
                    state->control_mode = CONTROL_MODE_LOCKED_LEFT;
                    new_paddle_pos = (float)PADDLE_MIN_X;
                }
                break;

            case CONTROL_MODE_LOCKED_RIGHT:
                if (current_angle <= 90.0f &&
                    (state->last_angle > 90.0f && state->last_angle < 180.0f))
                {
                    state->control_mode = CONTROL_MODE_NORMAL;
                    float crank_progress = current_angle + 90.0f;
                    new_paddle_pos = PADDLE_MIN_X + (paddle_range * (crank_progress / 180.0f));
                }
                else
                {
                    new_paddle_pos = (float)PADDLE_MAX_X;
                }
                break;

            case CONTROL_MODE_LOCKED_LEFT:
                if (current_angle >= 270.0f &&
                    (state->last_angle >= 180.0f && state->last_angle < 270.0f))
                {
                    state->control_mode = CONTROL_MODE_NORMAL;
                    float crank_progress = current_angle - 270.0f;
                    new_paddle_pos = PADDLE_MIN_X + (paddle_range * (crank_progress / 180.0f));
                }
                else
                {
                    new_paddle_pos = (float)PADDLE_MIN_X;
                }
                break;
            }
            state->paddle_x_pos = new_paddle_pos;
            state->last_angle = current_angle;
        }

        if (state->paddle_x_pos < PADDLE_MIN_X)
            state->paddle_x_pos = PADDLE_MIN_X;
        if (state->paddle_x_pos > PADDLE_MAX_X)
            state->paddle_x_pos = PADDLE_MAX_X;

        uint8_t final_x = (uint8_t)state->paddle_x_pos;

        ram_poke(ADDR_PADDLE_LOGIC_X, final_x + PADDLE_LOGIC_OFFSET);
        // write_paddle_sprite_data(ADDR_PADDLE_SHADOW_OAM_1, final_x);
        // write_paddle_sprite_data(ADDR_PADDLE_SHADOW_OAM_2, final_x);
        write_paddle_sprite_data(ADDR_PADDLE_HW_OAM, final_x);
    }
    else
    {
        // --- NOT IN-GAME ---
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
    .experimental = true,
    .on_begin = (CS_OnBegin)on_begin,
    .on_tick = (CS_OnTick)on_tick,
    .on_end = (CS_OnEnd)on_end,
};
