//
//  app.h
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef app_h
#define app_h

#include "pd_api.h"
#include "preferences.h"
#include "scene.h"
#include "utility.h"

#include <math.h>
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

#define AUDIO_RING_BUFFER_SIZE 4096  // ~90ms of audio at 44.1kHz.

typedef struct
{
    int16_t left[AUDIO_RING_BUFFER_SIZE];
    int16_t right[AUDIO_RING_BUFFER_SIZE];
    atomic_uint write_pos;
    atomic_uint read_pos;
} AudioSyncBuffer;

enum cgb_support_e {
    // these enum values are cached to disk,
    // so don't modify existing ones.
    GB_SUPPORT_DMG = 1,
    GB_SUPPORT_CGB = 2,
    GB_SUPPORT_DMG_AND_CGB = 3,
};

// Defines the main stack size. This value provides a necessary safety
// margin to prevent intermittent crashes. It was increased to 0x2700
// specifically to ensure stability in games like Pokemon Gold/Silver,
// which have a higher runtime stack requirement.
#define PLAYDATE_STACK_SIZE 0x2700

#define FPS_AVG_DECAY 0.8f

#define TENDENCY_BASED_ADAPTIVE_INTERLACING 1

typedef struct
{
    // basename, including extension
    char* filename;

    // CRC32 of rom's contents
    uint32_t crc32;
    
    // TODO: add these
    bool rom_has_battery;
    enum cgb_support_e rom_cgb_support;

    // common database name, for thumbnail matching etc.
    char* name_database;

    // human-readable variations
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

typedef struct
{
    char* rom_path;
    void* compressed_data;
    int compressed_size;
    int original_size;
    int width;
    int height;
    int rowbytes;
    bool has_mask;
} CB_CoverCacheEntry;

typedef struct CB_Application
{
    float dt;
    float avg_dt;       // for fps calculation
    float avg_dt_mult;  // reciprocal number of emulated frames last frame
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
    CB_Array* coverCache;
    CB_Array* gameListCache;
    bool gameListCacheIsSorted;
    json_value rhdb_cache;
    struct PDSynth* clickSynth;

    unsigned simulate_button_presses[6];

    PDButtons buttons_down;
    PDButtons buttons_pressed;
    PDButtons buttons_released;
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
    
    char* directory;
    char* hbApiDomain;
    char* hbApiPath;
    char* hbStaticPath;

    // If this is non-null, then the app is intended to contain exactly one ROM due to the presence
    // of bundle.json The following changes are made:
    // - library view is omitted
    // - credits accessible via setings
    // - no per-game/global settings distinction
    // - some settings become inaccessible
    bool migration_modal_needed;
    char* bundled_rom;  // (path to bundled rom)
    int bundled_rom_cgb_mode; // 0: unspecified. 1: force dmg. 2: force cgb.
} CB_Application;

extern CB_Application* CB_App;
extern AudioSyncBuffer g_audio_sync_buffer;
extern atomic_uint g_samples_generated_total;

void CB_init(void);
void CB_event(PDSystemEvent event, uint32_t arg);
void CB_update(float dt);
void CB_present(CB_Scene* scene);
void CB_quit(void);
void CB_goToLibrary(void);
void CB_presentModal(CB_Scene* scene);
void CB_dismiss(CB_Scene* scene);
void CB_headphone_state_changed(int headphone, int mic);
void CB_showHelp(bool first_time);

// allocates in DTCM region (if enabled).
// note, there is no associated free.
void* dtcm_alloc(size_t size);

// returns NULL if was not booted by pdboot.
const char* get_pdboot_name_and_version(void);

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
    )
#else
#define __shell __attribute((noinline)) __section__(".text.cb")
#endif
#endif

// don't exceed 60 fps
#define CAP_FRAME_RATE 1

#define SAVE_STATE_SLOT_COUNT 10
#define SAVE_SLOT_COUNT 10
#define SAVE_STATE_THUMBNAIL_W 160
#define SAVE_STATE_THUMBNAIL_H 144

// for playdate extension crank menu IO register;
// how far one has to turn the crank before getting to the next menu item
#define CRANK_MENU_DELTA_BINANGLE 0x2800

#define THUMBNAIL_WIDTH 240
#define THUMBNAIL_HEIGHT 240

// files that have been copied from PDX to data folder
#define COPIED_FILES "manifest.json"
#define PATCH_LIST_FILE "manifest.json"
#define VERSION_INFO_FILE "version.json"
#define BUNDLE_FILE "bundle.json"
#define ROMHACK_DB_FILE "rhdb.json"
#define DIRECTORY_POINTER "directory.txt"
#define GLOBAL_FILE "global.json"
#define LAST_SELECTED_FILE "library_last_selected.txt"
#define HOMEBREW_HUB_API_FILE "hbapi.txt"

#define DEFAULT_SHARED_DIRECTORY "/Shared/Emulation/gb"
#define PDX_BUNDLE_ID "app.crankboyhq.crankboy"

#define DISK_IMAGE "__homebrew_dl_img.pdi"

// for files which should only appear in data unless we're in bundle mode
#define kFileReadDataOrBundle (CB_App->bundled_rom ? (kFileRead | kFileReadData) : kFileReadData)

#endif /* app_h */
