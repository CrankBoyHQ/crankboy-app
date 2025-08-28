#include "serial.h"
#include "app.h"

/*
 * Presently, this is for sending serial commands to the simulator.
 * Perhaps in the future it can exchange messages between playdates??
*/

#define TOKEN_MAX 8

static bool serial_pd_simulate_button_press(const char* const* tokens)
{
    // token 1: the playdate button(s) to press
    // token 2 (optional): the number of frames to hold the buttons for
    
    int n = 1;
    if (!tokens[1]) return false;
    if (tokens[2])
    {
        n = atoi(tokens[2]);
        if (n <= 0) return false;
    }
    
    PDButtons b = 0;
    
    for (const char* c = tokens[1]; *c; ++c)
    {
        switch(*c)
        {
        case 'a':
        case 'A':
            b |= kButtonA;
            break;
        case 'b':
        case 'B':
            b |= kButtonB;
            break;
        case 'l':
        case 'L':
            b |= kButtonLeft;
            break;
        case 'r':
        case 'R':
            b |= kButtonRight;
            break;
        case 'u':
        case 'U':
            b |= kButtonUp;
            break;
        case 'd':
        case 'D':
            b |= kButtonDown;
            break;
        }
    }
    
    for (int i = 0; i < 6; ++i)
    {
        if (b & (1 << i))
        {
            CB_App->simulate_button_presses[i] = n;
        }
    }
    return !!b;
}

typedef struct Command {
    const char* opcode;
    bool(*handler)(const char* const* tokens);
} Command;

static Command commands[] = {
    {.opcode = "pd", .handler = serial_pd_simulate_button_press},
    
    // terminator
    {.opcode = NULL}
};

static void CB_serial_command(char* command)
{
    char* tokens[TOKEN_MAX + 1];
    tokens[0] = command;
    for (size_t i = 1; i < TOKEN_MAX; ++i)
    {
        tokens[i] = 0;
        tokens[i + 1] = 0;
        char* colon = strchr(tokens[i-1], ':');
        if (!colon) break;
        colon[0] = 0; // replace colon with string-terminator
        tokens[i] = colon + 1;
    }
    
    for (Command* command = commands; command->opcode != NULL; ++command)
    {
        if (!strcasecmp(command->opcode, tokens[0]))
        {
            if (!command->handler((const char* const*)tokens))
            {
                playdate->system->logToConsole("[SERIAL] Serial command \"%s\" failed.", tokens[0]);
            }
            return;
        }
    }
    
    playdate->system->logToConsole("[SERIAL] Unrecognized command: \"%s\"", tokens[0]);
}

/*
* valid messages:
* pd:{abudlr}[:N]  (press playdate button [for N frames])
*   example: pd:a (press A button)
*   example: pd:l:8 (press d-pad left for 8 frames)
*/
void CB_on_serial_message(const char* data)
{
    while (data[0] != 0)
    {
        // split into spaces
        const char* space = strchr(data, ' ');
        if (space)
        {
            size_t command_len = space - data;
            char cmd[command_len + 1];
            memcpy(cmd, data, command_len);
            cmd[command_len] = 0;
            CB_serial_command(cmd);
            data += command_len + 1;
        }
        else
        {
            size_t len = strlen(data);
            char cmd[len + 1];
            memcpy(cmd, data, len);
            CB_serial_command(cmd);
            return;
        }
    }
}