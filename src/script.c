#include "../libs/minigb_apu/minigb_apu.h"
#include "../libs/peanut_gb.h"
#include "app.h"
#include "dtcm.h"
#include "jparse.h"
#include "pd_api.h"
#include "scenes/game_scene.h"
/* clang-format off */
#include "script.h"
/* clang-format on */
#include "gbz.h"
#include "scriptutil.h"
#include "userstack.h"
#include "utility.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CScriptNode* c_script_list_head = NULL;

size_t c_script_count = 0;
const struct CScriptInfo** c_scripts = NULL;

gb_s* script_gb;

int set_hw_breakpoint(gb_s* gb, uint32_t rom_addr);

__section__(".rare") void script_info_free(ScriptInfo* info)
{
    if (!info)
        return;
    if (info->info)
        cb_free(info->info);
    cb_free(info);
}

__section__(".rare") ScriptInfo* get_script_info(const char* game_name)
{
    for (size_t i = 0; i < c_script_count && c_scripts; ++i)
    {
        const struct CScriptInfo* cinfo = c_scripts[i];
        if (cinfo && !strcmp(cinfo->rom_name, game_name))
        {
            ScriptInfo* info = allocz(ScriptInfo);
            info->c_script_info = cinfo;
            info->info = cinfo->description ? cb_strdup(strltrim(cinfo->description)) : NULL;
            info->experimental = cinfo->experimental;
            info->launch_cgb = cinfo->launch_cgb;
            strncpy(info->rom_name, game_name, 16);
            info->rom_name[16] = 0;  // paranoia
            return info;
        }
    }

    return NULL;
}

// TODO: should take rom data instead (and extract from it the game name)
ScriptState* script_begin(const char* game_name, struct CB_GameScene* game_scene)
{
    DTCM_VERIFY();

    ScriptInfo* info = get_script_info(game_name);
    script_gb = game_scene->context->gb;

    if (!info)
        return NULL;

    ScriptState* state = allocz(ScriptState);

    if (info->c_script_info)
    {
        const struct CScriptInfo* csi = info->c_script_info;
        state->c = csi;
        game_scene->script = state;  // ugly hack, set it early before the script starts

        playdate->system->logToConsole("Using C script for %s", info->rom_name);
        if (csi->on_begin)
        {
            state->ud = csi->on_begin(game_scene->context->gb, game_name);

            if (!state->ud)
            {
                playdate->system->error("Script returned NULL from on_begin, indicating an error.");
                cb_free(state);
                return NULL;
            }
        }
    }

    script_info_free(info);
    return state;
}

void script_end(ScriptState* state, struct CB_GameScene* game_scene)
{
    script_gb = game_scene->context->gb;

    if (state->c)
    {
        state->c->on_end(game_scene->context->gb, state->ud);
    }

    if (state->cbp)
        cb_free(state->cbp);

    cb_free(state);
}

size_t script_query_savestate_size(ScriptState* state)
{
    if (state->c && state->c->query_serial_size)
    {
        return state->c->query_serial_size(state->ud);
    }
    return 0;
}
bool script_save_state(ScriptState* state, uint8_t* out)
{
    if (state->c->query_serial_size && state->c->serialize)
    {
        return state->c->serialize((void*)out, state->ud);
    }
    return true;
}
bool script_load_state(ScriptState* state, const uint8_t* in, size_t size)
{
    if (state->c->query_serial_size && state->c->deserialize)
    {
        return state->c->deserialize((void*)in, size, state->ud);
    }
    return true;
}

bool suppress_gb_frame;

bool script_tick(ScriptState* state, struct CB_GameScene* game_scene, int frames_elapsed)
{
    script_gb = game_scene->context->gb;
    suppress_gb_frame = false;

    if (state->c && state->c->on_tick)
    {
        state->c->on_tick(game_scene->context->gb, state->ud, frames_elapsed);
    }

    return suppress_gb_frame;
}

void script_draw(ScriptState* state, struct CB_GameScene* game_scene)
{
    script_gb = game_scene->context->gb;

    if (state->c && state->c->on_draw)
    {
        state->c->on_draw(game_scene->context->gb, state->ud);
    }
}

unsigned script_menu(ScriptState* state, struct CB_GameScene* game_scene)
{
    if (!state)
        return 0;

    if (state->c && state->c->on_menu)
    {
        return state->c->on_menu(game_scene->context->gb, state->ud);
    }

    return 0;
}

void script_add_settings(ScriptState* state)
{
    if (state && state->c && state->c->on_settings)
    {
        state->c->on_settings(state->ud);
    }
}

__section__(".rare") int c_script_add_hw_breakpoint(
    gb_s* gb, uint32_t addr, CS_OnBreakpoint callback
)
{
    // get script from gb (rather indirect :/)
    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* scene = context->scene;
    ScriptState* state = scene->script;

    CB_ASSERT(state);

    int bp = set_hw_breakpoint(gb, addr);

    if (bp < 0)
    {
        return bp;
    }
    if (bp >= MAX_BREAKPOINTS)
        return -101;

    if (!state->cbp)
    {
        state->cbp = cb_realloc(state->cbp, MAX_BREAKPOINTS * sizeof(*state->cbp));
        if (!state->cbp)
            return -100;
    }

    state->cbp[bp] = callback;
    return bp;
}

__section__(".rare") void script_on_breakpoint(struct CB_GameScene* gameScene, int index)
{
    script_gb = gameScene->context->gb;

    ScriptState* state = gameScene->script;
    gb_s* gb = gameScene->context->gb;

    if (state->c && state->cbp)
    {
        CS_OnBreakpoint cb = state->cbp[index];
        cb(gb, gb->cpu_reg.pc, index, state->ud);
    }
}

const char* gb_get_rom_name(uint8_t* gb_rom, char* title_str);

struct ScriptInfoArgs
{
    const char* game_path;
    char* o_rom_name;
    unsigned* o_battery;
    enum cgb_support_e* o_cgb;
    int* o_is_gbz;
    uint32_t* o_gbz_checksum;
};

ScriptInfo* script_get_info_by_rom_path_(struct ScriptInfoArgs* args)
{
    if (!args)
        return NULL;

    if (args->o_rom_name)
        args->o_rom_name[0] = 0;

    // first, open the ROM to read the game name
    size_t len;
    SDFile* file = playdate->file->open(args->game_path, kFileReadDataOrBundle);
    if (!file)
        return NULL;

    uint8_t buff[0x150];

    int read = playdate->file->read(file, buff, sizeof(buff));
    playdate->file->close(file);
    if (read != sizeof(buff))
    {
        return NULL;
    }

    // handle compressed roms
    GBZ_Header gbz;
    if (gbz_parse_header(&gbz, buff, sizeof(buff)))
    {
        // replace header
        memcpy(buff + GBZ_ROM_HDR_START, gbz.gb_header, GBZ_ROM_HDR_SIZE);

        if (args->o_is_gbz)
            *args->o_is_gbz = 1;
        if (args->o_gbz_checksum)
            *args->o_gbz_checksum = gbz.crc32;
    }

    char title[17];
    gb_get_rom_name(buff, title);

    if (args->o_rom_name)
    {
        memcpy(args->o_rom_name, title, sizeof(title));
    }

    if (args->o_cgb)
    {
        *args->o_cgb = gb_get_models_supported(buff);
    }

    if (args->o_battery)
    {
        *args->o_battery = gb_get_rom_uses_battery(buff);
    }

    ScriptInfo* info = get_script_info(title);

    return info;
}

ScriptInfo* script_get_info_by_rom_path(const char* game_path)
{
    struct ScriptInfoArgs args = {
        .game_path = game_path,
        .o_rom_name = NULL,
        .o_battery = NULL,
        .o_cgb = NULL,
        .o_is_gbz = NULL,
        .o_gbz_checksum = NULL,
    };
    return (ScriptInfo*)call_with_main_stack_1(script_get_info_by_rom_path_, &args);
}

ScriptInfo* script_get_info_by_rom_path_and_get_header_info(
    const char* game_path, char* o_rom_name, enum cgb_support_e* o_cgb, unsigned* o_battery,
    int* o_is_gbz, uint32_t* o_gbz_checksum
)
{
    struct ScriptInfoArgs args = {
        .game_path = game_path,
        .o_rom_name = o_rom_name,
        .o_battery = o_battery,
        .o_cgb = o_cgb,
        .o_is_gbz = o_is_gbz,
        .o_gbz_checksum = o_gbz_checksum,
    };
    return (ScriptInfo*)call_with_main_stack_1(script_get_info_by_rom_path_, &args);
}

bool script_exists(const char* game_path)
{
    ScriptInfo* info = script_get_info_by_rom_path(game_path);

    if (!info)
        return false;

    script_info_free(info);
    return true;
}

void cb_register_all_c_scripts(void)
{
    for (struct CScriptNode* node = c_script_list_head; node; node = node->next)
    {
        register_c_script(node->info);
    }
}

void register_c_script(const struct CScriptInfo* info)
{
    c_scripts = cb_realloc(c_scripts, sizeof(struct CScriptInfo*) * ++c_script_count);
    if (c_scripts == NULL)
    {
        c_script_count = 0;
        playdate->system->error("Failed to allocate memory for C script list.");
        return;
    }

    c_scripts[c_script_count - 1] = info;
}

void script_quit(void)
{
    if (c_scripts)
    {
        cb_free(c_scripts);
        c_scripts = NULL;
        c_script_count = 0;
    }
}
