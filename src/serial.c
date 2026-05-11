#include "serial.h"

#include "app.h"
#include "ft.h"
#include "scenes/sft_modal.h"
#include "utility.h"
#include "version.h"

#include <stdlib.h>
#include <string.h>

/*
 * Presently, this is for sending serial commands to the simulator.
 * Perhaps in the future it can exchange messages between playdates??
 */

#define TOKEN_MAX 8

// Main ft command handler
// Commands:
//   ft:b:<filename>:<size>:<crc32>    Begin transfer
//   ft:c:<seq>:<base64>               Send chunk (seq = 4-digit hex)
//   ft:e:<crc32>                      End transfer
//   ft:s                              Query status
static bool serial_ft_handler(const char* const* tokens)
{
    if (!tokens[1])
    {
        return false;
    }

    const char* subcmd = tokens[1];

    // ft:b - Begin transfer
    if (strcmp(subcmd, "b") == 0)
    {
        // ft:b:<filename>:<size>:<crc32>[:<original_filename>:<original_crc>]
        if (!tokens[2] || !tokens[3] || !tokens[4])
        {
            return false;
        }
        // Optional original filename and CRC (tokens[5] and tokens[6])
        return ft_handle_begin(tokens[2], tokens[3], tokens[4], tokens[5], tokens[6]);
    }
    // ft:c - Send chunk
    else if (strcmp(subcmd, "c") == 0)
    {
        // ft:c:<seq>:<crc16>:<base64data>
        if (!tokens[2] || !tokens[3] || !tokens[4])
        {
            return false;
        }
        return ft_handle_chunk(tokens[2], tokens[3], tokens[4]);
    }
    // ft:e - End transfer
    else if (strcmp(subcmd, "e") == 0)
    {
        // ft:e:crc32
        if (!tokens[2])
        {
            return false;
        }
        return ft_handle_end(tokens[2]);
    }
    // ft:s - Query status
    else if (strcmp(subcmd, "s") == 0)
    {
        return ft_handle_status();
    }

    return false;
}

static bool serial_pd_simulate_button_press(const char* const* tokens)
{
    // token 1: the playdate button(s) to press
    // token 2 (optional): the number of frames to hold the buttons for

    int n = 1;
    if (!tokens[1])
        return false;
    if (tokens[2])
    {
        n = atoi(tokens[2]);
        if (n <= 0)
            return false;
    }

    PDButtons b = 0;

    for (const char* c = tokens[1]; *c; ++c)
    {
        switch (*c)
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

// Handle cb:restart command - Restart CrankBoy
// Format: cb:restart
static bool serial_cb_restart(const char* const* tokens)
{
    (void)tokens;  // Unused
    serial_send_response("cb:restarting");
    // Small delay to allow response to be sent
    playdate->system->delay(100);
    playdate->system->restartGame(playdate->system->getLaunchArgs(NULL));
    return true;  // Never reached, but keeps compiler happy
}

// Handle cb:ping command - Host queries if CrankBoy is running
// Format: cb:ping
// Response: cb:pong:CrankBoy:<version>
static bool serial_cb_ping(const char* const* tokens)
{
    (void)tokens;  // Unused
    const char* version = get_current_version();
    if (version)
    {
        serial_send_response("cb:pong:CrankBoy:%s", version);
    }
    else
    {
        serial_send_response("cb:pong:CrankBoy:unknown");
    }
    return true;
}

// Handle cb:sft command - Serial File Transfer mode
// Format: cb:sft:on  - Show SFT modal
//         cb:sft:off - Hide SFT modal
// Response: cb:sft:ok or cb:sft:error:<reason>
static bool serial_cb_sft_handler(const char* const* tokens)
{
    if (!tokens[2])
    {
        return false;
    }

    const char* subcmd = tokens[2];

    if (strcmp(subcmd, "on") == 0)
    {
        // Check if already active
        if (CB_SFTModal_get_instance())
        {
            serial_send_response("cb:sft:error:already-active");
            return true;
        }

        CB_SFTModal* sftModal = CB_SFTModal_new();
        if (sftModal)
        {
            CB_presentModal(sftModal->scene);
            serial_send_response("cb:sft:ok");
        }
        else
        {
            serial_send_response("cb:sft:error:alloc-failed");
        }
        return true;
    }
    else if (strcmp(subcmd, "off") == 0)
    {
        CB_SFTModal* sftModal = CB_SFTModal_get_instance();
        if (sftModal)
        {
            CB_dismiss(sftModal->scene);
            // Note: The modal's free function will clear the global instance
            serial_send_response("cb:sft:ok");
        }
        else
        {
            serial_send_response("cb:sft:error:not-active");
        }
        return true;
    }

    return false;
}

// Handle cb:scene:get command - Get current scene stack
// Format: cb:scene:get
// Response: cb:scene:<root>.<...>.<current>
static bool serial_cb_scene_get(const char* const* tokens)
{
    (void)tokens;

    if (!CB_App->scene)
    {
        serial_send_response("cb:scene:unknown");
        return true;
    }

    size_t total = 0;
    int depth = 0;
    for (CB_Scene* s = CB_App->scene; s; s = s->parentScene)
    {
        const char* id = s->id ? s->id : "unknown";
        total += strlen(id);
        depth++;
    }
    total += (depth > 0 ? depth - 1 : 0);

    char* buf = cb_malloc(total + 1);
    buf[total] = 0;

    char* end = buf + total;
    for (CB_Scene* s = CB_App->scene; s; s = s->parentScene)
    {
        const char* id = s->id ? s->id : "unknown";
        size_t len = strlen(id);
        end -= len;
        memcpy(end, id, len);
        if (s->parentScene)
        {
            end -= 1;
            *end = '.';
        }
    }

    serial_send_response("cb:scene:%s", buf);
    cb_free(buf);
    return true;
}

// Handle cb: command - Routes to subcommands (restart, ping, sft)
// Format: cb:<subcommand>
static bool serial_cb_handler(const char* const* tokens)
{
    if (!tokens[1])
    {
        return false;
    }

    const char* subcmd = tokens[1];

    if (strcmp(subcmd, "restart") == 0)
    {
        return serial_cb_restart(tokens);
    }
    else if (strcmp(subcmd, "ping") == 0)
    {
        return serial_cb_ping(tokens);
    }
    else if (strcmp(subcmd, "sft") == 0)
    {
        return serial_cb_sft_handler(tokens);
    }
    else if (strcmp(subcmd, "scene") == 0)
    {
        if (tokens[2] && strcmp(tokens[2], "get") == 0)
        {
            return serial_cb_scene_get(tokens);
        }
        return false;
    }

    return false;
}

typedef struct Command
{
    const char* opcode;
    bool (*handler)(const char* const* tokens);
} Command;

static Command commands[] = {
    {.opcode = "pd", .handler = serial_pd_simulate_button_press},
    {.opcode = "ft", .handler = serial_ft_handler},
    {.opcode = "cb", .handler = serial_cb_handler},

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
        char* colon = strchr(tokens[i - 1], ':');
        if (!colon)
            break;
        colon[0] = 0;  // replace colon with string-terminator
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
 *
 * ft:b:<filename>:<size>:<crc32>[:<orig_name>:<orig_crc>]  (begin file transfer)
 *   example: ft:b:game.gbz:69120:D57F85C8:game.gb:A1B2C3D4
 * ft:c:<seq>:<crc16>:<base64>                              (send chunk)
 *   example: ft:c:0000:C3D4:SGVsbG8gV29ybGQ=
 * ft:e:<crc32>                                             (end transfer)
 *   example: ft:e:D57F85C8
 * ft:s                                                     (query status)
 *   example: ft:s
 *
 * Device responses:
 *   ft:r:<WWCC>        - Ready (WW=window size, CC=chunk size)
 *                        example: ft:r:10B1 (window=16, chunk=177)
 *   ft:a:<seq>         - ACK, example: ft:a:0000
 *   ft:n:<seq>:<code>  - NACK with error code, example: ft:n:0000:crc
 *                        codes: crc, seq, write, size
 *   ft:d:<base>:<bitmap> - Status (window_base + bitmap of received chunks)
 *                        example: ft:d:0010:FFFE
 *   ft:o:<filename>    - OK (decompressed name for GBZ), example: ft:o:game.gb
 *   ft:x:<code>        - Error, example: ft:x:crc
 *                        codes: busy, size, filename, extension, toobig, write, crc, nomem,
 *                               gbz_header, decompress, orig_crc, notransfer
 */
void CB_on_serial_message(const char* data)
{
    // Process messages immediately (keep messages small to avoid fragmentation)
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
            // Make a mutable copy of the final command
            size_t len = strlen(data);
            char cmd[len + 1];
            memcpy(cmd, data, len + 1);
            CB_serial_command(cmd);
            return;
        }
    }
}
