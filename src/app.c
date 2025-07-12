//
//  app.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "app.h"

#include "../minigb_apu/minigb_apu.h"
#include "credits_scene.h"
#include "dtcm.h"
#include "game_scanning_scene.h"
#include "game_scene.h"
#include "image_conversion_scene.h"
#include "info_scene.h"
#include "jparse.h"
#include "library_scene.h"
#include "pdnewlib.h"
#include "preferences.h"
#include "script.h"
#include "userstack.h"
#include "version.h"

PGB_Application* PGB_App;

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
        dst_path = aprintf("%s/%s", PGB_coversPath, filename);
    }
    else if (!strcasecmp(extension, ".gb") || !strcasecmp(extension, ".gbc"))
    {
        dst_path = aprintf("%s/%s", PGB_gamesPath, filename);
    }
    else if (!strcasecmp(extension, ".sav"))
    {
        dst_path = aprintf("%s/%s", PGB_savesPath, filename);
    }
    else if (!strcasecmp(extension, ".state"))
    {
        dst_path = aprintf("%s/%s", PGB_statesPath, filename);
    }

    if (!dst_path)
    {
        pgb_free(full_path);
        return;
    }

    if (already_copied.type != kJSONTrue)
    {
        size_t size;
        void* dat = pgb_read_entire_file(full_path, &size, kFileRead);

        if (dat && size > 0)
        {
            char* msg = aprintf("Copying \"%s\" from PDX…", full_path);
            if (msg)
            {
                playdate->system->logToConsole("%s\n", msg);
                pgb_draw_logo_screen_and_display(msg);
                pgb_free(msg);
            }

            bool success = pgb_write_entire_file(dst_path, dat, size);
            pgb_free(dat);

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

    pgb_free(full_path);
}

static int check_is_bundle(void)
{
    // check for CLI arg
    const char* arg = playdate->system->getLaunchArgs(NULL);
    if (startswith(arg, "rom="))
    {
        arg += strlen("rom=");
        PGB_App->bundled_rom = string_copy(arg);
        return true;
    }

    // check for bundle.json

    json_value jbundle;
    if (!parse_json(BUNDLE_FILE, &jbundle, kFileRead | kFileReadData))
        return false;

    json_value jrom = json_get_table_value(jbundle, "rom");

    if (jrom.type == kJSONString)
        PGB_App->bundled_rom = string_copy(jrom.data.stringval);

    if (PGB_App->bundled_rom)
    {
        // verify pdxinfo has different bundle ID
        size_t pdxlen;
        char* pdxinfo = (void*)pgb_read_entire_file("pdxinfo", &pdxlen, kFileRead);
        if (pdxinfo)
        {
            pdxinfo[pdxlen - 1] = 0;
            if (strstr(pdxinfo, "bundleID=" PDX_BUNDLE_ID))
            {
                PGB_InfoScene* infoScene = PGB_InfoScene_new(
                    "ERROR: For bundled ROMs, bundleID in pdxinfo must differ from \"" PDX_BUNDLE_ID
                    "\".\n"
                );
                PGB_presentModal(infoScene->scene);
                return -1;
            }

            pgb_free(pdxinfo);
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
    return !!PGB_App->bundled_rom;
}

void PGB_init(void)
{
    PGB_App = pgb_calloc(1, sizeof(PGB_Application));
    memset(PGB_App, 0, sizeof(*PGB_App));

    pgb_register_all_scripts();

    PGB_App->gameNameCache = array_new();
    PGB_App->gameListCache = array_new();
    PGB_App->coverCache = NULL;
    PGB_App->gameListCacheIsSorted = false;
    PGB_App->scene = NULL;

    PGB_App->pendingScene = NULL;

    PGB_App->coverArtCache.rom_path = NULL;
    PGB_App->coverArtCache.art.bitmap = NULL;

    playdate->file->mkdir(PGB_gamesPath);
    playdate->file->mkdir(PGB_coversPath);
    playdate->file->mkdir(PGB_savesPath);
    playdate->file->mkdir(PGB_statesPath);
    playdate->file->mkdir(PGB_settingsPath);

    PGB_App->bodyFont = playdate->graphics->loadFont("fonts/Roobert-11-Medium", NULL);
    PGB_App->titleFont = playdate->graphics->loadFont("fonts/Roobert-20-Medium", NULL);
    PGB_App->subheadFont = playdate->graphics->loadFont("fonts/Asheville-Sans-14-Bold", NULL);
    PGB_App->labelFont = playdate->graphics->loadFont("fonts/Nontendo-Bold", NULL);
    PGB_App->logoBitmap = playdate->graphics->loadBitmap("images/logo.pdi", NULL);

    check_is_bundle();

    if (!PGB_App->bundled_rom)
        pgb_draw_logo_screen_and_display("Initializing…");
    preferences_init();

    PGB_App->clickSynth = playdate->sound->synth->newSynth();
    playdate->sound->synth->setWaveform(PGB_App->clickSynth, kWaveformSquare);
    playdate->sound->synth->setAttackTime(PGB_App->clickSynth, 0.0001f);
    playdate->sound->synth->setDecayTime(PGB_App->clickSynth, 0.05f);
    playdate->sound->synth->setSustainLevel(PGB_App->clickSynth, 0.0f);
    playdate->sound->synth->setReleaseTime(PGB_App->clickSynth, 0.0f);

    PGB_App->selectorBitmapTable =
        playdate->graphics->loadBitmapTable("images/selector/selector", NULL);
    PGB_App->startSelectBitmap =
        playdate->graphics->loadBitmap("images/selector-start-select", NULL);

    // --- Boot ROM data ---
    const char* bootRomPath = "dmg_boot.bin";
    SDFile* file = playdate->file->open(bootRomPath, kFileRead | kFileReadData);
    if (file)
    {
        PGB_App->bootRomData = pgb_malloc(256);
        int bytesRead = playdate->file->read(file, PGB_App->bootRomData, 256);
        playdate->file->close(file);
        if (bytesRead != 256)
        {
            playdate->system->logToConsole(
                "Error: Read %d bytes from dmg_boot.bin, expected 256.", bytesRead
            );
            pgb_free(PGB_App->bootRomData);
            PGB_App->bootRomData = NULL;
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
    PGB_App->soundSource = NULL;

    // custom frame rate delimiter
    playdate->display->setRefreshRate(0);

    // copy in files if not already copied in
    if (!PGB_App->bundled_rom)
    {
        json_value manifest;
        parse_json(COPIED_FILES, &manifest, kFileReadData | kFileRead);

        if (manifest.type != kJSONTable)
        {
            manifest.type = kJSONTable;
            JsonObject* obj = pgb_malloc(sizeof(JsonObject));
            obj->n = 0;
            manifest.data.tableval = obj;
        }

        const char* sources[] = {".", PGB_coversPath, PGB_gamesPath, PGB_savesPath, PGB_statesPath};
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

        PGB_GameScanningScene* scanningScene = PGB_GameScanningScene_new();
        PGB_present(scanningScene->scene);
    }
    else
    {
        PGB_GameScene* gameScene = PGB_GameScene_new(PGB_App->bundled_rom, "Bundled ROM");
        if (gameScene)
        {
            PGB_present(gameScene->scene);
        }
        else
        {
            playdate->system->error("Failed to launch bundled ROM \"%s\"", PGB_App->bundled_rom);
        }
    }
}

static void collect_game_filenames_callback(const char* filename, void* userdata)
{
    PGB_Array* filenames_array = userdata;
    char* extension;
    char* dot = pgb_strrchr(filename, '.');

    if (!dot || dot == filename)
    {
        extension = "";
    }
    else
    {
        extension = dot + 1;
    }

    if ((pgb_strcmp(extension, "gb") == 0 || pgb_strcmp(extension, "gbc") == 0))
    {
        array_push(filenames_array, string_copy(filename));
    }
}

__section__(".rare") static void switchToPendingScene(void)
{
    PGB_Scene* scene = PGB_App->scene;

    PGB_App->scene = PGB_App->pendingScene;
    PGB_App->pendingScene = NULL;

    if (scene)
    {
        void* managedObject = scene->managedObject;
        scene->free(managedObject);
    }
}

__section__(".text.main") void PGB_update(float dt)
{
    PGB_App->dt = dt;
    PGB_App->avg_dt =
        (PGB_App->avg_dt * FPS_AVG_DECAY) + (1 - FPS_AVG_DECAY) * dt * PGB_App->avg_dt_mult;
    PGB_App->avg_dt_mult = 1.0f;

    PGB_App->crankChange = playdate->system->getCrankChange();

    playdate->system->getButtonState(
        &PGB_App->buttons_down, &PGB_App->buttons_pressed, &PGB_App->buttons_released
    );

    PGB_App->buttons_released &= ~PGB_App->buttons_suppress;
    PGB_App->buttons_suppress &= PGB_App->buttons_down;
    PGB_App->buttons_down &= ~PGB_App->buttons_suppress;

    if (PGB_App->scene)
    {
        void* managedObject = PGB_App->scene->managedObject;
        DTCM_VERIFY_DEBUG();
        if (PGB_App->scene->use_user_stack)
        {
            uint32_t udt = FLOAT_AS_UINT32(dt);
            call_with_user_stack_2(PGB_App->scene->update, managedObject, udt);
        }
        else
        {
            PGB_App->scene->update(managedObject, dt);
        }
        DTCM_VERIFY_DEBUG();
    }

    playdate->graphics->display();

    if (PGB_App->pendingScene)
    {
        DTCM_VERIFY();
        call_with_user_stack(switchToPendingScene);
        DTCM_VERIFY();
    }

#if PGB_DEBUG
    playdate->display->setRefreshRate(60);
#else

    float refreshRate = 30.0f;

    if (PGB_App->scene)
    {
        refreshRate = PGB_App->scene->preferredRefreshRate;
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

void PGB_present(PGB_Scene* scene)
{
    playdate->system->removeAllMenuItems();
    PGB_App->buttons_suppress |= PGB_App->buttons_down;
    PGB_App->buttons_down = 0;
    PGB_App->buttons_released = 0;
    PGB_App->buttons_pressed = 0;

    PGB_App->pendingScene = scene;
}

void PGB_presentModal(PGB_Scene* scene)
{
    playdate->system->removeAllMenuItems();
    PGB_App->buttons_suppress |= PGB_App->buttons_down;
    PGB_App->buttons_down = 0;
    PGB_App->buttons_released = 0;
    PGB_App->buttons_pressed = 0;

    scene->parentScene = PGB_App->scene;
    PGB_App->scene = scene;
    PGB_Scene_refreshMenu(PGB_App->scene);
}

void PGB_dismiss(PGB_Scene* sceneToDismiss)
{
    playdate->system->logToConsole("Dismiss\n");
    PGB_ASSERT(sceneToDismiss == PGB_App->scene);
    PGB_Scene* parent = sceneToDismiss->parentScene;
    if (parent)
    {
        parent->forceFullRefresh = true;
        PGB_present(parent);
    }
}

void PGB_goToLibrary(void)
{
    PGB_LibraryScene* libraryScene = PGB_LibraryScene_new();
    PGB_present(libraryScene->scene);
}

__section__(".rare") void PGB_event(PDSystemEvent event, uint32_t arg)
{
    PGB_ASSERT(PGB_App);
    if (PGB_App->scene)
    {
        PGB_ASSERT(PGB_App->scene->event != NULL);
        PGB_App->scene->event(PGB_App->scene->managedObject, event, arg);

        if (event == kEventPause)
        {
            // This probably supersedes any need to call PGB_Scene_refreshMenu anywhere else
            PGB_Scene_refreshMenu(PGB_App->scene);
        }
    }
}

void free_game_names(const PGB_GameName* gameName)
{
    pgb_free(gameName->filename);
    if (gameName->name_database)
        pgb_free(gameName->name_database);
    pgb_free(gameName->name_short);
    pgb_free(gameName->name_detailed);
    pgb_free(gameName->name_filename);
    pgb_free(gameName->name_short_leading_article);
    pgb_free(gameName->name_detailed_leading_article);
    pgb_free(gameName->name_filename_leading_article);
}

void PGB_quit(void)
{
    if (PGB_App->scene)
    {
        void* managedObject = PGB_App->scene->managedObject;
        PGB_App->scene->free(managedObject);
    }

    pgb_clear_global_cover_cache();

    if (PGB_App->logoBitmap)
    {
        playdate->graphics->freeBitmap(PGB_App->logoBitmap);
    }

    if (PGB_App->clickSynth)
    {
        playdate->sound->synth->freeSynth(PGB_App->clickSynth);
        PGB_App->clickSynth = NULL;
    }

    if (PGB_App->gameNameCache)
    {
        for (int i = 0; i < PGB_App->gameNameCache->length; i++)
        {
            PGB_GameName* gameName = PGB_App->gameNameCache->items[i];
            free_game_names(gameName);
            pgb_free(gameName);
        }
        array_free(PGB_App->gameNameCache);
    }

    if (PGB_App->gameListCache)
    {
        for (int i = 0; i < PGB_App->gameListCache->length; i++)
        {
            PGB_Game_free(PGB_App->gameListCache->items[i]);
        }
        array_free(PGB_App->gameListCache);
        PGB_App->gameListCache = NULL;
    }

    if (PGB_App->coverCache)
    {
        for (int i = 0; i < PGB_App->coverCache->length; i++)
        {
            PGB_CoverCacheEntry* entry = PGB_App->coverCache->items[i];
            pgb_free(entry->rom_path);
            pgb_free(entry->compressed_data);
            pgb_free(entry);
        }
        array_free(PGB_App->coverCache);
        PGB_App->coverCache = NULL;
    }

    pgb_free(PGB_App->bundled_rom);
    pgb_free(PGB_App->bootRomData);

    script_quit();
    version_quit();

#ifdef TARGET_PLAYDATE
    pdnewlib_quit();
#endif

    pgb_free(PGB_App);
}
