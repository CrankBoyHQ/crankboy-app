//
//  app.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include "app.h"

#include "../libs/libcrankemu/libcrankemu.h"
#include "../libs/pdll/pdll.h"

#ifdef TARGET_PLAYDATE
#include "../libs/pdnewlib/pdnewlib.h"
#endif

#include "dtcm.h"
#include "global.h"
#include "jparse.h"
#include "preferences.h"
#include "recommended_json.h"
#include "scenes/emucore_game_scene.h"
#include "scenes/file_copying_scene.h"
#include "scenes/game_scene.h"
#include "scenes/info_scene.h"
#include "scenes/library_scene.h"
#include "scenes/parental_lock_scene.h"
#include "script.h"
#include "serial.h"
#include "userstack.h"
#include "version.h"

#include <string.h>

CB_Application* CB_App;

AudioSyncBuffer g_audio_sync_buffer;
atomic_uint g_samples_generated_total = 0;

#if defined(TARGET_SIMULATOR)
pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void read_pdx(void)
{
    // verify pdxinfo has different bundle ID
    size_t pdxlen;
    char* pdxinfo = (void*)cb_read_entire_file("pdxinfo", &pdxlen, kFileRead);
    CB_App->pdxBundleID = NULL;
    if (pdxinfo && pdxlen > 0)
    {
        pdxinfo[pdxlen - 1] = 0;
        char* bundleIDEq = "bundleID=";
        char* bundleID = strstr(pdxinfo, bundleIDEq);
        if (bundleID)
        {
            bundleID += strlen(bundleIDEq);
            char* nl = strchr(bundleID, '\n');
            int len = strlen(bundleID);
            if (nl)
                len = nl - bundleID;
            CB_App->pdxBundleID = cb_memdup(bundleID, len + 1);
            CB_App->pdxBundleID[len] = 0;
            playdate->system->logToConsole("pdxinfo: BundleID=%s", CB_App->pdxBundleID);
        }

        cb_free(pdxinfo);
    }
}

const char* CB_get_forwarded_path(const char* path)
{
    if (!CB_App->bundle_fwd_path)
        return path;

    static char* fwdpath = NULL;

    if (!path || !path[0])
        return path;

    // absolute paths are unchanged
    if (path[0] == '/')
        return path;

    cb_free(fwdpath);
    return fwdpath = aprintf("%s/%s", CB_App->bundle_fwd_path, path);
}

static void load_assets(void)
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;

#define CB_LOAD_FONT(p)                                                  \
    ({                                                                   \
        const char* _err = NULL;                                         \
        const char* _path = CB_get_forwarded_path(p);                    \
        LCDFont* _f = playdate->graphics->loadFont(_path, &_err);        \
        if (!_f || _err)                                                 \
            playdate->system->logToConsole(                              \
                "loadFont(%s) failed: %s", _path, _err ? _err : "(null)" \
            );                                                           \
        _f;                                                              \
    })

    CB_App->bodyFont = CB_LOAD_FONT("fonts/Roobert-11-Medium");
    CB_App->titleFont = CB_LOAD_FONT("fonts/Roobert-20-Medium");
    CB_App->subheadFont = CB_LOAD_FONT("fonts/Asheville-Sans-14-Bold");
    CB_App->labelFont = CB_LOAD_FONT("fonts/Nontendo-Bold");
#undef CB_LOAD_FONT

    CB_App->logoBitmap = playdate->graphics->loadBitmap(CB_get_forwarded_path("images/logo"), NULL);
}

// parses both '\ ' and '\\ ' as spaces.
// (Simulator launch is inconsistent about these)
static char* parse_escaped_value(const char* start)
{
    char* out = cb_malloc(strlen(start) + 1);
    size_t n = 0;
    const char* p = start;
    while (*p)
    {
        if (*p == '\\')
        {
            ++p;
        }
        else if (*p == ' ')
        {
            if (p > start && p[-1] == '\\')
                out[n++] = *p++;
            else
                break;
        }
        else
        {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    return out;
}

// check for CLI arg and launch path, as well as bundle mode
// FIXME: ugly that we have this all in one function~
static int check_is_bundle(void)
{
    const char* launch_path = NULL;

    const char* arg = playdate->system->getLaunchArgs(&launch_path);
    CB_App->pdxLaunchPath = cb_strdup(launch_path);
    playdate->system->logToConsole("launch path: %s", launch_path);

    if (arg && arg[0])
    {
        playdate->system->logToConsole("launch arg: %s", arg);

        if (strstr(arg, "--check-version"))
        {
            CB_App->forceCheckVersion = true;
        }

        if (strstr(arg, "--check-version-local"))
        {
            CB_App->forceCheckVersion = true;
            CB_App->forceCheckVersionLocal = true;
        }

        if (strstr(arg, "--update-forwarder"))
        {
            CB_App->forceUpdateForwarder = true;
        }

        const char* device_arg = strstr(arg, "device=");
        if (device_arg && (device_arg == arg || device_arg[-1] == ' '))
        {
            const char* device_val = device_arg + strlen("device=");
            if (!strncasecmp(device_val, "cgb", 3) || !strncasecmp(device_val, "gbc", 3))
            {
                CB_App->bundled_rom_cgb_mode = 2;
            }
            else if (!strncasecmp(device_val, "dmg", 3))
            {
                CB_App->bundled_rom_cgb_mode = 1;
            }
        }

        const char* core_arg = NULL;
        if (startswith(arg, "core="))
            core_arg = arg + strlen("core=");
        else
        {
            const char* found = strstr(arg, " core=");
            if (found)
                core_arg = found + strlen(" core=");
        }
        if (core_arg)
            CB_App->bundled_core = parse_escaped_value(core_arg);

        const char* rom_arg = NULL;
        if (startswith(arg, "rom="))
        {
            rom_arg = arg + strlen("rom=");
        }
        else
        {
            const char* found = strstr(arg, " rom=");
            if (found)
                rom_arg = found + strlen(" rom=");
        }
        if (rom_arg)
        {
            CB_App->bundled_rom = parse_escaped_value(rom_arg);
            return true;
        }
    }

    // check for bundle.json

    json_value jbundle;
    if (!parse_json(BUNDLE_FILE, &jbundle, kFileRead | kFileReadData))
        return false;

    json_value jrom = json_get_table_value(jbundle, "rom");

    if (jrom.type == kJSONString)
        CB_App->bundled_rom = cb_strdup(jrom.data.stringval);

    json_value jcore = json_get_table_value(jbundle, "core");
    if (jcore.type == kJSONString)
        CB_App->bundled_core = cb_strdup(jcore.data.stringval);

    json_value jdevice = json_get_table_value(jbundle, "device");
    if (jdevice.type == kJSONString)
    {
        if (!strcasecmp(jdevice.data.stringval, "CGB") ||
            !strcasecmp(jdevice.data.stringval, "GBC"))
        {
            CB_App->bundled_rom_cgb_mode = 2;
        }
        else if (!strcasecmp(jdevice.data.stringval, "DMG"))
        {
            CB_App->bundled_rom_cgb_mode = 1;
        }
    }

    json_value jshared = json_get_table_value(jbundle, "shared");
    if (jshared.type == kJSONTrue)
        CB_App->bundle_shared = true;

    json_value jfwd = json_get_table_value(jbundle, "fwd");
    if (jfwd.type == kJSONString)
        CB_App->bundle_fwd_path = cb_strdup(jfwd.data.stringval);

    // ugly hack -- we need this before showing an info scene :/
    load_assets();

    if (CB_App->bundled_rom)
    {
        if (CB_App->pdxBundleID)
        {
            if (strstr(CB_App->pdxBundleID, PDX_STANDARD_BUNDLE_ID))
            {
                CB_InfoScene* infoScene = CB_InfoScene_new(
                    NULL,
                    "ERROR: For bundled ROMs, bundleID in pdxinfo must differ from "
                    "\"" PDX_STANDARD_BUNDLE_ID "\".\n"
                );
                CB_presentModal(infoScene->scene);
                return -1;
            }

            if (strstr(CB_App->pdxBundleID, PDX_CATALOG_BUNDLE_ID))
            {
                CB_InfoScene* infoScene = CB_InfoScene_new(
                    NULL,
                    "ERROR: For bundled ROMs, bundleID in pdxinfo must differ from "
                    "\"" PDX_CATALOG_BUNDLE_ID "\".\n"
                );
                CB_presentModal(infoScene->scene);
                return -1;
            }
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

// Copy a single file to shared forwarder destination
static bool fwd_copy_one(const char* fpath, const char* rename, const char* dest_dir)
{
    size_t size = 0;
    char* data = cb_read_entire_file(fpath, &size, kFileRead | kFileReadData);
    if (!data)
    {
        playdate->system->logToConsole(
            "[fwd] missing source: %s (%s)", fpath, playdate->file->geterr()
        );
        return false;
    }
    char* dest = aprintf("%s/%s", dest_dir, rename ? rename : fpath);
    bool ok = cb_write_entire_file(dest, data, size);
    if (!ok)
    {
        playdate->system->logToConsole(
            "[fwd] failed to write %s (%s)", dest, playdate->file->geterr()
        );
    }
    cb_free(dest);
    cb_free(data);
    return ok;
}

static bool fwd_copy_recursive(const char* fpath, const char* dest_dir);

struct fwd_copy_ud
{
    bool result;
    const char* src_dir;   // current source dir (relative to .pdx)
    const char* dest_dir;  // destination root (absolute)
};

static void fwd_copy_recursive_cb(const char* filename, void* vd)
{
    struct fwd_copy_ud* ud = vd;

    while (*filename == '/')
        ++filename;
    size_t len = strlen(filename);
    while (len > 0 && filename[len - 1] == '/')
        --len;
    if (len == 0)
        return;

    char name[len + 1];
    memcpy(name, filename, len);
    name[len] = 0;

    char* child = aprintf("%s/%s", ud->src_dir, name);
    if (!child)
    {
        ud->result = false;
        return;
    }
    ud->result &= fwd_copy_recursive(child, ud->dest_dir);
    cb_free(child);
}

static bool fwd_copy_recursive(const char* fpath, const char* dest_dir)
{
    FileStat stat;
    if (playdate->file->stat(fpath, &stat) != 0)
        return false;

    if (stat.isdir)
    {
        char* dst_subdir = aprintf("%s/%s", dest_dir, fpath);
        if (dst_subdir)
        {
            full_mkdir(dst_subdir);
            cb_free(dst_subdir);
        }

        struct fwd_copy_ud ud;
        ud.result = true;
        ud.src_dir = fpath;
        ud.dest_dir = dest_dir;
        if (playdate->file->listfiles(fpath, fwd_copy_recursive_cb, &ud, true) != 0)
            return false;
        return ud.result;
    }

    return fwd_copy_one(fpath, NULL, dest_dir);
}

char* CB_install_shared_forwarder(void)
{
    if (!CB_App->pdxBundleID || !*CB_App->pdxBundleID)
    {
        playdate->system->logToConsole("[fwd] no pdxBundleID, cannot install");
        return NULL;
    }

    char* dest_dir = aprintf("%s/%s", SHARED_FORWARDER_ROOT, CB_App->pdxBundleID);
    if (full_mkdir(dest_dir) != 0)
    {
        // pass
    }

    bool ok =
        (fwd_copy_one("crankboy.bin", NULL, dest_dir) ||
         fwd_copy_one("pdex.bin", "crankboy.bin", dest_dir));

    if (!ok)
    {
        cb_free(dest_dir);
        return NULL;
    }

    // copy all assets (no need to copy launcher/)
    const char* asset_sources[] = {"fonts", "images"};
    for (size_t i = 0; i < CB_ARRAY_SIZE(asset_sources); ++i)
    {
        const char* source = asset_sources[i];
        if (!fwd_copy_recursive(source, dest_dir))
        {
            playdate->system->logToConsole("[fwd] failed to copy %s", source);
        }
    }

    // fwdex file (indicates last forwarder installed version)
    const char* version = get_current_version();
    const char* marker = version ? version : "unknown";
    if (!cb_write_entire_file(FORWARDER_INDICATOR_FILE, marker, strlen(marker)))
    {
        playdate->system->logToConsole(
            "[fwd] failed to write %s (%s)", FORWARDER_INDICATOR_FILE, playdate->file->geterr()
        );
        // not fatal
    }

    return dest_dir;
}

// update forwader install if fwdex present and lists differing version,
// or unconditionally when --update-forwarder was passed,
// or if (A) button is held (secret method for power-users)
static void maybe_refresh_shared_forwarder(void)
{
    bool stale = false;
    PDButtons bdown;
    playdate->system->getButtonState(&bdown, NULL, NULL);
    bool forced = CB_App->forceUpdateForwarder || (bdown & kButtonA);

    if (!forced)
    {
        size_t flen = 0;
        char* recorded = cb_read_entire_file(FORWARDER_INDICATOR_FILE, &flen, kFileReadData);
        if (!recorded)
            return;
        const char* version = get_current_version();
        stale = true;
        if (version && strlen(recorded) == strlen(version) && !strcmp(recorded, version))
            stale = false;
        cb_free(recorded);
        if (!stale)
            return;
    }

    cb_draw_logo_screen_and_display(CB_App->subheadFont, "Updating Forwarders...");
    playdate->system->logToConsole(
        "[fwd] %s -- refreshing shared forwarder for %s", forced ? "force-update" : "fwdex stale",
        CB_App->pdxBundleID ? CB_App->pdxBundleID : "?"
    );
    char* dir = CB_install_shared_forwarder();
    if (dir)
        cb_free(dir);
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
        no_copy = (newline[1] == 'n');  // nocopy
    }

    // remove trailing `/`
    while (shared_directory && *shared_directory)
    {
        size_t len = strlen(shared_directory);
        if (shared_directory[len - 1] == '/')
        {
            shared_directory[len - 1] = '\0';
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
        bool did_move_files = false;
        const char* sources[] = {CB_settingsPath, CB_coversPath, CB_patchesPath,
                                 CB_gamesPath,    CB_statesPath, CB_savesPath};

        for (size_t i = 0; i < sizeof(sources) / sizeof(char*); ++i)
        {
            const char* dst = cb_gb_directory_path(sources[i]);
            const char* src = cb_data_directory_path(sources[i]);
            // move files from data/ but don't replace existing directory
            if (cb_directory_exists_and_nonempty_or_file_exists(src) &&
                !cb_directory_exists_and_nonempty_or_file_exists(dst))
            {
                did_move_files = true;
                int result = playdate->file->rename(src, dst);
                if (result == 0)
                {
                    playdate->system->logToConsole("Moved %s -> %s", src, dst);
                }
                else
                {
                    playdate->system->logToConsole("Failed to move %s -> %s", src, dst);
                    err = true;
                    break;
                }
            }
        }

        if (did_move_files)
        {
            CB_App->migration_modal_needed = true;
        }

        if (!err)
        {
            playdate->system->logToConsole("Done moving files.");
            shared_directory = aprintf("%s\nnocopy", shared_directory);
        }
    }

    cb_write_entire_file(DIRECTORY_POINTER, shared_directory, strlen(shared_directory));

    if (shared_directory != CB_App->directory)
    {
        cb_free(shared_directory);
    }

    full_mkdir(cb_gb_directory_path(CB_savesPath));
    full_mkdir(cb_gb_directory_path(CB_gamesPath));
    full_mkdir(cb_gb_directory_path(CB_coversPath));
    full_mkdir(cb_gb_directory_path(CB_statesPath));
    full_mkdir(cb_gb_directory_path(CB_settingsPath));
    full_mkdir(cb_gb_directory_path(CB_customSettingsPath));
    full_mkdir(cb_gb_directory_path(CB_patchesPath));
}

static void get_homebrew_hub_api(void)
{
    char* hbapi = cb_read_entire_file(HOMEBREW_HUB_API_FILE, NULL, kFileRead | kFileReadData);
    if (!hbapi)
        return;

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
    char* staticpath = nl + 1;
    char* spnl = strchr(staticpath, '\n');
    if (spnl)
        spnl[0] = 0;
    char* path = strchr(domain, '/');
    if (!path)
    {
        cb_free(hbapi);
        return;
    }

    // check for another newline
    nl = spnl;
    CB_App->hbSearchExtraFlags = NULL;
    if (nl)
    {
        nl[0] = 0;
        CB_App->hbSearchExtraFlags = nl + 1;
        nl = strchr(CB_App->hbSearchExtraFlags, '\n');
        if (nl)
        {
            nl[0] = 0;
        }
        if (strlen(CB_App->hbSearchExtraFlags) == 0)
        {
            CB_App->hbSearchExtraFlags = NULL;
        }
    }

    // strip final slash
    while (path[0] && path[strlen(path) - 1] == '/')
    {
        path[strlen(path) - 1] = 0;
    }

    if (!path[0])
    {
        cb_free(hbapi);
        return;
    }

    while (staticpath[0] && staticpath[strlen(path) - 1] == '/')
    {
        staticpath[strlen(staticpath) - 1] = 0;
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

    // playdate->system->logToConsole("%s\n%s\n%s", CB_App->hbApiDomain, CB_App->hbApiPath,
    // CB_App->hbSearchExtraFlags);
}

// true if slug is already claimed by any known core
static bool CB_system_slug_claimed(const char* slug)
{
    if (!strcmp(slug, "gb")) return true;
    for (size_t i = 0; i < CB_App->cores_n; ++i)
        for (size_t j = 0; j < CB_App->cores[i].n_system_slugs; ++j)
            if (strcmp(CB_App->cores[i].system_slugs[j], slug) == 0)
                return true;
    return false;
}

static void CB_load_core(const char* path)
{
    pdll_t* pdll = pdll_open(playdate, path, PDLL_FILE_PDX | PDLL_FILE_DATA, 2);
    if (!pdll)
    {
        playdate->system->logToConsole("CB_load_core: %s", pdll_get_error());
        return;
    }

    if (!pdll->getSymbol)
    {
        playdate->system->logToConsole(
            "CB_load_core: '%s' is not a libcrankemu core (no symbol resolver)", path
        );
        pdll->flags |= PDLL_NO_TERM;
        pdll_close(pdll);
        return;
    }

    const char* (*core_id)(void) = pdll_symbol(pdll, "ce_core_id");
    const char* (*core_name)(void) = pdll_symbol(pdll, "ce_core_name");
    const char* (*core_version)(void) = pdll_symbol(pdll, "ce_core_version");
    const char* (*system_slugs)(void) = pdll_symbol(pdll, "ce_get_system_slugs");

    if (!core_id || !core_name || !core_version || !system_slugs)
    {
        playdate->system->logToConsole(
            "CB_load_core: '%s' missing required symbol -- %s", path, pdll_get_error()
        );
        pdll_close(pdll);
        return;
    }

    uint32_t (*get_version)(void) = pdll_symbol(pdll, "ce_get_version");
    if (!get_version)
    {
        playdate->system->logToConsole(
            "CB_load_core: '%s' is not a valid core, lacks ce_get_version", path
        );
        pdll_close(pdll);
        return;
    }

    uint32_t ce_version = get_version();
    if (ce_version > CRANKEMU_VERSION)
    {
        playdate->system->logToConsole(
            "CB_load_core: '%s' needs core version %u, frontend supports up to %u", path,
            (unsigned)ce_version, (unsigned)CRANKEMU_VERSION
        );
        pdll_close(pdll);
        return;
    }

    emucore_t core = {0};
    core.ce_version = ce_version;
    core.id = cb_strdup(core_id());
    core.path = cb_strdup(path);
    core.core_version = cb_strdup(core_version());
    core.human_name = cb_strdup(core_name());

    core.pdll = NULL;

    // split the ;-separated system directories, dropping empties and ones already claimed.
    const char* slugs = system_slugs();
    for (const char* p = slugs ? slugs : ""; *p;)
    {
        const char* start = p;
        while (*p && *p != ';')
            ++p;
        size_t toklen = (size_t)(p - start);
        if (*p == ';')
            ++p;
        if (toklen == 0)
            continue;

        char* token = cb_malloc(toklen + 1);
        memcpy(token, start, toklen);
        token[toklen] = 0;

        bool dup = CB_system_slug_claimed(token);
        for (size_t j = 0; !dup && j < core.n_system_slugs; ++j)
            dup = strcmp(core.system_slugs[j], token) == 0;
        if (dup)
        {
            cb_free(token);
            continue;
        }

        core.system_slugs =
            cb_realloc(core.system_slugs, (core.n_system_slugs + 1) * sizeof(char*));
        core.system_slugs[core.n_system_slugs++] = token;
    }

    CB_App->cores = cb_realloc(CB_App->cores, (CB_App->cores_n + 1) * sizeof(emucore_t));
    CB_App->cores[CB_App->cores_n++] = core;

    playdate->system->logToConsole(
        "CB_load_core: loaded '%s' (%s %s, id=%s) with %u system dir(s)", path, core.human_name,
        core.core_version, core.id, (unsigned)core.n_system_slugs
    );

    pdll_close(pdll);
}

typedef struct
{
    char* basepath;
    unsigned long long mtime;  // YYYYMMDDHHMMSS, 0 if stat failed
} CB_core_candidate;

typedef struct
{
    const char* dir;
    CB_core_candidate* items;
    size_t n;
    size_t cap;
} CB_cores_scan;

static void CB_cores_scan_cb(const char* filename, void* ud)
{
    CB_cores_scan* scan = ud;

    static const char* const exts[] = {".bin", ".pdll", ".so", ".dll", ".dylib"};
    size_t len = strlen(filename);
    size_t extlen = 0;
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i)
    {
        size_t el = strlen(exts[i]);
        if (len > el && strcmp(filename + len - el, exts[i]) == 0)
        {
            extlen = el;
            break;
        }
    }
    if (extlen == 0)
        return;

    char* fullpath = aprintf("%s/%s", scan->dir, filename);
    FileStat st;
    unsigned long long mtime = 0;
    if (playdate->file->stat(fullpath, &st) == 0)
        mtime = (unsigned long long)st.m_year * 10000000000ull +
                (unsigned long long)st.m_month * 100000000ull +
                (unsigned long long)st.m_day * 1000000ull +
                (unsigned long long)st.m_hour * 10000ull +
                (unsigned long long)st.m_minute * 100ull + (unsigned long long)st.m_second;
    cb_free(fullpath);

    if (scan->n == scan->cap)
    {
        scan->cap = scan->cap ? scan->cap * 2 : 8;
        scan->items = cb_realloc(scan->items, scan->cap * sizeof(CB_core_candidate));
    }
    scan->items[scan->n].basepath = aprintf("%s/%.*s", scan->dir, (int)(len - extlen), filename);
    scan->items[scan->n].mtime = mtime;
    scan->n++;
}

static void CB_load_cores(void)
{
    const char* dir = global.cores_dir ? global.cores_dir : DEFAULT_CORES_DIRECTORY;
    playdate->system->logToConsole("CB_load_cores: dir=%s", dir);

    CB_cores_scan scan = {.dir = dir, .items = NULL, .n = 0, .cap = 0};
    cb_listfiles(dir, CB_cores_scan_cb, &scan, 0, kFileRead | kFileReadData);
    playdate->system->logToConsole("CB_load_cores: scan n=%u", (unsigned)scan.n);

    if (scan.n == 0)
        return;
    cb_draw_logo_screen_and_display(CB_App->subheadFont, "Loading cores...");

    // sort reverse-chronologically
    for (size_t i = 1; i < scan.n; ++i)
    {
        CB_core_candidate cur = scan.items[i];
        size_t j = i;
        while (j > 0 && scan.items[j - 1].mtime < cur.mtime)
        {
            scan.items[j] = scan.items[j - 1];
            --j;
        }
        scan.items[j] = cur;
    }

    for (size_t i = 0; i < scan.n; ++i)
    {
        playdate->system->logToConsole("CB_load_cores: pre-load[%u] %s", (unsigned)i, scan.items[i].basepath);
        CB_load_core(scan.items[i].basepath);
        playdate->system->logToConsole("CB_load_cores: post-load[%u]", (unsigned)i);
        cb_free(scan.items[i].basepath);
    }
    cb_free(scan.items);
}

static void non_bundle_init(void)
{
    playdate->system->logToConsole("non_bundle_init: start");
    cb_draw_logo_screen_and_display(CB_App->subheadFont, "Initializing...");
    playdate->system->logToConsole("non_bundle_init: after draw");
    get_homebrew_hub_api();
    playdate->system->logToConsole("non_bundle_init: after hub api");

    CB_App->rhdb_present =
        cb_file_exists_maybe_compressed(ROMHACK_DB_FILE, kFileReadData | kFileRead);
    playdate->system->logToConsole("non_bundle_init: after rhdb check");

    global.shown_intro = true;
    save_global();
    playdate->system->logToConsole("non_bundle_init: after save_global");

    CB_load_cores();
    playdate->system->logToConsole("non_bundle_init: after CB_load_cores");

    CB_FileCopyingScene* copyingScene = CB_FileCopyingScene_new();
    CB_present(copyingScene->scene);
}

void CB_showHelp(bool first_time)
{
    const char* title = first_time ? "Welcome to CrankBoy!" : "CrankBoy Usage";

    const char* A0 = first_time ? "This is a quick guide to getting started.\n\nIn the future, you "
                                  "can review these instructions from the \"help\" option in "
                                  "CrankBoy's main menu.\n\n(Scroll down with the crank!)\n\n"
                                : "";

    const char* A = first_time ? "To get started, you'll want to add some ROMs to CrankBoy.\n\n"
                                 "We recommend using CrankBoy Manager."
                               : "Use CrankBoy Manager to add ROMs";

    const char* B = "                                        - OR -";
    const char* C1 = "1. Connect your Playdate to another device via USB.\n";
    const char* C2 =
        "2. Hold LEFT + MENU + POWER for 10 seconds to put your Playdate into Data Disk mode.\n";
    const char* C3 =
        "3. From the connected device, copy your ROM files (.gb, .gbc, or .gbz) onto your "
        "Playdate at the following directory: ";

    const char* D =
        "\n\nAlternatively, you can download free \"homebrew\" titles from within CrankBoy in the "
        "main menu via ⊙ > settings > Get ROMs. ";

#ifdef CRANKBOY_OFFICIAL_CATALOG
    const char* E = first_time
                        ? "You can also press Ⓑ now to start playing the included ROMs immediately."
                        : "";
#else
    const char* E = first_time ? "Press Ⓑ to continue." : "";
#endif

    char* s = aprintf(
        "%s%s%s%s%s%s%s%s%s%s%s", A0, A, "\n\n", B, "\n\n", C1, C2, C3,
        cb_gb_directory_path(CB_gamesPath), D, E
    );

    CB_InfoScene* infoScene = CB_InfoScene_new(title, s);

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
        cb_gb_directory_path(CB_gamesPath), (void*)any_file_found, &any_found, false
    );
    return any_found;
}

void CB_init(void)
{
    CB_App = allocz(CB_Application);
    CB_App->active_emucore = -1;

    cb_register_all_c_scripts();

    CB_App->gameNameCache = array_new();
    CB_App->gameListCache = array_new();
    CB_App->coverCache = NULL;
    CB_App->gameListCacheIsSorted = false;
    CB_App->scene = NULL;

    CB_App->pendingScene = NULL;

    CB_App->coverArtCache.rom_path = NULL;
    CB_App->coverArtCache.art.bitmap = NULL;

    CB_App->migration_modal_needed = false;

    read_pdx();

    check_is_bundle();

    load_assets();

    if (!CB_App->bundled_rom)
    {
        cb_draw_logo_screen_and_display(CB_App->subheadFont, "Initializing...");
        initialize_directory();
        maybe_refresh_shared_forwarder();
#if !defined(CRANKBOY_OFFICIAL_CATALOG)
        if (CB_App->forceCheckVersion)
            check_for_updates();
        else if (GITHUB_RELEASE)
            possibly_check_for_updates();
#endif
        check_for_parental_lock();

        playdate->system->logToConsole("shown intro: %d", (int)global.shown_intro);

        if (global.shown_intro || cb_file_exists(LAST_SELECTED_FILE, kFileReadData) ||
            games_exist_in_data())
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
        // Paint the screen black, hides pdboot
        playdate->graphics->clear(kColorBlack);
        playdate->graphics->markUpdatedRows(0, LCD_ROWS - 1);
        playdate->graphics->display();

        if (CB_App->bundle_shared)
        {
            CB_App->directory = aprintf(DEFAULT_SHARED_DIRECTORY);
            full_mkdir(CB_App->directory);
            full_mkdir(cb_gb_directory_path(CB_savesPath));
            full_mkdir(cb_gb_directory_path(CB_statesPath));
            full_mkdir(cb_gb_directory_path(CB_settingsPath));
            full_mkdir(cb_gb_directory_path(CB_coversPath));
            full_mkdir(cb_gb_directory_path(CB_gamesPath));
        }
        else
        {
            // non-shared bundle
            CB_App->directory = aprintf(".");
            playdate->file->mkdir(CB_savesPath);
            playdate->file->mkdir(CB_statesPath);
            playdate->file->mkdir(CB_settingsPath);
        }
    }

    preferences_init();

    CB_App->clickSynth = playdate->sound->synth->newSynth();
    playdate->sound->synth->setWaveform(CB_App->clickSynth, kWaveformSquare);
    playdate->sound->synth->setAttackTime(CB_App->clickSynth, 0.0001f);
    playdate->sound->synth->setDecayTime(CB_App->clickSynth, 0.05f);
    playdate->sound->synth->setSustainLevel(CB_App->clickSynth, 0.0f);
    playdate->sound->synth->setReleaseTime(CB_App->clickSynth, 0.0f);

    CB_App->selectorBitmapTable = playdate->graphics->loadBitmapTable(
        CB_get_forwarded_path("images/selector/selector"), NULL
    );
    CB_App->startSelectBitmap =
        playdate->graphics->loadBitmap(CB_get_forwarded_path("images/selector-start-select"), NULL);

    // add audio callback later
    CB_App->soundSource = NULL;

    // custom frame rate delimiter
    playdate->display->setRefreshRate(0);

    if (CB_App->bundled_rom && CB_App->bundled_core)
    {
        char* base = cb_strip_extension(CB_App->bundled_core);
        size_t before = CB_App->cores_n;
        CB_load_core(base);
        cb_free(base);

        emucore_t* core = (CB_App->cores_n > before) ? &CB_App->cores[CB_App->cores_n - 1] : NULL;
        // TODO: don't hardcode slug as [0] -- require bundle.json to specify system slug if >= 2 slugs present.
        const char* slug = (core && core->n_system_slugs > 0) ? core->system_slugs[0] : NULL;
        if (!slug)
        {
            playdate->system->error("Failed to load core \"%s\"", CB_App->bundled_core);
            return;
        }

        CB_EmucoreGameScene* es = CB_EmucoreGameScene_new(CB_App->bundled_rom, slug, "Bundled ROM");
        if (es)
        {
            CB_present(es->scene);
        }
        else
        {
            playdate->system->error(
                "Failed to launch bundled ROM \"%s\" with core \"%s\"", CB_App->bundled_rom,
                CB_App->bundled_core
            );
            return;
        }
    }
    else if (CB_App->bundled_rom)
    {
        CB_GameScene* gameScene =
            CB_GameScene_new(CB_App->bundled_rom, "Bundled ROM", CB_App->bundled_rom_cgb_mode == 2);
        if (gameScene)
        {
            CB_present(gameScene->scene);
        }
        else
        {
            playdate->system->error("Failed to launch bundled ROM \"%s\"", CB_App->bundled_rom);
            return;
        }
    }
    else  // non-bundled mode
    {
        // so as not to confuse rom manager, only
        // do serial communication if not on bundle mode.
        playdate->system->setSerialMessageCallback(CB_on_serial_message);
    }
}

void CB_headphone_state_changed(int headphone, int mic)
{
    if (audioGameScene)
    {
        reconfigure_audio_source(audioGameScene, headphone);
    }
}

// note: used in other files too
void collect_game_filenames_callback(const char* filename, void* userdata)
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

    if ((cb_strcmp(extension, "gb") == 0 || cb_strcmp(extension, "gbc") == 0 ||
         cb_strcmp(extension, "gbz") == 0))
    {
        array_push(filenames_array, cb_strdup(filename));
    }
}

__section__(".rare") static void switchToPendingScene(void)
{
    CB_Scene* oldScene = CB_App->scene;
    CB_Scene* newScene = CB_App->pendingScene;

    CB_App->scene = newScene;
    CB_App->pendingScene = NULL;

    // Free old scene and any ancestors the new scene isn't keeping,
    // so non-modal transitions over a parent don't leak it.
    while (oldScene && oldScene != newScene)
    {
        CB_Scene* parent = oldScene->parentScene;
        void* managedObject = oldScene->managedObject;
        oldScene->free(managedObject);
        oldScene = parent;
    }
}

__section__(".text.main") void CB_poll_buttons(void)
{
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
}

__section__(".text.main") void CB_update(float dt)
{
    CB_App->dt = dt;
    CB_App->avg_dt_raw = (CB_App->avg_dt_raw * FPS_AVG_DECAY) + (1 - FPS_AVG_DECAY) * dt;
    CB_App->avg_dt =
        (CB_App->avg_dt * FPS_AVG_DECAY) + (1 - FPS_AVG_DECAY) * dt * CB_App->avg_dt_mult;
    CB_App->avg_dt_mult = 1.0f;

    CB_App->crankChange = playdate->system->getCrankChange();

    // buttons already polled at the top of main.c::update() (so the fast
    // update_override path sees the same masked state).

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
    if (event == kEventMirrorStarted)
    {
        CB_App->mirror_active = true;
    }
    else if (event == kEventMirrorEnded)
    {
        CB_App->mirror_active = false;
    }
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
    cb_free(gameName->system_slug);
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
        CB_App->scene = NULL;
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

    if (CB_App->bundled_rom)
    {
        cb_free(CB_App->bundled_rom);
    }

    if (CB_App->bundled_core)
    {
        cb_free(CB_App->bundled_core);
    }

    if (CB_App->bundle_fwd_path)
    {
        cb_free(CB_App->bundle_fwd_path);
    }

    script_quit();
    recommended_json_quit();
    version_quit();

#ifdef TARGET_PLAYDATE
    pdnewlib_quit();
#endif

    cb_free(CB_App);
}
