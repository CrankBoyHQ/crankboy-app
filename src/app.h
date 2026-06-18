//
//  app.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef app_h
#define app_h

#include "scene.h"
#include "utility.h"

#include <pd_api.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR) && !defined(TARGET_DEVICE)
#define TARGET_DEVICE 1
#endif

#if defined(TARGET_SIMULATOR)
#include <pthread.h>
extern pthread_mutex_t audio_mutex;
#endif

#if !defined(TARGET_DEVICE) && defined(DTCM_ALLOC)
#undef DTCM_ALLOC
#endif

#ifdef TARGET_SIMULATOR
#define __space
#else
#define __space __attribute__((optimize("Os")))
#endif

#define AUDIO_RING_BUFFER_SIZE 4096                          // ~90ms of audio at 44.1kHz.
#define AUDIO_RING_BUFFER_MASK (AUDIO_RING_BUFFER_SIZE - 1)  // For fast bitwise modulo

// For the official catalog release from the crankboy team. Not
// for third-party catalog releases:
// #define CRANKBOY_OFFICIAL_CATALOG

typedef struct pdll_s pdll_t;

typedef struct
{
    int16_t left[AUDIO_RING_BUFFER_SIZE];
    int16_t right[AUDIO_RING_BUFFER_SIZE];
    atomic_uint write_pos;
    atomic_uint read_pos;
} AudioSyncBuffer;

enum cgb_support_e
{
    // these enum values are cached to disk,
    // so don't modify existing ones.
    NON_GB_SYSTEM = 0,
    GB_SUPPORT_DMG = 1,
    GB_SUPPORT_CGB = 2,
    GB_SUPPORT_DMG_AND_CGB = 3,
};

/*
 * Defines the main stack size. This value provides a necessary safety
 * margin to prevent intermittent crashes. It was increased to 0x2700
 * initially to ensure stable CGB emulation.
 *
 * 0x2700 is the max we should use. 0x2760 is possible but leaves no
 * headroom if there are any changes to the Playdate OS in the future.
 * 0x2730 is currently used to avoid occasional DTCM crashes on RevA.
 */
#define PLAYDATE_STACK_SIZE 0x2730

#define FPS_AVG_DECAY 0.8f

typedef struct
{
    // basename, including extension
    char* filename;

    // e.g. "pm", "gb" (includes both dmg and cgb)
    char* system_slug;

    // CRC32 of rom's contents
    uint32_t crc32;

    bool rom_has_battery;

    enum cgb_support_e rom_cgb_support;

    // common database name, for thumbnail matching etc.
    char* name_database;

    // full path resolved during scanning, e.g. "packed/file.gb" or "<dir>/file.gb"
    char* fullpath;

    // human-readable variations (all may be NULL except name_filename and
    // name_filename_leading_article)
    char* name_short;
    char* name_detailed;
    char* name_filename;  // (basename, extension stripped)
    char* name_short_leading_article;
    char* name_detailed_leading_article;
    char* name_filename_leading_article;
    char* name_header;
} CB_GameName;

// Note: does not free CB_GameName struct, only its members.
void free_game_names(const CB_GameName* gameNames);

typedef struct
{
    CB_LoadedCoverArt art;
    char* rom_path;
} CB_GlobalCoverCache;

typedef struct emucore_s
{
    unsigned ce_version;
    char* id;  // e.g. "stella_pd"
    char* path;
    char* core_version;
    char* human_name;  // e.g. "StellaPD"
    size_t n_system_slugs;
    char** system_slugs;
    pdll_t* pdll; /* NULL if not currently open */
} emucore_t;

typedef struct CB_Application
{
    // used by some scenes to bypass normal input
    void (*update_override)(void* ud);
    void* update_override_ud;

    float dt;
    float avg_dt;       // for fps calculation (scaled by avg_dt_mult)
    float avg_dt_mult;  // reciprocal number of emulated frames last frame
    float avg_dt_raw;   // unscaled dt average for interlace decision
    float crankChange;
    CB_Scene* scene;
    CB_Scene* pendingScene;
    LCDFont* bodyFont;
    LCDFont* titleFont;
    LCDFont* subheadFont;
    LCDFont* labelFont;
    LCDFont* progressFont;
    LCDBitmap* logoBitmap;
    LCDBitmapTable* selectorBitmapTable;
    LCDBitmap* startSelectBitmap;
    SoundSource* soundSource;
    CB_GlobalCoverCache coverArtCache;
    CB_Array* gameNameCache;
    CB_Array* gameListCache;
    bool gameListCacheIsSorted;
    bool rhdb_present;
    struct PDSynth* clickSynth;

    unsigned simulate_button_presses[6];

    PDButtons buttons_down;  // (discards suppressed)
    PDButtons buttons_pressed;
    PDButtons buttons_released;  // (discards suppressed)
    PDButtons buttons_suppress;  // prevent these from registering until they
                                 // are released

    char* directory;

    // can use restricted playdate functionality.
    bool hasSystemAccess : 1;

    // true when menu is open
    bool currentlyPaused : 1;

    // should check the latest-update as saved on the disk
    bool shouldCheckUpdateInfo : 1;

    bool hbApiUseHTTPS : 1;

    bool parentalLockEngaged : 1;

    bool forceCheckVersion : 1;
    bool forceCheckVersionLocal : 1;
    bool forceUpdateForwarder : 1;

    // playdate-level lua enabled (main.pdz)
    bool lua : 1;

    // from pdx "bundleID" field (not related to CrankBoy "bundle mode");
    char* pdxBundleID;

    char* pdxLaunchPath;  // from system->getLaunchArgs

    char* hbApiDomain;
    char* hbApiPath;
    char* hbSearchExtraFlags;
    char* hbStaticPath;
    char* hbApiBuffer;

    bool migration_modal_needed;

    // Playdate mirror (streaming video)
    bool mirror_active;

    // If this is non-null, then the app is intended to contain exactly one ROM due to the presence
    // of bundle.json The following changes are made:
    // - library view is omitted
    // - credits accessible via setings
    // - no per-game/global settings distinction
    // - some settings become inaccessible
    char* bundled_rom;         // (path to bundled rom)
    int bundled_rom_cgb_mode;  // 0: unspecified. 1: force dmg. 2: force cgb.
    char* bundled_core;        // (path to emucore for bundle or NULL)

    // use shared roms/saves/settings directory
    bool bundle_shared;

    // ex. "/Shared/.forwarders/app.crankboyhq.crankboy"
    char* bundle_fwd_path;

    size_t cores_n;
    emucore_t* cores;
    int active_emucore;  // index into cores of the currently-loaded core, or -1

    // "safe mode": skip loading emucores this session (set by crash recovery).
    bool skip_emucores;

    // sorted array of char* filenames present in "packed/" (CRANKBOY_OFFICIAL_CATALOG)
    CB_Array* packed_filenames;
} CB_Application;

extern CB_Application* CB_App;
extern AudioSyncBuffer g_audio_sync_buffer;
extern atomic_uint g_samples_generated_total;
extern const char* const save_slot_labels[10];

void CB_init(void);
void CB_event(PDSystemEvent event, uint32_t arg);

void CB_set_setup_canary(bool value);
void CB_update(float dt);
void CB_account_frame_timing(float dt);
void CB_poll_buttons(void);
void CB_present(CB_Scene* scene);
void CB_quit(void);
void CB_goToLibrary(void);
void CB_presentModal(CB_Scene* scene);
void CB_dismiss(CB_Scene* scene);
void CB_headphone_state_changed(int headphone, int mic);
void CB_showHelp(bool first_time);

// Unload active core, then load the given core. (NULL: unload only.)
void CB_load_emucore(emucore_t* core);

void cb_emucore_set_frontend(pdll_t* pdll);

emucore_t* CB_get_emucore_by_slug(const char* slug);

// allocates in DTCM region (if enabled).
// note, there is no associated free.
void* dtcm_alloc(size_t size);

// returns NULL if was not booted by pdboot.
const char* get_pdboot_name_and_version(void);

// Install/refresh the shared forwarder (/Shared/.forwader/<...>/crankboy.bin, etc.)
// and write FORWARDER_INDICATOR_FILE marker in this data dir.
// Returns a caller-owned path to the install dir on success
char* CB_install_shared_forwarder(void);

// If we're running in forwarded bundle mode,
// returns the given path in the forwarded assets dir.
// Otherwise, returns the original path.
// (Note: string could be freed on next call.)
const char* CB_get_forwarded_path(const char* path);

#define PLAYDATE_ROW_STRIDE 52

// Any function which a __core fn can call MUST be marked as long_call (i.e.
// __shell) to ensure portability.
#ifdef TARGET_SIMULATOR
#define __shell
#else
#ifdef ITCM_CORE
#define __shell                                                     \
    __attribute__((long_call)) __attribute((noinline)) __section__( \
        ".text."                                                    \
        "cb"                                                        \
    ) __attribute__((optimize("Os")))
#else
#define __shell __attribute((noinline)) __section__(".text.cb") __attribute__((optimize("Os")))
#endif
#endif

// don't exceed 60 fps
#define CAP_FRAME_RATE 1

#define SAVE_STATE_SLOT_COUNT 10
#define SAVE_SLOT_COUNT 10
#define SAVE_STATE_THUMBNAIL_W 160
#define SAVE_STATE_THUMBNAIL_H 144

// indicates sram file version.
// If not present at end of file, it's an old version or from another emulator.
// Can adjust the second-to-last byte to indicate version, perhaps.
#define SRAM_MAGIC_NUMBER 0x5900424B4E415243
// for playdate extension crank menu IO register;
// how far one has to turn the crank before getting to the next menu item
#define CRANK_MENU_DELTA_BINANGLE 0x2800

#define THUMBNAIL_WIDTH 240
#define THUMBNAIL_HEIGHT 240

// files that have been copied from PDX to data folder
#define COPIED_FILES "manifest.json"
#define PATCH_LIST_FILE "manifest.json"
#define BUNDLE_FILE "bundle.json"
#define ROMHACK_DB_FILE "rhdb.json"
#define DIRECTORY_POINTER "directory.txt"
#define GLOBAL_FILE "global.json"
#define LAST_SELECTED_FILE "library_last_selected.txt"
#define HOMEBREW_HUB_API_FILE "hbapi.txt"
#define PARENTAL_LOCK_FILE "parental_lock.bin"

#define DEFAULT_SHARED_DIRECTORY "/Shared/Emulation/gb"
#define DEFAULT_CORES_DIRECTORY "/Shared/Emulation/cores"
#define GB_SYSTEM_SLUG "gb"  // built-in Game Boy system (lives in CB_App->directory)
#define PDX_STANDARD_BUNDLE_ID "app.crankboyhq.crankboy"
#define PDX_CATALOG_BUNDLE_ID "catalog.crankboyhq.crankboy"
#define SHARED_FORWARDER_ROOT "/Shared/.forwarder"

#define FORWARDER_INDICATOR_FILE "fwdex"

#define DISK_IMAGE "__homebrew_dl_img.pdi"

// for files which should only appear in data unless we're in bundle mode
#define kFileReadDataOrBundle (CB_App->bundled_rom ? (kFileRead | kFileReadData) : kFileReadData)

#ifdef CRANKBOY_OFFICIAL_CATALOG
#define kFileReadDataOrPacked (kFileRead | kFileReadData)
#else
#define kFileReadDataOrPacked kFileReadDataOrBundle
#endif

#endif /* app_h */
