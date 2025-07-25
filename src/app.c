//
//  app.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "app.h"

#include "../libs/minigb_apu/minigb_apu.h"
#include "../libs/pdnewlib/pdnewlib.h"
#include "dtcm.h"
#include "jparse.h"
#include "preferences.h"
#include "scenes/credits_scene.h"
#include "scenes/game_scanning_scene.h"
#include "scenes/game_scene.h"
#include "scenes/image_conversion_scene.h"
#include "scenes/info_scene.h"
#include "scenes/library_scene.h"
#include "script.h"
#include "userstack.h"
#include "version.h"

#include <string.h>

CB_Application* CB_App;

#if defined(TARGET_SIMULATOR)
pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

struct copy_file_callback_ud
{
    json_value* manifest;
    const char* directory;
    bool* modified;
};

static void copy_file_callback(const char* filename, void* userdata)
{
    struct copy_file_callback_ud* ud = userdata;
    json_value* manifest = ud->manifest;
    const char* directory = ud->directory;

    const char* extension = strrchr((char*)filename, '.');
    if (!extension)
        return;

    char* full_path = aprintf("%s/%s", directory, filename);
    if (!full_path)
        return;

    json_value already_copied = json_get_table_value(*manifest, full_path);

    char* dst_path = NULL;
    if (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".jpg") ||
        !strcasecmp(extension, ".jpeg") || !strcasecmp(extension, ".bmp") ||
        !strcasecmp(extension, ".pdi"))
    {
        dst_path = aprintf("%s/%s", CB_coversPath, filename);
    }
    // TODO: .ips/.bps
    else if (!strcasecmp(extension, ".gb") || !strcasecmp(extension, ".gbc"))
    {
        dst_path = aprintf("%s/%s", CB_gamesPath, filename);
    }
    else if (!strcasecmp(extension, ".sav"))
    {
        dst_path = aprintf("%s/%s", CB_savesPath, filename);
    }
    else if (!strcasecmp(extension, ".state"))
    {
        dst_path = aprintf("%s/%s", CB_statesPath, filename);
    }
    else if (!strcmp(filename, "dmg_boot.bin"))
    {
        dst_path = aprintf("./%s", filename);
    }

    if (!dst_path)
    {
        cb_free(full_path);
        return;
    }

    if (already_copied.type != kJSONTrue)
    {
        size_t size;
        void* dat = cb_read_entire_file(full_path, &size, kFileRead);

        if (dat && size > 0)
        {
            char* msg = aprintf("Copying \"%s\" from PDX", full_path);
            if (msg)
            {
                playdate->system->logToConsole("%s\n", msg);
                cb_draw_logo_screen_and_display(CB_App->subheadFont, msg);
                cb_free(msg);
            }

            bool success = cb_write_entire_file(dst_path, dat, size);
            cb_free(dat);

            // mark file as transferred
            if (success)
            {
                json_value _true;
                _true.type = kJSONTrue;
                json_set_table_value(manifest, full_path, _true);
                *ud->modified = true;
            }
        }
        else
        {
            // file was not in PDX directory; silently skip it.
        }
    }

    cb_free(full_path);
}

static int check_is_bundle(void)
{
    // check for CLI arg
    const char* arg = playdate->system->getLaunchArgs(NULL);
    if (startswith(arg, "rom="))
    {
        arg += strlen("rom=");
        CB_App->bundled_rom = cb_strdup(arg);
        return true;
    }

    // check for bundle.json

    json_value jbundle;
    if (!parse_json(BUNDLE_FILE, &jbundle, kFileRead | kFileReadData))
        return false;

    json_value jrom = json_get_table_value(jbundle, "rom");

    if (jrom.type == kJSONString)
        CB_App->bundled_rom = cb_strdup(jrom.data.stringval);

    if (CB_App->bundled_rom)
    {
        // verify pdxinfo has different bundle ID
        size_t pdxlen;
        char* pdxinfo = (void*)cb_read_entire_file("pdxinfo", &pdxlen, kFileRead);
        if (pdxinfo)
        {
            pdxinfo[pdxlen - 1] = 0;
            if (strstr(pdxinfo, "bundleID=" PDX_BUNDLE_ID))
            {
                CB_InfoScene* infoScene = CB_InfoScene_new(
                    NULL,
                    "ERROR: For bundled ROMs, bundleID in pdxinfo must differ from \"" PDX_BUNDLE_ID
                    "\".\n"
                );
                CB_presentModal(infoScene->scene);
                return -1;
            }

            cb_free(pdxinfo);
        }

        // check for default/visible/hidden preferences
        json_value jdefault = json_get_table_value(jbundle, "default");
        json_value jhidden = json_get_table_value(jbundle, "hidden");
        json_value jvisible = json_get_table_value(jbundle, "visible");

#define getvalue(j, value)         \
    int value = -1;                \
    if (j.type == kJSONInteger)    \
    {                              \
        value = j.data.intval;     \
    }                              \
    else if (j.type == kJSONTrue)  \
    {                              \
        value = 1;                 \
    }                              \
    else if (j.type == kJSONFalse) \
    {                              \
        value = 0;                 \
    }                              \
    if (value < 0)                 \
    continue

        preferences_bitfield_t preferences_default_bitfield = 0;

        // defaults
        if (jdefault.type == kJSONTable)
        {
            JsonObject* obj = jdefault.data.tableval;
            for (size_t i = 0; i < obj->n; ++i)
            {
                getvalue(obj->data[i].value, value);

                const char* key = obj->data[i].key;
                int i = 0;

#define PREF(p, ...)                                                    \
    if (!strcmp(key, #p))                                               \
    {                                                                   \
        preferences_##p = value;                                        \
        preferences_default_bitfield |= (preferences_bitfield_t)1 << i; \
        continue;                                                       \
    }                                                                   \
    ++i;
#include "prefs.x"
            }
        }

        // hidden
        if (jhidden.type == kJSONArray)
        {
            preferences_bundle_hidden = 0;
            JsonArray* obj = jhidden.data.arrayval;
            for (size_t i = 0; i < obj->n; ++i)
            {
                json_value value = obj->data[i];
                if (value.type != kJSONString)
                    continue;
                const char* key = value.data.stringval;

                int i = 0;
#define PREF(p, ...)                                                   \
    if (!strcmp(key, #p))                                              \
    {                                                                  \
        preferences_bundle_hidden |= ((preferences_bitfield_t)1 << i); \
        continue;                                                      \
    }                                                                  \
    ++i;
#include "prefs.x"
            }
        }

        // visible
        if (jvisible.type == kJSONArray)
        {
            preferences_bundle_hidden = -1;
            JsonArray* obj = jvisible.data.arrayval;
            for (size_t i = 0; i < obj->n; ++i)
            {
                json_value value = obj->data[i];
                if (value.type != kJSONString)
                    continue;
                const char* key = value.data.stringval;

                int i = 0;
#define PREF(p, ...)                                                    \
    if (!strcmp(key, #p))                                               \
    {                                                                   \
        preferences_bundle_hidden &= ~((preferences_bitfield_t)1 << i); \
        continue;                                                       \
    }                                                                   \
    ++i;
#include "prefs.x"
            }
        }

        // always fixed in a bundle
        preferences_default_bitfield |= PREFBIT_per_game;
        preferences_bundle_hidden |= PREFBIT_per_game;
        preferences_per_game = 0;

        // store the default values for engine use
        preferences_bundle_default = preferences_store_subset(preferences_default_bitfield);
    }

    free_json_data(jbundle);
    return !!CB_App->bundled_rom;
}

void CB_init(void)
{
    CB_App = cb_calloc(1, sizeof(CB_Application));
    memset(CB_App, 0, sizeof(*CB_App));

    cb_register_all_scripts();

    CB_App->gameNameCache = array_new();
    CB_App->gameListCache = array_new();
    CB_App->coverCache = NULL;
    CB_App->gameListCacheIsSorted = false;
    CB_App->scene = NULL;

    CB_App->pendingScene = NULL;

    CB_App->coverArtCache.rom_path = NULL;
    CB_App->coverArtCache.art.bitmap = NULL;

    playdate->file->mkdir(CB_gamesPath);
    playdate->file->mkdir(CB_coversPath);
    playdate->file->mkdir(CB_savesPath);
    playdate->file->mkdir(CB_statesPath);
    playdate->file->mkdir(CB_settingsPath);
    playdate->file->mkdir(CB_patchesPath);

    CB_App->bodyFont = playdate->graphics->loadFont("fonts/Roobert-11-Medium", NULL);
    CB_App->titleFont = playdate->graphics->loadFont("fonts/Roobert-20-Medium", NULL);
    CB_App->subheadFont = playdate->graphics->loadFont("fonts/Asheville-Sans-14-Bold", NULL);
    CB_App->labelFont = playdate->graphics->loadFont("fonts/Nontendo-Bold", NULL);
    CB_App->logoBitmap = playdate->graphics->loadBitmap("images/logo.pdi", NULL);

    check_is_bundle();

    if (!CB_App->bundled_rom)
        cb_draw_logo_screen_and_display(CB_App->subheadFont, "Initializing...");
    preferences_init();

    CB_App->clickSynth = playdate->sound->synth->newSynth();
    playdate->sound->synth->setWaveform(CB_App->clickSynth, kWaveformSquare);
    playdate->sound->synth->setAttackTime(CB_App->clickSynth, 0.0001f);
    playdate->sound->synth->setDecayTime(CB_App->clickSynth, 0.05f);
    playdate->sound->synth->setSustainLevel(CB_App->clickSynth, 0.0f);
    playdate->sound->synth->setReleaseTime(CB_App->clickSynth, 0.0f);

    CB_App->selectorBitmapTable =
        playdate->graphics->loadBitmapTable("images/selector/selector", NULL);
    CB_App->startSelectBitmap =
        playdate->graphics->loadBitmap("images/selector-start-select", NULL);

    // --- Boot ROM data ---
    const char* bootRomPath = "dmg_boot.bin";
    SDFile* file = playdate->file->open(bootRomPath, kFileRead | kFileReadData);
    if (file)
    {
        CB_App->bootRomData = cb_malloc(256);
        int bytesRead = playdate->file->read(file, CB_App->bootRomData, 256);
        playdate->file->close(file);
        if (bytesRead != 256)
        {
            playdate->system->logToConsole(
                "Error: Read %d bytes from dmg_boot.bin, expected 256.", bytesRead
            );
            cb_free(CB_App->bootRomData);
            CB_App->bootRomData = NULL;
        }
        else
        {
            playdate->system->logToConsole("Successfully loaded dmg_boot.bin");
        }
    }
    else
    {
        playdate->system->logToConsole("Note: could not find %s. Skipping Boot ROM.", bootRomPath);
    }

    // add audio callback later
    CB_App->soundSource = NULL;

    // custom frame rate delimiter
    playdate->display->setRefreshRate(0);

    // copy in files if not already copied in
    if (!CB_App->bundled_rom)
    {
        json_value manifest;
        parse_json(COPIED_FILES, &manifest, kFileReadData | kFileRead);

        if (manifest.type != kJSONTable)
        {
            manifest.type = kJSONTable;
            JsonObject* obj = cb_malloc(sizeof(JsonObject));
            obj->n = 0;
            manifest.data.tableval = obj;
        }

        const char* sources[] = {".", CB_coversPath, CB_gamesPath, CB_savesPath, CB_statesPath};
        bool modified = false;

        for (size_t i = 0; i < sizeof(sources) / sizeof(const char*); ++i)
        {
            struct copy_file_callback_ud ud;
            ud.manifest = &manifest;
            ud.directory = sources[i];
            ud.modified = &modified;

            playdate->file->listfiles(sources[i], copy_file_callback, &ud, true);
        }

        write_json_to_disk(COPIED_FILES, manifest);
        free_json_data(manifest);

        CB_GameScanningScene* scanningScene = CB_GameScanningScene_new();
        CB_present(scanningScene->scene);
    }
    else
    {
        CB_GameScene* gameScene = CB_GameScene_new(CB_App->bundled_rom, "Bundled ROM");
        if (gameScene)
        {
            CB_present(gameScene->scene);
        }
        else
        {
            playdate->system->error("Failed to launch bundled ROM \"%s\"", CB_App->bundled_rom);
        }
    }
}

static void collect_game_filenames_callback(const char* filename, void* userdata)
{
    CB_Array* filenames_array = userdata;
    char* extension;
    char* dot = cb_strrchr(filename, '.');

    if (!dot || dot == filename)
    {
        extension = "";
    }
    else
    {
        extension = dot + 1;
    }

    if ((cb_strcmp(extension, "gb") == 0 || cb_strcmp(extension, "gbc") == 0))
    {
        array_push(filenames_array, cb_strdup(filename));
    }
}

__section__(".rare") static void switchToPendingScene(void)
{
    CB_Scene* scene = CB_App->scene;

    CB_App->scene = CB_App->pendingScene;
    CB_App->pendingScene = NULL;

    if (scene)
    {
        void* managedObject = scene->managedObject;
        scene->free(managedObject);
    }
}

__section__(".text.main") void CB_update(float dt)
{
    CB_App->dt = dt;
    CB_App->avg_dt =
        (CB_App->avg_dt * FPS_AVG_DECAY) + (1 - FPS_AVG_DECAY) * dt * CB_App->avg_dt_mult;
    CB_App->avg_dt_mult = 1.0f;

    CB_App->crankChange = playdate->system->getCrankChange();

    playdate->system->getButtonState(
        &CB_App->buttons_down, &CB_App->buttons_pressed, &CB_App->buttons_released
    );

    CB_App->buttons_released &= ~CB_App->buttons_suppress;
    CB_App->buttons_suppress &= CB_App->buttons_down;
    CB_App->buttons_down &= ~CB_App->buttons_suppress;

    if (CB_App->scene)
    {
        void* managedObject = CB_App->scene->managedObject;
        DTCM_VERIFY_DEBUG();
        if (CB_App->scene->use_user_stack)
        {
            uint32_t udt = FLOAT_AS_UINT32(dt);
            call_with_user_stack_2(CB_App->scene->update, managedObject, udt);
        }
        else
        {
            CB_App->scene->update(managedObject, dt);
        }
        DTCM_VERIFY_DEBUG();
    }

    playdate->graphics->display();

    if (CB_App->pendingScene)
    {
        DTCM_VERIFY();
        call_with_user_stack(switchToPendingScene);
        DTCM_VERIFY();
    }

#if CB_DEBUG
    playdate->display->setRefreshRate(60);
#else

    float refreshRate = 30.0f;

    if (CB_App->scene)
    {
        refreshRate = CB_App->scene->preferredRefreshRate;
    }

#if CAP_FRAME_RATE
    // cap frame rate
    if (refreshRate > 0)
    {
        float refreshInterval = 1.0f / refreshRate;
        while (playdate->system->getElapsedTime() < refreshInterval)
            ;
    }
#endif

#endif
    DTCM_VERIFY_DEBUG();
}

void CB_present(CB_Scene* scene)
{
    playdate->system->removeAllMenuItems();
    CB_App->buttons_suppress |= CB_App->buttons_down;
    CB_App->buttons_down = 0;
    CB_App->buttons_released = 0;
    CB_App->buttons_pressed = 0;

    CB_App->pendingScene = scene;
}

void CB_presentModal(CB_Scene* scene)
{
    playdate->system->removeAllMenuItems();
    CB_App->buttons_suppress |= CB_App->buttons_down;
    CB_App->buttons_down = 0;
    CB_App->buttons_released = 0;
    CB_App->buttons_pressed = 0;

    scene->parentScene = CB_App->scene;
    CB_App->scene = scene;
    CB_Scene_refreshMenu(CB_App->scene);
}

void CB_dismiss(CB_Scene* sceneToDismiss)
{
    playdate->system->logToConsole("Dismiss\n");
    CB_ASSERT(sceneToDismiss == CB_App->scene);
    CB_Scene* parent = sceneToDismiss->parentScene;
    if (parent)
    {
        parent->forceFullRefresh = true;
        CB_present(parent);
    }
}

void CB_goToLibrary(void)
{
    CB_LibraryScene* libraryScene = CB_LibraryScene_new();
    CB_present(libraryScene->scene);
}

__section__(".rare") void CB_event(PDSystemEvent event, uint32_t arg)
{
    CB_ASSERT(CB_App);
    if (CB_App->scene)
    {
        CB_ASSERT(CB_App->scene->event != NULL);
        CB_App->scene->event(CB_App->scene->managedObject, event, arg);

        if (event == kEventPause)
        {
            // This probably supersedes any need to call CB_Scene_refreshMenu anywhere else
            CB_Scene_refreshMenu(CB_App->scene);
        }
    }
}

void free_game_names(const CB_GameName* gameName)
{
    cb_free(gameName->filename);
    if (gameName->name_database)
        cb_free(gameName->name_database);
    cb_free(gameName->name_short);
    cb_free(gameName->name_detailed);
    cb_free(gameName->name_filename);
    cb_free(gameName->name_short_leading_article);
    cb_free(gameName->name_detailed_leading_article);
    cb_free(gameName->name_filename_leading_article);
}

void CB_quit(void)
{
    if (CB_App->scene)
    {
        void* managedObject = CB_App->scene->managedObject;
        CB_App->scene->free(managedObject);
    }

    cb_clear_global_cover_cache();

    if (CB_App->logoBitmap)
    {
        playdate->graphics->freeBitmap(CB_App->logoBitmap);
    }

    if (CB_App->clickSynth)
    {
        playdate->sound->synth->freeSynth(CB_App->clickSynth);
        CB_App->clickSynth = NULL;
    }

    if (CB_App->gameNameCache)
    {
        for (int i = 0; i < CB_App->gameNameCache->length; i++)
        {
            CB_GameName* gameName = CB_App->gameNameCache->items[i];
            free_game_names(gameName);
            cb_free(gameName);
        }
        array_free(CB_App->gameNameCache);
    }

    if (CB_App->gameListCache)
    {
        for (int i = 0; i < CB_App->gameListCache->length; i++)
        {
            CB_Game_free(CB_App->gameListCache->items[i]);
        }
        array_free(CB_App->gameListCache);
        CB_App->gameListCache = NULL;
    }

    if (CB_App->coverCache)
    {
        for (int i = 0; i < CB_App->coverCache->length; i++)
        {
            CB_CoverCacheEntry* entry = CB_App->coverCache->items[i];
            cb_free(entry->rom_path);
            cb_free(entry->compressed_data);
            cb_free(entry);
        }
        array_free(CB_App->coverCache);
        CB_App->coverCache = NULL;
    }

    cb_free(CB_App->bundled_rom);
    cb_free(CB_App->bootRomData);

    script_quit();
    version_quit();

#ifdef TARGET_PLAYDATE
    pdnewlib_quit();
#endif

    cb_free(CB_App);
}
