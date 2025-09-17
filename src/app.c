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
#include "scenes/file_copying_scene.h"
#include "scenes/game_scene.h"
#include "scenes/info_scene.h"
#include "scenes/library_scene.h"
#include "script.h"
#include "serial.h"
#include "userstack.h"
#include "version.h"
#include "global.h"

#include <string.h>

CB_Application* CB_App;

AudioSyncBuffer g_audio_sync_buffer;
atomic_uint g_samples_generated_total = 0;

#if defined(TARGET_SIMULATOR)
pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

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

static void initialize_directory(void)
{
    size_t len;
    char* shared_directory = (void*)cb_read_entire_file(DIRECTORY_POINTER, &len, kFileRead);
    if (!shared_directory)
    {
        shared_directory = aprintf(DEFAULT_SHARED_DIRECTORY);
    }
    
    // check for 'nocopy' tag
    char* newline = strchr(shared_directory, '\n');
    bool no_copy = false;
    if (newline)
    {
        *newline = '\0';
        no_copy = (newline[1] == 'n'); // nocopy
    }
    
    // remove trailing `/`
    while (shared_directory && *shared_directory)
    {
        size_t len = strlen(shared_directory);
        if (shared_directory[len-1] == '/')
        {
            shared_directory[len-1] = '\0';
        }
        else
        {
            break;
        }
    }
    
    playdate->system->logToConsole("Directory: %s", shared_directory);
    
    CB_App->directory = shared_directory;
    CB_ASSERT(!!CB_App->directory);
    
    full_mkdir(shared_directory);
    
    // copy files in from data/ if needed
    // (Previous versions of CrankBoy used the data/ folder for
    // storing ROMs.)
    if (!no_copy)
    {   
        playdate->system->logToConsole("Moving files from data/ to new directory...");
        
        bool err = false;
        const char* sources[] = {
            CB_settingsPath,
            CB_coversPath,
            CB_patchesPath,
            CB_gamesPath,
            CB_statesPath,
            CB_savesPath
        };
        
        for (size_t i = 0; i < sizeof(sources)/sizeof(char*); ++i)
        {
            const char* dst = cb_gb_directory_path(sources[i]);
            // move files from data/ but don't replace existing directory
            if (cb_directory_exists_and_nonempty_or_file_exists(sources[i]) && !cb_directory_exists_and_nonempty_or_file_exists(dst))
            {
                int result = playdate->file->rename(sources[i], dst);
                if (result == 0)
                {
                    playdate->system->logToConsole("Moved %s -> %s", sources[i], dst);
                }
                else
                {
                    playdate->system->logToConsole("Failed to move %s -> %s", sources[i], dst);
                    err = true;
                    break;
                }
            }
        }
        
        if (!err)
        {
            playdate->system->logToConsole("Done moving files.");
            shared_directory = aprintf("%s\nnocopy", shared_directory);
        }
    }
    
    cb_write_entire_file(
        DIRECTORY_POINTER, shared_directory, strlen(shared_directory)
    );
    
    if (shared_directory != CB_App->directory)
    {
        cb_free(shared_directory);
    }
    
    full_mkdir(cb_gb_directory_path(CB_savesPath));
    full_mkdir(cb_gb_directory_path(CB_gamesPath));
    full_mkdir(cb_gb_directory_path(CB_coversPath));
    full_mkdir(cb_gb_directory_path(CB_statesPath));
    full_mkdir(cb_gb_directory_path(CB_settingsPath));
    full_mkdir(cb_gb_directory_path(CB_patchesPath));
}

static void get_homebrew_hub_api(void)
{
    char* hbapi = cb_read_entire_file(HOMEBREW_HUB_API_FILE, NULL, kFileRead | kFileReadData);
    if (!hbapi) return;
    
    char* scheme_end = strstr(hbapi, "://");
    if (!scheme_end)
    {
        cb_free(hbapi);
        return;
    };
    
    scheme_end[0] = 0;
    char* domain = scheme_end + 3;
    bool https = !strcmp(hbapi, "https");
    char* nl = strchr(domain, '\n');
    if (!nl)
    {
        cb_free(hbapi);
        return;
    }
    nl[0] = 0;
    char* staticpath = nl+1;
    char* spnl = strchr(staticpath, '\n');
    if (spnl) spnl[0] = 0;
    char* path = strchr(domain, '/');
    if (!path)
    {
        cb_free(hbapi);
        return;
    }
    
    // strip final slash
    while (path[0] && path[strlen(path)-1] == '/')
    {
        path[strlen(path)-1] = 0;
    }
    
    if (!path[0])
    {
        cb_free(hbapi);
        return;
    }
    
    while (staticpath[0] && staticpath[strlen(path)-1] == '/')
    {
        staticpath[strlen(staticpath)-1] = 0;
    }
    
    if (!staticpath[0])
    {
        cb_free(hbapi);
        return;
    }
    
    CB_App->hbApiUseHTTPS = https;
    CB_App->hbApiPath = cb_strdup(path);
    CB_App->hbStaticPath = staticpath;
    path[0] = 0;
    CB_App->hbApiDomain = domain;
}

static void non_bundle_init(void)
{
    cb_draw_logo_screen_and_display(CB_App->subheadFont, "Initializing...");
    get_homebrew_hub_api();
    parse_json(ROMHACK_DB_FILE, &CB_App->rhdb_cache, kFileRead | kFileReadData);
    
    global.shown_intro = true;
    save_global();
    
    CB_FileCopyingScene* copyingScene = CB_FileCopyingScene_new();
    CB_present(copyingScene->scene);
}

void CB_showHelp(bool first_time)
{
    const char* title = first_time
        ? "Welcome to CrankBoy!"
        : "CrankBoy Usage";
        
    const char* A0 = first_time
        ? "This is a quick guide to getting started.\n\nIn the future, you can review these instructions from the \"help\" option in CrankBoy's main menu.\n\n(Scroll down with the crank!)\n\n"
        : "";
    
    const char* A = first_time
        ? "To get started, you'll want to add some ROMs to CrankBoy."
        : "To add ROMs to CrankBoy, do the following:";
    
    #if 0
    const char* B = first_time
        ? "Alternatively, press Ⓑ now to start playing the included ROMs immediately.\n\n"
        : "\n\n";
    #else
    const char* B = "\n\n";
    #endif
    
    const char* C1 = "1. Connect your Playdate to another device via USB.\n";
    const char* C2 = "2. Hold LEFT + MENU + POWER for 10 seconds to put your Playdate into Data Disk mode.\n";
    const char* C3 = "3. From the connected device, copy your ROM files (.gb or .gbc extension) onto your Playdate at the following directory: ";
    
    const char* D = "\n\nAlternatively, you can download free \"homebrew\" titles from within CrankBoy in the main menu via ⊙ > settings > Get ROMs.";
    
    char* s = aprintf(
        "%s%s%s%s%s%s%s%s", A0, A, B, C1, C2, C3, cb_gb_directory_path(CB_gamesPath), D
    );
    
    CB_InfoScene* infoScene = CB_InfoScene_new(
        title,
        s
    );
                
    if (first_time)
    {
        infoScene->complete_callback = non_bundle_init;
        infoScene->min_dismiss_time = 1.2f;
    }
    
    CB_presentModal(infoScene->scene);
    
    cb_free(s);
}

static void any_file_found(const char* p, bool* any_found)
{
    *any_found = true;
}

static bool games_exist_in_data(void)
{
    bool any_found = false;
    playdate->file->listfiles(
        cb_gb_directory_path(CB_gamesPath),
        (void*)any_file_found,
        &any_found, false
    );
    return any_found;
}

void CB_init(void)
{
    CB_App = allocz(CB_Application);

    cb_register_all_c_scripts();

    CB_App->gameNameCache = array_new();
    CB_App->gameListCache = array_new();
    CB_App->coverCache = NULL;
    CB_App->gameListCacheIsSorted = false;
    CB_App->scene = NULL;

    CB_App->pendingScene = NULL;

    CB_App->coverArtCache.rom_path = NULL;
    CB_App->coverArtCache.art.bitmap = NULL;

    CB_App->bodyFont = playdate->graphics->loadFont("fonts/Roobert-11-Medium", NULL);
    CB_App->titleFont = playdate->graphics->loadFont("fonts/Roobert-20-Medium", NULL);
    CB_App->subheadFont = playdate->graphics->loadFont("fonts/Asheville-Sans-14-Bold", NULL);
    CB_App->labelFont = playdate->graphics->loadFont("fonts/Nontendo-Bold", NULL);
    CB_App->logoBitmap = playdate->graphics->loadBitmap("images/logo", NULL);

    check_is_bundle();

    if (!CB_App->bundled_rom)
    {
        cb_draw_logo_screen_and_display(CB_App->subheadFont, "Initializing...");
        initialize_directory();
        
        possibly_check_for_updates();
        
        playdate->system->logToConsole("shown intro: %d", (int)global.shown_intro);
        
        if (global.shown_intro || cb_file_exists(LAST_SELECTED_FILE, kFileReadData) || games_exist_in_data())
        {
            non_bundle_init();
        }
        else
        {
            CB_showHelp(true);
        }
    }
    else
    {
        // use local directory as root
        CB_App->directory = aprintf(".");
        playdate->file->mkdir(CB_savesPath);
        playdate->file->mkdir(CB_statesPath);
        playdate->file->mkdir(CB_settingsPath);
    }

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

    // add audio callback later
    CB_App->soundSource = NULL;

    // custom frame rate delimiter
    playdate->display->setRefreshRate(0);

    playdate->system->setSerialMessageCallback(CB_on_serial_message);
    
    if (CB_App->bundled_rom)
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

void CB_headphone_state_changed(int headphone, int mic)
{
    if (audioGameScene)
    {
        reconfigure_audio_source(audioGameScene, headphone);
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

    PDButtons prev_down = CB_App->buttons_down;

    playdate->system->getButtonState(
        &CB_App->buttons_down, &CB_App->buttons_pressed, &CB_App->buttons_released
    );

    // simulated button presses
    for (int i = 0; i < 6; ++i)
    {
        if (CB_App->simulate_button_presses[i])
        {
            PDButtons b = (1 << i);
            --CB_App->simulate_button_presses[i];
            CB_App->buttons_down |= b;
            if (!(prev_down & b))
            {
                CB_App->buttons_pressed |= b;
            }
            // TODO: buttons released
        }
    }
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
    if (gameName->name_header)
        cb_free(gameName->name_header);
}

void CB_quit(void)
{
    playdate->sound->getHeadphoneState(NULL, NULL, NULL);

    if (CB_App->scene)
    {
        void* managedObject = CB_App->scene->managedObject;
        CB_App->scene->free(managedObject);
    }

    cb_clear_global_cover_cache();

    if (CB_App->bodyFont)
    {
        cb_free(CB_App->bodyFont);
    }
    if (CB_App->titleFont)
    {
        cb_free(CB_App->titleFont);
    }
    if (CB_App->subheadFont)
    {
        cb_free(CB_App->subheadFont);
    }
    if (CB_App->labelFont)
    {
        cb_free(CB_App->labelFont);
    }

    if (CB_App->startSelectBitmap)
    {
        playdate->graphics->freeBitmap(CB_App->startSelectBitmap);
    }
    if (CB_App->selectorBitmapTable)
    {
        playdate->graphics->freeBitmapTable(CB_App->selectorBitmapTable);
    }

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

    if (!CB_App->bundled_rom)
    {
        free_json_data(CB_App->rhdb_cache);
    }

    if (CB_App->bundled_rom)
    {
        cb_free(CB_App->bundled_rom);
    }

    script_quit();
    version_quit();

#ifdef TARGET_PLAYDATE
    pdnewlib_quit();
#endif

    cb_free(CB_App);
}
