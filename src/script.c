#define lua_State pd_lua_State
#define lua_CFunction pd_lua_CFunction
#include "pd_api.h"
#undef lua_State
#undef lua_CFunction

#include "../minigb_apu/minigb_apu.h"
#include "utility.h"

// clang-format off
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
// clang-format on

#include "../peanut_gb/peanut_gb.h"
#include "app.h"
#include "dtcm.h"
#include "game_scene.h"
#include "jparse.h"
#include "script.h"
#include "scriptutil.h"
#include "userstack.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NOLUA

#define REGISTRY_GAME_SCENE_KEY "PGB_GameScene"

struct CScriptNode* c_script_list_head = NULL;

size_t c_script_count = 0;
const struct CScriptInfo** c_scripts = NULL;

struct gb_s* script_gb;

static bool lua_check_args(lua_State* L, int min, int max)
{
    int argc = lua_gettop(L);
    return argc >= min && argc <= max;
}

static struct PGB_GameScene* get_game_scene(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_GAME_SCENE_KEY);
    struct PGB_GameScene* scene = (struct PGB_GameScene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return scene;
}

static struct gb_s* get_gb(lua_State* L)
{
    return get_game_scene(L)->context->gb;
}

static int pgb_rom_size(lua_State* L)
{
    struct gb_s* gb = get_gb(L);
    lua_pushinteger(L, gb->gb_rom_size);
    return 1;
}

static int pgb_rom_poke(lua_State* L)
{
    if (!lua_check_args(L, 2, 2))
    {
        return luaL_error(L, "pgb.rom_poke(addr, value) takes two arguments");
    }

    struct gb_s* gb = get_gb(L);

    int addr = luaL_checkinteger(L, 1);
    int value = luaL_checkinteger(L, 2);
    size_t rom_size = gb->gb_rom_size;

    if (addr < 0 || addr >= rom_size)
    {
        return luaL_error(L, "pgb.rom_poke: addr %p out of range (0-%p)", addr, rom_size - 1);
    }

    gb->gb_rom[addr] = value;
    return 0;
}

int set_hw_breakpoint(struct gb_s* gb, uint32_t rom_addr);
static int pgb_rom_set_breakpoint(lua_State* L)
{

    // returns: breakpoint index, or null on failure
    if (!lua_check_args(L, 2, 2))
    {
        return luaL_error(L, "pgb.rom_set_breakpoint(addr, function) takes two arguments");
    }

    struct gb_s* gb = get_gb(L);
    int addr = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    size_t rom_size = gb->gb_rom_size;
    if (addr < 0 || addr >= rom_size)
    {
        return luaL_error(
            L, "pgb.rom_set_breakpoint: addr %[ out of range (0-%p)", addr, rom_size - 1
        );
    }
    int breakpoint_index = set_hw_breakpoint(gb, addr);
    if (breakpoint_index == -1)
    {
        return luaL_error(L, "pgb.rom_set_breakpoint: too many breakpoints set");
    }
    else if (breakpoint_index < 0)
    {
        return luaL_error(L, "pgb.rom_set_breakpoint: failed to set breakpoint at addr %p", addr);
    }

    // store the function in a table in the registry
    lua_getfield(L, LUA_REGISTRYINDEX, "pgb_breakpoints");
    if (!lua_istable(L, -1))
    {
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "pgb_breakpoints");
        lua_getfield(L, LUA_REGISTRYINDEX, "pgb_breakpoints");
    }

    lua_pushinteger(L, breakpoint_index);
    lua_pushvalue(L, 2);  // push the function
    lua_settable(L,
                 -3);  // set the function in the table with the breakpoint index as key

    lua_pop(L, 1);                         // pop the table
    lua_pushinteger(L, breakpoint_index);  // return the breakpoint index
    return 1;
}

static int pgb_rom_peek(lua_State* L)
{
    if (!lua_check_args(L, 1, 1))
    {
        return luaL_error(L, "pgb.rom_peek(addr) takes one argument");
    }

    struct gb_s* gb = get_gb(L);

    int addr = luaL_checkinteger(L, 1);
    size_t rom_size = gb->gb_rom_size;

    if (addr < 0 || addr >= rom_size)
    {
        return luaL_error(L, "pgb.rom_peek: addr %p out of range (0-%p)", addr, rom_size - 1);
    }

    lua_pushinteger(L, gb->gb_rom[addr]);
    return 1;
}

uint8_t __gb_read_full(struct gb_s* gb, const uint_fast16_t addr);
void __gb_write_full(struct gb_s* gb, const uint_fast16_t addr, uint8_t);

static int pgb_ram_peek(lua_State* L)
{
    if (!lua_check_args(L, 1, 1))
    {
        return luaL_error(L, "pgb.ram_peek(addr) takes one argument");
    }

    struct gb_s* gb = get_gb(L);

    int addr = luaL_checkinteger(L, 1);

    if (addr < 0 || addr >= 0x10000)
    {
        return luaL_error(L, "pgb.ram_peek: addr out of range (0-FFFF)");
    }

    lua_pushinteger(L, __gb_read_full(gb, addr));
    return 1;
}

static int pgb_ram_poke(lua_State* L)
{
    if (!lua_check_args(L, 2, 2))
    {
        return luaL_error(L, "pgb.ram_poke(addr, value) takes two arguments");
    }

    struct gb_s* gb = get_gb(L);

    int addr = luaL_checkinteger(L, 1);
    int val = luaL_checkinteger(L, 2);

    if (addr < 0 || addr >= 0x10000)
    {
        return luaL_error(L, "pgb.ram_peek: addr out of range (0-FFFF)");
    }

    __gb_write_full(gb, addr, val);
    return 0;
}

static int pgb_get_gb_buttons(lua_State* L)
{
    struct gb_s* gb = get_gb(L);
    lua_pushinteger(L, gb->direct.joypad ^ 0xFF);
    return 1;
}

static int pgb_get_crank(lua_State* L)
{
    if (playdate->system->isCrankDocked())
        return 0;

    float angle = playdate->system->getCrankAngle();
    lua_pushnumber(L, angle);
    return 1;
}

static int pgb_setCrankSoundsDisabled(lua_State* L)
{
    if (playdate->system->isCrankDocked())
        return 0;

    // get boolean value
    int disabled = lua_toboolean(L, 1);
    playdate->system->setCrankSoundsDisabled(disabled);
    return 0;
}

__section__(".rare") static int pgb_force_pref(lua_State* L)
{
    if (!lua_check_args(L, 2, 2))
    {
        return luaL_error(L, "pgb.force_pref(preference, value) takes two arguments");
    }

    const char* preference = luaL_checkstring(L, 1);
    int value = luaL_checkinteger(L, 2);

    int i = 0;
#define PREF(x, ...)                                                                    \
    if (strcmp(preference, #x) == 0)                                                    \
    {                                                                                   \
        preferences_##x = value;                                                        \
        prefs_locked_by_script |= (1 << (preferences_bitfield_t)i);          \
        playdate->system->logToConsole("forced preference %s=%d\n", preference, value); \
        return 0;                                                                       \
    }                                                                                   \
    i++;
#include "prefs.x"

    return luaL_error(L, "ERROR: unrecognized pref \"%s\"", preference);
}

static int pgb_step_cpu(lua_State* L)
{
    // UNTESTED
    struct gb_s* gb = get_gb(L);
    __gb_step_cpu(gb);
    return 0;
}

static int pgb_close(lua_State* L)
{
    if (!lua_check_args(L, 0, 0))
    {
        return luaL_error(L, "pgb.close() takes no arguments");
    }
    struct PGB_GameScene* scene = get_game_scene(L);
    if (scene)
    {
        PGB_GameScene_didSelectLibrary(scene);
    }
    return 0;
}

__section__(".rare") static int pgb_regs_index(lua_State* L)
{
    struct gb_s* gb = get_gb(L);
    const char* reg_name = luaL_checkstring(L, 2);

    if (strcmp(reg_name, "af") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.af);
    }
    else if (strcmp(reg_name, "a") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.a);
    }
    else if (strcmp(reg_name, "f") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.f);
    }
    else if (strcmp(reg_name, "bc") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.bc);
    }
    else if (strcmp(reg_name, "b") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.b);
    }
    else if (strcmp(reg_name, "c") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.c);
    }
    else if (strcmp(reg_name, "de") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.de);
    }
    else if (strcmp(reg_name, "d") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.d);
    }
    else if (strcmp(reg_name, "e") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.e);
    }
    else if (strcmp(reg_name, "hl") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.hl);
    }
    else if (strcmp(reg_name, "h") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.h);
    }
    else if (strcmp(reg_name, "l") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.l);
    }
    else if (strcmp(reg_name, "sp") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.sp);
    }
    else if (strcmp(reg_name, "pc") == 0)
    {
        lua_pushinteger(L, gb->cpu_reg.pc);
    }
    else
    {
        return luaL_error(L, "pgb.regs: unknown register '%s'", reg_name);
    }

    return 1;
}

__section__(".rare") static int pgb_regs_newindex(lua_State* L)
{
    struct gb_s* gb = get_gb(L);
    const char* reg_name = luaL_checkstring(L, 2);
    int value = luaL_checkinteger(L, 3);

    if (strcmp(reg_name, "af") == 0)
    {
        gb->cpu_reg.af = value;
    }
    else if (strcmp(reg_name, "a") == 0)
    {
        gb->cpu_reg.a = value;
    }
    else if (strcmp(reg_name, "f") == 0)
    {
        gb->cpu_reg.f = value;
    }
    else if (strcmp(reg_name, "bc") == 0)
    {
        gb->cpu_reg.bc = value;
    }
    else if (strcmp(reg_name, "b") == 0)
    {
        gb->cpu_reg.b = value;
    }
    else if (strcmp(reg_name, "c") == 0)
    {
        gb->cpu_reg.c = value;
    }
    else if (strcmp(reg_name, "de") == 0)
    {
        gb->cpu_reg.de = value;
    }
    else if (strcmp(reg_name, "d") == 0)
    {
        gb->cpu_reg.d = value;
    }
    else if (strcmp(reg_name, "e") == 0)
    {
        gb->cpu_reg.e = value;
    }
    else if (strcmp(reg_name, "hl") == 0)
    {
        gb->cpu_reg.hl = value;
    }
    else if (strcmp(reg_name, "h") == 0)
    {
        gb->cpu_reg.h = value;
    }
    else if (strcmp(reg_name, "l") == 0)
    {
        gb->cpu_reg.l = value;
    }
    else
    {
        return luaL_error(L, "pgb.regs: unknown register '%s'", reg_name);
    }

    return 0;
}

__section__(".rare") static void register_pgb_library(lua_State* L)
{
    lua_newtable(L);
    {
        lua_pushcfunction(L, pgb_close);
        lua_setfield(L, -2, "close");

        lua_pushcfunction(L, pgb_rom_poke);
        lua_setfield(L, -2, "rom_poke");

        lua_pushcfunction(L, pgb_rom_peek);
        lua_setfield(L, -2, "rom_peek");

        lua_pushcfunction(L, pgb_rom_set_breakpoint);
        lua_setfield(L, -2, "rom_set_breakpoint");

        lua_pushcfunction(L, pgb_ram_poke);
        lua_setfield(L, -2, "ram_poke");

        lua_pushcfunction(L, pgb_ram_peek);
        lua_setfield(L, -2, "ram_peek");

        lua_pushcfunction(L, pgb_get_gb_buttons);
        lua_setfield(L, -2, "get_gb_buttons");

        lua_pushcfunction(L, pgb_get_crank);
        lua_setfield(L, -2, "get_crank");

        lua_pushcfunction(L, pgb_setCrankSoundsDisabled);
        lua_setfield(L, -2, "setCrankSoundsDisabled");

        lua_pushcfunction(L, pgb_step_cpu);
        lua_setfield(L, -2, "step_cpu");

        lua_pushcfunction(L, pgb_rom_size);
        lua_setfield(L, -2, "rom_size");

        lua_pushcfunction(L, pgb_force_pref);
        lua_setfield(L, -2, "force_pref");

        // pgb.regs
        lua_newtable(L);
        {
            lua_newtable(L);
            lua_pushcfunction(L, pgb_regs_index);
            lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, pgb_regs_newindex);
            lua_setfield(L, -2, "__newindex");
            lua_setmetatable(L, -2);
        }
        lua_setfield(L, -2, "regs");
    }
    lua_setglobal(L, "pgb");
}

static void open_sandboxed_libs(lua_State* L)
{
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1);
    lua_pop(L, 1);
}

static void set_package_path_l(lua_State* L)
{
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }

#ifdef TARGET_SIMULATOR
    lua_pushstring(L, "./Source/scripts/?.l");
#else
    lua_pushstring(L, "./scripts/?.l");
#endif
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
}

#endif

__section__(".rare") void script_info_free(ScriptInfo* info)
{
    if (!info)
        return;
    if (info->info)
        pgb_free(info->info);
    if (info->lua_script_path)
        pgb_free(info->lua_script_path);
    pgb_free(info);
}

__section__(".rare") ScriptInfo* get_script_info(const char* game_name)
{
#ifndef NOLUA
    // first, check for a lua script
    json_value v;
    if (!parse_json("scripts.json", &v, kFileRead | kFileReadData))
    {
        return NULL;
    }

    if (v.type != kJSONArray)
    {
        free_json_data(v);
        return NULL;
    }

    JsonArray* array = v.data.arrayval;
    for (size_t i = 0; i < array->n; i++)
    {
        json_value item = array->data[i];
        if (item.type != kJSONTable)
            continue;

        json_value jenabled = json_get_table_value(item, "enabled");
        if (jenabled.type == kJSONFalse)
            continue;

        json_value jname = json_get_table_value(item, "name");
        json_value jscript = json_get_table_value(item, "script");
        json_value jinfo = json_get_table_value(item, "info");

        const char* name = (jname.type == kJSONString) ? jname.data.stringval : NULL;
        const char* lua_script_path_from_json =
            (jscript.type == kJSONString) ? jscript.data.stringval : NULL;

        if (name && lua_script_path_from_json && strcmp(name, game_name) == 0)
        {
            ScriptInfo* info = allocz(ScriptInfo);
            const char* script_info_str = (jinfo.type == kJSONString) ? jinfo.data.stringval : NULL;

#ifdef TARGET_SIMULATOR
            char fullpath[1024];
            snprintf(fullpath, sizeof(fullpath), "Source/%s", lua_script_path_from_json);
            info->lua_script_path = string_copy(fullpath);
#else
            info->lua_script_path = string_copy(lua_script_path_from_json);
#endif

            info->info = script_info_str ? string_copy(strltrim(script_info_str)) : NULL;

            json_value jexperimental = json_get_table_value(item, "experimental");
            info->experimental = jexperimental.type == kJSONTrue;

            strncpy(info->rom_name, game_name, 16);
            info->rom_name[16] = 0;

            free_json_data(v);
            return info;
        }
    }
    free_json_data(v);
#endif

    // no lua script, so check for C script.
    // (We prioritize lua scripts to allow a user to replace a C
    //  script with a Lua script.)
    for (size_t i = 0; i < c_script_count && c_scripts; ++i)
    {
        const struct CScriptInfo* cinfo = c_scripts[i];
        if (cinfo && !strcmp(cinfo->rom_name, game_name))
        {
            ScriptInfo* info = allocz(ScriptInfo);
            info->c_script_info = cinfo;
            info->info = cinfo->description ? string_copy(strltrim(cinfo->description)) : NULL;
            info->experimental = cinfo->description;
            strncpy(info->rom_name, game_name, 16);
            info->rom_name[16] = 0;  // paranoia
            return info;
        }
    }

    return NULL;
}

// TODO: should take rom data instead (and extract from it the game name)
ScriptState* script_begin(const char* game_name, struct PGB_GameScene* game_scene)
{
    DTCM_VERIFY();

    ScriptInfo* info = get_script_info(game_name);
    script_gb = game_scene->context->gb;

    if (!info)
        return NULL;

    ScriptState* state = allocz(ScriptState);

    // (exactly one or the other)
    PGB_ASSERT(!info->lua_script_path ^ !info->c_script_info);

#ifndef NOLUA
    if (info->lua_script_path)
    {
        lua_State* L = NULL;

        playdate->system->logToConsole("Using Lua script %s", info->lua_script_path);

        L = luaL_newstate();
        state->L = L;
        game_scene->script = state;  // ugly hack, set it early before the script starts
        open_sandboxed_libs(L);
        set_package_path_l(L);

        lua_pushlightuserdata(L, (void*)game_scene);
        lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_GAME_SCENE_KEY);

        register_pgb_library(L);

        DTCM_VERIFY();

        if (luaL_dofile(L, info->lua_script_path) != LUA_OK)
        {
            const char* err = lua_tostring(L, -1);
            playdate->system->logToConsole("Lua error: %s", err);
            lua_close(L);

            DTCM_VERIFY();
            script_info_free(info);
            return NULL;
        }

        script_info_free(info);
        DTCM_VERIFY();
    }
#endif

    if (info->c_script_info)
    {
        const struct CScriptInfo* csi = info->c_script_info;
        state->c = csi;
        game_scene->script = state;  // ugly hack, set it early before the script starts

        playdate->system->logToConsole("Using C script for %s", info->rom_name);
        if (csi->on_begin)
        {
            state->ud = csi->on_begin(game_scene->context->gb, game_name);
        }
        else
        {
            playdate->system->error("Script returned NULL from on_begin, indicating an error.");
            pgb_free(state);
            return NULL;
        }
    }

    script_info_free(info);
    return state;
}

void script_end(ScriptState* state, struct PGB_GameScene* game_scene)
{
    script_gb = game_scene->context->gb;

#ifndef NOLUA
    if (state->L)
    {
        lua_close(state->L);
    }
#endif

    if (state->c)
    {
        state->c->on_end(game_scene->context->gb, state->ud);
    }

    if (state->cbp)
        pgb_free(state->cbp);

    pgb_free(state);
}

void script_tick(ScriptState* state, struct PGB_GameScene* game_scene)
{
    script_gb = game_scene->context->gb;

#ifndef NOLUA
    if (state->L)
    {
        lua_State* L = state->L;
        lua_getglobal(L, "pgb");
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return;
        }

        lua_getfield(L, -1, "update");
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 2);  // pop update and pgb
            return;
        }

        lua_remove(L, -2);  // remove pgb, leave update
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        {
            const char* err = lua_tostring(L, -1);
            playdate->system->logToConsole("script_tick error: %s", err);
            lua_pop(L, 1);
        }
    }
#endif

    if (state->c && state->c->on_tick)
    {
        state->c->on_tick(game_scene->context->gb, state->ud);
    }
}

void script_draw(ScriptState* state, struct PGB_GameScene* game_scene)
{
    script_gb = game_scene->context->gb;

#ifndef NOLUA
    if (state->L)
    {
        // TODO: lua on-draw
    }
#endif

    if (state->c && state->c->on_draw)
    {
        state->c->on_draw(game_scene->context->gb, state->ud);
    }
}

// for C scripts
__section__(".rare") int c_script_add_hw_breakpoint(
    struct gb_s* gb, uint16_t addr, CS_OnBreakpoint callback
)
{
    // get script from gb (rather indirect :/)
    PGB_GameSceneContext* context = gb->direct.priv;
    PGB_GameScene* scene = context->scene;
    ScriptState* state = scene->script;

    PGB_ASSERT(state);

    int bp = set_hw_breakpoint(gb, addr);

    if (bp < 0)
    {
        return bp;
    }
    if (bp >= MAX_BREAKPOINTS)
        return -101;

    if (!state->cbp)
    {
        state->cbp = pgb_realloc(state->cbp, MAX_BREAKPOINTS * sizeof(*state->cbp));
        if (!state->cbp)
            return -100;
    }

    state->cbp[bp] = callback;
    return bp;
}

__section__(".rare") void script_on_breakpoint(struct PGB_GameScene* gameScene, int index)
{
    script_gb = gameScene->context->gb;

    ScriptState* state = gameScene->script;
    struct gb_s* gb = gameScene->context->gb;

#ifndef NOLUA
    if (state->L)
    {
        lua_State* L = state->L;

        // get lua top, store so it can be reset to later
        int top = lua_gettop(L);

        // Execute function from registry, breakpoint number index
        lua_getfield(L, LUA_REGISTRYINDEX, "pgb_breakpoints");
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return;
        }
        lua_pushinteger(L, index);
        lua_gettable(L, -2);  // get function at index
        if (!lua_isfunction(L, -1))
        {
            playdate->system->logToConsole("Unknown breakpoint %d\n", index);
            lua_pop(L, 2);  // pop function and table
            return;
        }
        lua_remove(L, -2);  // remove table, leave function
        // call function, pass index as argument
        lua_pushinteger(L, index);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            const char* err = lua_tostring(L, -1);
            playdate->system->logToConsole("script breakpoint error: %s", err);
            lua_pop(L, 1);
        }

        lua_settop(L, top);
    }
#endif

    if (state->c && state->cbp)
    {
        CS_OnBreakpoint cb = state->cbp[index];
        cb(gb, gb->cpu_reg.pc, index, state->ud);
    }
}

const char* gb_get_rom_name(uint8_t* gb_rom, char* title_str);

ScriptInfo* script_get_info_by_rom_path_(const char* game_path)
{
    // first, open the ROM to read the game name
    size_t len;
    SDFile* file = playdate->file->open(game_path, kFileReadDataOrBundle);
    if (!file)
        return NULL;

    uint8_t buff[0x200];

    int read = playdate->file->read(file, buff, sizeof(buff));
    playdate->file->close(file);
    if (read != sizeof(buff))
    {
        return NULL;
    }

    char title[17];
    gb_get_rom_name(buff, title);

    ScriptInfo* info = get_script_info(title);

    return info;
}

ScriptInfo* script_get_info_by_rom_path(const char* game_path)
{
    return (ScriptInfo*)call_with_main_stack_1(script_get_info_by_rom_path_, game_path);
}

bool script_exists(const char* game_path)
{
    ScriptInfo* info = script_get_info_by_rom_path(game_path);

    if (!info)
        return false;

    script_info_free(info);
    return true;
}

void pgb_register_all_scripts(void)
{
    for (struct CScriptNode* node = c_script_list_head; node; node = node->next)
    {
        register_c_script(node->info);
    }
}

void register_c_script(const struct CScriptInfo* info)
{
    c_scripts = pgb_realloc(c_scripts, sizeof(struct CScriptInfo*) * ++c_script_count);
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
        pgb_free(c_scripts);
        c_scripts = NULL;
        c_script_count = 0;
    }
}
