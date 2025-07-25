//
// settings_scene.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
#include "settings_scene.h"

#include "../../libs/minigb_apu/minigb_apu.h"
#include "../app.h"
#include "../dtcm.h"
#include "../preferences.h"
#include "../revcheck.h"
#include "../scenes/modal.h"
#include "../userstack.h"
#include "../utility.h"
#include "credits_scene.h"
#include "patches_scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_VISIBLE_ITEMS 6
#define SCROLL_INDICATOR_MIN_HEIGHT 10

static void CB_SettingsScene_update(void* object, uint32_t u32enc_dt);
static void CB_SettingsScene_free(void* object);
static void CB_SettingsScene_menu(void* object);
static void CB_SettingsScene_didSelectBack(void* userdata);
static void CB_SettingsScene_rebuildEntries(CB_SettingsScene* settingsScene);
static void CB_SettingsScene_attemptDismiss(CB_SettingsScene* settingsScene);
static void settings_load_state(CB_GameScene* gameScene, CB_SettingsScene* settingsScene);

bool save_state(CB_GameScene* gameScene, unsigned slot);
bool load_state(CB_GameScene* gameScene, unsigned slot);
extern const uint16_t CB_dither_lut_c0[];
extern const uint16_t CB_dither_lut_c1[];

static void update_thumbnail(CB_SettingsScene* settingsScene);

static char* itcm_base_desc = NULL;
static char* itcm_restart_desc = NULL;
static char* patches_desc = NULL;

#define HOLD_TIME_SUPPRESS_RELEASE 0.25f
#define HOLD_TIME_MARGIN 0.15f
#define HOLD_TIME 1.09f
#define HOLD_FADE_RATE 2.9f
#define HEADER_ANIMATION_RATE 2.8f
#define HEADER_HEIGHT 18

struct OptionsMenuEntry;

typedef struct OptionsMenuEntry
{
    const char* name;
    const char** values;
    const char* description;
    int* pref_var;
    unsigned max_value;

    bool locked : 1;
    bool show_value_only_on_hover : 1;
    bool thumbnail : 1;
    bool graphics_test : 1;
    bool header : 1;

    void (*on_press)(struct OptionsMenuEntry*, CB_SettingsScene* settingsScene);
    void (*on_hold)(struct OptionsMenuEntry*, CB_SettingsScene* settingsScene);
    void* ud;
} OptionsMenuEntry;

// how long to remember last-selected preference in menu
#define TIME_FORGET_LAST_PREFERENCE 15
static void* last_selected_preference;
static unsigned last_selected_preference_time;

static OptionsMenuEntry* getOptionsEntries(CB_SettingsScene* scene);

void display_credits(struct OptionsMenuEntry* entry, CB_SettingsScene* settingsScene)
{
    CB_showCredits(settingsScene);
}

void display_script_info(struct OptionsMenuEntry* entry, CB_SettingsScene* settingsScene)
{
    cb_play_ui_sound(CB_UISound_Confirm);
    CB_GameScene* gameScene = settingsScene->gameScene;
    if (gameScene && gameScene->script_info_available)
    {
        show_game_script_info(gameScene->rom_filename, gameScene->name_short);
    }
}

static void open_patches(OptionsMenuEntry* option, CB_SettingsScene* settingsScene)
{
    cb_play_ui_sound(CB_UISound_Confirm);
    CB_PatchesScene* patchesScene = CB_PatchesScene_new(option->ud);
    CB_presentModal(patchesScene->scene);
}

CB_SettingsScene* CB_SettingsScene_new(CB_GameScene* gameScene, CB_LibraryScene* libraryScene)
{
    setCrankSoundsEnabled(true);
    CB_SettingsScene* settingsScene = cb_malloc(sizeof(CB_SettingsScene));
    memset(settingsScene, 0, sizeof(*settingsScene));
    settingsScene->gameScene = gameScene;
    settingsScene->libraryScene = libraryScene;
    settingsScene->cursorIndex = 0;
    settingsScene->topVisibleIndex = 0;
    settingsScene->crankAccumulator = 0.0f;
    settingsScene->shouldDismiss = false;
    settingsScene->entries = getOptionsEntries(settingsScene);

    // Initialize continuous scrolling variables
    settingsScene->scroll_direction = 0;
    settingsScene->repeatLevel = 0;
    settingsScene->repeatIncrementTime = 0.0f;
    settingsScene->repeatTime = 0.0f;

    // Store the true global value for UI sounds before any potential changes.
    int global_ui_sounds = preferences_ui_sounds;

    if (gameScene)
    {
        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 1.0f);
    }

    settingsScene->totalMenuItemCount = 0;
    if (settingsScene->entries)
    {
        for (int i = 0; settingsScene->entries[i].name; i++)
        {
            settingsScene->totalMenuItemCount++;
        }
    }

    // Ensure the initial cursor position is not on a header item.
    if (settingsScene->totalMenuItemCount > 0)
    {
        while (settingsScene->entries[settingsScene->cursorIndex].header)
        {
            settingsScene->cursorIndex++;
            // This check prevents an infinite loop if all items are somehow headers.
            if (settingsScene->cursorIndex >= settingsScene->totalMenuItemCount)
            {
                settingsScene->cursorIndex = 0;
                break;
            }
        }
    }

    if (gameScene)
    {
        settingsScene->wasAudioLocked = gameScene->audioLocked;
        gameScene->audioLocked = true;
    }

    CB_Scene* scene = CB_Scene_new();
    scene->managedObject = settingsScene;
    scene->update = CB_SettingsScene_update;
    scene->free = CB_SettingsScene_free;
    scene->menu = CB_SettingsScene_menu;

    settingsScene->scene = scene;

    settingsScene->initial_sound_mode = preferences_sound_mode;
    settingsScene->initial_sample_rate = preferences_sample_rate;
    settingsScene->initial_per_game = preferences_per_game;

    if (gameScene)
    {
        // some settings cannot be changed
        settingsScene->immutable_settings = preferences_store_subset(prefs_locked_by_script);
    }
    else
    {
        // (dummy)
        settingsScene->immutable_settings = preferences_store_subset(0);
    }

    settingsScene->header_animation_p = preferences_per_game;

    preferences_ui_sounds = global_ui_sounds;

    CB_Scene_refreshMenu(scene);

    if (last_selected_preference &&
        playdate->system->getSecondsSinceEpoch(NULL) - last_selected_preference_time <=
            TIME_FORGET_LAST_PREFERENCE)
    {
        int i = 0;
        for (OptionsMenuEntry* entry = settingsScene->entries; entry->name; ++entry, ++i)
        {
            if (entry->pref_var == last_selected_preference)
            {
                settingsScene->cursorIndex = i;
                break;
            }
        }
    }

    update_thumbnail(settingsScene);

    return settingsScene;
}

static void state_action_modal_callback(void* userdata, int option)
{
    CB_SettingsScene* settingsScene = userdata;

    if (option == 0)
    {
        settingsScene->shouldDismiss = true;
    }
}

static void settings_load_state(CB_GameScene* gameScene, CB_SettingsScene* settingsScene)
{
    if (!load_state(gameScene, preferences_save_state_slot))
    {
        const char* options[] = {"OK", NULL};
        CB_presentModal(CB_Modal_new("Failed to load state.", options, NULL, NULL)->scene);
        playdate->system->logToConsole("Error loading state %d", 0);
    }
    else
    {
        playdate->system->logToConsole("Loaded save state %d", 0);

        // TODO: something less invasive than a modal here.
        const char* options[] = {"Game", "Settings", NULL};
        CB_presentModal(CB_Modal_new(
                            "State loaded. Return to:", options, state_action_modal_callback,
                            settingsScene
        )
                            ->scene);
    }
}

typedef struct
{
    CB_GameScene* gameScene;
    CB_SettingsScene* settingsScene;
} LoadStateUserdata;

static void settings_confirm_load_state(void* userdata, int option)
{
    LoadStateUserdata* data = userdata;
    if (option == 1)
    {
        settings_load_state(data->gameScene, data->settingsScene);
    }
    cb_free(data);
}

static void CB_SettingsScene_attemptDismiss(CB_SettingsScene* settingsScene)
{
    int result = 0;

    if (settingsScene->gameScene)
    {
        if (preferences_per_game)
        {
            result = (int)(intptr_t)call_with_main_stack_2(
                preferences_save_to_disk, settingsScene->gameScene->settings_filename,
                prefs_locked_by_script | PREFBITS_LIBRARY_ONLY
            );
        }
        else
        {
            result = (int)(intptr_t)call_with_main_stack_2(
                preferences_save_to_disk, CB_globalPrefsPath,
                0
                    // never save these to global prefs
                    | PREFBIT_per_game | PREFBIT_save_state_slot |
                    PREFBITS_LIBRARY_ONLY

                    // these prefs are locked, so we shouldn't be able to change them
                    | prefs_locked_by_script
            );

            if (result)
            {
                // also save that preferences are global in the per-game script,
                // and also the save slot
                result = (int)(intptr_t)call_with_main_stack_2(
                    preferences_save_to_disk, settingsScene->gameScene->settings_filename,
                    ~(PREFBIT_per_game | PREFBIT_save_state_slot)
                );
            }
        }
    }
    else
    {
        // Not in a game, just save the global file
        result =
            (int)(intptr_t)call_with_main_stack_2(preferences_save_to_disk, CB_globalPrefsPath, 0);
    }

    if (!result)
    {
        CB_presentModal(CB_Modal_new("Error saving preferences.", NULL, NULL, NULL)->scene);
    }
    else
    {
        CB_dismiss(settingsScene->scene);
    }
}

#define ROTATE(var, dir, max)          \
    {                                  \
        var = (var + dir + max) % max; \
    }
#define STRFMT_LAMBDA(...)                                  \
    LAMBDA(char*, (struct OptionsMenuEntry * e), {          \
        char* _RET;                                         \
        playdate->system->formatString(&_RET, __VA_ARGS__); \
        return _RET;                                        \
    })

static const char* sound_mode_labels[] = {"Off", "Fast", "Accurate"};
static const char* off_on_labels[] = {"Off", "On"};
static const char* gb_button_labels[] = {"None", "Start", "Select", "A", "B"};
static const char* crank_mode_labels[] = {"Start/Select", "Turbo A/B", "Turbo B/A", "Off"};
static const char* sample_rate_labels[] = {"High", "Medium", "Low"};
static const char* dynamic_rate_labels[] = {"Off", "On", "Auto"};
static const char* fps_labels[] = {"Off", "On", "Playdate"};
static const char* slot_labels[] = {"[slot 0]", "[slot 1]", "[slot 2]", "[slot 3]", "[slot 4]",
                                    "[slot 5]", "[slot 6]", "[slot 7]", "[slot 8]", "[slot 9]"};
static const char* dither_pattern_labels[] = {"Staggered", "Grid",          "Staggered (L)",
                                              "Grid (L)",  "Staggered (D)", "Grid (D)"};
static const char* overclock_labels[] = {"Off", "x2", "x4"};
static const char* dynamic_level_labels[] = {"1", "2", "3", "4",  "5", "6",
                                             "7", "8", "9", "10", "11"};
static const char* settings_scope_labels[] = {"Global", "Game"};
static const char* display_name_mode_labels[] = {"Short", "Detailed", "Filename"};
static const char* sort_labels[] = {"Filename", "Database", "DB (w/article)", "File (w/article)"};
static const char* article_labels[] = {"Leading", "As-is"};
static const char* next_scene[] = {">"};

static void update_thumbnail(CB_SettingsScene* settingsScene)
{
    int slot = preferences_save_state_slot;
    CB_GameScene* gameScene = settingsScene->gameScene;

    if (!gameScene)
        return;

    bool result = load_state_thumbnail(gameScene, slot, settingsScene->thumbnail);

    if (!result)
    {
        memset(settingsScene->thumbnail, 0xFF, sizeof(settingsScene->thumbnail));
    }
}

static void confirm_save_state(CB_SettingsScene* settingsScene, int option)
{
    // must select 'yes'
    if (option != 1)
        return;

    CB_GameScene* gameScene = settingsScene->gameScene;
    int slot = preferences_save_state_slot;
    if (!save_state(gameScene, slot))
    {
        char* msg;
        playdate->system->formatString(&msg, "Error saving state:\n%s", playdate->file->geterr());
        const char* options[] = {"OK", NULL};
        CB_presentModal(CB_Modal_new(msg, options, NULL, NULL)->scene);
        cb_free(msg);
    }
    else
    {
        playdate->system->logToConsole("Saved state %d successfully", slot);

        // TODO: something less invasive than a modal here.
        const char* options[] = {"Game", "Settings", NULL};
        CB_presentModal(CB_Modal_new(
                            "State saved. Return to:", options, state_action_modal_callback,
                            settingsScene
        )
                            ->scene);
    }

    update_thumbnail(settingsScene);
}

static void settings_action_save_state(OptionsMenuEntry* e, CB_SettingsScene* settingsScene)
{
    cb_play_ui_sound(CB_UISound_Confirm);
    CB_GameScene* gameScene = e->ud;
    int slot = preferences_save_state_slot;

    unsigned timestamp = get_save_state_timestamp(gameScene, slot);
    unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);

    // warn if overwriting an old save state
    if (timestamp != 0 && timestamp <= now)
    {
        char* human_time = en_human_time(now - timestamp);
        char* msg;
        playdate->system->formatString(&msg, "Overwrite state which is %s old?", human_time);
        cb_free(human_time);

        const char* options[] = {"Cancel", "Yes", NULL};
        CB_presentModal(
            CB_Modal_new(msg, options, (CB_ModalCallback)confirm_save_state, settingsScene)->scene
        );

        cb_free(msg);
    }
    else
    {
        confirm_save_state(settingsScene, 1);
    }
}

static void settings_action_load_state(OptionsMenuEntry* e, CB_SettingsScene* settingsScene)
{
    cb_play_ui_sound(CB_UISound_Confirm);
    CB_GameScene* gameScene = e->ud;
    int slot = preferences_save_state_slot;

    // confirmation needed if more than 2 minutes of progress made
    if (gameScene->playtime >= 60 * 120)
    {
        const char* confirm_options[] = {"No", "Yes", NULL};
        LoadStateUserdata* data = cb_malloc(sizeof(LoadStateUserdata));
        data->gameScene = gameScene;
        data->settingsScene = settingsScene;
        unsigned timestamp = get_save_state_timestamp(gameScene, slot);
        unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);

        char* text;
        if (timestamp == 0 || timestamp > now)
        {
            text = cb_strdup("Really load state?");
        }
        else
        {
            char* human_time = en_human_time(now - timestamp);
            playdate->system->formatString(&text, "Really load state from %s ago?", human_time);
            cb_free(human_time);
        }

        CB_presentModal(
            CB_Modal_new(text, confirm_options, (void*)settings_confirm_load_state, data)->scene
        );
        cb_free(text);
    }
    else
    {
        settings_load_state(gameScene, settingsScene);
    }
}

static OptionsMenuEntry* getOptionsEntries(CB_SettingsScene* scene)
{
    CB_GameScene* gameScene = scene->gameScene;
    CB_LibraryScene* libraryScene = scene->libraryScene;
    CB_Game* selectedGame =
        (libraryScene && libraryScene->listView->selectedItem < libraryScene->games->length)
            ? libraryScene->games->items[libraryScene->listView->selectedItem]
            : NULL;

    int max_entries = 30;  // we can overshoot, it's ok
    OptionsMenuEntry* entries = cb_malloc(sizeof(OptionsMenuEntry) * max_entries);
    if (!entries)
        return NULL;
    memset(entries, 0, sizeof(OptionsMenuEntry) * max_entries);

    /* clang-format off */
    int i = -1;

    if (gameScene)
    {
        if (!gameScene->save_states_supported)
        {
            entries[++i] = (OptionsMenuEntry){
                .name = "Save state",
                .values = NULL,
                .description =
                    "CrankBoy does not\ncurrently support\ncreating save states\n"
                    "with a ROM that has its\nown save data.",
                .pref_var = &preferences_save_state_slot,
                .max_value = 0,
                .locked = 1,
                .on_press = NULL
            };
        }
        else
        {
            // save state
            entries[++i] = (OptionsMenuEntry){
                .name = "Save state",
                .values = slot_labels,
                .description =
                    "Create a snapshot of\nthis moment, which\ncan be resumed later.",
                .pref_var = &preferences_save_state_slot,
                .max_value = SAVE_STATE_SLOT_COUNT,
                .show_value_only_on_hover = 1,
                .thumbnail = 1,
                .on_press = settings_action_save_state,
                .ud = gameScene,
            };

            // load state
            entries[++i] = (OptionsMenuEntry){
                .name = "Load state",
                .values = slot_labels,
                .description =
                    "Restore the previously-\ncreated snapshot."
                ,
                .pref_var = &preferences_save_state_slot,
                .max_value = SAVE_STATE_SLOT_COUNT,
                .show_value_only_on_hover = 1,
                .thumbnail = 1,
                .on_press = settings_action_load_state,
                .ud = gameScene,
            };
        }

        entries[++i] = (OptionsMenuEntry){
            .name = "Settings scope",
            .values = settings_scope_labels,
            .description =
                "Use shared settings or\ncreate custom ones for\nthis game.\n \n"
                "Global:\nSettings are shared\nacross all games.\n \n"
                "Game:\nSettings are unique\nto this game.",
            .pref_var = &preferences_per_game,
            .max_value = 2,
            .on_press = NULL,
        };
    }

    if (libraryScene && selectedGame)
    {
        if (patches_desc != NULL) {
            cb_free(patches_desc);
        }

        patches_desc = aprintf("Press Ⓐ to view and toggle\npatches and ROMhacks for\n%s", selectedGame->names->name_short_leading_article);

        entries[++i] = (OptionsMenuEntry){
            .name = "Patches",
            .description = patches_desc,
            .values = next_scene,
            .max_value = 0,
            .on_press = open_patches,
            .ud = selectedGame
        };
    }

    entries[++i] = (OptionsMenuEntry){
        .name = "Audio",
        .header = 1
    };

    // sound
    {
        entries[++i] = (OptionsMenuEntry){
            .name = "Sound",
            .values = sound_mode_labels,
            .description =
                "Accurate:\nHighest quality sound.\n \nFast:\nGood balance of\n"
                "quality and speed.\n \nOff:\nNo audio for best\nperformance.",
            .pref_var = &preferences_sound_mode,
            .max_value = 3,
            .on_press = NULL,
        };
    }

    // sample rate
    entries[++i] = (OptionsMenuEntry){
        .name = "Sample rate",
        .values = sample_rate_labels,
        .description =
            "Adjusts audio quality.\nHigher values may impact\nperformance.\n \n"
            "High:\nBest quality (44.1 kHz)\n \n"
            "Medium:\nGood quality (22.1 kHz)\n \n"
            "Low:\nReduced quality (14.7 kHz)",
        .pref_var = &preferences_sample_rate,
        .max_value = 3,
        .on_press = NULL,
    };

    entries[++i] = (OptionsMenuEntry){
        .name = "Display",
        .header = 1
    };

    // frame skip
    entries[++i] = (OptionsMenuEntry){
        .name = "30 FPS mode",
        .values = off_on_labels,
        .description =
            "Skips displaying every\nsecond frame. Greatly\nimproves performance\n"
            "for most games.\n \nDespite appearing to be\n30 FPS, the game "
            "itself\nstill runs at full speed.\n \nEnabling this mode\ndisables "
            "the Interlacing\nsettings.",
        .pref_var = &preferences_frame_skip,
        .max_value = 2,
        .on_press = NULL,
    };

    // dynamic rate adjustment
    if (preferences_frame_skip)
    {
        entries[++i] = (OptionsMenuEntry){
            .name = "Interlacing",
            .values = dynamic_rate_labels,
            .description = "Unavailable in\n30 FPS mode.",
            .pref_var = &preferences_dynamic_rate,
            .max_value = 0,
            .on_press = NULL,
        };
    }
    else
    {
        entries[++i] = (OptionsMenuEntry){
            .name = "Interlacing",
            .values = dynamic_rate_labels,
            .description =
                "Skips lines to keep the\nframerate smooth.\n \n"
                "Off:\nFull quality, no skipping.\n \n"
                "On:\nAlways on for a reliable\nspeed boost.\n \n"
                "Auto:\nRecommended. Skips lines\nonly when needed.",
            .pref_var = &preferences_dynamic_rate,
            .max_value = 3,
            .on_press = NULL,
        };
    }

    #if TENDENCY_BASED_ADAPTIVE_INTERLACING
    // dynamic level
    if (preferences_dynamic_rate == DYNAMIC_RATE_AUTO && !preferences_frame_skip)
    {
        entries[++i] = (OptionsMenuEntry){
            .name = "Interlacing level",
            .values = dynamic_level_labels,
            .description =
                "Adjusts sensitivity\nbased on the amount of\non-screen change.\n \n"
                "Higher values are less\nsensitive and require more\n"
                "change to activate\ninterlacing.",
            .pref_var = &preferences_dynamic_level,
            .max_value = 11,
            .on_press = NULL,
        };
    }
    else
    {
        entries[++i] = (OptionsMenuEntry){
            .name = "Interlacing level",
            .values = dynamic_level_labels,
            .description =
                "Adjusts sensitivity\nbased on the amount of\non-screen change.\n \n"
                "Higher values are less\nsensitive and require more\n"
                "change to activate\ninterlacing.",
            .pref_var = &preferences_dynamic_level,
            .max_value = 0,
            .on_press = NULL,
        };
    }
    #endif

    // dither
    entries[++i] = (OptionsMenuEntry){
        .name = "Dither",
        .values = dither_pattern_labels,
        .description =
            "How to represent\n4-color graphics\non a 1-bit display.\n \nL: bias toward light\n \nD: bias toward dark"
        ,
        .pref_var = &preferences_dither_pattern,
        .max_value = 6,
        .graphics_test = 1,
        .on_press = NULL
    };

    // dither line
    entries[++i] = (OptionsMenuEntry){
        .name = "First scaling line",
        .values = dynamic_level_labels,
        .description =
            "Due to the 3:5 ratio\nbetween the GB's and\nPlaydate's vertical\nresolutions, 1 in every\n3 scanlines must be\nvertically squished.\n \nThis means there are three\nchoices for which lines are\nto be the ones to squish.\n \nIf text is uneven, try\nadjusting this."
        ,
        .pref_var = &preferences_dither_line,
        .max_value = 3,
        .on_press = NULL
    };

    // stabilization
    entries[++i] = (OptionsMenuEntry){
        .name = "Stabilization",
        .values = off_on_labels,
        .description =
            "Stabilizes scaling artifacts\nby making them scroll with\nthe camera.\n \n"
            "This prevents textures\nfrom shimmering, but may\n"
            "reduce performance\nslightly in scroll-heavy\ngames.\n \n"
            "Works best with Dither\nset to \"Staggered\".",
        .pref_var = &preferences_dither_stable,
        .max_value = 2,
        .on_press = NULL
    };
    #if ENABLE_BGCACHE
    // transparency
    entries[++i] = (OptionsMenuEntry){
        .name = "Transparency",
        .values = off_on_labels,
        .description =
            "Enables drawing of\ntransparent pixels.\n \n"
            "Increses visual fidelity\nbut comes at the cost\n"
            "of performance.",
        .pref_var = &preferences_transparency,
        .max_value = 2,
        .on_press = NULL
    };
    #endif

    entries[++i] = (OptionsMenuEntry){
        .name = "Behavior",
        .header = 1
    };

    // crank mode
    entries[++i] = (OptionsMenuEntry){
        .name = "Crank",
        .values = crank_mode_labels,
        .description =
            "Assign a (turbo) function\nto the crank.\n \nStart/Select:\nCW for "
            "Start, CCW for Select.\n \nTurbo A/B:\nCW for A, CCW for B.\n \nTurbo "
            "B/A:\nCW for B, CCW for A.\n \n",
        .pref_var = &preferences_crank_mode,
        .max_value = 4,
        .on_press = NULL
    };

    // undock
    entries[++i] = (OptionsMenuEntry){
        .name = "Undock",
        .values = gb_button_labels,
        .description =
            "Assign a button input\nfor undocking the crank.\n \n",
        .pref_var = &preferences_crank_undock_button,
        .max_value = 3,
        .on_press = NULL
    };

    // dock
    entries[++i] = (OptionsMenuEntry){
        .name = "Dock",
        .values = gb_button_labels,
        .description =
            "Assign a button input\nfor docking the crank.\n \n",
        .pref_var = &preferences_crank_dock_button,
        .max_value = 3,
        .on_press = NULL
    };

    // overclocking
    entries[++i] = (OptionsMenuEntry){
        .name = "Overclock",
        .values = overclock_labels,
        .description =
            "Attempt to reduce lag\nin emulated device, but\nthe Playdate must work\nharder to achieve this.\n \n"
            "Allows the emulated CPU\nto run much faster\nduring VBLANK.\n \n"
            "Not a guaranteed way to\nimprove performance,\nand may introduce\ninaccuracies."
        ,
        .pref_var = &preferences_overclock,
        .max_value = 3,
        .on_press = NULL
    };

    // BIOS
    entries[++i] = (OptionsMenuEntry){
        .name = "Boot sequence",
        .values = off_on_labels,
        .description =
            "Enables \"Boot ROM\" on\ngame start.\n \n\"dmg_boot.bin\" must\nbe present."
        ,
        .pref_var = &preferences_bios,
        .max_value = 2,
        .on_press = NULL
    };

    #define BASE_LUA_STRING "Scripts attempt to add\nPlaydate feature support\ninto ROMs. For instance,\nthe crank might be used to\nnavigate menus. Enabling\nmay impact performance."

    #ifndef NOLUA
    // lua scripts
    entries[++i] = (OptionsMenuEntry){
        .name = "Game scripts",
        .values = off_on_labels,
        .description =
            BASE_LUA_STRING,
        .pref_var = &preferences_script_support,
        .max_value = 2,
        .locked = 0,
        .on_press = NULL,
    };

    if (gameScene)
    {
        if (gameScene->script_available)
        {
            entries[i].description = BASE_LUA_STRING "\n \nYou must restart the\nROM for this setting\nto take effect.";
            if (gameScene->script_info_available)
            {
                entries[i].description = BASE_LUA_STRING "\n \nHold the Ⓐ button now\nfor more information.\n \nYou must restart the\nROM for this setting\nto take effect.";
                entries[i].on_hold = display_script_info;
            }
        }
        else
        {
            entries[i].description = BASE_LUA_STRING "\n \nThere is no script\navailable for this ROM.";
            entries[i].locked = 1;
        }
    }
    #endif

    if (!gameScene)
    {
        entries[++i] = (OptionsMenuEntry){
            .name = "Library",
            .header = 1
        };

        // display name mode
        entries[++i] = (OptionsMenuEntry){
            .name = "Title display",
            .values = display_name_mode_labels,
            .description = "Choose how game titles\n"
                        "are displayed in the list.\n \n"
                        "Short:\nThe common game title\n(by database match).\n \n"
                        "Detailed:\nThe full title, including\nregion and version info.\n \n"
                        "Filename:\nThe original ROM filename.\n \n",
            .pref_var = &preferences_display_name_mode,
            .max_value = 3,
            .on_press = NULL
        };

        // display article
        entries[++i] = (OptionsMenuEntry){
            .name = "Article",
            .values = article_labels,
            .description = "If a game title ends with\n"
                        "an article, such as\n \n  \"Mummy, The (USA)\"\n \n"
                        "then it can displayed at\n"
                        "the start instead, i.e.\n \n"
                        "  \"The Mummy (USA)\"\n",
            .pref_var = &preferences_display_article,
            .max_value = 2,
            .on_press = NULL
        };

        // sorting
        entries[++i] = (OptionsMenuEntry){
            .name = "Sort",
            .values = sort_labels,
            .description = "Sort the games list\nby filename or\nby database name.\n \nCan also choose to include\narticles that have been\nmoved to the front of the\nname toward sorting.",
            .pref_var = &preferences_display_sort,
            .max_value = 4,
            .on_press = NULL
        };

        // remember selection
        entries[++i] = (OptionsMenuEntry){
            .name = "Remember Last",
            .values = off_on_labels,
            .description = "When opening the library,\n"
                "initial selection will\nbe the last game played.\n",
            .pref_var = &preferences_library_remember_selection,
            .max_value = 2,
            .on_press = NULL
        };
    }

    entries[++i] = (OptionsMenuEntry){
        .name = "Miscellaneous",
        .header = 1
    };

    // show fps
    entries[++i] = (OptionsMenuEntry){
        .name = "Show FPS",
        .values = fps_labels,
        .description =
            "Displays the current\nframes-per-second\non screen.\n \n"
            "Choice of displaying\nPlaydate screen refreshes\nor emulated frames.\n(These can differ if 30 FPS\nmode is enabled.)"
        ,
        .pref_var = &preferences_display_fps,
        .max_value = 3,
        .on_press = NULL
    };

    // uncap fps
    entries[++i] = (OptionsMenuEntry){
        .name = "Uncapped FPS",
        .values = off_on_labels,
        .description =
            "Removes the speed limit.\n \nThis is intended\njust for benchmarking\nperformance, not for\ncasual play."
        ,
        .pref_var = &preferences_uncap_fps,
        .max_value = 2,
        .on_press = NULL
    };

    if (!gameScene)
    {
        // ui sounds
        entries[++i] = (OptionsMenuEntry){
            .name = "UI sounds",
            .values = off_on_labels,
            .description = "Enable or disable\ninterface sound effects.",
            .pref_var = &preferences_ui_sounds,
            .max_value = 2,
            .on_press = NULL,
        };
    }

    #if defined(ITCM_CORE) && defined(DTCM_ALLOC)
    // itcm accel
        if (itcm_base_desc == NULL) {
            playdate->system->formatString(&itcm_base_desc, "Unstable, but greatly\nimproves performance.\n \n"
                "Runs emulator core\ndirectly from the stack.\n \n"
                "Works with Rev A.\n(Your device: %s)",
                pd_rev_description
            );
        }

        if (itcm_restart_desc == NULL) {
            playdate->system->formatString(&itcm_restart_desc, "%s\n \nYou need to restart the\ngame for these changes to\napply.", itcm_base_desc);
        }

        entries[++i] = (OptionsMenuEntry){
            .name = "ITCM acceleration",
            .values = off_on_labels,
            .pref_var = &preferences_itcm,
            .max_value = 2,
            .on_press = NULL
        };

        if (gameScene) {
            entries[i].description = itcm_restart_desc;
        } else {
            entries[i].description = itcm_base_desc;
        }
    #endif

    if (CB_App->bundled_rom)
    {
        entries[++i] = (OptionsMenuEntry){
            .name = "About CrankBoy",
            .values = NULL,
            .description = "This game is bundled for\nPlaydate via CrankBoy,\na Game Boy emulator.\n \nPress Ⓐ now to learn\nmore about CrankBoy\nand its developers.",
            .pref_var = NULL,
            .max_value = 0,
            .on_press = display_credits
        };
    }

    /* clang-format on */
    CB_ASSERT(i < max_entries - 1);

    // remove any entries hidden by bundle
    if (preferences_bundle_hidden)
    {
        for (size_t j = max_entries - 1; j-- > 0;)
        {
            bool remove = false;
            struct OptionsMenuEntry* entry = &entries[j];

            // remove header if no options below it
            if (entry->header && (entries[j + 1].header || !entries[j + 1].name))
                goto do_remove;

// remove normal option (if hidden)
#define PREF(p, ...)                                                                      \
    if ((preferences_bundle_hidden & PREFBIT_##p) && entry->pref_var == &preferences_##p) \
        goto do_remove;
#include "prefs.x"

            continue;

        do_remove:
            for (size_t k = j; k < max_entries - 1; ++k)
            {
                entries[k] = entries[k + 1];
            }
        }
    }

    // disable any entries if script requires it
    for (int i = 0; i < max_entries; ++i)
    {
        OptionsMenuEntry* entry = &entries[i];
        int j = 0;
#define PREF(x, ...)                                                   \
    if (entry->pref_var == &preferences_##x)                           \
    {                                                                  \
        if (prefs_locked_by_script & (1 << (preferences_bitfield_t)j)) \
        {                                                              \
            entry->locked = 1;                                         \
            entry->description = "Disabled by game script.";           \
        }                                                              \
    }                                                                  \
    ++j;
#include "prefs.x"
    }

    return entries;
};

static void CB_SettingsScene_rebuildEntries(CB_SettingsScene* settingsScene)
{
    if (settingsScene->entries)
    {
        cb_free(settingsScene->entries);
    }

    settingsScene->entries = getOptionsEntries(settingsScene);

    // count all entries that have a name
    settingsScene->totalMenuItemCount = 0;
    if (settingsScene->entries)
    {
        for (int i = 0; settingsScene->entries[i].name; i++)
        {
            settingsScene->totalMenuItemCount++;
        }
    }

    if (settingsScene->cursorIndex >= settingsScene->totalMenuItemCount)
    {
        settingsScene->cursorIndex = settingsScene->totalMenuItemCount - 1;
    }
}

static void CB_SettingsScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    float dt = UINT32_AS_FLOAT(u32enc_dt);
    static const uint8_t black_transparent_dither[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                         0xFF, 0xFF, 0xAA, 0x55, 0xAA, 0x55,
                                                         0xAA, 0x55, 0xAA, 0x55};
    static const uint8_t white_transparent_dither[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55
    };

    CB_SettingsScene* settingsScene = object;
    int oldCursorIndex = settingsScene->cursorIndex;

    if (settingsScene->shouldDismiss)
    {
        CB_SettingsScene_attemptDismiss(settingsScene);
        return;
    }

    CB_GameScene* gameScene = settingsScene->gameScene;

    TOWARD(settingsScene->header_animation_p, preferences_per_game, dt * HEADER_ANIMATION_RATE);

    int header_y = settingsScene->header_animation_p * HEADER_HEIGHT + 0.5f;

    const int kScreenHeight = 240;
    const int kDividerX = 240;
    const int kLeftPanePadding = 20;
    const int kRightPanePadding = 10;

    int menuItemCount = settingsScene->totalMenuItemCount;

    CB_Scene_update(settingsScene->scene, dt);

    // Consolidate all inputs into a single 'steps' variable
    int steps = 0;

    // Crank Input
    float crank_change = playdate->system->getCrankChange();
    settingsScene->crankAccumulator += crank_change;
    const float crank_threshold = 45.0f;

    while (settingsScene->crankAccumulator >= crank_threshold)
    {
        steps++;
        settingsScene->crankAccumulator -= crank_threshold;
    }

    while (settingsScene->crankAccumulator <= -crank_threshold)
    {
        steps--;
        settingsScene->crankAccumulator += crank_threshold;
    }

    // Button Input (discrete and continuous)
    PDButtons pushed = CB_App->buttons_pressed;
    PDButtons pressed = CB_App->buttons_down;
    PDButtons released = CB_App->buttons_released;

    if (pushed & kButtonDown)
    {
        steps++;
    }
    if (pushed & kButtonUp)
    {
        steps--;
    }

    // --- Continuous Scrolling Logic ---
    int old_direction = settingsScene->scroll_direction;
    int current_direction = 0;

    if (pressed & kButtonUp)
    {
        current_direction = -1;
    }
    else if (pressed & kButtonDown)
    {
        current_direction = 1;
    }
    settingsScene->scroll_direction = current_direction;

    if (settingsScene->scroll_direction == 0 || settingsScene->scroll_direction != old_direction)
    {
        settingsScene->repeatIncrementTime = 0;
        settingsScene->repeatLevel = 0;
        settingsScene->repeatTime = 0;
    }
    else
    {
        const float repeatInterval1 = 0.15f;
        const float repeatInterval2 = 2.0f;

        settingsScene->repeatIncrementTime += dt;

        float repeatInterval = (settingsScene->repeatLevel > 0) ? repeatInterval2 : repeatInterval1;

        if (settingsScene->repeatIncrementTime >= repeatInterval)
        {
            settingsScene->repeatLevel = CB_MIN(3, settingsScene->repeatLevel + 1);
            settingsScene->repeatIncrementTime =
                fmodf(settingsScene->repeatIncrementTime, repeatInterval);
        }

        if (settingsScene->repeatLevel > 0)
        {
            settingsScene->repeatTime += dt;

            float repeatRate = 0.16f;
            if (settingsScene->repeatLevel == 2)
            {
                repeatRate = 0.1f;
            }
            else if (settingsScene->repeatLevel == 3)
            {
                repeatRate = 0.05f;
            }

            while (settingsScene->repeatTime >= repeatRate)
            {
                settingsScene->repeatTime -= repeatRate;
                steps += settingsScene->scroll_direction;
            }
        }
    }
    // --- End Continuous Scrolling ---

    // Apply cursor movement if there are steps to take
    if (steps != 0)
    {
        settingsScene->option_hold_time = 0;
        int direction = (steps > 0) ? 1 : -1;
        int num_steps = abs(steps);

        for (int i = 0; i < num_steps; ++i)
        {
            // Move the cursor, skipping over any headers
            do
            {
                settingsScene->cursorIndex += direction;

                // Wrap the cursor index if it goes out of bounds
                if (settingsScene->cursorIndex >= menuItemCount)
                {
                    settingsScene->cursorIndex = 0;
                }
                else if (settingsScene->cursorIndex < 0)
                {
                    settingsScene->cursorIndex = menuItemCount - 1;
                }
            } while (settingsScene->entries[settingsScene->cursorIndex].header);
        }
    }

    // Note that storing last selected by the entry's corresponding pref var
    // has the unexpected side effect that "load state" will always be stored
    // as "save state."
    //
    // This is actually a good thing! We don't want to mess with players' muscle
    // memories and cause them to accidentally load state when they mean to save state
    last_selected_preference = settingsScene->entries[settingsScene->cursorIndex].pref_var;
    last_selected_preference_time = playdate->system->getSecondsSinceEpoch(NULL);

    if (oldCursorIndex != settingsScene->cursorIndex)
    {
        cb_play_ui_sound(CB_UISound_Navigate);
    }

    if (pushed & kButtonB)
    {
        CB_SettingsScene_attemptDismiss(settingsScene);
        return;
    }

    if (settingsScene->cursorIndex - 1 < settingsScene->topVisibleIndex)
    {
        settingsScene->topVisibleIndex = MAX(0, settingsScene->cursorIndex - 1);
    }
    else if (settingsScene->cursorIndex >= settingsScene->topVisibleIndex + MAX_VISIBLE_ITEMS - 1)
    {
        settingsScene->topVisibleIndex =
            MIN(settingsScene->cursorIndex - (MAX_VISIBLE_ITEMS - 2),
                settingsScene->totalMenuItemCount - MAX_VISIBLE_ITEMS);
    }

    OptionsMenuEntry* cursor_entry = &settingsScene->entries[settingsScene->cursorIndex];

    bool a_pressed = (pushed & kButtonA);
    if (cursor_entry->on_hold && !cursor_entry->locked)
    {
        a_pressed = released & kButtonA;
        if (settingsScene->option_hold_time >= HOLD_TIME_SUPPRESS_RELEASE)
            a_pressed = 0;
    }
    int direction = !!(pushed & kButtonRight) - !!(pushed & kButtonLeft);

    preference_t old_preferences_per_game = preferences_per_game;

    if (cursor_entry->on_hold && !cursor_entry->locked)
    {
        if (pressed & kButtonA)
        {
            settingsScene->option_hold_time += dt;
        }
        else
        {
            settingsScene->option_hold_time -= HOLD_FADE_RATE * dt;
        }

        if (settingsScene->option_hold_time >= HOLD_TIME)
        {
            settingsScene->option_hold_time = 0;
            cursor_entry->on_hold(cursor_entry, settingsScene);
            return;
        }

        if (settingsScene->option_hold_time < 0)
            settingsScene->option_hold_time = 0;
    }
    if (cursor_entry->on_press && a_pressed)
    {
        cursor_entry->on_press(cursor_entry, settingsScene);
    }
    else if (cursor_entry->pref_var && cursor_entry->max_value > 0 && !cursor_entry->locked)
    {
        if (direction == 0)
            direction = a_pressed;

        if (direction != 0)
        {
            // increment/decrement the setting
            int old_value = *cursor_entry->pref_var;

            *cursor_entry->pref_var =
                (old_value + direction + cursor_entry->max_value) % cursor_entry->max_value;

            if (old_value != *cursor_entry->pref_var)
            {
                // setting value has changed
                cb_play_ui_sound(CB_UISound_Confirm);

                // special behavior if we've switched between per-game and global settings
                if (cursor_entry->pref_var == &preferences_per_game)
                {
                    int global_ui_sounds = preferences_ui_sounds;
                    void* stored_save_slot = preferences_store_subset(PREFBIT_save_state_slot);

                    const char* game_settings_path = settingsScene->gameScene->settings_filename;
                    if (!preferences_per_game && old_preferences_per_game)
                    {
                        // write per-game prefs to disk
                        preferences_per_game = 0;  // paranoia: record in game settings that we're
                                                   // using global settings
                        call_with_main_stack_2(
                            preferences_save_to_disk, game_settings_path, prefs_locked_by_script
                        );

                        preferences_merge_from_disk(CB_globalPrefsPath);
                        preferences_per_game = 0;
                    }
                    else if (preferences_per_game && !old_preferences_per_game)
                    {
                        // write global prefs to disk
                        call_with_main_stack_2(
                            preferences_save_to_disk, CB_globalPrefsPath,
                            PREFBIT_per_game | PREFBIT_save_state_slot | prefs_locked_by_script
                        );

                        preferences_merge_from_disk(game_settings_path);
                        preferences_per_game = 1;
                    }

                    if (stored_save_slot)
                    {
                        preferences_restore_subset(stored_save_slot);
                        cb_free(stored_save_slot);
                    }

                    preferences_ui_sounds = global_ui_sounds;

                    CB_SettingsScene_rebuildEntries(settingsScene);
                    cursor_entry = &settingsScene->entries[settingsScene->cursorIndex];
                }

                if (strcmp(cursor_entry->name, "30 FPS mode") == 0 ||
                    strcmp(cursor_entry->name, "Interlacing") == 0)
                {
                    CB_SettingsScene_rebuildEntries(settingsScene);
                    cursor_entry = &settingsScene->entries[settingsScene->cursorIndex];
                }
            }

            if (cursor_entry->thumbnail)
                update_thumbnail(settingsScene);
        }
    }

    playdate->graphics->clear(kColorWhite);

    int fontHeight = playdate->graphics->getFontHeight(CB_App->bodyFont);
    int rowSpacing = 10;
    int rowHeight = fontHeight + rowSpacing;
    int totalMenuHeight = (MAX_VISIBLE_ITEMS * rowHeight) - rowSpacing;
    int initialY = (kScreenHeight - totalMenuHeight) / 2 + header_y / 2;

    // header y
    if (header_y > 0 && gameScene && gameScene->name_short)
    {
        LCDFont* font = CB_App->labelFont;
        playdate->graphics->setFont(font);
        int nameWidth = playdate->graphics->getTextWidth(
            font, gameScene->name_short, strlen(gameScene->name_short), kUTF8Encoding, 0
        );
        int textX = LCD_COLUMNS / 2 - nameWidth / 2;

        // Dynamically adjust vertical offset based on text content
        int fontHeight = playdate->graphics->getFontHeight(font);

        // Check if the title has descenders and apply a different offset.
        // This provides a better visual center for all titles.
        int vertical_offset = string_has_descenders(gameScene->name_short) ? 1 : 2;
        int textY = ((header_y - fontHeight) / 2) + vertical_offset;

        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, header_y, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeFillWhite);

        playdate->graphics->drawText(
            gameScene->name_short, strlen(gameScene->name_short), kUTF8Encoding, textX, textY
        );
    }

    playdate->graphics->setFont(CB_App->bodyFont);

    // --- Left Pane (Options - 60%) ---

    for (int i = 0; i < MAX_VISIBLE_ITEMS; i++)
    {
        int itemIndex = settingsScene->topVisibleIndex + i;

        if (itemIndex >= menuItemCount)
        {
            break;
        }

        OptionsMenuEntry* current_entry = &settingsScene->entries[itemIndex];
        bool is_static_text = (current_entry->pref_var == NULL && current_entry->on_press == NULL);
        bool is_locked_option = current_entry->locked;

        bool is_functionally_inactive =
            (current_entry->pref_var != NULL && current_entry->max_value == 0);
        bool is_disabled = is_static_text || is_locked_option || is_functionally_inactive;

        int y = initialY + i * rowHeight;
        const char* name = current_entry->name;
        const char* stateText = "";
        if (current_entry->values)
        {
            if (current_entry->pref_var && *current_entry->pref_var < current_entry->max_value)
            {
                stateText = current_entry->values[*current_entry->pref_var];
            }
            else if (current_entry->pref_var == NULL)
            {
                stateText = current_entry->values[0];
            }
        }

        if (current_entry->show_value_only_on_hover && itemIndex != settingsScene->cursorIndex)
            stateText = "";

        int nameWidth = playdate->graphics->getTextWidth(
            CB_App->bodyFont, name, strlen(name), kUTF8Encoding, 0
        );
        int stateWidth = playdate->graphics->getTextWidth(
            CB_App->bodyFont, stateText, strlen(stateText), kUTF8Encoding, 0
        );
        int stateX = kDividerX - stateWidth - kLeftPanePadding;

        if (itemIndex == settingsScene->cursorIndex)
        {
            playdate->graphics->fillRect(
                0, y - (rowSpacing / 2), kDividerX, rowHeight, kColorBlack
            );
            playdate->graphics->setDrawMode(kDrawModeFillWhite);
        }
        else
        {
            playdate->graphics->setDrawMode(kDrawModeFillBlack);
        }

        if (current_entry->header)
        {
            int nameWidth = playdate->graphics->getTextWidth(
                CB_App->bodyFont, name, strlen(name), kUTF8Encoding, 0
            );
            int textX = kDividerX / 2 - nameWidth / 2;

            playdate->graphics->drawText(name, strlen(name), kUTF8Encoding, textX, y);

            int fontHeight = playdate->graphics->getFontHeight(CB_App->bodyFont);

            int lineY = y + (fontHeight / 2);
            int padding = 5;

            // Draw the left line segment
            playdate->graphics->drawLine(
                kLeftPanePadding, lineY, textX - padding, lineY, 1,
                (itemIndex == settingsScene->cursorIndex) ? kColorWhite : kColorBlack
            );

            // Draw the right line segment
            playdate->graphics->drawLine(
                textX + nameWidth + padding, lineY, kDividerX - kLeftPanePadding, lineY, 1,
                (itemIndex == settingsScene->cursorIndex) ? kColorWhite : kColorBlack
            );
        }
        else
        {
            // Draw the option name (left-aligned)
            playdate->graphics->drawText(name, strlen(name), kUTF8Encoding, kLeftPanePadding, y);
        }

        if (stateText[0])
        {
            // Draw the state (right-aligned)
            playdate->graphics->drawText(stateText, strlen(stateText), kUTF8Encoding, stateX, y);
        }

        if (is_disabled && !current_entry->header)
        {
            const uint8_t* dither = (itemIndex != settingsScene->cursorIndex)
                                        ? black_transparent_dither
                                        : white_transparent_dither;
            playdate->graphics->fillRect(
                kLeftPanePadding, y, nameWidth, fontHeight, (LCDColor)dither
            );
            if (stateText[0])
            {
                playdate->graphics->fillRect(stateX, y, stateWidth, fontHeight, (LCDColor)dither);
            }
        }

        if (itemIndex == settingsScene->cursorIndex &&
            settingsScene->option_hold_time > HOLD_TIME_SUPPRESS_RELEASE)
        {
            float p = (settingsScene->option_hold_time - HOLD_TIME_SUPPRESS_RELEASE) /
                      (HOLD_TIME - HOLD_TIME_MARGIN - HOLD_TIME_SUPPRESS_RELEASE);
            if (p > 1.0f)
                p = 1.0f;

            playdate->graphics->fillRect(
                0, y - (rowSpacing / 2), kDividerX * p, rowHeight, kColorXOR
            );
        }
    }

    playdate->graphics->setDrawMode(kDrawModeFillBlack);

    if (menuItemCount > MAX_VISIBLE_ITEMS)
    {
        int scrollAreaY = initialY - (rowSpacing / 2);
        int scrollAreaHeight = totalMenuHeight + rowSpacing;

        float calculatedHeight =
            (float)scrollAreaHeight * ((float)MAX_VISIBLE_ITEMS / menuItemCount);

        float handleHeight = CB_MAX(calculatedHeight, SCROLL_INDICATOR_MIN_HEIGHT);

        float handleY =
            (float)scrollAreaY +
            ((float)scrollAreaHeight * ((float)settingsScene->topVisibleIndex / menuItemCount));

        int indicatorX = kDividerX - 4;
        int indicatorWidth = 2;

        // scroll bar
        playdate->graphics->fillRect(
            indicatorX - 1, (int)handleY, indicatorWidth + 2, (int)handleHeight, kColorWhite
        );
        playdate->graphics->fillRect(
            indicatorX, (int)handleY - 1, indicatorWidth, (int)handleHeight + 2, kColorWhite
        );

        playdate->graphics->fillRect(
            indicatorX, (int)handleY, indicatorWidth, (int)handleHeight, kColorBlack
        );
    }

    // --- Right Pane (Description - 40%) ---
    playdate->graphics->setFont(CB_App->labelFont);

    const char* description = cursor_entry->description;

    if (description)
    {
        char descriptionCopy[512];
        strncpy(descriptionCopy, description, sizeof(descriptionCopy));
        descriptionCopy[sizeof(descriptionCopy) - 1] = '\0';

        char* line = strtok(descriptionCopy, "\n");

        int descY = initialY;
        int descLineHeight = playdate->graphics->getFontHeight(CB_App->labelFont) + 2;

        while (line != NULL)
        {
            // Draw text in the right pane, with 10px padding from divider
            playdate->graphics->drawText(
                line, strlen(line), kUTF8Encoding, kDividerX + kRightPanePadding, descY
            );
            descY += descLineHeight;
            line = strtok(NULL, "\n");
        }

        // draw save state thumbnail
        if (cursor_entry->thumbnail)
        {
            int thumbx = kDividerX + (LCD_COLUMNS - kDividerX) / 2 - (SAVE_STATE_THUMBNAIL_W / 2);
            thumbx /= 8;  // for memcpy
            int thumby = LCD_ROWS - (LCD_COLUMNS - kDividerX) / 2 + (SAVE_STATE_THUMBNAIL_W / 2) -
                         SAVE_STATE_THUMBNAIL_H;

            uint8_t* frame = playdate->graphics->getFrame();

            const int rowsize = ((SAVE_STATE_THUMBNAIL_W + 7) / 8);
            for (size_t i = 0; i < SAVE_STATE_THUMBNAIL_H; ++i)
            {
                uint8_t* frame_row_start = frame + (thumby + i) * LCD_ROWSIZE + thumbx;
                memcpy(frame_row_start, &settingsScene->thumbnail[i * rowsize], rowsize);
            }

            playdate->graphics->markUpdatedRows(thumby, thumby + SAVE_STATE_THUMBNAIL_H);
        }

        // graphics test
        if (cursor_entry->graphics_test)
        {
            uint16_t d0 = CB_dither_lut_c0[preferences_dither_pattern];
            uint16_t d1 = CB_dither_lut_c1[preferences_dither_pattern];

            int cwidth = 4 * 8;

            int total_width = (cwidth * 4);
            int total_height = 64;
            int start = kDividerX + (LCD_COLUMNS - kDividerX) / 2 - (total_width / 2);
            start = (start + 6) / 8;

            uint8_t* frame = playdate->graphics->getFrame();

            for (int k = 0; k < total_height; ++k)
            {
                int y = LCD_ROWS - 24 - total_height + k;
                uint8_t* pix = &frame[y * LCD_ROWSIZE + start];
                for (int i = 0; i < 4; ++i)
                {
                    bool double_size = (k > total_height / 2);

                    uint16_t d = ((double_size ? (k / 2) : k) % 2) ? d0 : d1;
                    uint8_t col = (d >> (4 * (3 - i))) & 0x0F;

                    if (k == total_height / 2 || k == total_height / 2 + 1)
                        col = 0xFF;
                    else if (double_size)
                    {
                        uint8_t tmp = col;
                        col = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            col |= (tmp & (1 << i)) << i;
                        }
                        col = col | (col << 1);
                    }
                    else
                    {
                        col |= col << 4;
                    }

                    if (k <= 1 || k >= total_height - 2)
                        col = 0;  // border

                    for (int j = 0; j < cwidth / 8; ++j)
                    {
                        pix[j + (cwidth / 8) * i] = col;
                        if (j == cwidth / 8 - 1 && i == 3)
                        {
                            pix[j + (cwidth / 8) * i] &= ~3;  // border
                        }
                        if (j == 0 && i == 0)
                        {
                            pix[0] &= ~0xC0;  // border
                        }
                    }
                }
            }

            playdate->graphics->markUpdatedRows(100, 250);
        }
    }

    // Draw the 60/40 vertical divider line
    playdate->graphics->drawLine(kDividerX, header_y, kDividerX, kScreenHeight, 1, kColorBlack);
}

static void CB_SettingsScene_didSelectBack(void* userdata)
{
    CB_SettingsScene* settingsScene = userdata;
    settingsScene->shouldDismiss = true;
}

static void CB_SettingsScene_menu(void* object)
{
    CB_SettingsScene* settingsScene = object;
    playdate->system->removeAllMenuItems();

    if (settingsScene->gameScene)
    {
        playdate->system->addMenuItem("Resume", CB_SettingsScene_didSelectBack, settingsScene);
    }
    else
    {
        playdate->system->addMenuItem("Library", CB_SettingsScene_didSelectBack, settingsScene);
    }
}

static void CB_SettingsScene_free(void* object)
{
    DTCM_VERIFY();
    CB_SettingsScene* settingsScene = object;

    if (settingsScene->gameScene)
    {
        bool audio_settings_changed =
            (settingsScene->initial_sound_mode != preferences_sound_mode) ||
            (settingsScene->initial_sample_rate != preferences_sample_rate);

        CB_GameScene_apply_settings(settingsScene->gameScene, audio_settings_changed);
        settingsScene->gameScene->audioLocked = settingsScene->wasAudioLocked;
    }

    // ensures no immutable setting can be modified
    if (settingsScene->immutable_settings)
    {
        preferences_restore_subset(settingsScene->immutable_settings);
        cb_free(settingsScene->immutable_settings);
    }

    if (settingsScene->entries)
        cb_free(settingsScene->entries);

    if (itcm_base_desc)
    {
        cb_free(itcm_base_desc);
        itcm_base_desc = NULL;
    }

    if (itcm_restart_desc)
    {
        cb_free(itcm_restart_desc);
        itcm_restart_desc = NULL;
    }

    if (patches_desc)
    {
        cb_free(patches_desc);
        patches_desc = NULL;
    }

    CB_Scene_free(settingsScene->scene);
    cb_free(settingsScene);
    DTCM_VERIFY();
}
