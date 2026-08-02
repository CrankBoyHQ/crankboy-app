//
//  settings_scene.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
#include "settings_scene.h"

#include "../../libs/libcrankemu/libcrankemu.h"
#include "../../libs/pdll/pdll.h"
#include "../app.h"
#include "../dtcm.h"
#include "../emucore_prefs.h"
#include "../preferences.h"
#include "../revcheck.h"  // IWYU pragma: keep
#include "../scenes/modal.h"
#include "../script.h"
#include "../utility.h"
#include "credits_scene.h"
#include "emucore_game_scene.h"
#include "info_scene.h"
#include "manage_rom_scene.h"
#include "patch_download_scene.h"

#include <pd_api/pd_api_gfx.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#define MAX_VISIBLE_ITEMS 6
#define SCROLL_INDICATOR_MIN_HEIGHT 10

static void CB_SettingsScene_update(void* object, uint32_t u32enc_dt);
static void CB_SettingsScene_free(void* object);
static void CB_SettingsScene_menu(void* object);
static void CB_SettingsScene_didSelectBack(void* userdata);
static void CB_SettingsScene_rebuildEntries(CB_SettingsScene* settingsScene);
static void CB_SettingsScene_attemptDismiss(CB_SettingsScene* settingsScene, bool returnToLibrary);
static void settings_load_state(CB_GameScene* gameScene, CB_SettingsScene* settingsScene);

bool save_state(CB_GameScene* gameScene, unsigned slot);
bool load_state(CB_GameScene* gameScene, unsigned slot);
extern const uint16_t CB_dither_lut_c0[];
extern const uint16_t CB_dither_lut_c1[];

static void update_thumbnail(CB_SettingsScene* settingsScene);

static void cb_wrap_invalidate(void);

static const char* get_settings_game_name(CB_SettingsScene* settingsScene);

static char* itcm_base_desc = NULL;
static char* itcm_device_desc = NULL;
static char* itcm_base_with_device_desc = NULL;
static char* itcm_restart_desc = NULL;

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
    const char* const* values;
    const char* description;

    // used for emucore prefs
    ce_preference_t* emucore_pref;

    // used for standard prefs.x preferences
    preference_t* pref_var;
    unsigned max_value;

    /* values with a 1 here are skipped over, not normally accessible. */
    uint64_t disabled_entries;

    bool locked : 1;
    bool dimmed : 1;
    bool show_value_only_on_hover : 1;
    bool suppress_nondefault_indicator : 1;
    bool thumbnail : 1;
    bool graphics_test : 1;
    bool header : 1;
    bool rebuild_when_changed : 1;

    void (*on_press)(struct OptionsMenuEntry*, CB_SettingsScene* settingsScene);
    void (*on_hold)(struct OptionsMenuEntry*, CB_SettingsScene* settingsScene);
    void (*on_change)(struct OptionsMenuEntry*, CB_SettingsScene* settingsScene, int prev_val);
    void* ud;
} OptionsMenuEntry;

// Per-section building
#define MAX_SECTION_ENTRIES 20

typedef struct SectionDef
{
    const char* name;
    OptionsMenuEntry* (*builder)(struct SectionDef*, struct CB_SettingsScene*, int* count);
    bool emucore_merge;
    ce_preference_t* emucore_category_base;
} SectionDef;

static void applyBundleHiddenFilter(OptionsMenuEntry* entries, int* count);
static void applyScriptLockedFilter(OptionsMenuEntry* entries, int count);
static void switchToSection(struct CB_SettingsScene* s, int sectionIndex);

static OptionsMenuEntry* build_general(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_script(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_audio(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_display(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_input(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_cgb(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_behavior(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_library(SectionDef*, struct CB_SettingsScene*, int* count);
static OptionsMenuEntry* build_misc(SectionDef*, struct CB_SettingsScene*, int* count);

const static SectionDef section_defs_base[] = {
    {"General", build_general, true},    {"Script", build_script, false},
    {"Audio", build_audio, false},       {"Display", build_display, false},
    {"Input", build_input, false},       {"CGB", build_cgb, false},
    {"Behavior", build_behavior, false}, {"Library", build_library, true},
    {"Miscellaneous", build_misc, true},
};

// how long to remember last-selected preference in menu (seconds)s
#define TIME_FORGET_LAST_PREFERENCE 15
static void* last_selected_preference;
static char last_selected_emucore_id[64];
static unsigned last_selected_preference_time;

void clear_last_selected_preference(void)
{
    last_selected_preference = NULL;
    last_selected_emucore_id[0] = '\0';
}

static bool entry_matches_last_selected(const OptionsMenuEntry* e)
{
    if (last_selected_preference && e->pref_var == last_selected_preference)
        return true;
    if (last_selected_emucore_id[0] && e->emucore_pref && e->emucore_pref->id)
        return strcmp(e->emucore_pref->id, last_selected_emucore_id) == 0;
    return false;
}

void display_credits(struct OptionsMenuEntry* entry, CB_SettingsScene* settingsScene)
{
    CB_showCredits(settingsScene);
}

static void display_changelog(struct OptionsMenuEntry* entry, CB_SettingsScene* settingsScene)
{
    size_t clen;
    char* changelog =
        cb_read_entire_file_maybe_compressed("CHANGELOG.md", &clen, kFileRead | kFileReadData);
    if (!changelog)
        changelog = cb_strdup("No changelog found.");

    char* plain = cb_markdown_to_plaintext(changelog);
    cb_free(changelog);
    if (!plain)
        return;

    CB_InfoScene* infoScene = CB_InfoScene_new("What's New", plain);
    infoScene->min_dismiss_time = 1.0f;
    infoScene->textIsStatic = false;
    CB_presentModal(infoScene->scene);
}

static void display_changelog_menu(void* userdata)
{
    display_changelog(NULL, NULL);
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
    CB_PatchDownloadScene* s =
        CB_PatchDownloadScene_new(option->ud, settingsScene, settingsScene->header_animation_p);
    CB_presentModal(s->scene);
}

static void open_manage_rom(OptionsMenuEntry* option, CB_SettingsScene* settingsScene)
{
    cb_play_ui_sound(CB_UISound_Confirm);
    CB_ManageRomScene* s =
        CB_ManageRomScene_new((CB_Game*)option->ud, settingsScene->header_animation_p);
    if (s)
        CB_presentModal(s->scene);
}

static SectionDef* push_section_def(CB_SettingsScene* scene)
{
    size_t n = scene->sections_count + 1;
    SectionDef* p = cb_realloc(scene->sections, n * sizeof(SectionDef));
    scene->sections = p;
    scene->sections_count = n;
    memset(&p[n - 1], 0, sizeof(SectionDef));
    return &p[n - 1];
}

static void pop_section_def(CB_SettingsScene* scene)
{
    if (scene->sections_count > 0)
        scene->sections_count--;
}

static SectionDef* insert_section_def_at(CB_SettingsScene* scene, size_t at)
{
    if (at > scene->sections_count)
        at = scene->sections_count;
    push_section_def(scene);
    SectionDef* arr = scene->sections;
    size_t tail = scene->sections_count - 1;
    if (at < tail)
        memmove(&arr[at + 1], &arr[at], (tail - at) * sizeof(SectionDef));
    memset(&arr[at], 0, sizeof(SectionDef));
    return &arr[at];
}

// determine category root for pref, or NULL
static ce_preference_t* emu_pref_category(ce_preference_t** emu_prefs, int idx)
{
    for (int j = idx - 1; j >= 0; --j)
    {
        if (emu_prefs[j]->type == CE_PREFERENCE_CATEGORY)
        {
            return emu_prefs[j];
        }
    }
    return NULL;
}

static ce_preference_t* find_emu_category_by_name(ce_preference_t** emu_prefs, const char* name)
{
    if (!emu_prefs || !name)
        return NULL;
    for (int i = 0; emu_prefs[i]; ++i)
    {
        if (emu_prefs[i]->type != CE_PREFERENCE_CATEGORY)
            continue;
        ce_preference_t* cat = emu_prefs[i];
        const char* cat_name = cat->name ? cat->name(cat) : cat->id;
        if (cat_name && strcasecmp(cat_name, name) == 0)
            return cat;
    }
    return NULL;
}

static bool cb_settings_emucore_mode(CB_SettingsScene* scene)
{
    return scene && (scene->emucoreGameScene != NULL || scene->emu_prefs != NULL);
}

static OptionsMenuEntry make_emucore_entry(ce_preference_t* p)
{
    OptionsMenuEntry e = {0};
    e.name = p->name ? p->name(p) : "Preference";
    e.description = p->description ? p->description(p) : NULL;
    e.values = p->values;
    e.max_value = (unsigned)cb_nullterm_array_len((void* const*)p->values);
    e.emucore_pref = p;
    uint32_t flags = p->flags ? p->flags(p) : 0;
    e.locked = (flags & CE_PREF_LOCKED) ? 1 : 0;
    return e;
}

static const char* settings_emu_slug(CB_SettingsScene* s)
{
    if (s->emucoreGameScene && s->emucoreGameScene->slug)
        return s->emucoreGameScene->slug;
    if (s->libraryScene)
    {
        CB_Game* g = (s->libraryScene->listView->selectedItem < s->libraryScene->games->length)
                         ? s->libraryScene->games->items[s->libraryScene->listView->selectedItem]
                         : NULL;
        if (g && g->names)
            return g->names->system_slug;
    }
    return NULL;
}

// caller-owned
static char* settings_emu_game_path(CB_SettingsScene* s)
{
    if (s->emucoreGameScene && s->emucoreGameScene->rom_path)
        return cb_game_config_path(s->emucoreGameScene->rom_path);
    if (s->libraryScene && s->selected_game_settings_path)
        return cb_strdup(s->selected_game_settings_path);
    return NULL;
}

static void settings_persist_emucore_pref(CB_SettingsScene* s, ce_preference_t* p, unsigned value)
{
    const char* slug = settings_emu_slug(s);
    if (!slug || !p->id)
        return;
    char key[96];
    snprintf(key, sizeof(key), "%s:%s", slug, p->id);
    uint32_t flags = p->flags ? p->flags(p) : 0;
    if (flags & CE_PREF_ALWAYS_GLOBAL)
        cb_emucore_prefs_set_global(key, value);
    else if (flags & CE_PREF_ALWAYS_LOCAL)
        cb_emucore_prefs_set_local(key, value);
    else if (preferences_per_game)
        cb_emucore_prefs_set_local(key, value);
    else
        cb_emucore_prefs_set_global(key, value);
}

static void settings_save_emucore_prefs(CB_SettingsScene* s)
{
    if (!s->emu_prefs)
        return;
    cb_emucore_prefs_save_to_disk(CB_globalPrefsPath, true);

    char* game_path = settings_emu_game_path(s);
    if (game_path)
    {
        cb_emucore_prefs_save_to_disk(game_path, false);
        cb_free(game_path);
    }
}

static void append_emucore_prefs_for_section(
    SectionDef* def, struct CB_SettingsScene* scene, OptionsMenuEntry* entries, int* count
)
{
    if (!def->emucore_merge)
        return;
    ce_preference_t** prefs = scene->emu_prefs;
    if (!prefs)
        return;
    int n = (int)cb_nullterm_array_len((void* const*)prefs);
    for (int k = 0; k < n && *count < MAX_SECTION_ENTRIES; ++k)
    {
        ce_preference_t* p = prefs[k];
        if (p->type != CE_PREFERENCE_STANDARD)
            continue;
        ce_preference_t* cat = emu_pref_category(prefs, k);
        if (cat != def->emucore_category_base)
            continue;
        entries[(*count)++] = make_emucore_entry(p);
    }
}

static OptionsMenuEntry* build_emucore_category(
    SectionDef* def, struct CB_SettingsScene* scene, int* count
)
{
    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    if (!section)
    {
        *count = 0;
        return NULL;
    }
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = 1;

    ce_preference_t** prefs = scene->emu_prefs;
    if (prefs)
    {
        int n = (int)cb_nullterm_array_len((void* const*)prefs);
        for (int k = 0; k < n && i < MAX_SECTION_ENTRIES; ++k)
        {
            ce_preference_t* p = prefs[k];
            if (p->type != CE_PREFERENCE_STANDARD)
                continue;
            ce_preference_t* cat = emu_pref_category(prefs, k);
            if (cat != def->emucore_category_base)
                continue;
            section[i++] = make_emucore_entry(p);
        }
    }

    if (i == 1)
    {
        // No matching prefs -- drop the section entirely.
        cb_free(section);
        *count = 0;
        return NULL;
    }
    section[0] = (OptionsMenuEntry){.name = def->name, .header = 1};
    *count = i;
    return section;
}

CB_SettingsScene* CB_SettingsScene_new(
    CB_GameScene* gameScene, CB_EmucoreGameScene* emucoreScene, CB_LibraryScene* libraryScene
)
{
    setCrankSoundsEnabled(true);
    CB_SettingsScene* settingsScene = cb_malloc(sizeof(CB_SettingsScene));
    memset(settingsScene, 0, sizeof(*settingsScene));

    settingsScene->rec_entry_index = -1;
    settingsScene->gameScene = gameScene;
    settingsScene->emucoreGameScene = emucoreScene;
    settingsScene->libraryScene = libraryScene;
    settingsScene->selected_game_settings_path = NULL;
    settingsScene->cursorIndex = 0;
    settingsScene->topVisibleIndex = 0;
    settingsScene->crankAccumulator = 0.0f;
    settingsScene->shouldDismiss = false;
    settingsScene->shouldReturnToLibrary = false;
    settingsScene->needsRebuild = false;

    // Initialize continuous scrolling variables
    settingsScene->scroll_direction = 0;
    settingsScene->repeatLevel = 0;
    settingsScene->repeatIncrementTime = 0.0f;
    settingsScene->repeatTime = 0.0f;

    settingsScene->gradient =
        playdate->graphics->loadBitmap(CB_get_forwarded_path("images/gradient32"), NULL);

    void* always_global = preferences_store_subset(PREFBITS_ALWAYS_GLOBAL);

    if (libraryScene)
    {
        CB_Game* selectedGame =
            (libraryScene->listView->selectedItem < libraryScene->games->length)
                ? libraryScene->games->items[libraryScene->listView->selectedItem]
                : NULL;
        if (selectedGame)
        {
            settingsScene->selected_game_settings_path =
                cb_game_config_path(selectedGame->fullpath);

            void* stored_globals = preferences_store_subset(~PREFBITS_NEVER_GLOBAL);
            settingsScene->stored_neutrals =
                preferences_store_subset(~(PREFBITS_NEVER_GLOBAL | PREFBITS_ALWAYS_GLOBAL));

            if (settingsScene->selected_game_settings_path)
            {
                preferences_merge_from_disk(settingsScene->selected_game_settings_path);
            }

            int game_uses_per_game_scope = preferences_per_game;

            if (!game_uses_per_game_scope)
            {
                preferences_restore_subset(stored_globals);
            }

            preferences_per_game = game_uses_per_game_scope;

            if (stored_globals)
            {
                cb_free(stored_globals);
            }
            if (always_global)
            {
                preferences_restore_subset(always_global);
            }
        }
    }

    ce_preference_t** emu_prefs = NULL;

    if (gameScene)
    {
        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 1.0f);
    }
    else if (emucoreScene)
    {
        if (emucoreScene->core && emucoreScene->core->pdll)
        {
            ce_preference_t** (*get_prefs)(void) =
                pdll_symbol(emucoreScene->core->pdll, "ce_get_preferences");
            if (get_prefs)
                emu_prefs = get_prefs();
        }
    }
    else if (libraryScene)
    {
        CB_Game* selectedGame =
            (libraryScene->listView->selectedItem < libraryScene->games->length)
                ? libraryScene->games->items[libraryScene->listView->selectedItem]
                : NULL;
        const char* slug =
            (selectedGame && selectedGame->names) ? selectedGame->names->system_slug : NULL;
        if (slug && strcmp(slug, GB_SYSTEM_SLUG))
        {
            // non-gb slug -- use emucore

            emucore_t* core = CB_get_emucore_by_slug(slug);
            if (core && core->path)
            {
                // FIXME -- very ugly

                pdll_t* pdll = NULL;
                bool we_opened = false;
                if (CB_App->active_emucore >= 0 && &CB_App->cores[CB_App->active_emucore] == core &&
                    core->pdll)
                {
                    pdll = core->pdll;
                }
                else
                {
                    pdll = pdll_open(playdate, core->path, PDLL_FILE_PDX | PDLL_FILE_DATA, 2);
                    we_opened = (pdll != NULL);
                }
                if (pdll)
                {
                    if (we_opened)
                    {
                        settingsScene->peek_pdll = pdll;
                        cb_emucore_set_frontend(pdll);
                    }

                    ce_preference_t** (*get_prefs)(void) = pdll_symbol(pdll, "ce_get_preferences");

                    if (we_opened)
                    {
                        bool (*load_rom)(uint8_t*, size_t, const char*, const char*) =
                            pdll_symbol(pdll, "ce_load_rom");
                        size_t rom_size = 0;
                        uint8_t* rom = (uint8_t*)cb_read_entire_file_maybe_compressed(
                            selectedGame->fullpath, &rom_size, kFileRead | kFileReadData
                        );
                        if (rom && load_rom)
                        {
                            char* basename = cb_basename(selectedGame->fullpath, false);
                            if (load_rom(rom, rom_size, slug, basename ? basename : ""))
                            {
                                settingsScene->peek_rom = rom;
                                rom = NULL;  // ownership transferred to scene
                                if (get_prefs)
                                    emu_prefs = get_prefs();
                                if (emu_prefs)
                                {
                                    if (settingsScene->selected_game_settings_path)
                                        cb_emucore_prefs_read_from_disk(
                                            settingsScene->selected_game_settings_path, false
                                        );
                                    cb_apply_persisted_emucore_prefs(core, slug);
                                }
                            }
                            cb_free(basename);
                        }
                        if (rom)
                            cb_free(rom);
                    }
                    else
                    {
                        if (get_prefs)
                            emu_prefs = get_prefs();
                    }
                }
            }
        }
    }
    settingsScene->emu_prefs = emu_prefs;

    int emu_pref_count = (int)cb_nullterm_array_len((void* const*)emu_prefs);
    bool* emu_pref_cat_assigned = NULL;
    if (emu_prefs)
    {
        emu_pref_cat_assigned = allocza(bool, emu_pref_count + 1 /* paranoia */);
    }

    bool emucore_mode = cb_settings_emucore_mode(settingsScene);
    for (int j = 0; j < CB_ARRAY_SIZE(section_defs_base); j++)
    {
        SectionDef* def = push_section_def(settingsScene);
        memcpy(def, &section_defs_base[j], sizeof(SectionDef));

        if (emucore_mode)
        {
            def->emucore_category_base = find_emu_category_by_name(emu_prefs, def->name);
        }

        // check if non-empty
        int probe_count = 0;
        OptionsMenuEntry* probe =
            def->builder ? def->builder(def, settingsScene, &probe_count) : NULL;
        if (probe && probe_count > 0)
        {
            applyBundleHiddenFilter(probe, &probe_count);
        }
        if (probe)
            cb_free(probe);

        if (emu_prefs && (def->emucore_merge || def->emucore_category_base))
        {
            for (int i = 0; i < emu_pref_count; ++i)
            {
                if (emu_prefs[i]->type != CE_PREFERENCE_STANDARD)
                    continue;
                ce_preference_t* cat = emu_pref_category(emu_prefs, i);
                bool matches;
                if (def->emucore_category_base)
                    matches = (cat == def->emucore_category_base);
                else if (cat == NULL)
                    matches = (j == 0);  // uncategorised -> General
                else
                    matches = (strcasecmp(cat->id, def->name) == 0);
                if (matches)
                {
                    emu_pref_cat_assigned[i] = true;
                    ++probe_count;
                }
            }
        }

        if (probe_count == 0)
            pop_section_def(settingsScene);
    }

    // add in unabsorbed emu prefs into new sections
    if (emu_prefs)
    {
        size_t section_def_insert_idx = 1;
        for (int i = 0; i < emu_pref_count; ++i)
        {
            if (emu_prefs[i]->type != CE_PREFERENCE_CATEGORY)
                continue;

            ce_preference_t* cat = emu_prefs[i];
            const char* cat_name = cat->name ? cat->name(cat) : cat->id;
            bool any_orphan = false;
            for (int k = i + 1; k < emu_pref_count; ++k)
            {
                if (emu_prefs[k]->type == CE_PREFERENCE_CATEGORY)
                    break;
                if (emu_prefs[k]->type != CE_PREFERENCE_STANDARD)
                    continue;
                if (!emu_pref_cat_assigned[k])
                {
                    any_orphan = true;
                    break;
                }
            }
            if (!any_orphan)
                continue;

            SectionDef* def = insert_section_def_at(settingsScene, section_def_insert_idx++);
            if (!def)
                break;
            def->name = cat_name;
            def->emucore_category_base = cat;
            def->builder = build_emucore_category;
            def->emucore_merge = true;

            for (int k = i + 1; k < emu_pref_count; ++k)
            {
                if (emu_prefs[k]->type == CE_PREFERENCE_CATEGORY)
                    break;
                if (emu_prefs[k]->type == CE_PREFERENCE_STANDARD)
                    emu_pref_cat_assigned[k] = true;
            }
        }
    }
    cb_free(emu_pref_cat_assigned);

    if (settingsScene->sections_count > 0)
    {
        // open to "General" pane; fall back to "Library" if no "General" pane.
        int initial_section = 0;
        if (strcmp(settingsScene->sections[0].name, "General") != 0)
        {
            for (size_t j = 0; j < settingsScene->sections_count; j++)
            {
                if (strcmp(settingsScene->sections[j].name, "Library") == 0)
                {
                    initial_section = (int)j;
                    break;
                }
            }
        }
        switchToSection(settingsScene, initial_section);
    }
    else
        settingsScene->entries = NULL;

    if (gameScene)
    {
        settingsScene->wasAudioLocked = gameScene->audioLocked;
        gameScene->audioLocked = true;
    }

    CB_Scene* scene = CB_Scene_new();
    scene->id = "settings";
    scene->managedObject = settingsScene;
    scene->update = CB_SettingsScene_update;
    scene->free = CB_SettingsScene_free;
    scene->menu = CB_SettingsScene_menu;

    settingsScene->scene = scene;

    settingsScene->initial_sound_mode = preferences_sound_mode;
    settingsScene->initial_sample_rate = preferences_sample_rate;
    settingsScene->initial_headphone_audio = preferences_headphone_audio;
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

    settingsScene->header_animation_p = CB_App->bundled_rom ? 0 : preferences_per_game;

    if (always_global)
    {
        preferences_restore_subset(always_global);
        cb_free(always_global);
    }

    CB_Scene_refreshMenu(scene);
    int t_since =
        (int)playdate->system->getSecondsSinceEpoch(NULL) - (int)last_selected_preference_time;

    if ((last_selected_preference || last_selected_emucore_id[0]) &&
        t_since <= TIME_FORGET_LAST_PREFERENCE && settingsScene->entries)
    {
        bool found = false;
        for (int i = 0; i < settingsScene->totalMenuItemCount; i++)
        {
            if (entry_matches_last_selected(&settingsScene->entries[i]))
            {
                playdate->system->logToConsole(
                    "Last selected option: %p; t=%d", last_selected_preference, t_since
                );
                settingsScene->cursorIndex = i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (size_t s = 1; s < settingsScene->sections_count; s++)
            {
                SectionDef* def = &settingsScene->sections[s];
                if (!def->builder)
                    continue;
                int probe_count = 0;
                OptionsMenuEntry* probe = def->builder(def, settingsScene, &probe_count);
                if (!probe)
                    continue;
                int at = -1;
                for (int i = 0; i < probe_count; i++)
                {
                    if (entry_matches_last_selected(&probe[i]))
                    {
                        at = i;
                        break;
                    }
                }
                cb_free(probe);
                if (at >= 0)
                {
                    switchToSection(settingsScene, (int)s);
                    settingsScene->cursorIndex = at;
                    break;
                }
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
    else if (option == 2)
    {
        settingsScene->shouldReturnToLibrary = true;
    }
}

static void settings_load_state(CB_GameScene* gameScene, CB_SettingsScene* settingsScene)
{
    if (!load_state(gameScene, preferences_save_state_slot))
    {
        playdate->system->logToConsole("Error loading state %d", preferences_save_state_slot);
    }
    else
    {
        playdate->system->logToConsole("Loaded save state %d", preferences_save_state_slot);

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

static void CB_SettingsScene_attemptDismiss(CB_SettingsScene* settingsScene, bool returnToLibrary)
{
    int result = 0;

    const char* game_settings_path = NULL;
    if (settingsScene->gameScene)
        game_settings_path = settingsScene->gameScene->settings_filename;
    else if (settingsScene->libraryScene)
        game_settings_path = settingsScene->selected_game_settings_path;

    if (settingsScene->libraryScene)
    {
        if (preferences_per_game)
        {
            if (game_settings_path)
            {
                result = preferences_save_to_disk(game_settings_path, PREFBITS_ALWAYS_GLOBAL);
            }

            // Save always-globals
            result = preferences_save_to_disk(CB_globalPrefsPath, ~PREFBITS_ALWAYS_GLOBAL);
        }
        else
        {
            result = preferences_save_to_disk(CB_globalPrefsPath, PREFBITS_NEVER_GLOBAL);

            if (result && game_settings_path)
            {
                result = preferences_save_to_disk(game_settings_path, (~PREFBITS_NEVER_GLOBAL));
            }
        }
    }
    else if (game_settings_path)
    {
        // Leave script-locked prefs as-is on disk so disabling scripts
        // later doesn't bleed force_pref'd values into the config.
        if (CB_App->bundled_rom)
        {
            // Bundled ROM: save everything to the bundled game's file.
            result = preferences_save_to_disk(game_settings_path, prefs_locked_by_script);
        }
        else if (preferences_per_game)
        {
            // Save per-game settings.
            result = preferences_save_to_disk(
                game_settings_path, PREFBITS_ALWAYS_GLOBAL | prefs_locked_by_script
            );

            if (result)
            {
                // Also save always-global settings to global preferences file
                result = preferences_save_to_disk(CB_globalPrefsPath, ~(PREFBITS_ALWAYS_GLOBAL));
            }
        }
        else
        {
            // Global mode - save all settings to global preferences
            result = preferences_save_to_disk(
                CB_globalPrefsPath, PREFBITS_NEVER_GLOBAL | prefs_locked_by_script
            );

            if (result)
            {
                // also save per-game-only settings to game file
                result = preferences_save_to_disk(
                    game_settings_path, ~(PREFBITS_NEVER_GLOBAL) | PREFBITS_ALWAYS_GLOBAL
                );
            }
        }
    }
    else
    {
        // (not sure when this would apply...)
        result = preferences_save_to_disk(CB_globalPrefsPath, PREFBITS_NEVER_GLOBAL);
    }

    settings_save_emucore_prefs(settingsScene);

    if (!result)
    {
        playdate->system->logToConsole(
            "Error saving preferences (per_game=%d, game_path=%s, global_path=%s)",
            preferences_per_game, game_settings_path ? game_settings_path : "(null)",
            CB_globalPrefsPath
        );
        CB_presentModal(CB_Modal_new("Error saving preferences.", NULL, NULL, NULL)->scene);
    }
    else
    {
        CB_dismiss(settingsScene->scene);
        if (returnToLibrary && /* paranoia */ settingsScene->gameScene)
        {
            settingsScene->wasAudioLocked = true;
            settingsScene->gameScene->quitGameModalConfirmOverride = true;
            CB_GameScene_didSelectLibrary(settingsScene->gameScene);
        }
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
static const char* itcm_labels[] = {"Off", "Full", "Core", "Draw"};
static const char* cgb_dmg_labels[] = {"Standard", "DMG"};
static const char* cgb_bias_labels[] = {"Darker", "Dark", "Neutral", "Bright", "Brighter"};
static const char* audio_output_labels[] = {"Mono", "Stereo"};
static const char* boot_fade_labels[] = {"Off", "Short", "Long", "Short (W)", "Long (W)"};
static const char* gb_button_labels[] = {"None", "Start", "Select", "Start+Select", "A", "B"};
static const char* menu_button_labels[] = {"None", "Start", "Select", "Start+Select"};
static const char* gb_button_labels_hp[] = {
    "Default",   "Start",          "Select",  "Start+Select", "Start+A",
    "Select+A",  "Start+Select+A", "Start+B", "Select+B",     "Start+Select+B",
    "Start+A+B", "Select+A+B",     "All"
};
static const char* gb_button_labels_ab_release_a[] = {
    "Default",      "B",       "None",     "Start",          "Select",
    "Start+Select", "Start+B", "Select+B", "Start+Select+B",
};
static const char* gb_button_labels_ab_release_b[] = {
    "Default",      "A",       "None",     "Start",          "Select",
    "Start+Select", "Start+A", "Select+A", "Start+Select+A",
};
static const char* crank_mode_labels[] = {"Start/Select", "Turbo A/B", "Turbo B/A", "None"};
static const char* crank_down_action_labels[] = {"None", "Select+Start"};
static const char* sample_rate_labels[] = {"High", "Medium", "Low"};
static const char* fps_labels[] = {"Off", "On", "Playdate"};
static const char* frame_skip_labels[] = {"Off", "On", "Adaptive"};
static const char* slot_labels[] = {"Slot 0", "Slot 1", "Slot 2", "Slot 3", "Slot 4",
                                    "Slot 5", "Slot 6", "Slot 7", "Slot 8", "Slot 9"};
const char* const save_slot_labels[10] = {
    "Slot A", "Slot B", "Slot C", "Slot D", "Slot E",
    "Slot F", "Slot G", "Slot H", "Slot I", "Slot K",
};
static const char* dither_pattern_labels[] = {"Staggered", "Grid",          "Staggered (L)",
                                              "Grid (L)",  "Staggered (D)", "Grid (D)"};
static const char* overclock_labels[] = {"Off", "x2", "x4"};
static const char* dither_line_labels[] = {"1", "2", "3"};
static const char* settings_scope_labels[] = {"Global", "Game"};
static const char* cgb_prompt_labels[] = {"No", "Yes", "Always"};
static const char* display_name_mode_labels[] = {"Short", "Detailed", "Filename"};
static const char* sort_labels[] = {"Filename", "Database", "DB (w/article)", "File (w/article)"};
static const char* article_labels[] = {"Leading", "As-is"};
static const char* show_hide_labels[] = {"Hide", "Show"};
static const char* next_scene[] = {"›"};

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

static OptionsMenuEntry* find_load_state_entry(CB_SettingsScene* settingsScene)
{
    int count = settingsScene->totalMenuItemCount;
    for (int i = 0; i < count; ++i)
    {
        if (settingsScene->entries[i].name && !strcmp(settingsScene->entries[i].name, "Load state"))
            return &settingsScene->entries[i];
    }
    return NULL;
}

static void update_state_descriptions(CB_SettingsScene* settingsScene)
{
    cb_wrap_invalidate();
    CB_GameScene* gameScene = settingsScene->gameScene;
    if (!gameScene)
        return;

    int slot = preferences_save_state_slot;
    unsigned timestamp = get_save_state_timestamp(gameScene, slot);
    unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);
    bool has_save = (timestamp != 0 && timestamp <= now);
    unsigned age = now - timestamp;
    char* allocated_time = NULL;
    const char* human_time;
    if (has_save)
    {
        if (age < 10)
            human_time = "just now";
        else
            human_time = allocated_time = en_human_time(age);
    }
    else
    {
        human_time = NULL;
    }

    cb_free(settingsScene->save_state_desc);
    settingsScene->save_state_desc = NULL;
    cb_free(settingsScene->load_state_desc);
    settingsScene->load_state_desc = NULL;

    if (has_save)
    {
        const char* save_fmt;
        const char* load_fmt;
        if (age < 10)
        {
            save_fmt = "Overwrite snapshot from\njust now.";
            load_fmt = "Restore snapshot from\njust now.";
        }
        else
        {
            save_fmt = NULL;  // formatString will be used
            load_fmt = NULL;
        }

        if (save_fmt)
        {
            settingsScene->save_state_desc = cb_strdup(save_fmt);
            settingsScene->load_state_desc = cb_strdup(load_fmt);
        }
        else
        {
            playdate->system->formatString(
                &settingsScene->save_state_desc, "Overwrite snapshot from\n%s ago.", human_time
            );
            playdate->system->formatString(
                &settingsScene->load_state_desc, "Restore snapshot from\n%s ago.", human_time
            );
        }
    }

    cb_free(allocated_time);

    int count = settingsScene->totalMenuItemCount;
    for (int i = 0; i < count; ++i)
    {
        OptionsMenuEntry* entry = &settingsScene->entries[i];
        if (entry->name && !strcmp(entry->name, "Save state"))
        {
            entry->description =
                has_save ? settingsScene->save_state_desc
                         : "Create a snapshot of this moment, which can be resumed later.";
            break;
        }
    }

    OptionsMenuEntry* load_entry = find_load_state_entry(settingsScene);
    if (load_entry)
    {
        load_entry->dimmed = !has_save;
        load_entry->description =
            has_save ? settingsScene->load_state_desc : "Restore a previously created snapshot.";
    }
}

static void settings_post_action_save_state_slot_change(
    OptionsMenuEntry* e, CB_SettingsScene* settingsScene, int prev_val
)
{
    (void)e;
    (void)prev_val;
    update_state_descriptions(settingsScene);
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
        const char* options[] = {"Game", "Settings", NULL, NULL};
        if (!CB_App->bundled_rom)
        {
            options[2] = T(pdmenu_library);
        }
        CB_Modal* modal = CB_Modal_new(
            "State saved. Return to:", options, state_action_modal_callback, settingsScene
        );
        if (modal)
        {
            modal->width = 324;
            CB_presentModal(modal->scene);
        }

        // After saving, the slot now has a state - unlock Load state
        update_state_descriptions(settingsScene);
        settingsScene->desc_update_timer = 0.0f;
    }

    update_thumbnail(settingsScene);
}

static void settings_post_action_lock_button(
    OptionsMenuEntry* e, CB_SettingsScene* settingsScene, int prev_val
)
{
    static bool has_warned = false;
    if (prev_val != PREF_BUTTON_NONE)
        has_warned = true;
    if (has_warned)
        return;

    if (preferences_lock_button != PREF_BUTTON_NONE)
    {
        has_warned = true;

        CB_Modal* modal = CB_Modal_new(
            "Note: holding the lock button for 5 seconds will reboot your Playdate.\n\nAlso note: "
            "with lock button override enabled, you will not be able to lock the Playdate normally "
            "while in game.\nInstead, press ⊙ to open the menu, then lock as normal.",
            NULL, NULL, NULL
        );

        modal->height = 202;
        modal->width = 380;
        modal->margin = 12;
        modal->warning = CB_MODAL_WARNING_TOP;

        CB_presentModal(modal->scene);
    }
}

static void settings_post_action_per_game(
    OptionsMenuEntry* e, CB_SettingsScene* settingsScene, int prev_val
)
{
    // Special behavior for switching between per-game and global settings
    void* stored_always_global = preferences_store_subset(PREFBITS_ALWAYS_GLOBAL);
    void* stored_save_slot = preferences_store_subset(PREFBIT_save_state_slot);

    const char* game_settings_path;
    if (settingsScene->gameScene)
    {
        game_settings_path = settingsScene->gameScene->settings_filename;
    }
    else
    {
        game_settings_path = settingsScene->selected_game_settings_path;
    }

    if (!preferences_per_game && prev_val)
    {
        // write per - game prefs to disk
        preferences_per_game = 1;
        preferences_save_to_disk(game_settings_path, prefs_locked_by_script);

        preferences_merge_from_disk(CB_globalPrefsPath);
        preferences_per_game = 0;
    }
    else if (preferences_per_game && !prev_val)
    {
        // write global prefs to disk
        preferences_save_to_disk(
            CB_globalPrefsPath, PREFBITS_NEVER_GLOBAL | prefs_locked_by_script
        );

        preferences_set_defaults();
        preferences_merge_from_disk(game_settings_path);
        preferences_per_game = 1;
    }

    if (stored_save_slot)
    {
        preferences_restore_subset(stored_save_slot);
        cb_free(stored_save_slot);
    }

    if (stored_always_global)
    {
        preferences_restore_subset(stored_always_global);
        cb_free(stored_always_global);
    }
}

static void settings_post_action_script(
    OptionsMenuEntry* e, CB_SettingsScene* settingsScene, int prev_val
)
{
    const CB_GameScene* gameScene = settingsScene->gameScene;
    if (!prev_val && preferences_script_support && gameScene)
    {
        ScriptInfo* info = script_get_info_by_rom_path(gameScene->rom_filename);
        if (info->experimental)
        {
            CB_Modal* modal = CB_Modal_new(
                "This game's script is marked as \"experimental.\"\n\nExpect glitches.", NULL, NULL,
                NULL
            );

            modal->width = 300;
            modal->height = 150;

            CB_presentModal(modal->scene);
        }

        script_info_free(info);
    }
}

static void settings_action_save_state(void* _settingsScene, int option)
{
    CB_SettingsScene* settingsScene = _settingsScene;
    if (option != 0)
        return;

    CB_GameScene* gameScene = settingsScene->gameScene;
    gameScene->save_state_requires_warning = false;
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

static void settings_action_load_state(void* _settingsScene, int option)
{
    CB_SettingsScene* settingsScene = _settingsScene;
    if (option != 1)
        return;
    CB_GameScene* gameScene = settingsScene->gameScene;
    gameScene->save_state_requires_warning = false;
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

static void settings_action_save_state_possibly_warn(
    OptionsMenuEntry* e, CB_SettingsScene* settingsScene
)
{
    CB_GameScene* gameScene = e->ud;
    if (gameScene->save_state_requires_warning)
    {
        const char* options[] = {"Understood", NULL};
        CB_Modal* modal = CB_Modal_new(
            "WARNING! This game has an internal save data system, a snapshot of which will be "
            "included in this save state.\n\nIf you later load this state, this game's internal "
            "save data will irreversibly revert to its current state.",
            options, (void*)settings_action_save_state, settingsScene
        );
        modal->width = 390;
        modal->height = 234;
        modal->margin = 12;
        // modal->warning = CB_MODAL_WARNING_BOTTOM_LR; // perhaps a little overzealous.
        CB_presentModal(modal->scene);
    }
    else
    {
        settings_action_save_state(settingsScene, 0);
    }
}

static void settings_action_load_state_possibly_warn(
    OptionsMenuEntry* e, CB_SettingsScene* settingsScene
)
{
    CB_GameScene* gameScene = e->ud;

    unsigned timestamp = get_save_state_timestamp(gameScene, preferences_save_state_slot);
    if (timestamp == 0)
        return;

    if (gameScene->cartridge_has_battery)
    {
        const char* options[] = {"Cancel", "Load", NULL};
        unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);

        int h = 234;

        char* text;
        if (timestamp == 0 || timestamp >= now)
        {
            text = aprintf(
                "WARNING! This game has its own save data system, which will be PERMANENTLY reset "
                "to match that of this state.\n\nYou will not be able to undo this by resetting. "
                "If this game has multiple files, all will be affected. You have been warned."
            );
        }
        else
        {
            char* human_time = en_human_time(now - timestamp);
            if (now - timestamp >= 900)
            {
                // more elaborate error message if state >= 15 minutes old
                text = aprintf(
                    "WARNING! This game has its own save data system, which will be PERMANENTLY "
                    "reset back %s. You will not be able to recover it by resetting. If this game "
                    "has multiple files, all files will be affected.\n\nPerhaps make a back-up "
                    "before proceeding. You have been warned.",
                    human_time
                );
            }
            else
            {
                text = aprintf(
                    "WARNING! This game has its own save data system, which will be permanently "
                    "reset back %s. You will not be able to undo this by resetting the game.",
                    human_time
                );
                h = 180;
            }
            cb_free(human_time);
        }
        CB_Modal* modal =
            CB_Modal_new(text, options, (void*)settings_action_load_state, settingsScene);
        cb_free(text);
        modal->width = 390;
        modal->height = h;
        modal->margin = 12;
        modal->warning = CB_MODAL_WARNING_BOTTOM_LR;
        CB_presentModal(modal->scene);
    }
    else
    {
        settings_action_load_state(settingsScene, 1);
    }
}

static bool emucore_save_state_supported(CB_EmucoreGameScene* es)
{
    if (!es || !es->core || !es->core->pdll)
        return false;
    pdll_t* pdll = es->core->pdll;
    return pdll_symbol(pdll, "ce_get_state_size") && pdll_symbol(pdll, "ce_state_save") &&
           pdll_symbol(pdll, "ce_state_load");
}

static void settings_action_emucore_save_state(OptionsMenuEntry* e, CB_SettingsScene* settingsScene)
{
    (void)settingsScene;
    CB_EmucoreGameScene* es = e->ud;
    if (es)
        CB_emucore_save_state(es, (unsigned)preferences_save_state_slot);
}

static void settings_action_emucore_load_state(OptionsMenuEntry* e, CB_SettingsScene* settingsScene)
{
    (void)settingsScene;
    CB_EmucoreGameScene* es = e->ud;
    if (es)
        CB_emucore_load_state(es, (unsigned)preferences_save_state_slot);
}

struct ScriptSettingsInfo
{
    preference_t* preference;
    int maxvalue;

    char* name;
    char* description;
    char** options;
};

static int script_settings_info_count;
static struct ScriptSettingsInfo script_settings_info[] = {
    {.preference = &preferences_script_A},
    {.preference = &preferences_script_B},
    {.preference = &preferences_script_C}
    // can add more here as needed
};

static void clear_script_settings(void)
{
    script_settings_info_count = 0;
    for (int i = 0; i < CB_ARRAY_SIZE(script_settings_info); ++i)
    {
        cb_free(script_settings_info[i].description);
        script_settings_info[i].description = NULL;

        cb_free(script_settings_info[i].name);
        script_settings_info[i].name = NULL;

        for (char** opt = script_settings_info[i].options; opt && *opt; ++opt)
        {
            cb_free(*opt);
        }
        cb_free(script_settings_info[i].options);
        script_settings_info[i].options = NULL;
    }
}

bool script_custom_setting_add(const char* name, const char* description, const char** options)
{
    if (script_settings_info_count >= CB_ARRAY_SIZE(script_settings_info))
        return false;

    struct ScriptSettingsInfo* info = &script_settings_info[script_settings_info_count];
    info->name = cb_strdup(name);
    info->description = cb_strdup(description);
    info->maxvalue = 0;
    for (const char** opt = options; *opt; ++opt)
    {
        ++info->maxvalue;
    }

    info->options = allocza(char*, info->maxvalue + 1);
    for (int i = 0; i < info->maxvalue; ++i)
    {
        info->options[i] = cb_strdup(options[i]);
    }

    ++script_settings_info_count;
    return true;
}

static void addUISoundOption(CB_SettingsScene* scene, OptionsMenuEntry* entries, int* i)
{
    entries[++*i] = (OptionsMenuEntry){
        .name = "UI Sounds",
        .values = off_on_labels,
        .description = "Enable or disable interface sound effects.",
        .pref_var = &preferences_ui_sounds,
        .max_value = 2,
        .on_press = NULL,
    };
}

static void apply_recommended_in_settings(
    struct OptionsMenuEntry* entry, CB_SettingsScene* settingsScene
)
{
    cb_play_ui_sound(CB_UISound_Confirm);

    const char* game_name = get_settings_game_name(settingsScene);
    if (!game_name)
        return;

    const struct ScriptRecommendedSettings* rec = script_get_recommended_for_game(game_name);
    if (!rec)
        return;

    script_apply_recommended_current(rec);

    settingsScene->rec_dirty = true;
    CB_SettingsScene_rebuildEntries(settingsScene);
}

static const char* get_settings_game_name(CB_SettingsScene* settingsScene)
{
    if (settingsScene->libraryScene)
    {
        CB_Game* selectedGame =
            (settingsScene->libraryScene->listView->selectedItem <
             settingsScene->libraryScene->games->length)
                ? settingsScene->libraryScene->games
                      ->items[settingsScene->libraryScene->listView->selectedItem]
                : NULL;
        if (selectedGame)
            return selectedGame->names->name_header;
    }
    else if (settingsScene->gameScene)
    {
        char rom_name[17];
        ScriptInfo* sinfo = script_get_info_by_rom_path_and_get_header_info(
            settingsScene->gameScene->rom_filename, rom_name, NULL, NULL, NULL, NULL
        );
        if (rom_name[0])
        {
            static char name_buf[17];
            strncpy(name_buf, rom_name, 16);
            name_buf[16] = 0;
            script_info_free(sinfo);
            return name_buf;
        }
        script_info_free(sinfo);
    }
    return NULL;
}

static void recalc_recommended_entry_state(OptionsMenuEntry* entry, CB_SettingsScene* settingsScene)
{
    const char* game_name = get_settings_game_name(settingsScene);

    const struct ScriptRecommendedSettings* rec = script_get_recommended_for_game(game_name);

    if (!preferences_per_game)
    {
        if (script_check_recommended_current(rec))
            entry->description = "Recommended settings are already applied.";
        else
            entry->description = "Switch to 'Game' scope to apply recommended settings.";
        entry->locked = true;
    }
    else
    {
        bool already_optimal = script_check_recommended_current(rec);

        if (already_optimal)
        {
            entry->description = "Recommended settings are already applied.";
            entry->locked = true;
        }
        else
        {
            entry->description = "Apply the recommended settings for this game.";
            entry->locked = false;
        }
    }
}

static void applyBundleHiddenFilter(OptionsMenuEntry* entries, int* count)
{
    if (!preferences_bundle_hidden)
        return;
    int n = *count;
    for (int j = n - 1; j >= 0; j--)
    {
        OptionsMenuEntry* entry = &entries[j];
        bool remove = false;
        if (entry->header && (j + 1 >= n || entries[j + 1].header || !entries[j + 1].name))
            remove = true;
        if (!remove)
        {
#define PREF(p, ...)                                                                      \
    if ((preferences_bundle_hidden & PREFBIT_##p) && entry->pref_var == &preferences_##p) \
        remove = true;
#include "../prefs.x"
        }
        if (remove)
        {
            memmove(&entries[j], &entries[j + 1], (size_t)(n - j - 1) * sizeof(OptionsMenuEntry));
            n--;
        }
    }
    *count = n;
}

static void applyScriptLockedFilter(OptionsMenuEntry* entries, int count)
{
    for (int i = 0; i < count; i++)
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
#include "../prefs.x"
    }
}

/*
 * General
 *  Save state, Load state, Patches,
 *  Manage ROM, Save Data, Settings scope,
 *  Apply recommended
 */
static OptionsMenuEntry* build_general(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    CB_GameScene* gameScene = scene->gameScene;
    CB_LibraryScene* libraryScene = scene->libraryScene;
    CB_Game* selectedGame =
        (libraryScene && libraryScene->listView->selectedItem < libraryScene->games->length)
            ? libraryScene->games->items[libraryScene->listView->selectedItem]
            : NULL;

    scene->rec_entry_index = -1;

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    {
        const char* general_desc;
        if (gameScene)
            general_desc =
                "Save and load snapshots, manage settings scope.\n\n"
                "All other sections respect the current scope setting.";
        else if (selectedGame)
            general_desc =
                "Manage patches, save data, and settings scope.\n\n"
                "All other sections respect the current scope setting.";
        else
            general_desc = "Manage settings scope.";

        section[++i] =
            (OptionsMenuEntry){.name = T(sethdr_general), .header = 1, .description = general_desc};
    }

    if (gameScene)
    {
        section[++i] = (OptionsMenuEntry){
            .name = "Save state",
            .values = slot_labels,
            .pref_var = &preferences_save_state_slot,
            .max_value = SAVE_STATE_SLOT_COUNT,
            .show_value_only_on_hover = 1,
            .suppress_nondefault_indicator = 1,
            .thumbnail = 1,
            .on_press = settings_action_save_state_possibly_warn,
            .on_change = settings_post_action_save_state_slot_change,
            .ud = gameScene,
        };

        {
            section[++i] = (OptionsMenuEntry){
                .name = "Load state",
                .values = slot_labels,
                .pref_var = &preferences_save_state_slot,
                .max_value = SAVE_STATE_SLOT_COUNT,
                .show_value_only_on_hover = 1,
                .suppress_nondefault_indicator = 1,
                .thumbnail = 1,
                .on_press = settings_action_load_state_possibly_warn,
                .on_change = settings_post_action_save_state_slot_change,
                .ud = gameScene,
            };
        }
    }
    else if (scene->emucoreGameScene)
    {
        bool supported = emucore_save_state_supported(scene->emucoreGameScene);
        const char* save_desc =
            supported ? "Create a snapshot of this moment, which can be resumed later."
                      : "This emucore does not support save states.";
        const char* load_desc = supported ? "Restore the previously created snapshot."
                                          : "This emucore does not nsupport save states.";

        section[++i] = (OptionsMenuEntry){
            .name = "Save state",
            .values = slot_labels,
            .description = save_desc,
            .pref_var = &preferences_save_state_slot,
            .max_value = SAVE_STATE_SLOT_COUNT,
            .show_value_only_on_hover = 1,
            .suppress_nondefault_indicator = 1,
            .locked = !supported,
            .on_press = supported ? settings_action_emucore_save_state : NULL,
            .ud = scene->emucoreGameScene,
        };

        section[++i] = (OptionsMenuEntry){
            .name = "Load state",
            .values = slot_labels,
            .description = load_desc,
            .pref_var = &preferences_save_state_slot,
            .max_value = SAVE_STATE_SLOT_COUNT,
            .show_value_only_on_hover = 1,
            .suppress_nondefault_indicator = 1,
            .locked = !supported,
            .on_press = supported ? settings_action_emucore_load_state : NULL,
            .ud = scene->emucoreGameScene,
        };
    }

    if (libraryScene && selectedGame)
    {
        section[++i] = (OptionsMenuEntry){
            .name = "Manage ROM",
            .description = "View ROM info and delete the ROM, save data, or cover art.",
            .values = next_scene,
            .max_value = 0,
            .on_press = open_manage_rom,
            .ud = selectedGame
        };

        section[++i] = (OptionsMenuEntry){
            .name = "Patches/Hacks",
            .description =
                "Manage and download game patches, also known as ROM hacks.\n\n"
                "Remember to verify that a hack is compatible with your ROM before applying it.\n\n"
                "Parental lock is available.",
            .values = next_scene,
            .max_value = 0,
            .on_press = open_patches,
            .ud = selectedGame
        };

        section[++i] = (OptionsMenuEntry){
            .name = "Save Data",
            .values = save_slot_labels,
            .description =
                "Select which save file to use for the ROM's internal save data.\n\n"
                "If you apply softpatches, you may wish to use different saves for each.\n\n"
                "Note: \"save states\" are a different concept.",
            .pref_var = &preferences_save_slot,
            .max_value = SAVE_STATE_SLOT_COUNT,
            .suppress_nondefault_indicator = 1,
            .rebuild_when_changed = 1,
            .ud = gameScene,
        };

        if (!selectedGame->names->rom_has_battery)
        {
            section[i].locked = true;
            section[i].description =
                "Because this ROM does not use internal save data, this feature is disabled.";
        }
        else
        {
            static char* save_info = NULL;
            cb_free(save_info);
            char* save_file = cb_save_filename(selectedGame->fullpath, false);
            if (cb_file_exists(save_file, kFileReadData))
            {
                save_info = aprintf(
                    "Save data exists in %s.\n\n%s", save_slot_labels[preferences_save_slot],
                    section[i].description
                );
                section[i].description = save_info;
            }
            else
            {
                save_info = aprintf(
                    "%s is empty.\n\n%s", save_slot_labels[preferences_save_slot],
                    section[i].description
                );
                section[i].description = save_info;
            }
            cb_free(save_file);
        }
    }

    if ((gameScene || (libraryScene && selectedGame) || scene->emucoreGameScene) &&
        !CB_App->bundled_rom)
    {
        const char* scope_description;

        if (gameScene || scene->emucoreGameScene)
        {
            scope_description =
                "Use shared settings or create custom ones for this game.\n\n"
                "Global:\nSettings are shared across all games.\n\n"
                "Game:\nSettings are unique to this game.";
        }
        else
        {
            scope_description =
                "Which settings to edit.\n\n"
                "Global:\nEdit app-wide settings, including Library options.\n\n"
                "Game:\nEdit settings specific for the selected game.";
        }

        section[++i] = (OptionsMenuEntry){
            .name = "Settings Scope",
            .values = settings_scope_labels,
            .description = scope_description,
            .pref_var = &preferences_per_game,
            .max_value = 2,
            .suppress_nondefault_indicator = 1,
            .rebuild_when_changed = 1,
            .on_press = NULL,
            .on_change = settings_post_action_per_game,
        };

        {
            const char* game_name = get_settings_game_name(scene);
            bool has_rec = script_get_recommended_for_game(game_name) != NULL;

            if (has_rec)
            {
                section[++i] = (OptionsMenuEntry){
                    .name = "Apply recommended",
                    .on_press = apply_recommended_in_settings,
                    .locked = true,
                    .suppress_nondefault_indicator = 1,
                    .rebuild_when_changed = 1,
                };

                recalc_recommended_entry_state(&section[i], scene);
                scene->rec_entry_index = i;
            }
        }
    }

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    int n = i + 1;
    append_emucore_prefs_for_section(def, scene, section, &n);
    if (n <= 1)
    {
        cb_free(section);
        *count = 0;
        return NULL;
    }
    *count = n;
    return section;
}

/*
 * Script
 *  Script custom settings (if available)
 */
static OptionsMenuEntry* build_script(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    if (cb_settings_emucore_mode(scene))
        return build_emucore_category(def, scene, count);

    CB_GameScene* gameScene = scene->gameScene;
    scene->rec_entry_index = -1;

    if (!gameScene || !gameScene->script)
    {
        *count = 0;
        return NULL;
    }

    clear_script_settings();
    script_add_settings(gameScene->script);
    if (script_settings_info_count <= 0)
    {
        *count = 0;
        return NULL;
    }

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
        .name = T(sethdr_script),
        .header = 1,
        .description = "Game-specific settings provided by a custom script for this ROM."
    };

    for (int j = 0; j < script_settings_info_count; ++j)
    {
        struct ScriptSettingsInfo* info = &script_settings_info[j];
        if (*info->preference >= info->maxvalue)
            *info->preference = 0;
        section[++i] = (OptionsMenuEntry){
            .name = info->name,
            .values = (const char**)info->options,
            .description = info->description,
            .pref_var = info->preference,
            .max_value = info->maxvalue,
        };
    }

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    *count = i + 1;
    return section;
}

/*
 * Audio
 *  Sound, Timing, Sample rate,
 *  Headphones / Mirror/Aux
 */
static OptionsMenuEntry* build_audio(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    if (cb_settings_emucore_mode(scene))
        return build_emucore_category(def, scene, count);

    scene->rec_entry_index = -1;

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
        .name = T(sethdr_audio),
        .header = 1,
        .description = "Sound quality, sample rate, and headphone output."
    };

    section[++i] = (OptionsMenuEntry){
        .name = "Sound",
        .values = sound_mode_labels,
        .description =
            "Fast:\nGood performance. Sound timing is less precise.\n\n"
            "Accurate:\nMost faithful audio. Uses a buffer to ensure correct sound pitch and "
            "speed.\n"
            "Minor performance cost.",
        .pref_var = &preferences_sound_mode,
        .max_value = 3,
        .disabled_entries = (1 << 0), /* 'Off' removed */
        .rebuild_when_changed = 1,
    };

    if (preferences_sound_mode == 2)
        preferences_sample_rate = 0;

    // sample rate
    section[++i] = (OptionsMenuEntry){
        .name = "Sample Rate",
        .values = sample_rate_labels,
        .description = (preferences_sound_mode == 2)
                           ? "Accurate sound mode requires High sample rate."
                           : "Adjusts audio quality.\nHigher values may impact performance.\n\n"
                             "High:\nBest quality (44.1 kHz)\n\n"
                             "Medium:\nGood quality (22.1 kHz)\n\n"
                             "Low:\nReduced quality (14.7 kHz)",
        .pref_var = &preferences_sample_rate,
        .max_value = 3,
        .locked = (preferences_sound_mode == 2),
    };

    // high-pass filter
    section[++i] = (OptionsMenuEntry){
        .name = "High-Pass Filter",
        .values = off_on_labels,
        .description =
            "Emulates the hardware's capacitor coupling. Bass frequencies are weakend.\n\n"
            "On:\nHardware-accurate sound. Costs a little performance.\n\n"
            "Off:\nFuller bass, but less faithful to the original hardware.",
        .pref_var = &preferences_high_pass_filter,
        .max_value = 2,
    };

    // Mono/Stereo
    section[++i] = (OptionsMenuEntry){
        .name = CB_App->mirror_active ? "Mirror/Aux" : "Headphones",
        .values = audio_output_labels,
        .description =
            "Select the audio output mode when an aux cable is connected.\n\n"
            "Stereo:\nImmersive sound with spatial separation.\n\n"
            "Mono:\n Combines channels, which improves performance.\n\n"
            "Also used for Mirror.",
        .pref_var = &preferences_headphone_audio,
        .max_value = 2,
    };

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    *count = i + 1;
    return section;
}

/*
 * Display
 *  30 FPS mode, Frame blending, Dither,
 *  First scaling line, Stabilization
 */
static OptionsMenuEntry* build_display(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    if (cb_settings_emucore_mode(scene))
        return build_emucore_category(def, scene, count);

    scene->rec_entry_index = -1;

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
        .name = T(sethdr_display), .header = 1, .description = T(setdsc_display)
    };

    // frame skip
    section[++i] = (OptionsMenuEntry){
        .name = "30 FPS Mode",
        .values = frame_skip_labels,
        .description =
            "Skips displaying every second frame. Greatly improves performance for most games.\n\n"
            "Can generally be disabled if a game doesn't feature scrolling backgrounds.\n\n"
            "\"Adaptive\": switches between 30/60 depending on if the screen is scrolling etc.",
        .pref_var = &preferences_frame_skip,
        .max_value = 3,
        .rebuild_when_changed = 1,
        .on_press = NULL,
    };

    // frame blending
    if (preferences_frame_skip != 0)
    {
        section[++i] = (OptionsMenuEntry){
            .name = "Frame Blending",
            .values = off_on_labels,
            .description =
                "Blends two consecutive frames with dithering to reproduce the Game Boy's "
                "flicker-based transparency effects.",
            .pref_var = &preferences_blend_frames,
            .max_value = 2,
            .rebuild_when_changed = 1,
            .on_press = NULL,
        };
    }
    else
    {
        section[++i] = (OptionsMenuEntry){
            .name = "Frame Blending",
            .values = off_on_labels,
            .description =
                "Blends two consecutive frames with dithering to reproduce the Game Boy's "
                "flicker-based transparency effects.\n\n"
                "Only available in 30 FPS or Adaptive mode.",
            .pref_var = &preferences_blend_frames,
            .max_value = 0,
            .on_press = NULL,
        };
    }

    // dither
    section[++i] = (OptionsMenuEntry){
        .name = "Dither",
        .values = dither_pattern_labels,
        .description =
            "How to represent 4-color graphics on a 1-bit display.\n\n"
            "L: bias toward light\n\n"
            "D: bias toward dark",
        .pref_var = &preferences_dither_pattern,
        .max_value = 6,
        .graphics_test = 1,
        .on_press = NULL
    };

    // dither line
    section[++i] = (OptionsMenuEntry){
        .name = "First Scaling Line",
        .values = dither_line_labels,
        .description =
            "Due to the 3:5 ratio between the GB's and Playdate's vertical resolutions, 1 in every "
            "3 scanlines must be vertically squished.\n\n"
            "This means there are three choices for which lines are to be the ones to squish."
            "If text is uneven, try adjusting this.",
        .pref_var = &preferences_dither_line,
        .max_value = 3,
        .on_press = NULL
    };

    // stabilization
    section[++i] = (OptionsMenuEntry){
        .name = "Stabilization",
        .values = off_on_labels,
        .description =
            "Stabilizes scaling artifacts by making them scroll with the camera.\n\n"
            "This prevents textures from shimmering, but may reduce performance slightly in "
            "scroll-heavy games.\n\n"
            "Works best with Dither set to \"Staggered\".",
        .pref_var = &preferences_dither_stable,
        .max_value = 2,
        .on_press = NULL
    };

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    *count = i + 1;
    return section;
}

/*
 * Input
 *  Crank Mode, Crank Down, Undock, Dock,
 *  Ⓐ›Ⓑ, Ⓑ›Ⓐ, Ⓐ+Ⓑ, Lock Override
 */
static OptionsMenuEntry* build_input(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    if (cb_settings_emucore_mode(scene))
        return build_emucore_category(def, scene, count);

    scene->rec_entry_index = -1;

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
        .name = T(sethdr_input),
        .header = 1,
        .description = "Crank function, dock and undock mapping, and button remapping."
    };

    // crank mode
    section[++i] = (OptionsMenuEntry){
        .name = "Crank",
        .values = crank_mode_labels,
        .pref_var = &preferences_crank_mode,
        .max_value = 4,
        .rebuild_when_changed = 1,
        .on_press = NULL
    };

    switch (preferences_crank_mode)
    {
    case CRANK_MODE_START_SELECT:
        section[i].description =
            "Start/Select mode.\n\n"
            "Turn the crank back to press Select.\n\n"
            "Turn forward to press Start.\n\n"
            "See 'Down' for crank-down action.";
        break;
    case CRANK_MODE_TURBO_CW:
        section[i].description =
            "Turbo A/B mode.\n\n"
            "CW = turbo-press A.\n"
            "CCW = turbo-press B.";
        break;
    case CRANK_MODE_TURBO_CCW:
        section[i].description =
            "Turbo B/A mode.\n\n"
            "CW = turbo-press B.\n"
            "CCW = turbo-press A.";
        break;
    case CRANK_MODE_OFF:
        section[i].description =
            "Assign a function to the crank.\n\n"
            "Currently: no function.";
        break;
    }

    // crank down action
    if (preferences_crank_mode == CRANK_MODE_START_SELECT)
    {
        section[++i] = (OptionsMenuEntry){
            .name = "Down",
            .values = crank_down_action_labels,
            .description =
                "When the crank is pointed down (between Start and Select), choose whether to hold "
                "Start + Select or do nothing.",
            .pref_var = &preferences_crank_down_action,
            .max_value = 2,
            .on_press = NULL,
        };
    }
    else
    {
        section[++i] = (OptionsMenuEntry){
            .name = "Down",
            .values = crank_down_action_labels,
            .description = "Only available when Crank mode is set to Start/Select.",
            .pref_var = &preferences_crank_down_action,
            .max_value = 0,
            .on_press = NULL,
        };
    }

    // undock
    section[++i] = (OptionsMenuEntry){
        .name = "Undock",
        .values = gb_button_labels,
        .description = "Assign a button input for undocking the crank.\n\n",
        .pref_var = &preferences_crank_undock_button,
        .max_value = 4,
        .on_press = NULL
    };

    // dock
    section[++i] = (OptionsMenuEntry){
        .name = "Dock",
        .values = gb_button_labels,
        .description = "Assign a button input for docking the crank.\n\n",
        .pref_var = &preferences_crank_dock_button,
        .max_value = 4,
        .on_press = NULL
    };

    // A->B
    section[++i] = (OptionsMenuEntry){
        .name = "Ⓐ › ⧂",
        .values = gb_button_labels_hp,
        .description =
            "Assign a replacement button input for holding Ⓐ and then pressing Ⓑ.\n\n"
            "This replaces the normal button presses.",
        .pref_var = &preferences_hold_a_press_b,
        .max_value = 13,
        .on_press = NULL
    };

    // B->A
    section[++i] = (OptionsMenuEntry){
        .name = "Ⓑ › ⧂",
        .values = gb_button_labels_hp,
        .description =
            "Assign a replacement button input for holding Ⓑ and then pressing Ⓐ.\n\n"
            "This replaces the normal button presses.",
        .pref_var = &preferences_hold_b_press_a,
        .max_value = 13,
        .on_press = NULL
    };

    // B+A
    section[++i] = (OptionsMenuEntry){
        .name = "Ⓑ + Ⓐ",
        .values = gb_button_labels_hp,
        .description =
            "Assign a replacement button input when simultaneously pressing both Ⓐ and Ⓑ.\n\n"
            "This replaces the normal button presses.\n\nNote: both buttons must be pressed "
            "on exactly the same frame, which is quite precise!",
        .pref_var = &preferences_press_a_b,
        .max_value = 13,
        .on_press = NULL
    };

    // AB->A
    section[++i] = (OptionsMenuEntry){
        .name = "⧂ › Ⓐ",
        .values = gb_button_labels_ab_release_b,
        .description =
            "Assign a replacement button input for holding both Ⓐ and Ⓑ then releasing Ⓑ.\n\n"
            "If A is omitted here, then the A input is suppressed until next released.\n\n"
            "Default: maps to A normally, but to None if Ⓑ+Ⓐ triggered a combo input (see settings "
            "above)",
        .pref_var = &preferences_hold_ab_release_b,
        .max_value = 9,
        .on_press = NULL
    };

    // AB->B
    section[++i] = (OptionsMenuEntry){
        .name = "⧂ › Ⓑ",
        .values = gb_button_labels_ab_release_a,
        .description =
            "Assign a replacement button input for holding Ⓐ and Ⓑ then releasing Ⓐ (or both "
            "simultaneously).\n\n"
            "If B is omitted here, then the B input is suppressed until next released.\n\n"
            "Default: maps to B normally, but to None if Ⓑ+Ⓐ triggered a combo input (see settings "
            "above)",
        .pref_var = &preferences_hold_ab_release_a,
        .max_value = 9,
        .on_press = NULL
    };

    // menu quick-press
    section[++i] = (OptionsMenuEntry){
        .name = "⊙",
        .values = menu_button_labels,
        .description =
            "Quickly open and close the system menu to trigger a button press.\n\n"
            "Open and close within 1 second to activate.",
        .pref_var = &preferences_menu_button,
        .max_value = 4,
    };

    // lock button override.
    // Only available if launched with system privileges. (e.g. through FunnyLoader / FunnyOS)
    // Since this is still experimental, do not show this option at all unless system privileges are
    // detected.
    if (CB_App->hasSystemAccess)
    {
        section[++i] = (OptionsMenuEntry){
            .name = "Lock Override",
            .values = gb_button_labels,
            .description =
                "Playdate's lock button can be used for start or select.\n\n"
                "Requires system access\n(i.e. launch CrankBoy via FunnyLoader)\n\n"
                "Only applies input from the moment the lock button is pressed; holding the lock "
                "button has no effect.",
            .pref_var = &preferences_lock_button,
            .max_value = 3,
            .on_change = settings_post_action_lock_button
        };
    }

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    *count = i + 1;
    return section;
}

/*
 * Game Boy Color
 *  CPU Speed, HLE Routines
 */
static OptionsMenuEntry* build_cgb(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    if (cb_settings_emucore_mode(scene))
        return build_emucore_category(def, scene, count);

    CB_GameScene* gameScene = scene->gameScene;
    scene->rec_entry_index = -1;

    if (gameScene && !gameScene->context->cgb_mode)
    {
        *count = 0;
        return NULL;
    }

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
#ifdef CRANKBOY_OFFICIAL_CATALOG
        .name = "CGB (GB Color)",
#else
        .name = "Game Boy Color",
#endif
        .header = 1,
        .description = "CGB CPU speed control and high-level emulation routines."
    };

    section[++i] = (OptionsMenuEntry){
        .name = "CPU Speed",
        .values = cgb_dmg_labels,
        .description =
            "Normally, the CGB CPU can run twice as fast as the DMG (non-Color GB).\n\n"
            "Set to \"DMG\" to force it to run at DMG speed.\n\n"
            "Can greatly improve perf on titles which don't make efficient use of the CPU.\n\n"
            "This is the opposite of overclocking (see below).",
        .pref_var = &preferences_cgb_speed,
        .max_value = 2,
    };

    section[++i] = (OptionsMenuEntry){
        .name = "HLE Routines",
        .values = off_on_labels,
        .description =
            "Automatically identify certain common routines and replace them with high-level "
            "emulated versions.\n\n"
            "Can allow for large performance gains, but potentially inaccurate.",
        .pref_var = &preferences_hle,
        .max_value = 2,
        .on_press = NULL
    };

    section[++i] = (OptionsMenuEntry){
        .name = "Grayscale Bias",
        .values = cgb_bias_labels,
        .description = "Shift CGB grayscale conversion toward darker or brighter shades.",
        .pref_var = &preferences_cgb_blend_bias,
        .max_value = 5,
    };

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    *count = i + 1;
    return section;
}

/*
 * Behavior
 *  PPU Timing, Batching, Overclock,
 *  Game scripts
 */
static OptionsMenuEntry* build_behavior(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    if (cb_settings_emucore_mode(scene))
        return build_emucore_category(def, scene, count);

    CB_GameScene* gameScene = scene->gameScene;
    scene->rec_entry_index = -1;

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
        .name = T(sethdr_behavior),
        .header = 1,
        .description = "Rewind, overclock, and script support."
    };

    // Rewind
    section[++i] = (OptionsMenuEntry){
        .name = "Rewind",
        .values = off_on_labels,
        .description =
            "Rewind up to 10 seconds of gameplay.\n\n"
            "Hold Ⓑ or Up and undock the crank to enter rewind.\n\n"
            "Hold Ⓑ or Up while cranking to scrub.\n\n"
            "Release Ⓑ or Up then dock the crank to exit.\n\n"
            "DMG only.",
        .pref_var = &preferences_rewind_enabled,
        .max_value = 2,
    };

    // overclocking
    section[++i] = (OptionsMenuEntry){
        .name = "Overclock",
        .values = overclock_labels,
        .description =
            "Runs the emulated CPU faster between frames to reduce in-game lag.\n\n"
            "The Playdate must work harder, so results vary and timing may be less accurate.\n\n"
#ifdef CRANKBOY_OFFICIAL_CATALOG
            "In CGB double-speed mode, x4 is limited to x2.",
#else
            "On Game Boy Color at double speed, x4 is limited to x2.",
#endif
        .pref_var = &preferences_overclock,
        .max_value = 3,
        .on_press = NULL
    };

#define BASE_SCRIPT_STRING                                                                         \
    "Scripts attempt to add Playdate feature support into ROMs.\n\nFor instance, the crank might " \
    "be used to navigate menus.\n\nThis setting is always per-game."

    // C scripts
    section[++i] = (OptionsMenuEntry){
        .name = "Game Scripts",
        .values = off_on_labels,
        .description = BASE_SCRIPT_STRING,
        .pref_var = &preferences_script_support,
        .max_value = 2,
        .locked = 0,
        .on_change = settings_post_action_script,
    };

    if (gameScene)
    {
        if (gameScene->script_available)
        {
            section[i].description =
                BASE_SCRIPT_STRING "\n\nYou must restart the ROM for this setting to take effect.";
            if (gameScene->script_info_available)
            {
                section[i].description = BASE_SCRIPT_STRING
                    "\n\nHold the Ⓐ button now for more information."
                    "\n\nYou must restart the ROM for this setting to take effect.";
                section[i].on_hold = display_script_info;
            }
        }
        else
        {
            section[i].description =
                BASE_SCRIPT_STRING "\n\nThere is no script available for this ROM.";
            section[i].locked = 1;
        }
    }

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    *count = i + 1;
    return section;
}

/*
 * Library
 *  Title display, Article, Sort,
 *  Remember Last, Sample Rate,
 *  Launch Animation, CGB Prompt
 */
static OptionsMenuEntry* build_library(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    CB_GameScene* gameScene = scene->gameScene;
    scene->rec_entry_index = -1;

    // display name mode
    if (gameScene)
    {
        *count = 0;
        return NULL;
    }

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
        .name = T(sethdr_library),
        .header = 1,
        .description =
            "Game list appearance, sorting, launch behavior, and CGB prompt options.\n\n"
            "Visible in Library scope."
    };

    section[++i] = (OptionsMenuEntry){
        .name = "Title Display",
        .values = display_name_mode_labels,
        .description =
            "Choose how game titles"
            "are displayed in the list.\n\n"
            "Short:\nThe common game title\n(by database match).\n\n"
            "Detailed:\nThe full title, including region and version info.\n\n"
            "Filename:\nThe original ROM filename.\n\n",
        .pref_var = &preferences_display_name_mode,
        .max_value = 3,
        .on_press = NULL
    };

    // display article
    section[++i] = (OptionsMenuEntry){
        .name = "Article",
        .values = article_labels,
        .description =
            "If a game title ends with an article, such as\n\n"
            "      \"Mummy, The (USA)\"\n\n"
            "it can be displayed at the start instead, i.e.\n\n"
            "      \"The Mummy (USA)\"",
        .pref_var = &preferences_display_article,
        .max_value = 2,
        .on_press = NULL
    };

    // sorting
    section[++i] = (OptionsMenuEntry){
        .name = "Sort",
        .values = sort_labels,
        .description =
            "Sort the games list either by database name or by filename.\n\n"
            "Can also choose to include articles that have been moved to the front of the name "
            "toward sorting.",
        .pref_var = &preferences_display_sort,
        .max_value = 4,
        .on_press = NULL
    };

    // remember selection
    section[++i] = (OptionsMenuEntry){
        .name = "Remember Last",
        .values = off_on_labels,
        .description = "When opening the library, initial selection will be the last game played.",
        .pref_var = &preferences_library_remember_selection,
        .max_value = 2,
        .on_press = NULL
    };

    addUISoundOption(scene, section, &i);

    section[++i] = (OptionsMenuEntry){
        .name = "Launch Animation",
        .values = off_on_labels,
        .description = "Animation when launching a ROM from the library.",
        .pref_var = &preferences_library_launch_animation,
        .max_value = 2,
    };

    // remember selection
    section[++i] = (OptionsMenuEntry){
        .name = "CGB Prompt",
        .values = cgb_prompt_labels,
        .description =
            "When launching a ROM,"
            "show a prompt to choose:\n\n"
            "1. DMG (original) emulation\n"
            "2. CGB (\"Color\") emulation\n"
            "      (experimental)\n\n"
            "No: CGB-only ROMs\n\n"
            "Yes: CGB-compatible ROMs\n\n"
            "Always: CGB and DMG ROMs",
        .pref_var = &preferences_prompt_if_cgb_optional,
        .max_value = 3,
        .on_press = NULL
    };

#ifdef CRANKBOY_OFFICIAL_CATALOG
    section[++i] = (OptionsMenuEntry){
        .name = "Bundled Games",
        .values = show_hide_labels,
        .description =
            "Show or hide games that are bundled with CrankBoy in the library.\n\n"
            "Requires restart to apply.",
        .pref_var = &preferences_show_bundled_games,
        .max_value = 2,
    };
#endif

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    int n = i + 1;
    append_emucore_prefs_for_section(def, scene, section, &n);
    *count = n;
    return section;
}

/*
 * Miscellaneous
 *  Show FPS, Turbo Speed, UI sounds,
 *  Disable auto lock, Boot Fade,
 *  TCM acceleration, LCD acceleration.,
 *  About CrankBoy...
 */
static OptionsMenuEntry* build_misc(SectionDef* def, CB_SettingsScene* scene, int* count)
{
    bool emucore_mode = cb_settings_emucore_mode(scene);
    scene->rec_entry_index = -1;

    OptionsMenuEntry* section = cb_malloc(sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    memset(section, 0, sizeof(OptionsMenuEntry) * MAX_SECTION_ENTRIES);
    int i = -1;

    section[++i] = (OptionsMenuEntry){
        .name = T(sethdr_misc),
        .header = 1,
        .description = "FPS display, turbo speed, boot fade, TCM Mode, and more."
    };

    section[++i] = (OptionsMenuEntry){
        .name = "Show FPS",
        .values = fps_labels,
        .description =
            "Displays the current frames-per-second on screen.\n\n"
            "Choice of displaying Playdate screen refreshes or emulated frames. (These can "
            "differ if 30 FPS mode is enabled.)\n\n"
            "Ideal performance is just under 60 emulated frames per second.",
        .pref_var = &preferences_display_fps,
        .max_value = 3,
        .on_press = NULL
    };

    section[++i] = (OptionsMenuEntry){
        .name = "Turbo Speed",
        .values = off_on_labels,
        .description =
            "Removes the FPS limit.\n\n"
            "This is intended just for benchmarking performance, not for casual play.",
        .pref_var = &preferences_uncap_fps,
        .max_value = 2,
        .on_press = NULL
    };

    if (CB_App->bundled_rom)
    {
        // ui sounds (for non-bundled, part of library settings only)
        addUISoundOption(scene, section, &i);
    }

    // Disable Auto Lock
    section[++i] = (OptionsMenuEntry){
        .name = "Disable Auto Lock",
        .values = off_on_labels,
        .description =
            "Prevents the device from auto-locking after 3 minutes of inactivity.\n\n"
            "Note: This only applies while a game is running.",
        .pref_var = &preferences_disable_autolock,
        .max_value = 2,
        .on_press = NULL,
        .rebuild_when_changed = 0,
        .on_change = NULL,
    };

    // Starting fade
    section[++i] = (OptionsMenuEntry){
        .name = "Boot Fade",
        .values = boot_fade_labels,
        .description =
            "Fade from black/white on game start. Masks visual glitches.\n\n"
            "The options marked (W) fade from white.",
        .pref_var = &preferences_boot_fade,
        .max_value = 5,
    };

    if (CB_App->bundled_rom)
    {
        section[i].description =
            "Fade from black on game start.\n\n"
            "In bundled mode, fade from white is not currently possible.";
        section[i].max_value = 3;
    }

#if defined(ITCM_CORE) && defined(DTCM_ALLOC)
    // itcm accel
    if (itcm_base_desc == NULL)
    {
        playdate->system->formatString(
            &itcm_base_desc,
            "Runs emulator in TCM for better performance.\n\n"
            "Full: core + draw.\nCore: interpreter only.\nDraw: rendering only.\n\n"
            "Disable (or restrict) if you experience instability."
        );
    }

    if (itcm_device_desc == NULL)
    {
        playdate->system->formatString(&itcm_device_desc, "(Your device: %s)", pd_rev_description);
    }

    if (itcm_base_with_device_desc == NULL)
    {
        playdate->system->formatString(
            &itcm_base_with_device_desc, "%s\n\n%s", itcm_base_desc, itcm_device_desc
        );
    }

    if (itcm_restart_desc == NULL)
    {
        playdate->system->formatString(
            &itcm_restart_desc, "%s\n\nRestart the game for changes to apply.\n\n%s",
            itcm_base_desc, itcm_device_desc
        );
    }

    section[++i] = (OptionsMenuEntry){
        .name = "TCM Mode",
        .values = itcm_labels,
        .pref_var = &preferences_itcm,
        .max_value = 4,
        .on_press = NULL
    };

    if (scene->emucoreGameScene)
    {
        // external core: itcm setting only takes effect on next load
        section[i].description = itcm_restart_desc;
    }
    else
    {
        // built-in GB scene: applies immediately on leaving settings
        section[i].description = itcm_base_with_device_desc;
    }
#endif

    if (!emucore_mode)
    {
        section[++i] = (OptionsMenuEntry){
            .name = "LCD Acceleration",
            .values = off_on_labels,
            .description =
                "Provides minor perf boost by using the Playdate's own last-frame buffer "
                "to store the GB's internal frame buffer as well.\n\n"
                "Sometimes causes garbage data to become visible on screen.",
            .pref_var = &preferences_tcm_lcd,
            .max_value = 2,
            .on_press = NULL
        };
    }

    if (CB_App->bundled_rom)
    {
        section[++i] = (OptionsMenuEntry){
            .name = "About CrankBoy...",
            .values = NULL,
            .description =
                "This game is bundled for Playdate via CrankBoy, a Game Boy emulator.\n\n"
                "Press Ⓐ now to learn more about CrankBoy and its developers.",
            .pref_var = NULL,
            .max_value = 0,
            .on_press = display_credits
        };
    }

    CB_ASSERT(i < MAX_SECTION_ENTRIES - 1);
    int n = i + 1;
    append_emucore_prefs_for_section(def, scene, section, &n);
    *count = n;
    return section;
}

static void switchToSection(CB_SettingsScene* s, int sectionIndex)
{
    if (s->entries)
        cb_free(s->entries);
    s->entries = NULL;

    int count = 0;
    if (sectionIndex >= 0 && (size_t)sectionIndex < s->sections_count)
    {
        SectionDef* def = &s->sections[sectionIndex];
        if (def->builder)
            s->entries = def->builder(def, s, &count);
    }
    if (s->entries)
    {
        applyBundleHiddenFilter(s->entries, &count);
        applyScriptLockedFilter(s->entries, count);
    }
    s->totalMenuItemCount = count;
    s->currentSectionIndex = sectionIndex;

    s->cursorIndex = 0;
    s->topVisibleIndex = 0;

    update_state_descriptions(s);
}

static void cb_wrap_invalidate(void);

static void CB_SettingsScene_rebuildEntries(CB_SettingsScene* settingsScene)
{
    cb_wrap_invalidate();
    cb_free(settingsScene->save_state_desc);
    settingsScene->save_state_desc = NULL;
    cb_free(settingsScene->load_state_desc);
    settingsScene->load_state_desc = NULL;
    if (settingsScene->entries)
    {
        cb_free(settingsScene->entries);
        settingsScene->entries = NULL;
    }

    int count = 0;
    int idx = settingsScene->currentSectionIndex;
    if (idx >= 0 && (size_t)idx < settingsScene->sections_count)
    {
        SectionDef* def = &settingsScene->sections[idx];
        if (def->builder)
            settingsScene->entries = def->builder(def, settingsScene, &count);
    }
    if (settingsScene->entries)
    {
        applyBundleHiddenFilter(settingsScene->entries, &count);
        applyScriptLockedFilter(settingsScene->entries, count);
    }
    settingsScene->totalMenuItemCount = count;

    if (settingsScene->cursorIndex >= settingsScene->totalMenuItemCount)
    {
        settingsScene->cursorIndex = settingsScene->totalMenuItemCount - 1;
    }
    if (settingsScene->cursorIndex < 0)
    {
        settingsScene->cursorIndex = 0;
    }

    update_state_descriptions(settingsScene);
}

// word-wrap cache
typedef struct
{
    const char* start;
    int length;
} cb_line_span;

#define LINE_BUF_SIZE 2048

static struct
{
    const char* key_ptr;
    int key_width;
    LCDFont* key_font;
    cb_line_span* lines;
    int n_lines;
    int cap_lines;
} s_wrap_cache;

static void cb_wrap_invalidate(void)
{
    s_wrap_cache.key_ptr = NULL;
    s_wrap_cache.key_width = 0;
    s_wrap_cache.key_font = NULL;
    s_wrap_cache.n_lines = 0;
}

static void cb_wrap_emit_line(const char* start, int length)
{
    if (s_wrap_cache.n_lines >= s_wrap_cache.cap_lines)
    {
        int newcap = s_wrap_cache.cap_lines ? s_wrap_cache.cap_lines * 2 : 16;
        s_wrap_cache.lines = cb_realloc(s_wrap_cache.lines, newcap * sizeof(cb_line_span));
        s_wrap_cache.cap_lines = newcap;
    }
    s_wrap_cache.lines[s_wrap_cache.n_lines].start = start;
    s_wrap_cache.lines[s_wrap_cache.n_lines].length = length;
    ++s_wrap_cache.n_lines;
}

// U+200B zero-width space (for cjk rendering especially)
static bool cb_is_zwsp(const char* p, const char* end)
{
    return end - p >= 3 && (unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 &&
           (unsigned char)p[2] == 0x8B;
}

static int cb_strip_zwsp(char* dst, const char* src, int n)
{
    const char* end = src + n;
    int out = 0;
    while (src < end)
    {
        if (cb_is_zwsp(src, end))
        {
            src += 3;
            continue;
        }
        dst[out++] = *src++;
    }
    return out;
}

static int cb_wrap_measure(LCDFont* font, const char* s, int n)
{
    if (n <= 0)
        return 0;
    static char buf[LINE_BUF_SIZE];
    int safe_len = (n < (int)(sizeof(buf) - 1)) ? n : (int)(sizeof(buf) - 1);
    safe_len = cb_strip_zwsp(buf, s, safe_len);
    buf[safe_len] = '\0';
    return playdate->graphics->getTextWidth(font, buf, safe_len, kUTF8Encoding, 0);
}

static void cb_wrap_paragraph(const char* p, int len, int max_width, LCDFont* font)
{
    if (len <= 0)
    {
        cb_wrap_emit_line(p, 0);
        return;
    }
    const char* end = p + len;
    const char* line_start = p;
    const char* last_fit_end = p;
    bool have_fit_word = false;

    while (p < end)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        if (cb_is_zwsp(p, end))
        {
            p += 3;
            continue;
        }
        const char* word_start = p;
        while (p < end && *p != ' ' && !cb_is_zwsp(p, end))
            ++p;
        int trial = cb_wrap_measure(font, line_start, (int)(p - line_start));
        if (trial <= max_width || !have_fit_word)
        {
            last_fit_end = p;
            have_fit_word = true;
        }
        else
        {
            cb_wrap_emit_line(line_start, (int)(last_fit_end - line_start));
            line_start = word_start;
            last_fit_end = p;
            have_fit_word = true;
        }
    }
    if (have_fit_word)
        cb_wrap_emit_line(line_start, (int)(last_fit_end - line_start));
}

static void cb_wrap_rebuild(const char* desc, int max_width, LCDFont* font)
{
    s_wrap_cache.n_lines = 0;  // reuse capacity
    s_wrap_cache.key_ptr = desc;
    s_wrap_cache.key_width = max_width;
    s_wrap_cache.key_font = font;
    if (!desc)
        return;
    const char* p = desc;
    while (1)
    {
        const char* nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        cb_wrap_paragraph(p, len, max_width, font);
        if (!nl)
            break;
        p = nl + 1;
    }
}

static const cb_line_span* cb_settings_wrap(
    const char* desc, int max_width, LCDFont* font, int* out_n
)
{
    if (desc != s_wrap_cache.key_ptr || max_width != s_wrap_cache.key_width ||
        font != s_wrap_cache.key_font)
    {
        cb_wrap_rebuild(desc, max_width, font);
    }
    if (out_n)
        *out_n = s_wrap_cache.n_lines;
    return s_wrap_cache.lines;
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

    if (settingsScene->needsRebuild)
    {
        settingsScene->needsRebuild = false;
        CB_SettingsScene_rebuildEntries(settingsScene);
    }

    if (settingsScene->shouldDismiss || settingsScene->shouldReturnToLibrary)
    {
        CB_SettingsScene_attemptDismiss(settingsScene, settingsScene->shouldReturnToLibrary);
        return;
    }

    CB_GameScene* gameScene = settingsScene->gameScene;

    if (gameScene)
    {
        settingsScene->desc_update_timer += dt;
        if (settingsScene->desc_update_timer >= 15.0f)
        {
            settingsScene->desc_update_timer = 0.0f;
            if (get_save_state_timestamp(gameScene, preferences_save_state_slot))
                update_state_descriptions(settingsScene);
        }
    }

    float header_target = CB_App->bundled_rom ? 0.0f : (float)preferences_per_game;
    TOWARD(settingsScene->header_animation_p, header_target, dt * HEADER_ANIMATION_RATE);

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
    if (steps != 0 && menuItemCount > 0)
    {
        settingsScene->option_hold_time = 0;
        int direction = (steps > 0) ? 1 : -1;
        int num_steps = abs(steps);

        for (int i = 0; i < num_steps; ++i)
        {
            settingsScene->cursorIndex += direction;

            // Wrap the cursor index within the current section
            if (settingsScene->cursorIndex >= menuItemCount)
            {
                settingsScene->cursorIndex = 0;
            }
            else if (settingsScene->cursorIndex < 0)
            {
                settingsScene->cursorIndex = menuItemCount - 1;
            }
        }
    }

    // Note that storing last selected by the entry's corresponding pref var
    // has the unexpected side effect that "load state" will always be stored
    // as "save state."
    //
    // This is actually a good thing! We don't want to mess with players' muscle
    // memories and cause them to accidentally load state when they mean to save state
    if (menuItemCount > 0)
    {
        OptionsMenuEntry* sel = &settingsScene->entries[settingsScene->cursorIndex];
        last_selected_preference = sel->pref_var;
        if (sel->emucore_pref && sel->emucore_pref->id)
            snprintf(
                last_selected_emucore_id, sizeof(last_selected_emucore_id), "%s",
                sel->emucore_pref->id
            );
        else
            last_selected_emucore_id[0] = 0;
    }
    last_selected_preference_time = playdate->system->getSecondsSinceEpoch(NULL);

    if (oldCursorIndex != settingsScene->cursorIndex)
    {
        cb_play_ui_sound(CB_UISound_Navigate);
    }

    if (pushed & kButtonB)
    {
        CB_SettingsScene_attemptDismiss(settingsScene, false);
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
                menuItemCount - MAX_VISIBLE_ITEMS);
        settingsScene->topVisibleIndex = MAX(0, settingsScene->topVisibleIndex);
    }

    // Emptied sections (e.g., all entries bundle-hidden) have no valid cursor
    // entry; fall back to a zeroed dummy so entry-driven input handling no-ops.
    static OptionsMenuEntry dummy_entry;
    OptionsMenuEntry* cursor_entry =
        (menuItemCount > 0) ? &settingsScene->entries[settingsScene->cursorIndex] : &dummy_entry;

    bool a_pressed = (pushed & kButtonA);
    if (cursor_entry->on_hold && !cursor_entry->locked)
    {
        a_pressed = released & kButtonA;
        if (settingsScene->option_hold_time >= HOLD_TIME_SUPPRESS_RELEASE)
            a_pressed = 0;
    }
    int direction = !!(pushed & kButtonRight) - !!(pushed & kButtonLeft);

    // Left/Right on a section header switches section pages
    if (cursor_entry->header && direction != 0 && settingsScene->sections_count > 1)
    {
        cb_play_ui_sound(CB_UISound_Navigate);
        int n = (int)settingsScene->sections_count;
        switchToSection(settingsScene, (settingsScene->currentSectionIndex + direction + n) % n);
        menuItemCount = settingsScene->totalMenuItemCount;
        cursor_entry = (menuItemCount > 0) ? &settingsScene->entries[settingsScene->cursorIndex]
                                           : &dummy_entry;
        direction = 0;
        settingsScene->option_hold_time = 0;
    }

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
    if (cursor_entry->on_press && a_pressed && !cursor_entry->locked)
    {
        cursor_entry->on_press(cursor_entry, settingsScene);
        settingsScene->rec_dirty = true;
    }
    else if (cursor_entry->pref_var && cursor_entry->max_value > 0 && !cursor_entry->locked)
    {
        if (direction == 0)
            direction = a_pressed;

        if (direction != 0)
        {
            // increment/decrement the setting
            const int old_value = *cursor_entry->pref_var;

            for (int i = 0; i < cursor_entry->max_value; ++i)
            {
                *cursor_entry->pref_var =
                    (*cursor_entry->pref_var + direction + cursor_entry->max_value) %
                    cursor_entry->max_value;

                // can't land on a disabled entry
                if (((cursor_entry->disabled_entries >> *cursor_entry->pref_var) & 1) == 0)
                {
                    break;
                }
            }

            cb_play_ui_sound(CB_UISound_Confirm);

            if (old_value != *cursor_entry->pref_var)
            {
                settingsScene->rec_dirty = true;

                if (cursor_entry->on_change)
                {
                    cursor_entry->on_change(cursor_entry, settingsScene, old_value);
                }

                // setting value has changed
                if (cursor_entry->rebuild_when_changed)
                {
                    CB_SettingsScene_rebuildEntries(settingsScene);
                    cursor_entry = (settingsScene->totalMenuItemCount > 0)
                                       ? &settingsScene->entries[settingsScene->cursorIndex]
                                       : &dummy_entry;
                }

                if (cursor_entry->thumbnail)
                    update_thumbnail(settingsScene);
            }
        }
    }
    else if (cursor_entry->emucore_pref && cursor_entry->max_value > 0 && !cursor_entry->locked)
    {
        if (direction == 0)
            direction = a_pressed;

        if (direction != 0)
        {
            ce_preference_t* p = cursor_entry->emucore_pref;
            unsigned old_value = p->get ? p->get(p) : 0;
            unsigned new_value =
                (old_value + (unsigned)((int)cursor_entry->max_value + direction)) %
                cursor_entry->max_value;
            if (p->set && p->set(p, new_value) && old_value != new_value)
                settings_persist_emucore_pref(settingsScene, p, new_value);

            cb_play_ui_sound(CB_UISound_Confirm);

            if (old_value != new_value && cursor_entry->on_change)
            {
                cursor_entry->on_change(cursor_entry, settingsScene, (int)old_value);
            }
        }
    }

    // Recalc recommended settings button state when dirty
    if (settingsScene->rec_dirty && settingsScene->rec_entry_index >= 0 &&
        settingsScene->rec_entry_index < settingsScene->totalMenuItemCount)
    {
        recalc_recommended_entry_state(
            &settingsScene->entries[settingsScene->rec_entry_index], settingsScene
        );
        settingsScene->rec_dirty = false;
    }

    playdate->graphics->clear(kColorWhite);

    int fontHeight = playdate->graphics->getFontHeight(CB_App->bodyFont);
    int rowSpacing = 10;
    int rowHeight = fontHeight + rowSpacing;
    int totalMenuHeight = (MAX_VISIBLE_ITEMS * rowHeight) - rowSpacing;
    int initialY = (kScreenHeight - totalMenuHeight) / 2 + header_y / 2;

    const char* game_name_for_header = NULL;
    if (gameScene && gameScene->name_short)
    {
        game_name_for_header = gameScene->name_short;
    }
    else if (settingsScene->libraryScene)
    {
        CB_Game* selectedGame =
            (settingsScene->libraryScene->listView->selectedItem <
             settingsScene->libraryScene->games->length)
                ? settingsScene->libraryScene->games
                      ->items[settingsScene->libraryScene->listView->selectedItem]
                : NULL;
        if (selectedGame)
        {
            game_name_for_header = selectedGame->names->name_short_leading_article;
        }
    }

    // header y
    if (header_y > 0 && game_name_for_header)
    {
        LCDFont* font = CB_App->labelFont;
        playdate->graphics->setFont(font);
        int nameWidth = playdate->graphics->getTextWidth(
            font, game_name_for_header, strlen(game_name_for_header), kUTF8Encoding, 0
        );
        int textX = LCD_COLUMNS / 2 - nameWidth / 2;

        // Dynamically adjust vertical offset based on text content
        int fontHeight = playdate->graphics->getFontHeight(font);

        // Check if the title has descenders and apply a different offset.
        // This provides a better visual center for all titles.
        int vertical_offset = string_has_descenders(game_name_for_header) ? 1 : 2;
        int textY = ((header_y - fontHeight) / 2) + vertical_offset;

        playdate->graphics->fillRect(0, 0, LCD_COLUMNS, header_y, kColorBlack);
        playdate->graphics->setDrawMode(kDrawModeFillWhite);

        playdate->graphics->drawText(
            game_name_for_header, strlen(game_name_for_header), kUTF8Encoding, textX, textY
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
        // Static text: no CrankBoy pref, no emucore pref, no action.
        bool is_static_text =
            (current_entry->pref_var == NULL && current_entry->emucore_pref == NULL &&
             current_entry->on_press == NULL);
        bool is_locked_option = current_entry->locked;
        bool is_dimmed_option = current_entry->dimmed;

        bool is_functionally_inactive =
            (current_entry->pref_var != NULL && current_entry->max_value == 0);
        bool is_disabled =
            is_static_text || is_locked_option || is_dimmed_option || is_functionally_inactive;
        bool indicate_nondefault = false;
        bool is_selected = itemIndex == settingsScene->cursorIndex;
        int prefvar_index = prefvar_to_index(current_entry->pref_var);
        if (!is_disabled && /* paranoia */ current_entry->pref_var != NULL &&
            !current_entry->suppress_nondefault_indicator)
        {
            indicate_nondefault =
                *current_entry->pref_var != preference_default_value[prefvar_index];
        }
        else if (
            !is_disabled && current_entry->emucore_pref != NULL &&
            !current_entry->suppress_nondefault_indicator
        )
        {
            // TODO: cache this any time option is modified.
            ce_preference_t* p = current_entry->emucore_pref;
            uint32_t flags = p->flags ? p->flags(p) : 0;
            indicate_nondefault = (flags & CE_PREF_NONDEFAULT) != 0;
        }

        int y = initialY + i * rowHeight;
        const char* name = current_entry->name;
        const char* stateText = "";
        if (current_entry->values)
        {
            if (current_entry->pref_var && *current_entry->pref_var < current_entry->max_value)
            {
                stateText = current_entry->values[*current_entry->pref_var];
            }
            else if (
                current_entry->emucore_pref && current_entry->emucore_pref->get &&
                current_entry->max_value > 0
            )
            {
                unsigned v = current_entry->emucore_pref->get(current_entry->emucore_pref);
                if (v < current_entry->max_value)
                    stateText = current_entry->values[v];
            }
            else if (current_entry->pref_var == NULL)
            {
                stateText = current_entry->values[0];
            }
        }

        if (current_entry->show_value_only_on_hover && !is_selected)
            stateText = "";

        int nameWidth = playdate->graphics->getTextWidth(
            CB_App->bodyFont, name, strlen(name), kUTF8Encoding, 0
        );
        int stateWidth = playdate->graphics->getTextWidth(
            CB_App->bodyFont, stateText, strlen(stateText), kUTF8Encoding, 0
        );
        int stateX = kDividerX - stateWidth - kLeftPanePadding;

        if (is_selected)
        {
            playdate->graphics->fillRect(
                0, y - (rowSpacing / 2) - 1, kDividerX, rowHeight, kColorBlack
            );
            playdate->graphics->setDrawMode(kDrawModeFillWhite);
        }
        else
        {
            playdate->graphics->setDrawMode(kDrawModeFillBlack);
        }

        if (indicate_nondefault)
        {
            playdate->graphics->setDrawMode(kDrawModeNXOR);
            playdate->graphics->drawBitmap(
                settingsScene->gradient, kDividerX - 32, y - 2, kBitmapUnflipped
            );
            playdate->graphics->setDrawMode(is_selected ? kDrawModeFillWhite : kDrawModeFillBlack);
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

            int rightArrowWidth =
                playdate->graphics->getTextWidth(CB_App->bodyFont, "›", 1, kUTF8Encoding, 0);

            playdate->graphics->drawText("‹", 1, kUTF8Encoding, 6, y + 2);
            playdate->graphics->drawText(
                "›", 1, kUTF8Encoding, kDividerX - rightArrowWidth - 6, y + 2
            );

            playdate->graphics->drawLine(
                kLeftPanePadding, lineY, textX - padding, lineY, 1,
                is_selected ? kColorWhite : kColorBlack
            );

            playdate->graphics->drawLine(
                textX + nameWidth + padding, lineY, kDividerX - kLeftPanePadding, lineY, 1,
                is_selected ? kColorWhite : kColorBlack
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
            const uint8_t* dither =
                (!is_selected) ? black_transparent_dither : white_transparent_dither;
            playdate->graphics->fillRect(
                kLeftPanePadding, y, nameWidth, fontHeight, (LCDColor)dither
            );
            if (stateText[0])
            {
                playdate->graphics->fillRect(stateX, y, stateWidth, fontHeight, (LCDColor)dither);
            }
        }

        if (is_selected && settingsScene->option_hold_time > HOLD_TIME_SUPPRESS_RELEASE)
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
        const int wrap_width = LCD_COLUMNS - kDividerX - kRightPanePadding - 4;
        int n_lines = 0;
        const cb_line_span* lines =
            cb_settings_wrap(description, wrap_width, CB_App->labelFont, &n_lines);

        int descY = initialY;
        int descLineHeight = playdate->graphics->getFontHeight(CB_App->labelFont) + 2;
        char line_buf[LINE_BUF_SIZE];
        for (int li = 0; li < n_lines; ++li)
        {
            int safe_len = (lines[li].length < (int)(sizeof(line_buf) - 1))
                               ? lines[li].length
                               : (int)(sizeof(line_buf) - 1);
            safe_len = cb_strip_zwsp(line_buf, lines[li].start, safe_len);
            line_buf[safe_len] = '\0';
            playdate->graphics->drawText(
                line_buf, safe_len, kUTF8Encoding, kDividerX + kRightPanePadding, descY
            );
            descY += descLineHeight;
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
        playdate->system->addMenuItem(
            T(pdmenu_library), CB_SettingsScene_didSelectBack, settingsScene
        );
        playdate->system->addMenuItem("Changelog", display_changelog_menu, NULL);
    }
}

static void CB_SettingsScene_free(void* object)
{
    DTCM_VERIFY();
    CB_SettingsScene* settingsScene = object;

    playdate->graphics->setDrawMode(kDrawModeCopy);

    if (settingsScene->gameScene)
    {
        CB_GameScene_apply_settings(settingsScene->gameScene);

        // If the buffered audio sync is the active mode upon leaving the settings,
        // we MUST reset our timing baseline. This recalibrates our sample counter
        // against the hardware clock, closing the "time gap" that was created
        // while the settings menu was open.
        if (preferences_sound_mode == 2)
        {
            CB_reset_audio_sync_state();
        }

        settingsScene->gameScene->audioLocked = settingsScene->wasAudioLocked;
    }

    // ensures no immutable setting can be modified
    if (settingsScene->immutable_settings)
    {
        preferences_restore_subset(settingsScene->immutable_settings);
        cb_free(settingsScene->immutable_settings);
    }

    if (settingsScene->stored_neutrals)
    {
        if (preferences_per_game)
        {
            preferences_restore_subset(settingsScene->stored_neutrals);
        }
        cb_free(settingsScene->stored_neutrals);
        settingsScene->stored_neutrals = NULL;
    }

    if (settingsScene->selected_game_settings_path)
    {
        cb_free(settingsScene->selected_game_settings_path);
        settingsScene->selected_game_settings_path = NULL;
    }

    cb_wrap_invalidate();

    cb_free(settingsScene->save_state_desc);
    settingsScene->save_state_desc = NULL;
    cb_free(settingsScene->load_state_desc);
    settingsScene->load_state_desc = NULL;

    if (settingsScene->entries)
        cb_free(settingsScene->entries);

    if (settingsScene->sections)
    {
        cb_free(settingsScene->sections);
        settingsScene->sections = NULL;
        settingsScene->sections_count = 0;
    }

    if (settingsScene->peek_pdll)
    {
        void (*unload_rom)(void) = pdll_symbol(settingsScene->peek_pdll, "ce_unload_rom");
        if (unload_rom)
            unload_rom();
        pdll_close(settingsScene->peek_pdll);
        settingsScene->peek_pdll = NULL;
        settingsScene->emu_prefs = NULL;  // aliased into now-freed pdll memory
    }
    if (settingsScene->peek_rom)
    {
        cb_free(settingsScene->peek_rom);
        settingsScene->peek_rom = NULL;
    }

    if (itcm_base_desc)
    {
        cb_free(itcm_base_desc);
        itcm_base_desc = NULL;
    }

    if (itcm_device_desc)
    {
        cb_free(itcm_device_desc);
        itcm_device_desc = NULL;
    }

    if (itcm_base_with_device_desc)
    {
        cb_free(itcm_base_with_device_desc);
        itcm_base_with_device_desc = NULL;
    }

    if (itcm_restart_desc)
    {
        cb_free(itcm_restart_desc);
        itcm_restart_desc = NULL;
    }

    if (settingsScene->gradient)
        playdate->graphics->freeBitmap(settingsScene->gradient);

    CB_Scene_free(settingsScene->scene);
    cb_free(settingsScene);
    DTCM_VERIFY();
}
