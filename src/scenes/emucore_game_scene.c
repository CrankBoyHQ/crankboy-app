#include "emucore_game_scene.h"

#include "../../libs/pdll/pdll.h"
#include "../app.h"
#include "../emucore_prefs.h"
#include "../preferences.h"
#include "../userstack.h"
#include "../utility.h"
#include "settings_scene.h"

#include <string.h>

static char* emucore_file_path(CB_EmucoreGameScene* es, const char* subdir, const char* suffix)
{
    char* base = cb_basename(es->rom_path, true);
    char* dir = cb_system_directory_path_for_slug(es->slug, subdir);
    char* path = aprintf("%s/%s%s", dir, base, suffix);
    cb_free(dir);
    cb_free(base);
    return path;
}

// FIXME -- use cb_save_filename():
static char* emucore_sram_path(CB_EmucoreGameScene* es)
{
    char suffix[12];
    if (preferences_save_slot)
        snprintf(suffix, sizeof(suffix), ".%c.sav", 'A' + preferences_save_slot);
    else
        snprintf(suffix, sizeof(suffix), ".sav");
    return emucore_file_path(es, CB_savesPath, suffix);
}

static void emucore_load_sram(CB_EmucoreGameScene* es)
{
    pdll_t* pdll = es->core ? es->core->pdll : NULL;
    if (!pdll)
        return;

    size_t (*get_save_size)(void) = pdll_symbol(pdll, "ce_get_save_size");
    bool (*load)(const uint8_t*, size_t) = pdll_symbol(pdll, "ce_load");
    if (!get_save_size || !load)
        return;

    size_t size = get_save_size();
    if (size == 0)
        return;

    char* path = emucore_sram_path(es);
    size_t fsz = 0;
    uint8_t* buf = (uint8_t*)cb_read_entire_file(path, &fsz, kFileReadData | kFileRead);
    if (buf)
    {
        if (fsz == size)
            load(buf, size);
        else
            playdate->system->logToConsole(
                "emucore: ignoring save '%s' (%u bytes, expected %u)", path, (unsigned)fsz,
                (unsigned)size
            );
        cb_free(buf);
    }
    cb_free(path);
}

static void emucore_save_sram_if_dirty(CB_EmucoreGameScene* es)
{
    pdll_t* pdll = es->core ? es->core->pdll : NULL;
    if (!pdll || !es->rom_playing)
        return;

    size_t (*get_save_size)(void) = pdll_symbol(pdll, "ce_get_save_size");
    void (*save)(uint8_t*, size_t) = pdll_symbol(pdll, "ce_save");
    bool (*is_dirty)(void) = pdll_symbol(pdll, "ce_is_save_dirty");
    if (!get_save_size || !save)
        return;

    size_t size = get_save_size();
    if (size == 0)
        return;
    if (is_dirty && !is_dirty())
        return;

    uint8_t* buf = cb_malloc(size);
    save(buf, size);

    char* path = emucore_sram_path(es);
    if (!cb_write_entire_file(path, buf, size))
        playdate->system->logToConsole("emucore: failed to write save '%s'", path);
    cb_free(path);
    cb_free(buf);
}

bool CB_emucore_save_state(CB_EmucoreGameScene* es, unsigned slot)
{
    pdll_t* pdll = es->core ? es->core->pdll : NULL;
    if (!pdll || !es->rom_playing)
        return false;

    size_t (*get_state_size)(void) = pdll_symbol(pdll, "ce_get_state_size");
    bool (*state_save)(uint8_t*, size_t) = pdll_symbol(pdll, "ce_state_save");
    if (!get_state_size || !state_save)
        return false;

    size_t size = get_state_size();
    if (size == 0)
        return false;

    uint8_t* buf = cb_malloc(size);

    bool ok = state_save(buf, size);
    if (ok)
    {
        char suffix[24];
        snprintf(suffix, sizeof(suffix), ".%u.state", slot);
        char* path = emucore_file_path(es, CB_statesPath, suffix);
        ok = cb_write_entire_file(path, buf, size);
        cb_free(path);
    }
    cb_free(buf);
    return ok;
}

bool CB_emucore_load_state(CB_EmucoreGameScene* es, unsigned slot)
{
    pdll_t* pdll = es->core ? es->core->pdll : NULL;
    if (!pdll || !es->rom_playing)
        return false;

    bool (*state_load)(const uint8_t*, size_t) = pdll_symbol(pdll, "ce_state_load");
    if (!state_load)
        return false;

    char suffix[24];
    snprintf(suffix, sizeof(suffix), ".%u.state", slot);
    char* path = emucore_file_path(es, CB_statesPath, suffix);
    size_t fsz = 0;
    uint8_t* buf = (uint8_t*)cb_read_entire_file(path, &fsz, kFileReadData | kFileRead);
    cb_free(path);

    bool ok = false;
    if (buf)
    {
        ok = state_load(buf, fsz);
        cb_free(buf);
    }
    return ok;
}

static void CB_EmucoreGameScene_didSelectLibrary(void* userdata)
{
    CB_EmucoreGameScene* es = userdata;
    es->go_to_library = true;
}

static void CB_EmucoreGameScene_didSelectSettings(void* userdata)
{
    CB_EmucoreGameScene* es = userdata;
    CB_SettingsScene* settingsScene = CB_SettingsScene_new_userstack(NULL, es, NULL);
    CB_presentModal(settingsScene->scene);
}

static void CB_EmucoreGameScene_menu(void* object)
{
    CB_EmucoreGameScene* es = object;
    if (!CB_App->bundled_rom)
        playdate->system->addMenuItem("Library", CB_EmucoreGameScene_didSelectLibrary, es);
    playdate->system->addMenuItem("Settings", CB_EmucoreGameScene_didSelectSettings, es);

    // emucore can provide an entry too
    if (es->core && es->core->pdll && es->core->pdll->eventHandler)
        es->core->pdll->eventHandler(es->core->pdll->playdate_ptr, kEventPause, 0);
}

static void CB_EmucoreGameScene_update(void* object, uint32_t u32enc_dt)
{
    CB_EmucoreGameScene* es = object;
    (void)u32enc_dt;

    if (CB_App->pendingScene)
        return;

    if (es->go_to_library && !CB_App->bundled_rom)
    {
        call_with_user_stack(CB_goToLibrary);
        return;
    }

    if (es->rom_playing && es->update_rom)
        es->update_rom();
}

// for performance reasons, skip most stuff
static void emucore_update_override(void* ud)
{
    CB_EmucoreGameScene* es = ud;

    float dt = playdate->system->getElapsedTime();
    playdate->system->resetElapsedTime();

    if unlikely (CB_App->scene != es->scene || CB_App->pendingScene || es->go_to_library)
    {
        CB_update(dt);
        return;
    }

    int frames = 1;
    if likely (es->rom_playing && es->update_rom)
        frames = es->update_rom();

    if (frames < 1)
        frames = 1;

    CB_App->avg_dt_mult = (preferences_display_fps == 1) ? (1.0f / frames) : 1.0f;
    CB_account_frame_timing(dt);

    if (es->fade_frames > 0)
    {
        es->fade_frames =
            es->fade_frames > (unsigned)frames ? es->fade_frames - (unsigned)frames : 0;
        cb_render_boot_fade(es->fade_frames, es->fade_white);
    }

    if (preferences_display_fps)
        cb_render_fps(false, false);

    playdate->graphics->display();
}

static void CB_EmucoreGameScene_event(void* object, PDSystemEvent event, uint32_t arg)
{
    CB_EmucoreGameScene* es = object;

    // avoid kEventLowPower due to the potential for save corruption
    // (TODO: save to a back-up?)
    if (event == kEventPause || event == kEventLock || event == kEventTerminate)
        emucore_save_sram_if_dirty(es);

    if (event == kEventResume && es->core && es->core->pdll)
    {
        void (*full_redraw)(void) = pdll_symbol(es->core->pdll, "ce_full_redraw");
        if (full_redraw)
            full_redraw();
    }

    // kEventInit/kEventTerminate are driven by pdll_open/pdll_close instead.
    // kEventPause driven by *_menu()
    if (event == kEventInit || event == kEventTerminate || event == kEventPause)
        return;

    if (es->core && es->core->pdll && es->core->pdll->eventHandler)
        es->core->pdll->eventHandler(es->core->pdll->playdate_ptr, event, arg);
}

static void CB_EmucoreGameScene_free(void* object)
{
    CB_EmucoreGameScene* es = object;

    if (CB_App->update_override_ud == es)
    {
        CB_App->update_override = NULL;
        CB_App->update_override_ud = NULL;
    }

    if (es->rom_playing)
    {
        emucore_save_sram_if_dirty(es);
        if (es->stop)
            es->stop();
        es->rom_playing = false;
    }
    if (es->rom_loaded)
    {
        if (es->unload_rom)
            es->unload_rom();
        es->rom_loaded = false;
    }

    if (es->core && es->core->pdll)
        CB_load_emucore(NULL);

    cb_free(es->rom);
    cb_free(es->rom_path);
    cb_free(es->slug);
    cb_free(es->name_short);

    CB_Scene_free(es->scene);
    cb_free(es);
}

CB_EmucoreGameScene* CB_EmucoreGameScene_new(
    const char* rom_path, const char* slug, const char* name_short
)
{
    CB_EmucoreGameScene* es = cb_malloc(sizeof(CB_EmucoreGameScene));
    memset(es, 0, sizeof(*es));

    CB_Scene* scene = CB_Scene_new();
    scene->id = "emucore";
    scene->managedObject = es;
    scene->preferredRefreshRate = 0;  // handled by core
    scene->update = (void*)CB_EmucoreGameScene_update;
    scene->menu = (void*)CB_EmucoreGameScene_menu;
    scene->event = (void*)CB_EmucoreGameScene_event;
    scene->free = (void*)CB_EmucoreGameScene_free;
    scene->use_user_stack = 0;
    es->scene = scene;

    es->rom_path = cb_strdup(rom_path);
    es->slug = cb_strdup(slug);
    es->name_short = cb_strdup(name_short);

    es->core = CB_get_emucore_by_slug(slug);
    if (!es->core)
    {
        playdate->system->logToConsole("emucore: no core for slug '%s'", slug ? slug : "(null)");
        CB_EmucoreGameScene_free(es);
        return NULL;
    }

    CB_load_emucore(es->core);
    if (!es->core->pdll)
    {
        CB_EmucoreGameScene_free(es);
        return NULL;
    }

    es->load_rom = (ce_load_rom_fn)pdll_symbol(es->core->pdll, "ce_load_rom");
    es->update_rom = (ce_update_fn)pdll_symbol(es->core->pdll, "ce_update");
    es->unload_rom = (ce_unload_rom_fn)pdll_symbol(es->core->pdll, "ce_unload_rom");
    es->play = (ce_play_fn)pdll_symbol(es->core->pdll, "ce_play");
    es->stop = (ce_stop_fn)pdll_symbol(es->core->pdll, "ce_stop");
    if (!es->load_rom || !es->update_rom || !es->unload_rom || !es->play || !es->stop)
    {
        playdate->system->logToConsole("emucore: '%s' missing required symbol", es->core->path);
        CB_EmucoreGameScene_free(es);
        return NULL;
    }

    es->rom = (uint8_t*)cb_read_entire_file(rom_path, &es->rom_size, kFileReadData | kFileRead);
    if (!es->rom)
    {
        playdate->system->logToConsole("emucore: could not read ROM '%s'", rom_path);
        CB_EmucoreGameScene_free(es);
        return NULL;
    }

    char* rom_basename = cb_basename(rom_path, false);  // filename with extension, for the core
    bool loaded = es->load_rom(es->rom, es->rom_size, es->slug, rom_basename);
    cb_free(rom_basename);
    if (!loaded)
    {
        playdate->system->logToConsole("emucore: ce_load_rom failed for '%s'", rom_path);
        CB_EmucoreGameScene_free(es);
        return NULL;
    }
    es->rom_loaded = true;

    char* emu_cfg = cb_game_config_path(rom_path);
    if (emu_cfg)
    {
        cb_emucore_prefs_read_from_disk(emu_cfg, false);
        cb_free(emu_cfg);
    }
    cb_apply_persisted_emucore_prefs(es->core, es->slug);

    es->fade_frames = cb_boot_fade_initial_frames(preferences_boot_fade);
    es->fade_white = cb_boot_fade_initial_white(preferences_boot_fade);

    emucore_load_sram(es);

    if (!es->play())
    {
        playdate->system->logToConsole("emucore: ce_play failed for '%s'", rom_path);
        CB_EmucoreGameScene_free(es);
        return NULL;
    }
    es->rom_playing = true;

    CB_App->update_override = emucore_update_override;
    CB_App->update_override_ud = es;
    return es;
}
