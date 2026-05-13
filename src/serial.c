#include "serial.h"

#include "app.h"
#include "ft.h"
#include "scenes/library_scene.h"
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

// Main ft (file transfer) command handler
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

// emit "cb:<cmd>:error:<code>[:<urlenc-geterr>]"
static void serial_cb_send_fs_error(const char* cmd, const char* code)
{
    const char* err = playdate->file->geterr();
    if (err && *err)
    {
        char* enc = url_encode(err);
        if (enc)
        {
            serial_send_response("cb:%s:error:%s:%s", cmd, code, enc);
            cb_free(enc);
            return;
        }
    }
    serial_send_response("cb:%s:error:%s", cmd, code);
}

// cb:rm:<path>[:r]   (raw '/' OK; only ':' and '%' need %-escaping)
static bool serial_cb_rm(const char* const* tokens)
{
    if (!tokens[2])
    {
        serial_send_response("cb:rm:error:args");
        return true;
    }
    char path[512];
    if (url_decode(tokens[2], path, sizeof(path)) < 0)
    {
        serial_send_response("cb:rm:error:filename");
        return true;
    }
    int recursive = (tokens[3] && strcmp(tokens[3], "r") == 0) ? 1 : 0;
    if (playdate->file->unlink(path, recursive) != 0)
    {
        serial_cb_send_fs_error("rm", "io");
        return true;
    }
    serial_send_response("cb:rm:ok");
    return true;
}

// cb:mv:<from>:<to>
static bool serial_cb_mv(const char* const* tokens)
{
    if (!tokens[2] || !tokens[3])
    {
        serial_send_response("cb:mv:error:args");
        return true;
    }
    char from[512];
    char to[512];
    if (url_decode(tokens[2], from, sizeof(from)) < 0 || url_decode(tokens[3], to, sizeof(to)) < 0)
    {
        serial_send_response("cb:mv:error:filename");
        return true;
    }
    if (playdate->file->rename(from, to) != 0)
    {
        serial_cb_send_fs_error("mv", "io");
        return true;
    }
    serial_send_response("cb:mv:ok");
    return true;
}

// cb:stat:<path> -> cb:stat:ok:<isdir>:<size>:<YYYYMMDDhhmmss>
static bool serial_cb_stat(const char* const* tokens)
{
    if (!tokens[2])
    {
        serial_send_response("cb:stat:error:args");
        return true;
    }
    char path[512];
    if (url_decode(tokens[2], path, sizeof(path)) < 0)
    {
        serial_send_response("cb:stat:error:filename");
        return true;
    }
    FileStat st;
    if (playdate->file->stat(path, &st) != 0)
    {
        serial_cb_send_fs_error("stat", "notfound");
        return true;
    }
    serial_send_response(
        "cb:stat:ok:%d:%u:%04d%02d%02d%02d%02d%02d", st.isdir, st.size, st.m_year, st.m_month,
        st.m_day, st.m_hour, st.m_minute, st.m_second
    );
    return true;
}

// cb:mkdir:<path>[:p]   (:p creates intermediates via full_mkdir)
static bool serial_cb_mkdir(const char* const* tokens)
{
    if (!tokens[2])
    {
        serial_send_response("cb:mkdir:error:args");
        return true;
    }
    char path[512];
    if (url_decode(tokens[2], path, sizeof(path)) < 0)
    {
        serial_send_response("cb:mkdir:error:filename");
        return true;
    }
    bool with_intermediates = (tokens[3] && strcmp(tokens[3], "p") == 0);
    int rc = with_intermediates ? full_mkdir(path) : playdate->file->mkdir(path);
    if (rc != 0)
    {
        serial_cb_send_fs_error("mkdir", "io");
        return true;
    }
    serial_send_response("cb:mkdir:ok");
    return true;
}

typedef struct
{
    char** names;
    int count;
    int capacity;
    bool oom;
} CbLsCtx;

static void cb_ls_collect(const char* filename, void* userdata)
{
    CbLsCtx* ctx = userdata;
    if (ctx->oom)
        return;
    if (ctx->count >= ctx->capacity)
    {
        int new_cap = ctx->capacity ? ctx->capacity * 2 : 16;
        char** grown = cb_realloc(ctx->names, (size_t)new_cap * sizeof(char*));
        if (!grown)
        {
            ctx->oom = true;
            return;
        }
        ctx->names = grown;
        ctx->capacity = new_cap;
    }
    char* dup = cb_strdup(filename);
    if (!dup)
    {
        ctx->oom = true;
        return;
    }
    ctx->names[ctx->count++] = dup;
}

// cb:ls:<absolute-path>
//   cb:ls:ok:<count>
//   cb:ls:exists:<absolute-path>   (one per entry; trailing / for dirs)
//   cb:ls:omit                     (placeholder if path won't fit in 256B line)
static bool serial_cb_ls(const char* const* tokens)
{
    if (!tokens[2])
    {
        serial_send_response("cb:ls:error:args");
        return true;
    }
    char path[512];
    if (url_decode(tokens[2], path, sizeof(path)) < 0)
    {
        serial_send_response("cb:ls:error:filename");
        return true;
    }
    if (path[0] != '/')
    {
        serial_send_response("cb:ls:error:notabs");
        return true;
    }
    // always treat the arg as a directory: ensure a trailing slash so the
    // playdate file API distinguishes "list this dir" from "stat this name"
    size_t plen = strlen(path);
    if (plen == 0 || path[plen - 1] != '/')
    {
        if (plen + 1 >= sizeof(path))
        {
            serial_send_response("cb:ls:error:filename");
            return true;
        }
        path[plen++] = '/';
        path[plen] = '\0';
    }

    CbLsCtx ctx = {0};
    if (playdate->file->listfiles(path, cb_ls_collect, &ctx, 1) != 0)
    {
        serial_cb_send_fs_error("ls", "notfound");
        for (int i = 0; i < ctx.count; i++)
            cb_free(ctx.names[i]);
        cb_free(ctx.names);
        return true;
    }
    if (ctx.oom)
    {
        serial_send_response("cb:ls:error:nomem");
        for (int i = 0; i < ctx.count; i++)
            cb_free(ctx.names[i]);
        cb_free(ctx.names);
        return true;
    }

    // "cb:ls:exists:" is 13 chars; response buffer is 256 incl. NUL, so 255 max line.
    const size_t budget = 255 - 13;

    serial_send_response("cb:ls:ok:%d", ctx.count);

    for (int i = 0; i < ctx.count; i++)
    {
        // listfiles sometimes returns entries with a leading '/'; strip it
        // so we don't double up against the trailing '/' we ensured on path
        const char* entry = ctx.names[i];
        if (*entry == '/')
            entry++;

        if (plen + strlen(entry) > budget)
        {
            serial_send_response("cb:ls:omit");
        }
        else
        {
            serial_send_response("cb:ls:exists:%s%s", path, entry);
        }
        cb_free(ctx.names[i]);
    }
    cb_free(ctx.names);
    return true;
}

// cb:crc32:<path>  ->  cb:crc32:ok:<HEX8>
static bool serial_cb_crc32(const char* const* tokens)
{
    if (!tokens[2])
    {
        serial_send_response("cb:crc32:error:args");
        return true;
    }
    char path[512];
    if (url_decode(tokens[2], path, sizeof(path)) < 0 || strstr(path, "..") != NULL)
    {
        serial_send_response("cb:crc32:error:filename");
        return true;
    }
    uint32_t crc = 0;
    if (!cb_calculate_crc32(path, kFileRead | kFileReadData, &crc))
    {
        serial_cb_send_fs_error("crc32", "io");
        return true;
    }
    serial_send_response("cb:crc32:ok:%08X", crc);
    return true;
}

// cb:read:<path>:<decimal-offset>  ->  cb:read:ok:<bytes>:<base64>
// reads up to 64 bytes from offset. <bytes> = actual count (may be < 64 at EOF).
static bool serial_cb_read(const char* const* tokens)
{
    if (!tokens[2] || !tokens[3])
    {
        serial_send_response("cb:read:error:args");
        return true;
    }
    char path[512];
    if (url_decode(tokens[2], path, sizeof(path)) < 0 || strstr(path, "..") != NULL)
    {
        serial_send_response("cb:read:error:filename");
        return true;
    }
    char* endptr = NULL;
    long offset = strtol(tokens[3], &endptr, 10);
    if (!endptr || *endptr != '\0' || offset < 0)
    {
        serial_send_response("cb:read:error:offset");
        return true;
    }
    SDFile* f = playdate->file->open(path, kFileRead | kFileReadData);
    if (!f)
    {
        serial_cb_send_fs_error("read", "io");
        return true;
    }
    if (playdate->file->seek(f, (int)offset, SEEK_SET) != 0)
    {
        serial_cb_send_fs_error("read", "io");
        playdate->file->close(f);
        return true;
    }
    uint8_t buf[64];
    int n = playdate->file->read(f, buf, sizeof(buf));
    playdate->file->close(f);
    if (n < 0)
    {
        serial_cb_send_fs_error("read", "io");
        return true;
    }
    char b64[128];
    if (base64_encode(buf, (size_t)n, b64, sizeof(b64)) < 0)
    {
        serial_send_response("cb:read:error:io");
        return true;
    }
    serial_send_response("cb:read:ok:%d:%s", n, b64);
    return true;
}

// emit a per-game string field, dropping with :omit if the line would overflow
static void emit_games_str(unsigned int i, const char* label, const char* value)
{
    if (!value || !*value)
        return;
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "cb:games:%u:%s:%s", i, label, value);
    if (n < 0 || (size_t)n >= sizeof(buf))
    {
        serial_send_response("cb:games:%u:omit:%s", i, label);
        return;
    }
    serial_send_response("%s", buf);
}

// cb:games  ->  cb:games:<count>, then per-game records terminated by :end
static bool serial_cb_games(const char* const* tokens)
{
    (void)tokens;
    CB_Array* games = CB_App ? CB_App->gameListCache : NULL;
    unsigned int count = games ? games->length : 0;
    serial_send_response("cb:games:%u", count);

    for (unsigned int i = 0; i < count; i++)
    {
        CB_Game* g = games->items[i];
        if (g)
        {
            const CB_GameName* nm = g->names;
            if (nm)
            {
                serial_send_response("cb:games:%u:crc32:%08X", i, nm->crc32);
            }
            emit_games_str(i, "path", g->fullpath);
            if (nm)
            {
                emit_games_str(i, "short_title", nm->name_short);
                emit_games_str(i, "long_title", nm->name_detailed);
                emit_games_str(i, "header_title", nm->name_header);
                emit_games_str(i, "filename_title", nm->name_filename);
            }
            emit_games_str(i, "display_name", g->displayName);
            emit_games_str(i, "sort_name", g->sortName);
        }
        serial_send_response("cb:games:%u:end", i);
    }
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
    else if (strcmp(subcmd, "ls") == 0)
    {
        return serial_cb_ls(tokens);
    }
    else if (strcmp(subcmd, "rm") == 0)
    {
        return serial_cb_rm(tokens);
    }
    else if (strcmp(subcmd, "mv") == 0)
    {
        return serial_cb_mv(tokens);
    }
    else if (strcmp(subcmd, "stat") == 0)
    {
        return serial_cb_stat(tokens);
    }
    else if (strcmp(subcmd, "mkdir") == 0)
    {
        return serial_cb_mkdir(tokens);
    }
    else if (strcmp(subcmd, "crc32") == 0)
    {
        return serial_cb_crc32(tokens);
    }
    else if (strcmp(subcmd, "read") == 0)
    {
        return serial_cb_read(tokens);
    }
    else if (strcmp(subcmd, "games") == 0)
    {
        return serial_cb_games(tokens);
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
