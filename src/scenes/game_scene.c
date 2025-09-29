//
//  game_scene.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#include <stdbool.h>
#include "pd_api.h"

unsigned game_picture_x_offset;
unsigned game_picture_y_top;
unsigned game_picture_y_bottom;
unsigned game_picture_scaling;
LCDColor game_picture_background_color;
bool game_hide_indicator;
bool gbScreenRequiresFullRefresh;

#define PGB_IMPL
#include "game_scene.h"

#include "../../libs/minigb_apu/minigb_apu.h"
#include "../../libs/peanut_gb.h"
#include "../app.h"
#include "../dtcm.h"
#include "../preferences.h"
#include "../revcheck.h"
#include "../scenes/modal.h"
#include "../script.h"
#include "../softpatch.h"
#include "../userstack.h"
#include "../utility.h"
#include "credits_scene.h"
#include "info_scene.h"
#include "library_scene.h"
#include "settings_scene.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The maximum Playdate screen lines that can be updated (seems to be 208).
#define PLAYDATE_LINE_COUNT_MAX 208

// --- Parameters for the "Tendency Counter" Auto-Interlace System ---

// The tendency counter's ceiling. Higher values add more inertia.
#define INTERLACE_TENDENCY_MAX 10

// Counter threshold to activate interlacing. Lower is more reactive.
#define INTERLACE_TENDENCY_TRIGGER_ON 5

// Hysteresis floor; interlacing stays on until the counter drops below this.
#define INTERLACE_TENDENCY_TRIGGER_OFF 3

// --- Parameters for the Adaptive "Grace Period Lock" ---

// Defines the [min, max] frame range for the adaptive lock.
// A lower user sensitivity setting results in a longer lock duration (closer to MAX).
#define INTERLACE_LOCK_DURATION_MAX 60
#define INTERLACE_LOCK_DURATION_MIN 1

// Enables console logging for the dirty line update mechanism.
// WARNING: Performance-intensive. Use for debugging only.
#define LOG_DIRTY_LINES 0

CB_GameScene* audioGameScene = NULL;

void CB_reset_audio_sync_state(void)
{
    atomic_store(&g_audio_sync_buffer.read_pos, 0);
    atomic_store(&g_audio_sync_buffer.write_pos, 0);
    atomic_store(&g_samples_generated_total, playdate->sound->getCurrentTime());
}

static void generate_audio_chunk(CB_GameScene* gameScene, int samples_to_generate)
{
    if (samples_to_generate <= 0)
        return;

    audio_data* audio = &gameScene->context->gb->audio;

    int16_t* temp_left = cb_malloc(samples_to_generate * sizeof(int16_t));
    int16_t* temp_right =
        gameScene->is_stereo ? cb_malloc(samples_to_generate * sizeof(int16_t)) : temp_left;

    memset(temp_left, 0, samples_to_generate * sizeof(int16_t));
    if (gameScene->is_stereo)
        memset(temp_right, 0, samples_to_generate * sizeof(int16_t));

    audio_update_wave(audio, temp_left, temp_right, samples_to_generate);
    audio_update_square(audio, temp_left, temp_right, 0, samples_to_generate);
    audio_update_square(audio, temp_left, temp_right, 1, samples_to_generate);
    audio_update_noise(audio, temp_left, temp_right, samples_to_generate);

    uint32_t write_pos_local = atomic_load(&g_audio_sync_buffer.write_pos);
    for (int i = 0; i < samples_to_generate; ++i)
    {
        uint32_t current_pos = (write_pos_local + i) % AUDIO_RING_BUFFER_SIZE;
        g_audio_sync_buffer.left[current_pos] = temp_left[i];
        if (gameScene->is_stereo)
            g_audio_sync_buffer.right[current_pos] = temp_right[i];
    }

    atomic_fetch_add(&g_audio_sync_buffer.write_pos, samples_to_generate);
    cb_free(temp_left);
    if (gameScene->is_stereo)
        cb_free(temp_right);
}

static void tick_audio_sync(CB_GameScene* gameScene)
{
    if (!gameScene || !gameScene->audioEnabled || gameScene->audioLocked ||
        preferences_audio_sync != 1)
    {
        return;
    }

    uint32_t samples_played = playdate->sound->getCurrentTime();
    uint32_t samples_generated = atomic_load(&g_samples_generated_total);

    // Target having a buffer of ~3 frames of audio (at 60fps)
    uint32_t target_lead_samples = (44100 / 60) * 3;
    uint32_t target_sample_count = samples_played + target_lead_samples;

    int samples_to_generate = 0;
    if (target_sample_count > samples_generated)
    {
        samples_to_generate = target_sample_count - samples_generated;
    }

    int max_gen_this_frame = (44100 / 60) * 4;
    if (samples_to_generate > max_gen_this_frame)
    {
        samples_to_generate = max_gen_this_frame;
    }

    if (samples_to_generate > 0)
    {
        uint32_t write_pos = atomic_load(&g_audio_sync_buffer.write_pos);
        uint32_t read_pos = atomic_load(&g_audio_sync_buffer.read_pos);
        uint32_t available_space = AUDIO_RING_BUFFER_SIZE - (write_pos - read_pos);

        if (samples_to_generate < available_space)
        {
            generate_audio_chunk(gameScene, samples_to_generate);
            atomic_fetch_add(&g_samples_generated_total, samples_to_generate);
        }
    }
}

static void CB_GameScene_selector_init(CB_GameScene* gameScene);
static void CB_GameScene_update(void* object, uint32_t u32enc_dt);
static void CB_GameScene_menu(void* object);
static void CB_GameScene_generateBitmask(void);
static void CB_GameScene_free(void* object);
static void CB_GameScene_event(void* object, PDSystemEvent event, uint32_t arg);
static bool CB_GameScene_lock(void* object);

static uint8_t* read_rom_to_ram(
    const char* filename, CB_GameSceneError* sceneError, size_t* o_rom_size
);

// returns 0 if no pre-existing save data;
// returns 1 if data found and loaded, but not RTC
// returns 2 if data and RTC loaded
// returns -1 on error
static int read_cart_ram_file(const char* save_filename, gb_s* gb, unsigned int* last_save_time);
static void write_cart_ram_file(const char* save_filename, gb_s* gb);

static void gb_error(gb_s* gb, const enum gb_error_e gb_err, const uint16_t val);
static void gb_save_to_disk(gb_s* gb);

static const char* startButtonText = "start";
static const char* selectButtonText = "select";

static int last_scy = -1;
static uint8_t CB_dither_lut_row0[256];
static uint8_t CB_dither_lut_row1[256];

const uint16_t CB_dither_lut_c0[] = {
    (0b1111 << 0) | (0b0111 << 4) | (0b0001 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),

    // L
    (0b1111 << 0) | (0b0111 << 4) | (0b0101 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),

    // D
    (0b1111 << 0) | (0b0101 << 4) | (0b0001 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),
};

const uint16_t CB_dither_lut_c1[] = {
    (0b1111 << 0) | (0b1101 << 4) | (0b0100 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b1111 << 4) | (0b0000 << 8) | (0b0000 << 12),

    // L
    (0b1111 << 0) | (0b1101 << 4) | (0b1010 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b1111 << 4) | (0b1010 << 8) | (0b0000 << 12),

    // D
    (0b1111 << 0) | (0b1010 << 4) | (0b0100 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b1010 << 4) | (0b0000 << 8) | (0b0000 << 12),
};

__section__(".rare") static void generate_dither_luts(void)
{
    uint32_t dither_lut = CB_dither_lut_c0[preferences_dither_pattern] |
                          ((uint32_t)CB_dither_lut_c1[preferences_dither_pattern] << 16);

    // Loop through all 256 possible values of a 4-pixel Game Boy byte.
    for (int orgpixels_int = 0; orgpixels_int < 256; ++orgpixels_int)
    {
        uint8_t orgpixels = (uint8_t)orgpixels_int;

        // --- Calculate dithered pattern for the first (top) row of pixels ---
        uint8_t pixels_temp_c0 = orgpixels;
        unsigned p0 = 0;
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i)
        {
            p0 <<= 2;
            unsigned c0h = dither_lut >> ((pixels_temp_c0 & 3) * 4);
            unsigned c0 = (c0h >> ((i * 2) % 4)) & 3;
            p0 |= c0;
            pixels_temp_c0 >>= 2;
        }
        CB_dither_lut_row0[orgpixels_int] = p0;

        // --- Calculate dithered pattern for the second (bottom) row of pixels ---
        uint8_t pixels_temp_c1 = orgpixels;
        unsigned p1 = 0;
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i)
        {
            p1 <<= 2;
            unsigned c1h = dither_lut >> (((pixels_temp_c1 & 3) * 4) + 16);
            unsigned c1 = (c1h >> ((i * 2) % 4)) & 3;
            p1 |= c1;
            pixels_temp_c1 >>= 2;
        }
        CB_dither_lut_row1[orgpixels_int] = p1;
    }
}

static uint8_t g_blend_lut[2][256][256];
static bool g_blend_lut_generated = false;

__section__(".rare") static void generate_blend_lut(void)
{
    if (g_blend_lut_generated)
    {
        return;
    }

    static const uint8_t dither_patterns[7][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 2},
                                                  {2, 2}, {2, 3}, {3, 3}};

    for (int y_is_odd = 0; y_is_odd < 2; y_is_odd++)
    {
        for (int a_byte_int = 0; a_byte_int < 256; a_byte_int++)
        {
            for (int b_byte_int = 0; b_byte_int < 256; b_byte_int++)
            {
                uint8_t a_byte = (uint8_t)a_byte_int;
                uint8_t b_byte = (uint8_t)b_byte_int;
                uint8_t blended_byte = 0;

                uint8_t pa0 = (a_byte >> 0) & 3;
                uint8_t pb0 = (b_byte >> 0) & 3;
                blended_byte |= dither_patterns[pa0 + pb0][(y_is_odd + 0) & 1] << 0;

                uint8_t pa1 = (a_byte >> 2) & 3;
                uint8_t pb1 = (b_byte >> 2) & 3;
                blended_byte |= dither_patterns[pa1 + pb1][(y_is_odd + 1) & 1] << 2;

                uint8_t pa2 = (a_byte >> 4) & 3;
                uint8_t pb2 = (b_byte >> 4) & 3;
                blended_byte |= dither_patterns[pa2 + pb2][(y_is_odd + 2) & 1] << 4;

                uint8_t pa3 = (a_byte >> 6) & 3;
                uint8_t pb3 = (b_byte >> 6) & 3;
                blended_byte |= dither_patterns[pa3 + pb3][(y_is_odd + 3) & 1] << 6;

                g_blend_lut[y_is_odd][a_byte_int][b_byte_int] = blended_byte;
            }
        }
    }
    g_blend_lut_generated = true;
}

// forces screen refresh
bool game_menu_button_input_enabled;

static uint8_t CB_bitmask[4][4][4];
static bool CB_GameScene_bitmask_done = false;

static PDMenuItem* audioMenuItem;
static PDMenuItem* fpsMenuItem;
static PDMenuItem* frameSkipMenuItem;
static PDMenuItem* buttonMenuItem = NULL;

static const char* buttonMenuOptions[] = {
    "Select",
    "None",
    "Start",
    "Both",
};

static const char* quitGameOptions[] = {"No", "Yes", NULL};

#if ENABLE_RENDER_PROFILER
static bool CB_run_profiler_on_next_frame = false;
#endif

void reconfigure_audio_source(CB_GameScene* gameScene, int headphones)
{
    if (!gameScene)
        return;

    bool use_stereo = (headphones || gameScene->is_mirroring) ? preferences_headphone_audio : 0;

    playdate->system->logToConsole(
        "Reconfiguring audio. Headphones: %s, Mirroring: %s, New mode: %s",
        (headphones ? "Yes" : "No"), (gameScene->is_mirroring ? "Yes" : "No"),
        (use_stereo ? "Stereo" : "Mono")
    );

    gameScene->is_stereo = use_stereo;

    if (gameScene->audioEnabled)
    {
        float volume = use_stereo ? 0.2f : 0.4f;
        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), volume);
    }

    if (CB_App->soundSource != NULL)
    {
        playdate->sound->removeSource(CB_App->soundSource);
    }

    CB_App->soundSource = playdate->sound->addSource(audio_callback, &audioGameScene, use_stereo);

    if (headphones)
    {
        playdate->sound->setOutputsActive(1, 0);
    }
    else
    {
        playdate->sound->setOutputsActive(0, 1);
    }
}

#if ITCM_CORE
void* core_itcm_reloc = NULL;
intptr_t core_itcm_offset = 0;

extern char __itcm_dmg_start[];
extern char __itcm_dmg_end[];
extern char __itcm_cgb_start[];
extern char __itcm_cgb_end[];

#define ITCM_CORE_FN(fn) \
    ((void*)((uintptr_t)(void*)fn + core_itcm_offset))

__section__(".rare") void itcm_core_init(bool cgb)
{
    void* itcm_start = cgb
        ? &__itcm_cgb_start
        : &__itcm_dmg_start;
        
    void* itcm_end = cgb
        ? &__itcm_cgb_end
        : &__itcm_dmg_end;
        
    uintptr_t core_size = itcm_end - itcm_start;
    
    // ITCM seems to crash Rev B (not anymore it seems), so we leave this is an option
    if (!dtcm_enabled() || !preferences_itcm)
    {
        // just use original non-relocated code
        core_itcm_reloc = itcm_start;
        core_itcm_offset = 0;
        
        playdate->system->logToConsole("itcm_core_init but dtcm not enabled");
        return;
    }

    if (core_itcm_reloc == (void*)&__itcm_dmg_start)
        core_itcm_reloc = NULL;
    
    if (core_itcm_reloc == (void*)&__itcm_cgb_start)
        core_itcm_reloc = NULL;

    if (core_itcm_reloc != NULL)
        return;

    // paranoia
    int MARGIN = 4;

    // make region to copy instructions to; ensure it has same cache alignment
    core_itcm_reloc = dtcm_alloc_aligned(core_size + MARGIN, (uintptr_t)itcm_start);
    DTCM_VERIFY();
    memcpy(core_itcm_reloc, (void*)itcm_start, core_size);
    DTCM_VERIFY();
    core_itcm_offset = core_itcm_reloc - itcm_start;
    playdate->system->logToConsole(
        "itcm start: %p, end %p: (%s)", itcm_start, itcm_end, cgb ? "cgb" : "dmg"
    );
    playdate->system->logToConsole(
        "core is 0x%X bytes, relocated at %p", core_size, core_itcm_reloc
    );
    playdate->system->clearICache();
}
#else

#define ITCM_CORE_FN(fn) fn

void itcm_core_init(bool cgb)
{
}
#endif

static bool CB_GameScene_complete_successful_init(CB_GameScene* gameScene)
{
    CB_GameSceneContext* context = gameScene->context;

    gb_reset(context->gb, context->cgb_mode);

    context->gb->direct.joypad_interrupt_delay = -1;

    playdate->system->logToConsole("Initialized gb context.");
    char* save_filename = cb_save_filename(gameScene->rom_filename, false);
    gameScene->save_filename = save_filename;

    gameScene->base_filename = cb_basename(gameScene->rom_filename, true);

    gameScene->cartridge_has_battery = context->gb->cart_battery;
    gameScene->save_state_requires_warning = context->gb->cart_battery;
    playdate->system->logToConsole(
        "Cartridge has battery: %s", gameScene->cartridge_has_battery ? "Yes" : "No"
    );

    gameScene->last_save_time = 0;

    int ram_load_result =
        read_cart_ram_file(save_filename, context->gb, &gameScene->last_save_time);

    switch (ram_load_result)
    {
    case 0:
        playdate->system->logToConsole("No previous cartridge save data found");
        break;
    case 1:
    case 2:
        playdate->system->logToConsole("Loaded cartridge save data");
        break;
    default:
    {
        if (context->gb && context->gb->gb_cart_ram)
        {
            cb_free(context->gb->gb_cart_ram);
            context->gb->gb_cart_ram = NULL;
        }

        gameScene->error = CB_GameSceneErrorSaveData;
        return false;
    }
    }

    context->cart_ram = context->gb->gb_cart_ram;
    gameScene->save_data_loaded_successfully = true;

    unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);
    gameScene->rtc_time = now;
    gameScene->rtc_seconds_to_catch_up = 0;

    gameScene->cartridge_has_rtc = (context->gb->mbc == 3 && context->gb->cart_battery);

    if (gameScene->cartridge_has_rtc)
    {
        playdate->system->logToConsole("Cartridge is MBC3 with battery: RTC Enabled.");

        if (ram_load_result == 2)
        {
            playdate->system->logToConsole("Loaded RTC state and timestamp from save file.");

            if (now > gameScene->last_save_time)
            {
                unsigned int seconds_to_advance = now - gameScene->last_save_time;
                if (seconds_to_advance > 0)
                {
                    playdate->system->logToConsole(
                        "Catching up RTC by %u seconds...", seconds_to_advance
                    );
                    gb_catch_up_rtc_direct(context->gb, seconds_to_advance);
                }
            }
        }
        else
        {
            playdate->system->logToConsole(
                "No valid RTC save data. Initializing clock to system "
                "time."
            );
            time_t time_for_core = gameScene->rtc_time + 946684800;
            struct tm* timeinfo = localtime(&time_for_core);
            if (timeinfo != NULL)
            {
                gb_set_rtc(context->gb, timeinfo);
            }
        }
    }
    return true;
}

// Helper function to generate the config file path for a game
char* cb_game_config_path(const char* rom_filename)
{
    char* basename = cb_basename(rom_filename, true);
    char* path;
    playdate->system->formatString(&path, "%s/%s.json", cb_gb_directory_path(CB_settingsPath), basename);
    cb_free(basename);
    return path;
}

static LCDBitmap* numbers_bmp = NULL;
static uint32_t last_fps_digits;
static uint8_t fps_draw_timer;

CB_GameScene* CB_GameScene_new(const char* rom_filename, char* name_short, bool cgb_mode)
{
    // Seed the random number generator to ensure joypad interrupt timing is unpredictable.
    srand(time(NULL));

    last_scy = -1;

    playdate->system->logToConsole("ROM: %s", rom_filename);

    if (!numbers_bmp)
    {
        numbers_bmp = playdate->graphics->loadBitmap("fonts/numbers", NULL);
    }

    if (!DTCM_VERIFY_DEBUG())
        return NULL;

    game_picture_x_offset = CB_LCD_X;
    game_picture_scaling = 3;
    game_picture_y_top = 0;
    game_picture_y_bottom = LCD_HEIGHT;
    game_picture_background_color = kColorBlack;
    game_hide_indicator = false;
    game_menu_button_input_enabled = 1;

    CB_Scene* scene = CB_Scene_new();

    CB_GameScene* gameScene = allocz(CB_GameScene);
    gameScene->scene = scene;
    scene->managedObject = gameScene;

    scene->update = CB_GameScene_update;
    scene->menu = CB_GameScene_menu;
    scene->free = CB_GameScene_free;
    scene->event = CB_GameScene_event;
    scene->lock = CB_GameScene_lock;
    scene->use_user_stack = 0;  // user stack is slower

    scene->preferredRefreshRate = 30;

    gameScene->rom_filename = cb_strdup(rom_filename);
    gameScene->name_short = cb_strdup(name_short);
    gameScene->save_filename = NULL;

    gameScene->state = CB_GameSceneStateError;
    gameScene->error = CB_GameSceneErrorUndefined;

    gameScene->model = (CB_GameSceneModel){.state = CB_GameSceneStateError,
                                           .error = CB_GameSceneErrorUndefined,
                                           .selectorIndex = 0,
                                           .empty = true};

    gameScene->audioEnabled = (preferences_sound_mode > 0);
    gameScene->audioLocked = false;
    gameScene->button_hold_mode = 1;  // None
    gameScene->button_hold_frames_remaining = 0;

    gameScene->previous_joypad_state = 0xFF;

    gameScene->crank_turbo_accumulator = 0.0f;
    gameScene->crank_turbo_a_active = false;
    gameScene->crank_turbo_b_active = false;
    gameScene->crank_was_docked = playdate->system->isCrankDocked();

    gameScene->interlace_tendency_counter = 0;
    gameScene->interlace_lock_frames_remaining = 0;

    gameScene->isCurrentlySaving = false;
    gameScene->is_mirroring = false;

    gameScene->menuImage = NULL;

    gameScene->staticSelectorUIDrawn = false;

    gameScene->save_data_loaded_successfully = false;

    prefs_locked_by_script = 0;

    // Global settings are loaded by default. Check for a game-specific file.
    gameScene->settings_filename = cb_game_config_path(rom_filename);

    if (!CB_App->bundled_rom)
    {
        // Try loading game-specific preferences
        preferences_per_game = 0;

        // Store the global UI sound setting so it isn't overwritten by game-specific settings.
        void* stored_global = preferences_store_subset(PREFBITS_ALWAYS_GLOBAL);

        // FIXME: shouldn't we be using call_with_main_stack for these?
        call_with_user_stack_1(preferences_read_from_disk, gameScene->settings_filename);

        // we always use the per-game save slot, even if global settings are enabled
        void* stored_save_slot = preferences_store_subset(PREFBITS_NEVER_GLOBAL);

        // If the game-specific settings explicitly says "use Global"
        // (or there is no game-specific settings file),
        // load the global preferences file instead.
        if (preferences_per_game == 0)
        {
            call_with_user_stack_1(preferences_read_from_disk, CB_globalPrefsPath);
        }

        // re-apply never-global settings
        if (stored_save_slot)
        {
            preferences_restore_subset(stored_save_slot);
            cb_free(stored_save_slot);
        }

        // Restore the global UI sound setting after loading any other preferences.
        if (stored_global)
        {
            preferences_restore_subset(stored_global);
            cb_free(stored_global);
        }
    }
    else
    {
        // bundled ROMs always use global preferences
        call_with_user_stack_1(preferences_read_from_disk, CB_globalPrefsPath);
    }

    CB_GameScene_generateBitmask();

    generate_dither_luts();
    generate_blend_lut();

    CB_GameScene_selector_init(gameScene);

#if CB_DEBUG && CB_DEBUG_UPDATED_ROWS
    int highlightWidth = 10;
    gameScene->debug_highlightFrame = PDRectMake(
        CB_LCD_X - 1 - highlightWidth, 0, highlightWidth, playdate->display->getHeight()
    );
#endif

#if ITCM_CORE
    core_itcm_reloc = NULL;
#endif
    dtcm_deinit();
    dtcm_init();

    DTCM_VERIFY();

    CB_GameSceneContext* context = allocz(CB_GameSceneContext);
    static gb_s gb_fallback;  // use this gb struct if dtcm alloc not available. Also during initialization.
    context->gb = &gb_fallback;
    context->cgb_mode = cgb_mode;

    DTCM_VERIFY();
    memset(context->gb, 0, sizeof(gb_s));
    DTCM_VERIFY();

    audio_enabled = 1;
    context->scene = gameScene;
    context->rom = NULL;
    context->cart_ram = NULL;

    gameScene->context = context;

    CB_GameSceneError romError;
    size_t rom_size;
    uint8_t* rom = read_rom_to_ram(rom_filename, &romError, &rom_size);
    DTCM_VERIFY();
    if (rom)
    {
        playdate->system->logToConsole("Opened ROM.");

        // try patches
        SoftPatch* patches = list_patches(rom_filename, NULL);
        if (patches)
        {
            printf("softpatching ROM...\n");
            bool result = call_with_main_stack_3(patch_rom, (void*)&rom, &rom_size, patches);
            gameScene->patches_hash = patch_hash(patches);

            free_patches(patches);
        }

        context->rom = rom;
        context->rom_size = rom_size;

        static clalign uint8_t lcd[LCD_BUFFER_BYTES];
        memset(lcd, 0, sizeof(lcd));
        
        gameScene->cgb_compatible = (gb_get_models_supported(rom) & GB_SUPPORT_CGB);
        gameScene->dmg_compatible = (gb_get_models_supported(rom) & GB_SUPPORT_DMG);

        enum gb_init_error_e gb_ret = gb_init(
            context->gb, context->wram, context->vram, lcd, rom, rom_size, gb_error, context, cgb_mode
        );

        CB_ASSERT((((uintptr_t)context->gb->lcd) & 7) == 0);
        CB_ASSERT((((uintptr_t)context->previous_lcd) & 7) == 0);

        if (gb_ret == GB_INIT_NO_ERROR || gb_ret == GB_INIT_NO_ERROR_BUT_REQUIRES_CGB)
        {
            if (!CB_GameScene_complete_successful_init(gameScene))
            {
                gameScene->state = CB_GameSceneStateError;
            }
            else
            {
                DTCM_VERIFY();

                audio_init(&context->gb->audio);
                CB_GameScene_apply_settings(gameScene, true);
                CB_reset_audio_sync_state();

                gb_init_lcd(context->gb);
                memset(context->previous_lcd, 0, sizeof(context->previous_lcd));
                gameScene->state = CB_GameSceneStateLoaded;

                playdate->system->logToConsole("gb context initialized.");
            }
        
            if (dtcm_enabled())
            {
                context->gb = dtcm_alloc(sizeof(gb_s));
                memcpy(context->gb, &gb_fallback, sizeof(gb_s));
            }
            
            itcm_core_init(context->gb->is_cgb_mode);
        }
        else
        {
            playdate->system->logToConsole("Failed to initialize ROM.");
            gameScene->state = CB_GameSceneStateError;
            gameScene->error = CB_GameSceneErrorFatal;
            return gameScene;
        }
    }
    else
    {
        playdate->system->logToConsole("Failed to open ROM.");
        gameScene->state = CB_GameSceneStateError;
        gameScene->error = romError;
        return gameScene;
    }

    gameScene->script_available = false;
    gameScene->script_info_available = false;
#ifndef NOLUA
    ScriptInfo* scriptInfo = script_get_info_by_rom_path(gameScene->rom_filename);
    if (scriptInfo)
    {
        gameScene->script_available = true;
        gameScene->script_info_available = !!scriptInfo->info;
    }

    if (preferences_script_support && gameScene->script_available && scriptInfo)
    {
        playdate->system->logToConsole("ROM name: \"%s\"", scriptInfo->rom_name);
        gameScene->script = script_begin(scriptInfo->rom_name, gameScene);
        gameScene->prev_dt = 0;
        if (!gameScene->script)
        {
            playdate->system->logToConsole("Associated script failed to load or not found.");
        }
    }
    script_info_free(scriptInfo);
#endif
    DTCM_VERIFY();

    CB_ASSERT(gameScene->context == context);
    CB_ASSERT(gameScene->context->scene == gameScene);
    CB_ASSERT(gameScene->context->gb->direct.priv == context);

    return gameScene;
}

void CB_GameScene_apply_settings(CB_GameScene* gameScene, bool audio_settings_changed)
{
    CB_GameSceneContext* context = gameScene->context;

    generate_dither_luts();

    if (audio_settings_changed)
    {
        int headphones = 0;
        playdate->sound->getHeadphoneState(&headphones, NULL, CB_headphone_state_changed);
        reconfigure_audio_source(gameScene, headphones);
    }

    bool desiredAudioEnabled = (preferences_sound_mode > 0);
    gameScene->audioEnabled = desiredAudioEnabled;

    if (desiredAudioEnabled)
    {
        float volume = gameScene->is_stereo ? 0.2f : 0.4f;
        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), volume);
        context->gb->direct.sound = 1;
        audioGameScene = gameScene;
    }
    else
    {
        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 0.0f);
        context->gb->direct.sound = 0;
        audioGameScene = NULL;
    }

    // If the buffered audio sync is NOT the active mode, we MUST ensure
    // its buffer is cleared. This handles the case where a user disables
    // the feature mid-game, preventing stale audio from persisting.
    if (preferences_audio_sync != 1)
    {
        CB_reset_audio_sync_state();
        memset(g_audio_sync_buffer.left, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
        memset(g_audio_sync_buffer.right, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
    }

    if (preferences_crank_down_action == 0)
    {
        gameScene->selector.deadAngle = 45;
    }
    else
    {
        gameScene->selector.deadAngle = 20;
    }

    playdate->system->setAutoLockDisabled(preferences_disable_autolock);
}

static void CB_GameScene_selector_init(CB_GameScene* gameScene)
{
    int startButtonWidth = playdate->graphics->getTextWidth(
        CB_App->labelFont, startButtonText, strlen(startButtonText), kUTF8Encoding, 0
    );
    int selectButtonWidth = playdate->graphics->getTextWidth(
        CB_App->labelFont, selectButtonText, strlen(selectButtonText), kUTF8Encoding, 0
    );

    int width = 18;
    int height = 46;

    int startSpacing = 3;
    int selectSpacing = 6;

    int labelHeight = playdate->graphics->getFontHeight(CB_App->labelFont);

    int containerHeight = labelHeight + startSpacing + height + selectSpacing + labelHeight;

    int containerWidth = width;
    containerWidth = CB_MAX(containerWidth, startButtonWidth);
    containerWidth = CB_MAX(containerWidth, selectButtonWidth);

    const int rightBarX = 40 + 320;
    const int rightBarWidth = 40;

    int containerX = rightBarX + (rightBarWidth - containerWidth) / 2 - 1;
    int containerY = 8;
    int x = containerX + (containerWidth - width) / 2;
    int y = containerY + labelHeight + startSpacing;

    int startButtonX = rightBarX + (rightBarWidth - startButtonWidth) / 2;
    int startButtonY = containerY;

    int selectButtonX = rightBarX + (rightBarWidth - selectButtonWidth) / 2;
    int selectButtonY = containerY + containerHeight - labelHeight;

    gameScene->selector.x = x;
    gameScene->selector.y = y;
    gameScene->selector.width = width;
    gameScene->selector.height = height;
    gameScene->selector.containerX = containerX;
    gameScene->selector.containerY = containerY;
    gameScene->selector.containerWidth = containerWidth;
    gameScene->selector.containerHeight = containerHeight;
    gameScene->selector.startButtonX = startButtonX;
    gameScene->selector.startButtonY = startButtonY;
    gameScene->selector.selectButtonX = selectButtonX;
    gameScene->selector.selectButtonY = selectButtonY;
    gameScene->selector.numberOfFrames = 27;
    gameScene->selector.triggerAngle = 45;
    gameScene->selector.index = 0;
    gameScene->selector.startPressed = false;
    gameScene->selector.selectPressed = false;
}

/**
 * Returns a pointer to the allocated space containing the ROM. Must be freed.
 */
static uint8_t* read_rom_to_ram(
    const char* filename, CB_GameSceneError* sceneError, size_t* o_rom_size
)
{
    *sceneError = CB_GameSceneErrorUndefined;

    SDFile* rom_file = playdate->file->open(filename, kFileReadDataOrBundle);

    if (rom_file == NULL)
    {
        const char* fileError = playdate->file->geterr();
        playdate->system->logToConsole(
            "%s:%i: Can't open rom file %s", __FILE__, __LINE__, filename
        );
        playdate->system->logToConsole("%s:%i: File error %s", __FILE__, __LINE__, fileError);

        *sceneError = CB_GameSceneErrorLoadingRom;

        if (fileError)
        {
            char* fsErrorCode = cb_extract_fs_error_code(fileError);
            if (fsErrorCode)
            {
                if (strcmp(fsErrorCode, "0709") == 0)
                {
                    *sceneError = CB_GameSceneErrorWrongLocation;
                }
            }
        }
        return NULL;
    }

    playdate->file->seek(rom_file, 0, SEEK_END);
    int rom_size = playdate->file->tell(rom_file);
    *o_rom_size = rom_size;
    playdate->file->seek(rom_file, 0, SEEK_SET);

    uint8_t* rom = cb_malloc(rom_size);

    if (playdate->file->read(rom_file, rom, rom_size) != rom_size)
    {
        playdate->system->logToConsole(
            "%s:%i: Can't read rom file %s", __FILE__, __LINE__, filename
        );

        cb_free(rom);
        playdate->file->close(rom_file);
        *sceneError = CB_GameSceneErrorLoadingRom;
        return NULL;
    }

    playdate->file->close(rom_file);
    return rom;
}

static int read_cart_ram_file(const char* save_filename, gb_s* gb, unsigned int* last_save_time)
{
    *last_save_time = 0;

    const size_t sram_len = gb_get_save_size(gb);

    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    gb->gb_cart_ram = (sram_len > 0) ? cb_malloc(sram_len) : NULL;
    if (gb->gb_cart_ram)
    {
        memset(gb->gb_cart_ram, 0, sram_len);
    }
    gb->gb_cart_ram_size = sram_len;

    SDFile* f = playdate->file->open(save_filename, kFileReadData);
    if (f == NULL)
    {
        // We assume this only happens if file does not exist
        return 0;
    }

    if (sram_len > 0)
    {
        int read = playdate->file->read(f, gb->gb_cart_ram, (unsigned int)sram_len);
        if (read != sram_len)
        {
            playdate->system->logToConsole("Failed to read save data");
            playdate->file->close(f);
            return -1;
        }
    }

    int code = 1;
    if (gameScene->cartridge_has_battery)
    {
        if (playdate->file->read(f, gb->cart_rtc, sizeof(gb->cart_rtc)) == sizeof(gb->cart_rtc))
        {
            if (playdate->file->read(f, last_save_time, sizeof(unsigned int)) ==
                sizeof(unsigned int))
            {
                code = 2;
            }
        }
    }

    playdate->file->close(f);
    return code;
}

static void write_cart_ram_file(const char* save_filename, gb_s* gb)
{
    // Get the size of the save RAM from the gb context.
    const size_t sram_len = gb_get_save_size(gb);
    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    // If there is no battery, exit.
    if (!gameScene->cartridge_has_battery)
    {
        return;
    }

    // Generate .tmp and .bak filenames
    size_t len = strlen(save_filename);
    char* tmp_filename = cb_malloc(len + 2);
    char* bak_filename = cb_malloc(len + 2);

    if (!tmp_filename || !bak_filename)
    {
        playdate->system->logToConsole("Error: Failed to allocate memory for safe save filenames.");
        goto cleanup;
    }

    strcpy(tmp_filename, save_filename);
    strcpy(bak_filename, save_filename);

    char* ext_tmp = strrchr(tmp_filename, '.');
    if (ext_tmp && strcmp(ext_tmp, ".sav") == 0)
    {
        strcpy(ext_tmp, ".tmp");
    }
    else
    {
        strcat(tmp_filename, ".tmp");
    }

    char* ext_bak = strrchr(bak_filename, '.');
    if (ext_bak && strcmp(ext_bak, ".sav") == 0)
    {
        strcpy(ext_bak, ".bak");
    }
    else
    {
        strcat(bak_filename, ".bak");
    }

    playdate->file->unlink(tmp_filename, false);

    // Write data to the temporary file
    playdate->system->logToConsole("Saving to temporary file: %s", tmp_filename);
    SDFile* f = playdate->file->open(tmp_filename, kFileWrite);
    if (f == NULL)
    {
        playdate->system->logToConsole(
            "Error: Can't open temp save file for writing: %s", tmp_filename
        );
        goto cleanup;
    }

    if (sram_len > 0 && gb->gb_cart_ram != NULL)
    {
        playdate->file->write(f, gb->gb_cart_ram, (unsigned int)sram_len);
    }

    // write rtc
    playdate->file->write(f, gb->cart_rtc, sizeof(gb->cart_rtc));
    
    // write timestamp
    unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);
    gameScene->last_save_time = now;
    playdate->file->write(f, &now, sizeof(now));
    
    // write flags
    uint32_t flags = !!gameScene->script;
    playdate->file->write(f, &flags, sizeof(flags));
    
    // write patch hash
    playdate->file->write(f, &gameScene->patches_hash, sizeof(gameScene->patches_hash));
    
    // write magic number (must be at end of file)
    uint64_t magic = SRAM_MAGIC_NUMBER;
    playdate->file->write(f, &magic, sizeof(magic));

    playdate->file->close(f);

    // Verify that the temporary file is not zero-bytes
    FileStat stat;
    if (playdate->file->stat(tmp_filename, &stat) != 0)
    {
        playdate->system->logToConsole(
            "Error: Failed to stat temp save file %s. Aborting save.", tmp_filename
        );
        playdate->file->unlink(tmp_filename, false);
        goto cleanup;
    }

    if (stat.size == 0)
    {
        playdate->system->logToConsole(
            "Error: Wrote 0-byte temp save file %s. Aborting and deleting.", tmp_filename
        );
        playdate->file->unlink(tmp_filename, false);
        goto cleanup;
    }

    // Rename files: .sav -> .bak, then .tmp -> .sav
    playdate->system->logToConsole("Save successful, renaming files.");

    playdate->file->unlink(bak_filename, false);
    playdate->file->rename(save_filename, bak_filename);

    if (playdate->file->rename(tmp_filename, save_filename) != 0)
    {
        playdate->system->logToConsole(
            "CRITICAL: Failed to rename temp file to save file. Restoring "
            "backup."
        );
        playdate->file->rename(bak_filename, save_filename);
    }

cleanup:
    if (tmp_filename)
        cb_free(tmp_filename);
    if (bak_filename)
        cb_free(bak_filename);
}

static void gb_save_to_disk_(gb_s* gb)
{
    DTCM_VERIFY_DEBUG();

    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    if (gameScene->isCurrentlySaving)
    {
        playdate->system->logToConsole("Save to disk skipped: another save is in progress.");
        return;
    }

    if (!context->gb->direct.sram_dirty)
    {
        return;
    }

    gameScene->isCurrentlySaving = true;

    if (gameScene->save_filename)
    {
        write_cart_ram_file(gameScene->save_filename, context->gb);
    }
    else
    {
        playdate->system->logToConsole("No save file name specified; can't save.");
    }

    context->gb->direct.sram_dirty = false;

    gameScene->isCurrentlySaving = false;

    DTCM_VERIFY_DEBUG();
}

static void gb_save_to_disk(gb_s* gb)
{
    call_with_main_stack_1(gb_save_to_disk_, gb);
}

/**
 * Handles an error reported by the emulator. The emulator context may be used
 * to better understand why the error given in gb_err was reported.
 */
static void gb_error(gb_s* gb, const enum gb_error_e gb_err, const uint16_t val)
{
    CB_GameSceneContext* context = gb->direct.priv;

    bool is_fatal = false;

    if (gb_err == GB_INVALID_OPCODE)
    {
        is_fatal = true;

        playdate->system->logToConsole(
            "%s:%i: Invalid opcode %#04x at PC: %#06x, SP: %#06x", __FILE__, __LINE__, val,
            gb->cpu_reg.pc - 1, gb->cpu_reg.sp
        );
    }
    else if (gb_err == GB_INVALID_READ)
    {
        #if 0
        playdate->system->logToConsole("Invalid read: addr %04x", val);
        #endif
    }
    else if (gb_err == GB_INVALID_WRITE)
    {
        #if 0
        playdate->system->logToConsole("Invalid write: addr %04x", val);
        #endif
    }
    else
    {
        is_fatal = true;
        playdate->system->logToConsole("%s:%i: Unknown error occurred", __FILE__, __LINE__);
    }

    if (is_fatal)
    {
        // save a recovery file
        if (context->scene->save_data_loaded_successfully)
        {
            char* recovery_filename = cb_save_filename(context->scene->rom_filename, true);
            write_cart_ram_file(recovery_filename, context->gb);
            cb_free(recovery_filename);
        }

        // TODO: write recovery savestate

        context->scene->state = CB_GameSceneStateError;
        context->scene->error = CB_GameSceneErrorFatal;

        CB_Scene_refreshMenu(context->scene->scene);
    }

    return;
}

static void blend_frames_lut(uint8_t* frame_a, uint8_t* frame_b_and_dest)
{
    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        uint8_t (*lut_row)[256] = g_blend_lut[y & 1];

        uint32_t* frame_a_32 = (uint32_t*)frame_a;
        uint32_t* frame_b_32 = (uint32_t*)frame_b_and_dest;

        for (int x = 0; x < LCD_WIDTH_PACKED / 4; x++)
        {
            uint32_t a_word = frame_a_32[x];
            uint32_t b_word = frame_b_32[x];

            uint8_t b0 = lut_row[(a_word >> 0) & 0xFF][(b_word >> 0) & 0xFF];
            uint8_t b1 = lut_row[(a_word >> 8) & 0xFF][(b_word >> 8) & 0xFF];
            uint8_t b2 = lut_row[(a_word >> 16) & 0xFF][(b_word >> 16) & 0xFF];
            uint8_t b3 = lut_row[(a_word >> 24) & 0xFF][(b_word >> 24) & 0xFF];

            uint32_t blended_word = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);

            frame_b_32[x] = blended_word;
        }

        frame_a += LCD_WIDTH_PACKED;
        frame_b_and_dest += LCD_WIDTH_PACKED;
    }
}

static void blend_frames_lut_rect(
    uint8_t* frame_a, uint8_t* frame_b_and_dest, uint8_t x_min, uint8_t y_min, uint8_t x_max,
    uint8_t y_max
)
{
    if (y_max > LCD_HEIGHT)
        y_max = LCD_HEIGHT;
    if (x_max > LCD_WIDTH)
        x_max = LCD_WIDTH;

    int start_x_byte = x_min / LCD_PACKING;
    int end_x_byte = (x_max + (LCD_PACKING - 1)) / LCD_PACKING;

    for (int y = y_min; y < y_max; y++)
    {
        uint8_t (*lut_row)[256] = g_blend_lut[y & 1];

        uint8_t* row_a = frame_a + (y * LCD_WIDTH_PACKED);
        uint8_t* row_b = frame_b_and_dest + (y * LCD_WIDTH_PACKED);

        int x = start_x_byte;

        while ((x < end_x_byte) && ((uintptr_t)(row_a + x) % 4 != 0))
        {
            row_b[x] = lut_row[row_a[x]][row_b[x]];
            x++;
        }

        uint32_t* row_a_32 = (uint32_t*)(row_a + x);
        uint32_t* row_b_32 = (uint32_t*)(row_b + x);
        int end_x_word = (end_x_byte - x) / 4;

        for (int i = 0; i < end_x_word; i++)
        {
            uint32_t a_word = row_a_32[i];
            uint32_t b_word = row_b_32[i];

            uint8_t b0 = lut_row[(a_word >> 0) & 0xFF][(b_word >> 0) & 0xFF];
            uint8_t b1 = lut_row[(a_word >> 8) & 0xFF][(b_word >> 8) & 0xFF];
            uint8_t b2 = lut_row[(a_word >> 16) & 0xFF][(b_word >> 16) & 0xFF];
            uint8_t b3 = lut_row[(a_word >> 24) & 0xFF][(b_word >> 24) & 0xFF];

            row_b_32[i] = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        }

        x += end_x_word * 4;
        while (x < end_x_byte)
        {
            row_b[x] = lut_row[row_a[x]][row_b[x]];
            x++;
        }
    }
}

static void save_check(gb_s* gb);

static __section__(".text.tick") void display_fps(void)
{
    if (!numbers_bmp)
        return;

    if (++fps_draw_timer % 4 != 0)
        return;

    float fps;
    if (CB_App->avg_dt <= 1.0f / 98.5f)
    {
        fps = 99.9f;
    }
    else
    {
        fps = 1.0f / CB_App->avg_dt;
    }

    // for rounding
    fps += 0.004f;

    uint8_t* lcd = playdate->graphics->getFrame();

    uint8_t* data;
    int width, height, rowbytes;
    playdate->graphics->getBitmapData(numbers_bmp, &width, &height, &rowbytes, NULL, &data);

    if (!data || !lcd)
        return;

    char buff[5];

    int fps_multiplied = (int)(fps * 10.0f);

    if (fps_multiplied > 999)
    {
        fps_multiplied = 999;
    }

    buff[0] = (fps_multiplied / 100) + '0';
    buff[1] = ((fps_multiplied / 10) % 10) + '0';
    buff[2] = '.';
    buff[3] = (fps_multiplied % 10) + '0';
    buff[4] = '\0';

    uint32_t digits4 = *(uint32_t*)&buff[0];
    if (digits4 == last_fps_digits)
        return;
    last_fps_digits = digits4;

    for (int y = 0; y < height; ++y)
    {
        uint32_t out = 0;
        unsigned x = 0;
        uint8_t* rowdata = data + y * rowbytes;
        for (int i = 0; i < sizeof(buff); ++i)
        {
            char c = buff[i];
            int cidx = 11, advance = 0;
            if (c == '.')
            {
                cidx = 10;
                advance = 3;
            }
            else if (c >= '0' && c <= '9')
            {
                cidx = c - '0';
                advance = 7;
            }

            unsigned cdata = (rowdata[cidx]) & reverse_bits_u8((1 << (advance + 1)) - 1);
            out |= cdata << (32 - x - 8);
            x += advance;
        }

        uint32_t mask = ((1 << (30 - x)) - 1);

        for (int i = 0; i < 4; ++i)
        {
            lcd[y * LCD_ROWSIZE + i] &= (mask >> ((3 - i) * 8));
            lcd[y * LCD_ROWSIZE + i] |= (out >> ((3 - i) * 8));
        }
    }

    playdate->graphics->markUpdatedRows(0, height - 1);
}

__section__(".text.tick") __space static void crank_update(CB_GameScene* gameScene, float* progress)
{
    CB_GameSceneContext* context = gameScene->context;

    float angle = fmaxf(0, fminf(360, playdate->system->getCrankAngle()));

    if (preferences_crank_mode == CRANK_MODE_START_SELECT)
    {
        gameScene->selector.startPressed = false;
        gameScene->selector.selectPressed = false;

        if (preferences_crank_down_action == 1 && angle > (180 - gameScene->selector.deadAngle) &&
            angle < (180 + gameScene->selector.deadAngle))
        {
            gameScene->selector.startPressed = true;
            gameScene->selector.selectPressed = true;
        }
        else
        {
            if (angle <= 180)
            {
                if (angle >= gameScene->selector.triggerAngle &&
                    angle <= (180 - gameScene->selector.triggerAngle))
                {
                    gameScene->selector.startPressed = true;
                }

                float dist = fminf(angle, 180.0f - angle);
                float adjustedAngle = fminf(dist, gameScene->selector.triggerAngle);
                *progress = 0.5f - (adjustedAngle / gameScene->selector.triggerAngle * 0.5f);
            }
            else
            {
                if (angle >= (180 + gameScene->selector.triggerAngle) &&
                    angle <= (360 - gameScene->selector.triggerAngle))
                {
                    gameScene->selector.selectPressed = true;
                }

                float dist = fminf(360.0f - angle, angle - 180.0f);
                float adjustedAngle = fminf(dist, gameScene->selector.triggerAngle);
                *progress = 0.5f + (adjustedAngle / gameScene->selector.triggerAngle * 0.5f);
            }
        }
    }
    else if (preferences_crank_mode == CRANK_MODE_TURBO_CW ||
             preferences_crank_mode == CRANK_MODE_TURBO_CCW)  // Turbo mode
    {
        float crank_change = playdate->system->getCrankChange();
        gameScene->crank_turbo_accumulator += crank_change;

        // Handle clockwise rotation
        while (gameScene->crank_turbo_accumulator >= 45.0f)
        {
            if (preferences_crank_mode == CRANK_MODE_TURBO_CW)
            {
                gameScene->crank_turbo_a_active = true;
            }
            else
            {
                gameScene->crank_turbo_b_active = true;
            }
            gameScene->crank_turbo_accumulator -= 45.0f;
        }

        // Handle counter-clockwise rotation
        while (gameScene->crank_turbo_accumulator <= -45.0f)
        {
            if (preferences_crank_mode == CRANK_MODE_TURBO_CW)
            {
                gameScene->crank_turbo_b_active = true;
            }
            else
            {
                gameScene->crank_turbo_a_active = true;
            }
            gameScene->crank_turbo_accumulator += 45.0f;
        }
    }

    // playdate extension IO registers
    uint16_t crank16 = (angle / 360.0f) * 0x10000;

    if (context->gb->direct.ext_crank_menu_indexing)
    {
        int16_t crank_diff =
            context->gb->direct.crank_docked ? 0 : (int16_t)(crank16 - context->gb->direct.crank);

        int new_accumulation = (int)context->gb->direct.crank_menu_accumulation + crank_diff;
        if (new_accumulation <= 0x8000 - CRANK_MENU_DELTA_BINANGLE)
        {
            context->gb->direct.crank_menu_delta--;
            context->gb->direct.crank_menu_accumulation = 0x8000;
        }
        else if (new_accumulation >= 0x8000 + CRANK_MENU_DELTA_BINANGLE)
        {
            context->gb->direct.crank_menu_delta++;
            context->gb->direct.crank_menu_accumulation = 0x8000;
        }
        else
        {
            context->gb->direct.crank_menu_accumulation = (uint16_t)new_accumulation;
        }
    }

    context->gb->direct.crank = crank16;
    context->gb->direct.crank_docked = 0;
}

__section__(".text.tick") __space static void CB_GameScene_update(void* object, uint32_t u32enc_dt)
{
    // This prevents flicker when transitioning to the Library Scene.
    if (CB_App->pendingScene)
    {
        return;
    }

    bool force_all_lines_dirty = false;

    setCrankSoundsEnabled(
        !preferences_crank_dock_button && !preferences_crank_undock_button &&
        preferences_crank_mode != CRANK_MODE_START_SELECT
    );

    float dt = UINT32_AS_FLOAT(u32enc_dt);
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;

    CB_Scene_update(gameScene->scene, dt);

    float progress = 0.5f;

#if TENDENCY_BASED_ADAPTIVE_INTERLACING
    /*
     * =========================================================================
     * Dynamic Rate Control with Adaptive Interlacing
     * =========================================================================
     *
     * This system maintains a smooth 60 FPS by dynamically skipping screen
     * lines (interlacing) based on the rendering workload. The "Auto" mode
     * uses a smart, two-stage system to provide both stability and responsiveness.
     *
     * Stage 1: The Tendency Counter
     * This counter tracks recent frame activity. It increases when the number of
     * updated lines exceeds a user-settable threshold (indicating a busy
     * scene) and decreases when the scene is calm. When the counter passes a
     * 'trigger-on' value, it activates Stage 2.
     *
     * Stage 2: The Adaptive Grace Period Lock
     * Once activated, interlacing is "locked on" for a set duration to
     * guarantee stable performance during sustained action. This lock's duration
     * is adaptive, linked directly to the user's sensitivity preference:
     *  - Low Sensitivity: Long lock, ideal for racing games.
     *  - High Sensitivity: Minimal/no lock, ideal for brief screen transitions.
     *
     * This dual approach provides stability during high-motion sequences while
     * remaining highly responsive to brief bursts of activity.
     *
     * This entire feature is DISABLED in 30 FPS mode (`preferences_frame_skip`),
     * as the visual disturbance is more pronounced at a lower framerate.
     */

    bool activate_dynamic_rate = false;
    bool was_interlaced_last_frame = context->gb->direct.dynamic_rate_enabled;

    if (!preferences_frame_skip)
    {
        if (preferences_dynamic_rate == DYNAMIC_RATE_ON)
        {
            activate_dynamic_rate = true;
            gameScene->interlace_lock_frames_remaining = 0;
        }
        else if (preferences_dynamic_rate == DYNAMIC_RATE_AUTO)
        {
            if (gameScene->interlace_lock_frames_remaining > 0)
            {
                activate_dynamic_rate = true;
                gameScene->interlace_lock_frames_remaining--;
            }
            else
            {
                if (gameScene->interlace_tendency_counter > INTERLACE_TENDENCY_TRIGGER_ON)
                {
                    activate_dynamic_rate = true;
                }
                else if (was_interlaced_last_frame &&
                         gameScene->interlace_tendency_counter > INTERLACE_TENDENCY_TRIGGER_OFF)
                {
                    activate_dynamic_rate = true;
                }
            }
        }
    }

    if (activate_dynamic_rate && !was_interlaced_last_frame)
    {
        float inverted_level_normalized = (10.0f - preferences_dynamic_level) / 10.0f;

        int adaptive_lock_duration =
            INTERLACE_LOCK_DURATION_MIN +
            (int)((INTERLACE_LOCK_DURATION_MAX - INTERLACE_LOCK_DURATION_MIN) *
                  inverted_level_normalized);

        gameScene->interlace_lock_frames_remaining = adaptive_lock_duration;
    }

    if (preferences_dynamic_rate != DYNAMIC_RATE_AUTO || preferences_frame_skip)
    {
        gameScene->interlace_tendency_counter = 0;
    }

    context->gb->direct.dynamic_rate_enabled = activate_dynamic_rate;

    if (activate_dynamic_rate)
    {
        static int frame_i;
        frame_i++;

        context->gb->direct.interlace_mask = 0b101010101010 >> (frame_i % 2);
    }
    else
    {
        context->gb->direct.interlace_mask = 0xFF;
    }
#endif

    gameScene->selector.startPressed = false;
    gameScene->selector.selectPressed = false;

    gameScene->crank_turbo_a_active = false;
    gameScene->crank_turbo_b_active = false;

    if (preferences_crank_undock_button && gameScene->crank_was_docked &&
        !playdate->system->isCrankDocked())
    {
        if (preferences_crank_undock_button == PREF_BUTTON_START)
            gameScene->button_hold_mode = 2;
        else if (preferences_crank_undock_button == PREF_BUTTON_SELECT)
            gameScene->button_hold_mode = 0;
        else if (preferences_crank_undock_button == PREF_BUTTON_START_SELECT)
            gameScene->button_hold_mode = 3;
        gameScene->button_hold_frames_remaining = 10;
    }
    if (preferences_crank_dock_button && !gameScene->crank_was_docked &&
        playdate->system->isCrankDocked())
    {
        if (preferences_crank_dock_button == PREF_BUTTON_START)
            gameScene->button_hold_mode = 2;
        else if (preferences_crank_dock_button == PREF_BUTTON_SELECT)
            gameScene->button_hold_mode = 0;
        else if (preferences_crank_dock_button == PREF_BUTTON_START_SELECT)
            gameScene->button_hold_mode = 3;
        gameScene->button_hold_frames_remaining = 10;
    }

    gameScene->crank_was_docked = playdate->system->isCrankDocked();

    if (!playdate->system->isCrankDocked())
    {
        crank_update(gameScene, &progress);
    }
    else
    {
        context->gb->direct.crank_docked = 1;
        if (preferences_crank_mode == CRANK_MODE_TURBO_CCW ||
            preferences_crank_mode == CRANK_MODE_TURBO_CCW)
        {
            gameScene->crank_turbo_accumulator = 0.0f;
        }
        context->gb->direct.crank_menu_delta = 0;
        context->gb->direct.crank_menu_accumulation = 0x8000;
    }

    if (gameScene->button_hold_frames_remaining > 0)
    {
        if (gameScene->button_hold_mode == 2)
        {
            gameScene->selector.startPressed = true;
            gameScene->selector.selectPressed = false;
            progress = 0.0f;
        }
        else if (gameScene->button_hold_mode == 0)
        {
            gameScene->selector.startPressed = false;
            gameScene->selector.selectPressed = true;
            progress = 1.0f;
        }
        else if (gameScene->button_hold_mode == 3)
        {
            gameScene->selector.startPressed = true;
            gameScene->selector.selectPressed = true;
        }

        gameScene->button_hold_frames_remaining--;

        if (gameScene->button_hold_frames_remaining == 0)
        {
            gameScene->button_hold_mode = 1;
        }
    }

    int selectorIndex;

    if (gameScene->selector.startPressed && gameScene->selector.selectPressed)
    {
        selectorIndex = -1;
    }
    else
    {
        selectorIndex = 1 + floorf(progress * (gameScene->selector.numberOfFrames - 2));

        if (progress == 0)
        {
            selectorIndex = 0;
        }
        else if (progress == 1)
        {
            selectorIndex = gameScene->selector.numberOfFrames - 1;
        }
    }

    gameScene->selector.index = selectorIndex;

    gbScreenRequiresFullRefresh = false;
    if (gameScene->model.empty || gameScene->model.state != gameScene->state ||
        gameScene->model.error != gameScene->error || gameScene->scene->forceFullRefresh)
    {
        gbScreenRequiresFullRefresh = true;
        gameScene->scene->forceFullRefresh = false;
    }

    if (gameScene->model.crank_mode != preferences_crank_mode)
    {
        gameScene->staticSelectorUIDrawn = false;
    }

    // check if game picture bounds have changed
    {
        static unsigned prev_game_picture_x_offset, prev_game_picture_scaling,
            prev_game_picture_y_top, prev_game_picture_y_bottom, prev_game_picture_background_color;

        if (prev_game_picture_x_offset != game_picture_x_offset ||
            prev_game_picture_scaling != game_picture_scaling ||
            prev_game_picture_y_top != game_picture_y_top ||
            prev_game_picture_y_bottom != game_picture_y_bottom ||
            prev_game_picture_background_color != game_picture_background_color)
        {
            gbScreenRequiresFullRefresh = 1;
        }

        prev_game_picture_x_offset = game_picture_x_offset;
        prev_game_picture_scaling = game_picture_scaling;
        prev_game_picture_y_top = game_picture_y_top;
        prev_game_picture_y_bottom = game_picture_y_bottom;
        prev_game_picture_background_color = game_picture_background_color;
    }

    if (gameScene->state == CB_GameSceneStateLoaded)
    {
        bool shouldDisplayStartSelectUI = (!playdate->system->isCrankDocked() &&
                                           preferences_crank_mode == CRANK_MODE_START_SELECT) ||
                                          (gameScene->button_hold_frames_remaining > 0);

        static bool wasSelectorVisible = false;
        if (shouldDisplayStartSelectUI != wasSelectorVisible)
        {
            gameScene->staticSelectorUIDrawn = false;
        }
        wasSelectorVisible = shouldDisplayStartSelectUI;

        bool animatedSelectorBitmapNeedsRedraw = false;

        if (gbScreenRequiresFullRefresh || !gameScene->staticSelectorUIDrawn ||
            gameScene->model.selectorIndex != gameScene->selector.index)
        {
            animatedSelectorBitmapNeedsRedraw = true;
        }

        CB_GameSceneContext* context = gameScene->context;

        PDButtons current_pd_buttons = CB_App->buttons_down;

        bool gb_joypad_start_is_active_low = !(gameScene->selector.startPressed);
        bool gb_joypad_select_is_active_low = !(gameScene->selector.selectPressed);

        context->gb->direct.joypad_bits.start = gb_joypad_start_is_active_low;
        context->gb->direct.joypad_bits.select = gb_joypad_select_is_active_low;
        
        if (gameScene->lock_button_hold_frames_remaining > 0)
        {
            --gameScene->lock_button_hold_frames_remaining;
            switch (preferences_lock_button)
            {
            case PREF_BUTTON_START:
                context->gb->direct.joypad_bits.start = 0;
                break;
            case PREF_BUTTON_SELECT:
                context->gb->direct.joypad_bits.select = 0;
                break;
            case PREF_BUTTON_START_SELECT:
                context->gb->direct.joypad_bits.start = 0;
                context->gb->direct.joypad_bits.select = 0;
                break;
            default:
                break;
            }
        }

        context->gb->direct.joypad_bits.a =
            !((current_pd_buttons & kButtonA) || gameScene->crank_turbo_a_active);
        context->gb->direct.joypad_bits.b =
            !((current_pd_buttons & kButtonB) || gameScene->crank_turbo_b_active);
        context->gb->direct.joypad_bits.left = !(current_pd_buttons & kButtonLeft);
        context->gb->direct.joypad_bits.up = !(current_pd_buttons & kButtonUp);
        context->gb->direct.joypad_bits.right = !(current_pd_buttons & kButtonRight);
        context->gb->direct.joypad_bits.down = !(current_pd_buttons & kButtonDown);

        if (context->gb->direct.joypad_interrupts)
        {
            uint8_t new_joypad_state = context->gb->direct.joypad;
            uint8_t old_joypad_state = gameScene->previous_joypad_state;
            uint8_t newly_pressed_mask = (old_joypad_state & ~new_joypad_state);

            // Check if a new button was pressed AND no interrupt is already pending.
            if (newly_pressed_mask != 0 && context->gb->direct.joypad_interrupt_delay < 0)
            {
                uint8_t p1_select = context->gb->gb_reg.P1;
                bool is_dpad_selected = ((p1_select & 0x10) == 0);
                bool is_action_selected = ((p1_select & 0x20) == 0);

                bool dpad_pressed = (newly_pressed_mask & 0xF0);
                bool action_pressed = (newly_pressed_mask & 0x0F);

                // If a relevant button was pressed, schedule an interrupt.
                if ((is_dpad_selected && dpad_pressed) || (is_action_selected && action_pressed))
                {
                    // Schedule the interrupt to fire after a random number of CPU cycles.
                    // This range (up to ~4560 cycles) simulates the press happening
                    // at various points within the next few scanlines.
                    context->gb->direct.joypad_interrupt_delay = rand() % (LCD_LINE_CYCLES * 10);
                }
            }

            gameScene->previous_joypad_state = new_joypad_state;
        }

        context->gb->overclock = (unsigned)(preferences_overclock);

        if (gbScreenRequiresFullRefresh)
        {
            playdate->graphics->clear(game_picture_background_color);
        }

#if CB_DEBUG && CB_DEBUG_UPDATED_ROWS
        memset(gameScene->debug_updatedRows, 0, LCD_ROWS);
#endif

        context->gb->direct.sram_updated = 0;

        bool skip_frame = false;
        if (preferences_script_support && context->scene->script)
        {
            skip_frame = script_tick(context->scene->script, gameScene, gameScene->next_frames_elapsed);
        }
        gameScene->next_frames_elapsed = 0;

        if (!skip_frame)
        {
            CB_ASSERT(context == context->gb->direct.priv);

            gb_s* tmp_gb = context->gb;

    #ifdef TARGET_SIMULATOR
            pthread_mutex_lock(&audio_mutex);
    #endif

            // Static buffer for the !dtcm_enabled path to prevent stack overflow on the simulator.
            static char stack_gb_data[sizeof(gb_s)];

            if (!dtcm_enabled())
            {
                gameScene->audioLocked = 1;
                memcpy(stack_gb_data, tmp_gb, sizeof(gb_s));
                context->gb = (void*)stack_gb_data;
                gameScene->audioLocked = 0;
            }

            gameScene->playtime += 1 + preferences_frame_skip;
            CB_App->avg_dt_mult =
                (preferences_frame_skip && preferences_display_fps == 1) ? 0.5f : 1.0f;

            void* gb_run_frame_ = (context->gb->is_cgb_mode)
                ? gb_run_frame__cgb
                : gb_run_frame__dmg;
    #ifdef DTCM_ALLOC
            void (*run_frame_function_pointer)(gb_s*) = ITCM_CORE_FN(gb_run_frame_);
    #else
            void (*run_frame_function_pointer)(gb_s*) = gb_run_frame_;
    #endif

            if (preferences_frame_skip && preferences_blend_frames)
            {
                // --- 30fps Frame Blending ---
                static clalign uint8_t frame_A_buffer[LCD_BUFFER_BYTES];

                // 1. Render Frame A (Full Render, frame_skip = 0)
                context->gb->direct.frame_skip = 0;
    #ifdef DTCM_ALLOC
                DTCM_VERIFY_DEBUG();
                run_frame_function_pointer(context->gb);
                DTCM_VERIFY_DEBUG();
    #else
                run_frame_function_pointer(context->gb);
    #endif
                ++gameScene->next_frames_elapsed;
                tick_audio_sync(gameScene);
                memcpy(frame_A_buffer, context->gb->lcd, LCD_BUFFER_BYTES);

                // 2. Determine if the screen is static and if sprites were rendered.
                bool screen_is_static =
                    (memcmp(frame_A_buffer, context->previous_lcd, LCD_BUFFER_BYTES) == 0);
                bool has_blendable_sprites =
                    context->gb->direct.blend_rect_x_min < context->gb->direct.blend_rect_x_max;

                // 3. Run the emulator for the second frame period.
                context->gb->direct.frame_skip = screen_is_static;
    #ifdef DTCM_ALLOC
                DTCM_VERIFY_DEBUG();
                run_frame_function_pointer(context->gb);
                DTCM_VERIFY_DEBUG();
    #else
                run_frame_function_pointer(context->gb);
    #endif
                ++gameScene->next_frames_elapsed;
                tick_audio_sync(gameScene);

                // 4. Decide whether to blend based on the preference setting
                if (preferences_blend_frames == 1)  // "On" mode
                {
                    if (!screen_is_static)
                    {
                        blend_frames_lut(frame_A_buffer, context->gb->lcd);
                    }
                }
                else if (preferences_blend_frames == 2)  // "Auto" mode
                {
                    if (!screen_is_static && has_blendable_sprites)
                    {
                        blend_frames_lut_rect(
                            frame_A_buffer, context->gb->lcd, context->gb->direct.blend_rect_x_min,
                            context->gb->direct.blend_rect_y_min, context->gb->direct.blend_rect_x_max,
                            context->gb->direct.blend_rect_y_max
                        );
                    }
                }
            }
            else
            {
                // --- 30fps Ghost frame logic ---
                if (preferences_frame_skip && preferences_ghost_frame_30fps)
                {

                    static clalign uint8_t oam_ghost_buffer_storage[OAM_SIZE];
                    context->gb->direct.oam_ghost_buffer = NULL;

                    context->gb->direct.frame_skip = 1;
    #ifdef DTCM_ALLOC
                    DTCM_VERIFY_DEBUG();
                    run_frame_function_pointer(context->gb);
                    DTCM_VERIFY_DEBUG();
    #else
                    run_frame_function_pointer(context->gb);
    #endif
                    ++gameScene->next_frames_elapsed;
                    tick_audio_sync(gameScene);
                    memcpy(oam_ghost_buffer_storage, context->gb->oam, OAM_SIZE);

                    context->gb->direct.oam_ghost_buffer = oam_ghost_buffer_storage;
                    context->gb->direct.frame_skip = 0;
    #ifdef DTCM_ALLOC
                    DTCM_VERIFY_DEBUG();
                    run_frame_function_pointer(context->gb);
                    DTCM_VERIFY_DEBUG();
    #else
                    run_frame_function_pointer(context->gb);
    #endif
                    ++gameScene->next_frames_elapsed;
                    tick_audio_sync(gameScene);
                    context->gb->direct.oam_ghost_buffer = NULL;
                }
                else
                {
                    // --- 60fps and non-blended 30fps logic ---
                    for (int frame = 0; frame <= preferences_frame_skip; ++frame)
                    {
                        context->gb->direct.frame_skip = (preferences_frame_skip != frame);
    #ifdef DTCM_ALLOC
                        DTCM_VERIFY_DEBUG();
                        run_frame_function_pointer(context->gb);
                        DTCM_VERIFY_DEBUG();
    #else
                        run_frame_function_pointer(context->gb);
    #endif
                        ++gameScene->next_frames_elapsed;
                        tick_audio_sync(gameScene);
                    }
                }
            }

            if (!dtcm_enabled())
            {
                gameScene->audioLocked = 1;
                memcpy(tmp_gb, context->gb, sizeof(gb_s));
                context->gb = tmp_gb;
                gameScene->audioLocked = 0;
            }

    #ifdef TARGET_SIMULATOR
            pthread_mutex_unlock(&audio_mutex);
    #endif

            if (gameScene->cartridge_has_battery)
            {
                save_check(context->gb);
            }

            // --- Conditional Screen Update (Drawing) Logic ---
            uint8_t* current_lcd = context->gb->lcd;
            uint8_t* previous_lcd = context->previous_lcd;
            uint16_t line_has_changed[LCD_HEIGHT / 16];
            memset(line_has_changed, 0, sizeof(line_has_changed));

            unsigned dither_preference = preferences_dither_line;
            bool stable_scaling_enabled = preferences_dither_stable;
            int scy = context->gb->gb_reg.SCY;

            const unsigned scaling = game_picture_scaling ? game_picture_scaling : 0x1000;
            if (preferences_dither_stable && scy % scaling != last_scy % scaling)
            {
                force_all_lines_dirty = true;
                last_scy = scy;
            }

    #if TENDENCY_BASED_ADAPTIVE_INTERLACING
            int updated_playdate_lines = 0;
            int scale_index_for_calc = dither_preference;
    #endif

            void (*gb_fast_memcpy_64_)(void* restrict _dst, const void* restrict _src, size_t len)
                = context->gb->is_cgb_mode
                    ? gb_fast_memcpy_64__cgb
                    : gb_fast_memcpy_64__dmg;
            
            gb_fast_memcpy_64_ = ITCM_CORE_FN(gb_fast_memcpy_64_);

            if (memcmp(current_lcd, previous_lcd, LCD_BUFFER_BYTES) != 0)
            {
                for (int y = 0; y < LCD_HEIGHT; y++)
                {
                    uint8_t* cur = &current_lcd[y * LCD_WIDTH_PACKED];
                    uint8_t* prv = &previous_lcd[y * LCD_WIDTH_PACKED];

                    if (memcmp(cur, prv, LCD_WIDTH_PACKED) != 0)
                    {
                        line_has_changed[y / 16] |= (1 << (y % 16));

                        gb_fast_memcpy_64_(prv, cur, LCD_WIDTH_PACKED);

    #if TENDENCY_BASED_ADAPTIVE_INTERLACING
                        if (!preferences_frame_skip && preferences_dynamic_rate == DYNAMIC_RATE_AUTO)
                        {
                            int row_height_on_playdate = 2;
                            if (scale_index_for_calc == 2)
                            {
                                row_height_on_playdate = 1;
                            }
                            updated_playdate_lines += row_height_on_playdate;
                        }
    #endif
                    }

    #if TENDENCY_BASED_ADAPTIVE_INTERLACING
                    scale_index_for_calc++;
                    if (scale_index_for_calc == 3)
                    {
                        scale_index_for_calc = 0;
                    }
    #endif
                }
            }

    #if TENDENCY_BASED_ADAPTIVE_INTERLACING
            if (!preferences_frame_skip && preferences_dynamic_rate == DYNAMIC_RATE_AUTO)
            {
                int percentage_threshold = 25 + (preferences_dynamic_level * 5);
                int line_threshold = (PLAYDATE_LINE_COUNT_MAX * percentage_threshold) / 100;

                if (updated_playdate_lines > line_threshold)
                {
                    gameScene->interlace_tendency_counter += 2;
                }
                else
                {
                    gameScene->interlace_tendency_counter--;
                }

                if (gameScene->interlace_tendency_counter < 0)
                    gameScene->interlace_tendency_counter = 0;
                if (gameScene->interlace_tendency_counter > INTERLACE_TENDENCY_MAX)
                    gameScene->interlace_tendency_counter = INTERLACE_TENDENCY_MAX;
            }
    #endif

    #if LOG_DIRTY_LINES
            playdate->system->logToConsole("--- Frame Update ---");
            int range_start = 0;
            bool is_dirty_range = (line_has_changed[0] & 1);

            for (int y = 1; y < LCD_HEIGHT; y++)
            {
                bool is_dirty_current = (line_has_changed[y / 16] >> (y % 16)) & 1;

                if (is_dirty_current != is_dirty_range)
                {
                    if (range_start == y - 1)
                    {
                        playdate->system->logToConsole(
                            "Line %d: %s", range_start, is_dirty_range ? "Updated" : "Omitted"
                        );
                    }
                    else
                    {
                        playdate->system->logToConsole(
                            "Lines %d-%d: %s", range_start, y - 1,
                            is_dirty_range ? "Updated" : "Omitted"
                        );
                    }
                    range_start = y;
                    is_dirty_range = is_dirty_current;
                }
            }

            if (range_start == LCD_HEIGHT - 1)
            {
                playdate->system->logToConsole(
                    "Line %d: %s", range_start, is_dirty_range ? "Updated" : "Omitted"
                );
            }
            else
            {
                playdate->system->logToConsole(
                    "Lines %d-%d: %s", range_start, LCD_HEIGHT - 1,
                    is_dirty_range ? "Updated" : "Omitted"
                );
            }
    #endif

        void (*update_fb_dirty_lines_)(
            uint8_t* restrict framebuffer, uint8_t* restrict lcd,
            const uint16_t* restrict line_changed_flags, markUpdateRows_t markUpdatedRows, int scy,
            bool stable_scaling_enabled, uint8_t* restrict dither_lut0, uint8_t* restrict dither_lut1
        )
                = context->gb->is_cgb_mode
                    ? update_fb_dirty_lines__cgb
                    : update_fb_dirty_lines__dmg;
            
        update_fb_dirty_lines_ = ITCM_CORE_FN(update_fb_dirty_lines_);

    #if ENABLE_RENDER_PROFILER
            if (CB_run_profiler_on_next_frame)
            {
                CB_run_profiler_on_next_frame = false;

                for (int i = 0; i < LCD_HEIGHT / 16; i++)
                {
                    line_has_changed[i] = 0xFFFF;
                }

                float startTime = playdate->system->getElapsedTime();

                update_fb_dirty_lines_(
                    playdate->graphics->getFrame(), current_lcd, line_has_changed,
                    playdate->graphics->markUpdatedRows, scy, stable_scaling_enabled,
                    CB_dither_lut_row0, CB_dither_lut_row1
                );

                float endTime = playdate->system->getElapsedTime();
                float totalRenderTime = endTime - startTime;
                float averageLineRenderTime = totalRenderTime / (float)LCD_HEIGHT;

                playdate->system->logToConsole("--- Profiler Result ---");
                playdate->system->logToConsole(
                    "Total Render Time for %d lines: %.8f s", LCD_HEIGHT, totalRenderTime
                );
                playdate->system->logToConsole(
                    "Average Line Render Time: %.8f s", averageLineRenderTime
                );
                playdate->system->logToConsole(
                    "New #define value suggestion: %.8ff", averageLineRenderTime
                );

                return;
            }
    #endif

            if (gbScreenRequiresFullRefresh || force_all_lines_dirty)
            {
                for (int i = 0; i < LCD_HEIGHT / 16; i++)
                {
                    line_has_changed[i] = 0xFFFF;
                }
            }

            update_fb_dirty_lines_(
                playdate->graphics->getFrame(), current_lcd, line_has_changed,
                playdate->graphics->markUpdatedRows, scy, stable_scaling_enabled, CB_dither_lut_row0,
                CB_dither_lut_row1
            );

            if (gbScreenRequiresFullRefresh || force_all_lines_dirty)
            {
                gb_fast_memcpy_64_(context->previous_lcd, current_lcd, LCD_BUFFER_BYTES);
            }

            // Always request the update loop to run at 30 FPS.
            // (60 game boy frames per second.)
            // This ensures gb_run_frame() is called at a consistent rate.
            gameScene->scene->preferredRefreshRate = preferences_frame_skip ? 30 : 60;

            if (preferences_uncap_fps)
                gameScene->scene->preferredRefreshRate = -1;

            if (gameScene->cartridge_has_rtc)
            {
                // Get the current time from the system clock.
                unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);

                // Check if time has passed since our last check.
                if (now > gameScene->rtc_time)
                {
                    unsigned int seconds_passed = now - gameScene->rtc_time;
                    gameScene->rtc_seconds_to_catch_up += seconds_passed;
                    gameScene->rtc_time = now;
                }

                if (gameScene->rtc_seconds_to_catch_up > 0)
                {
                    gb_catch_up_rtc_direct(context->gb, gameScene->rtc_seconds_to_catch_up);
                    gameScene->rtc_seconds_to_catch_up = 0;
                }
            }

            if (!game_hide_indicator &&
                (!gameScene->staticSelectorUIDrawn || gbScreenRequiresFullRefresh))
            {
                // Clear the right sidebar area before redrawing any static UI.
                const int rightBarX = 40 + 320;
                const int rightBarWidth = 40;
                playdate->graphics->fillRect(
                    rightBarX, 0, rightBarWidth, playdate->display->getHeight(),
                    game_picture_background_color
                );
            }

            if (preferences_script_support && context->scene->script)
            {
                script_draw(context->scene->script, gameScene);
            }

            if (!game_hide_indicator &&
                (!gameScene->staticSelectorUIDrawn || gbScreenRequiresFullRefresh))
            {
                // Draw the text labels ("Start/Select") if needed.
                if (shouldDisplayStartSelectUI)
                {
                    playdate->graphics->setFont(CB_App->labelFont);
                    playdate->graphics->setDrawMode(kDrawModeFillWhite);
                    playdate->graphics->drawText(
                        startButtonText, cb_strlen(startButtonText), kUTF8Encoding,
                        gameScene->selector.startButtonX, gameScene->selector.startButtonY
                    );
                    playdate->graphics->drawText(
                        selectButtonText, cb_strlen(selectButtonText), kUTF8Encoding,
                        gameScene->selector.selectButtonX, gameScene->selector.selectButtonY
                    );
                }

                // Draw the "Turbo" indicator if needed.
                if (preferences_crank_mode == CRANK_MODE_TURBO_CW ||
                    preferences_crank_mode == CRANK_MODE_TURBO_CCW)
                {
                    playdate->graphics->setFont(CB_App->labelFont);
                    playdate->graphics->setDrawMode(kDrawModeFillWhite);

                    const char* line1 = "Turbo";
                    const char* line2 = (preferences_crank_mode == CRANK_MODE_TURBO_CW) ? "A/B" : "B/A";

                    int fontHeight = playdate->graphics->getFontHeight(CB_App->labelFont);
                    int lineSpacing = 2;
                    int paddingBottom = 6;

                    int line1Width = playdate->graphics->getTextWidth(
                        CB_App->labelFont, line1, strlen(line1), kUTF8Encoding, 0
                    );
                    int line2Width = playdate->graphics->getTextWidth(
                        CB_App->labelFont, line2, strlen(line2), kUTF8Encoding, 0
                    );

                    const int rightBarX = 40 + 320;
                    const int rightBarWidth = 40;

                    int bottomEdge = playdate->display->getHeight();
                    int y2 = bottomEdge - paddingBottom - fontHeight;
                    int y1 = y2 - fontHeight - lineSpacing;

                    int x1 = rightBarX + (rightBarWidth - line1Width) / 2;
                    int x2 = rightBarX + (rightBarWidth - line2Width) / 2;

                    playdate->graphics->drawText(line1, strlen(line1), kUTF8Encoding, x1, y1);
                    playdate->graphics->drawText(line2, strlen(line2), kUTF8Encoding, x2, y2);

                    playdate->graphics->setDrawMode(kDrawModeCopy);
                }

                playdate->graphics->setDrawMode(kDrawModeCopy);

                if (shouldDisplayStartSelectUI)
                {
                    LCDBitmap* bitmap;
                    if (gameScene->selector.index < 0)
                    {
                        bitmap = CB_App->startSelectBitmap;
                    }
                    else
                    {
                        bitmap = playdate->graphics->getTableBitmap(
                            CB_App->selectorBitmapTable, gameScene->selector.index
                        );
                    }
                    playdate->graphics->drawBitmap(
                        bitmap, gameScene->selector.x, gameScene->selector.y, kBitmapUnflipped
                    );
                }

                playdate->graphics->setDrawMode(kDrawModeCopy);
                gameScene->staticSelectorUIDrawn = true;
            }
            else if (!game_hide_indicator &&
                    (animatedSelectorBitmapNeedsRedraw && shouldDisplayStartSelectUI))
            {
                playdate->graphics->fillRect(
                    gameScene->selector.x, gameScene->selector.y, gameScene->selector.width,
                    gameScene->selector.height, game_picture_background_color
                );

                LCDBitmap* bitmap;
                // Use gameScene->selector.index, which is the most current
                // calculated frame
                if (gameScene->selector.index < 0)
                {
                    bitmap = CB_App->startSelectBitmap;
                }
                else
                {
                    bitmap = playdate->graphics->getTableBitmap(
                        CB_App->selectorBitmapTable, gameScene->selector.index
                    );
                }
                playdate->graphics->drawBitmap(
                    bitmap, gameScene->selector.x, gameScene->selector.y, kBitmapUnflipped
                );

                playdate->graphics->markUpdatedRows(
                    gameScene->selector.y, gameScene->selector.y + gameScene->selector.height - 1
                );
            }

    #if CB_DEBUG && CB_DEBUG_UPDATED_ROWS
            PDRect highlightFrame = gameScene->debug_highlightFrame;
            playdate->graphics->fillRect(
                highlightFrame.x, highlightFrame.y, highlightFrame.width, highlightFrame.height,
                kColorBlack
            );

            for (int y = 0; y < CB_LCD_HEIGHT; y++)
            {
                int absoluteY = CB_LCD_Y + y;

                if (gameScene->debug_updatedRows[absoluteY])
                {
                    playdate->graphics->fillRect(
                        highlightFrame.x, absoluteY, highlightFrame.width, 1, kColorWhite
                    );
                }
            }
    #endif

            if (preferences_display_fps)
            {
                display_fps();
            }
        }
    }
    else if (gameScene->state == CB_GameSceneStateError)
    {
        // Check for pushed A or B button to return to the library
        PDButtons pushed;
        playdate->system->getButtonState(NULL, &pushed, NULL);

        if ((pushed & kButtonA) || (pushed & kButtonB))
        {
            CB_GameScene_didSelectLibrary(gameScene);
            return;
        }

        gameScene->scene->preferredRefreshRate = 30;

        if (gbScreenRequiresFullRefresh)
        {
            char* errorTitle = "Oh no!";

            int errorMessagesCount = 1;
            char* errorMessages[4];

            errorMessages[0] = "A generic error occurred";

            if (gameScene->error == CB_GameSceneErrorLoadingRom)
            {
                errorMessages[0] = "Can't load the selected ROM";
            }
            else if (gameScene->error == CB_GameSceneErrorWrongLocation)
            {
                errorTitle = "Wrong location";
                errorMessagesCount = 2;
                errorMessages[0] = "Please move the ROM to";
                errorMessages[1] = "/Data/*.crankboy/games/";
            }
            else if (gameScene->error == CB_GameSceneErrorSaveData)
            {
                errorTitle = "Save Data Error";
                errorMessagesCount = 1;
                errorMessages[0] = "Failed to load save data.";
            }
            else if (gameScene->error == CB_GameSceneErrorFatal)
            {
                errorMessages[0] = "A fatal error occurred";
            }

            errorMessages[errorMessagesCount++] = "";
            errorMessages[errorMessagesCount++] = "Press Ⓐ or Ⓑ to return to Library";

            playdate->graphics->clear(kColorWhite);

            int titleToMessageSpacing = 6;

            int titleHeight = playdate->graphics->getFontHeight(CB_App->titleFont);
            int lineSpacing = 2;
            int messageHeight = playdate->graphics->getFontHeight(CB_App->bodyFont);
            int messagesHeight =
                messageHeight * errorMessagesCount + lineSpacing * (errorMessagesCount - 1);

            int containerHeight = titleHeight + titleToMessageSpacing + messagesHeight;

            int titleX =
                (float)(playdate->display->getWidth() -
                        playdate->graphics->getTextWidth(
                            CB_App->titleFont, errorTitle, strlen(errorTitle), kUTF8Encoding, 0
                        )) /
                2;
            int titleY = (float)(playdate->display->getHeight() - containerHeight) / 2;

            playdate->graphics->setFont(CB_App->titleFont);
            playdate->graphics->drawText(
                errorTitle, strlen(errorTitle), kUTF8Encoding, titleX, titleY
            );

            int messageY = titleY + titleHeight + titleToMessageSpacing;

            for (int i = 0; i < errorMessagesCount; i++)
            {
                char* errorMessage = errorMessages[i];
                int messageX = (float)(playdate->display->getWidth() -
                                       playdate->graphics->getTextWidth(
                                           CB_App->bodyFont, errorMessage, strlen(errorMessage),
                                           kUTF8Encoding, 0
                                       )) /
                               2;

                playdate->graphics->setFont(CB_App->bodyFont);
                playdate->graphics->drawText(
                    errorMessage, strlen(errorMessage), kUTF8Encoding, messageX, messageY
                );

                messageY += messageHeight + lineSpacing;
            }

            gameScene->staticSelectorUIDrawn = false;
        }
    }
    gameScene->model.empty = false;
    gameScene->model.state = gameScene->state;
    gameScene->model.error = gameScene->error;
    gameScene->model.selectorIndex = gameScene->selector.index;
    gameScene->model.crank_mode = preferences_crank_mode;
}

__section__(".text.tick") __space static void save_check(gb_s* gb)
{
    static uint32_t frames_since_sram_update;

    gb->direct.sram_dirty |= gb->direct.sram_updated;

    if (gb->direct.sram_updated)
    {
        frames_since_sram_update = 0;
    }
    else
    {
        frames_since_sram_update++;
    }

    if (gb->cart_battery && gb->direct.sram_dirty && !gb->direct.sram_updated)
    {
        // With audio sync enabled, idle-saving can cause audio under-runs.
        // In this case, we rely on saving when the menu is opened or the system is locked.
        if (preferences_audio_sync != 1 && frames_since_sram_update >= CB_IDLE_FRAMES_BEFORE_SAVE)
        {
            playdate->system->logToConsole("Saving (idle detected)");
            gb_save_to_disk(gb);
        }
    }
}

static const char* loadStateErrorOptions[] = {"OK", "Details", NULL};

__section__(".rare") static void CB_LoadStateErrorModalCallback(void* userdata, int option)
{
    char* details = (char*)userdata;
    if (option == 1 && details)
    {
        CB_presentModal(CB_Modal_new(details, NULL, NULL, NULL)->scene);
    }
    if (details)
        cb_free(details);
}

void CB_LibraryConfirmModal(void* userdata, int option)
{
    CB_GameScene* gameScene = userdata;

    if (option == 1)
    {
        call_with_user_stack(CB_goToLibrary);
    }
    else
    {
        gameScene->button_hold_frames_remaining = 0;
        gameScene->button_hold_mode = 1;
        gameScene->audioLocked = false;
    }
}

__section__(".rare") void CB_GameScene_didSelectLibrary_(void* userdata)
{
    CB_GameScene* gameScene = userdata;
    gameScene->audioLocked = true;

    // if playing for more than 1 minute, ask confirmation
    if (gameScene->playtime >= 60 * 60)
    {
        const char* options[] = {"No", "Yes", NULL};
        CB_presentModal(
            CB_Modal_new("Quit game?", quitGameOptions, CB_LibraryConfirmModal, gameScene)->scene
        );
    }
    else
    {
        call_with_user_stack(CB_goToLibrary);
    }
}

__section__(".rare") void CB_GameScene_didSelectLibrary(void* userdata)
{
    DTCM_VERIFY();

    call_with_user_stack_1(CB_GameScene_didSelectLibrary_, userdata);

    DTCM_VERIFY();
}

__section__(".rare") static void CB_GameScene_showSettings(void* userdata)
{
    CB_GameScene* gameScene = userdata;
    CB_SettingsScene* settingsScene = CB_SettingsScene_new(gameScene, NULL);
    CB_presentModal(settingsScene->scene);

    // We need to set this here to None in case the user selected any button.
    // The menu automatically falls back to 0 and the selected button is never
    // pushed.
    playdate->system->setMenuItemValue(buttonMenuItem, 1);
    gameScene->button_hold_mode = 1;
}

__section__(".rare") void CB_GameScene_buttonMenuCallback(void* userdata)
{
    CB_GameScene* gameScene = userdata;
    if (buttonMenuItem)
    {
        int selected_option = playdate->system->getMenuItemValue(buttonMenuItem);

        if (selected_option != 1)
        {
            gameScene->button_hold_mode = selected_option;
            gameScene->button_hold_frames_remaining = 15;
            playdate->system->setMenuItemValue(buttonMenuItem, 1);
        }
    }
}

static void CB_GameScene_menu(void* object)
{
    CB_GameScene* gameScene = object;

    if (gameScene->menuImage != NULL)
    {
        playdate->graphics->freeBitmap(gameScene->menuImage);
        gameScene->menuImage = NULL;
    }

    gameScene->scene->forceFullRefresh = true;

    playdate->system->removeAllMenuItems();

    if (gameScene->state == CB_GameSceneStateError)
    {
        if (!CB_App->bundled_rom)
        {
            playdate->system->addMenuItem("Library", CB_GameScene_didSelectLibrary, gameScene);
        }
        return;
    }
    
    if (!CB_App->bundled_rom)
    {
        playdate->system->addMenuItem("Library", CB_GameScene_didSelectLibrary, gameScene);
    }
    if (preferences_bundle_hidden != (preferences_bitfield_t)-1)
    {
        // not sure what might happen if some settings are changed in an unusual game scene state.
        // best not find out.
        if (gameScene->state == CB_GameSceneStateLoaded)
        {
            playdate->system->addMenuItem("Settings", CB_GameScene_showSettings, gameScene);
        }
    }
    else
    {
        playdate->system->addMenuItem("About", CB_showCredits, gameScene);
    }
    
    unsigned script_menu_flags = script_menu(gameScene->script, gameScene);

    if (game_menu_button_input_enabled && gameScene->state == CB_GameSceneStateLoaded && !(script_menu_flags & SCRIPT_MENU_SUPPRESS_BUTTON))
    {
        buttonMenuItem = playdate->system->addOptionsMenuItem(
            "Button", buttonMenuOptions, 4, CB_GameScene_buttonMenuCallback, gameScene
        );
        playdate->system->setMenuItemValue(buttonMenuItem, gameScene->button_hold_mode);
    }

    if (!(script_menu_flags & SCRIPT_MENU_SUPPRESS_IMAGE))
    {
        if (gameScene->menuImage == NULL)
        {
            CB_LoadedCoverArt cover_art = {.bitmap = NULL};
            char* actual_cover_path = NULL;

            // --- Get Cover Art ---

            bool has_cover_art = false;
            if (CB_App->coverArtCache.rom_path &&
                strcmp(CB_App->coverArtCache.rom_path, gameScene->rom_filename) == 0 &&
                CB_App->coverArtCache.art.status == CB_COVER_ART_SUCCESS &&
                CB_App->coverArtCache.art.bitmap != NULL)
            {
                has_cover_art = true;
            }

            // --- Get Save Times ---

            unsigned int last_cartridge_save_time = 0;
            if (gameScene->cartridge_has_battery)
            {
                last_cartridge_save_time = gameScene->last_save_time;
            }

            unsigned int last_state_save_time = 0;
            for (int i = 0; i < SAVE_STATE_SLOT_COUNT; ++i)
            {
                last_state_save_time =
                    MAX(last_state_save_time, get_save_state_timestamp(gameScene, i));
            }

            bool show_time_info = false;
            const char* line1_text = NULL;
            unsigned int final_timestamp = 0;

            if (last_state_save_time > last_cartridge_save_time)
            {
                show_time_info = true;
                final_timestamp = last_state_save_time;
                line1_text = "Last save state:";
            }
            else if (last_cartridge_save_time > 0)
            {
                show_time_info = true;
                final_timestamp = last_cartridge_save_time;
                line1_text = "Cartridge data stored:";
            }

            // --- Drawing Logic ---
            if (has_cover_art || show_time_info)
            {
                gameScene->menuImage = playdate->graphics->newBitmap(400, 240, kColorClear);
                if (gameScene->menuImage != NULL)
                {
                    playdate->graphics->pushContext(gameScene->menuImage);
                    playdate->graphics->setDrawMode(kDrawModeCopy);

                    const int content_top = 40;
                    const int content_height = 160;

                    int cover_art_y = 0;
                    int cover_art_height = 0;

                    if (has_cover_art)
                    {
                        playdate->graphics->fillRect(0, 0, 400, 240, kColorBlack);

                        CB_LoadedCoverArt* cached_art = &CB_App->coverArtCache.art;

                        const int max_width = 200;
                        const int max_height = 200;

                        float scale_x = (float)max_width / cached_art->scaled_width;
                        float scale_y = (float)max_height / cached_art->scaled_height;
                        float scale = fminf(scale_x, scale_y);

                        int final_width = (int)(cached_art->scaled_width * scale);
                        int final_height = (int)(cached_art->scaled_height * scale);

                        int art_x = (200 - final_width) / 2;
                        if (!show_time_info)
                        {
                            cover_art_y = content_top + (content_height - final_height) / 2;
                        }

                        playdate->graphics->drawScaledBitmap(
                            cached_art->bitmap, art_x, cover_art_y, scale, scale
                        );

                        cover_art_height = final_height;
                    }
                    else if (show_time_info)
                    {
                        LCDBitmap* ditherOverlay = playdate->graphics->newBitmap(400, 240, kColorWhite);
                        if (ditherOverlay)
                        {
                            int width, height, rowbytes;
                            uint8_t* overlayData;
                            playdate->graphics->getBitmapData(
                                ditherOverlay, &width, &height, &rowbytes, NULL, &overlayData
                            );

                            for (int y = 0; y < height; ++y)
                            {
                                uint8_t pattern_byte = (y % 2 == 0) ? 0xAA : 0x55;
                                uint8_t* row = overlayData + y * rowbytes;
                                memset(row, pattern_byte, rowbytes);
                            }

                            playdate->graphics->setDrawMode(kDrawModeWhiteTransparent);
                            playdate->graphics->drawBitmap(ditherOverlay, 0, 0, kBitmapUnflipped);
                            playdate->graphics->setDrawMode(kDrawModeCopy);
                            playdate->graphics->freeBitmap(ditherOverlay);
                        }
                    }

                    // 2. Draw Save Time if it exists
                    if (show_time_info)
                    {
                        playdate->graphics->setFont(CB_App->labelFont);
                        const char* line1 = line1_text;

                        unsigned current_time = playdate->system->getSecondsSinceEpoch(NULL);

                        const int max_human_time = 60 * 60 * 24 * 10;

                        unsigned use_absolute_time = (current_time < final_timestamp) ||
                                                    (final_timestamp + max_human_time < current_time);

                        char line2[40];
                        if (use_absolute_time)
                        {
                            unsigned int utc_epoch = final_timestamp;
                            int32_t offset = playdate->system->getTimezoneOffset();
                            unsigned int local_epoch = utc_epoch + offset;

                            struct PDDateTime time_info;
                            playdate->system->convertEpochToDateTime(local_epoch, &time_info);

                            if (playdate->system->shouldDisplay24HourTime())
                            {
                                snprintf(
                                    line2, sizeof(line2), "%02d.%02d.%d - %02d:%02d:%02d",
                                    time_info.day, time_info.month, time_info.year, time_info.hour,
                                    time_info.minute, time_info.second
                                );
                            }
                            else
                            {
                                const char* suffix = (time_info.hour < 12) ? " am" : " pm";
                                int display_hour = time_info.hour;
                                if (display_hour == 0)
                                {
                                    display_hour = 12;
                                }
                                else if (display_hour > 12)
                                {
                                    display_hour -= 12;
                                }
                                snprintf(
                                    line2, sizeof(line2), "%02d.%02d.%d - %d:%02d:%02d%s",
                                    time_info.day, time_info.month, time_info.year, display_hour,
                                    time_info.minute, time_info.second, suffix
                                );
                            }
                        }
                        else
                        {
                            char* human_time = en_human_time(current_time - final_timestamp);
                            snprintf(line2, sizeof(line2), "%s ago", human_time);
                            cb_free(human_time);
                        }

                        int font_height = playdate->graphics->getFontHeight(CB_App->labelFont);
                        int line1_width = playdate->graphics->getTextWidth(
                            CB_App->labelFont, line1, strlen(line1), kUTF8Encoding, 0
                        );
                        int line2_width = playdate->graphics->getTextWidth(
                            CB_App->labelFont, line2, strlen(line2), kUTF8Encoding, 0
                        );
                        int text_spacing = 4;
                        int text_block_height = font_height * 2 + text_spacing;

                        if (has_cover_art)
                        {
                            playdate->graphics->setDrawMode(kDrawModeFillWhite);
                            int text_y = cover_art_y + cover_art_height + 6;
                            playdate->graphics->drawText(
                                line1, strlen(line1), kUTF8Encoding, (200 - line1_width) / 2, text_y
                            );
                            playdate->graphics->drawText(
                                line2, strlen(line2), kUTF8Encoding, (200 - line2_width) / 2,
                                text_y + font_height + text_spacing
                            );
                        }
                        else
                        {
                            int padding_x = 10;
                            int padding_y = 8;
                            int black_border_size = 2;
                            int white_border_size = 1;

                            int box_width = CB_MAX(line1_width, line2_width) + (padding_x * 2);
                            int box_height = text_block_height + (padding_y * 2);

                            int total_border_size = black_border_size + white_border_size;
                            int total_width = box_width + (total_border_size * 2);
                            int total_height = box_height + (total_border_size * 2);

                            int final_box_x = (200 - total_width + 1) / 2;
                            int final_box_y = content_top + (content_height - total_height) / 2;

                            playdate->graphics->fillRect(
                                final_box_x, final_box_y, total_width, total_height, kColorWhite
                            );

                            playdate->graphics->fillRect(
                                final_box_x + white_border_size, final_box_y + white_border_size,
                                box_width + (black_border_size * 2),
                                box_height + (black_border_size * 2), kColorBlack
                            );

                            playdate->graphics->fillRect(
                                final_box_x + total_border_size, final_box_y + total_border_size,
                                box_width, box_height, kColorWhite
                            );

                            playdate->graphics->setDrawMode(kDrawModeFillBlack);

                            int text_y = final_box_y + total_border_size + padding_y;
                            playdate->graphics->drawText(
                                line1, strlen(line1), kUTF8Encoding,
                                final_box_x + total_border_size + (box_width - line1_width) / 2, text_y
                            );
                            playdate->graphics->drawText(
                                line2, strlen(line2), kUTF8Encoding,
                                final_box_x + total_border_size + (box_width - line2_width) / 2,
                                text_y + font_height + text_spacing
                            );
                        }
                    }
                    playdate->graphics->popContext();
                }
            }
        }
        playdate->system->setMenuImage(gameScene->menuImage, 0);
    }
}

static void CB_GameScene_generateBitmask(void)
{
    if (CB_GameScene_bitmask_done)
    {
        return;
    }

    CB_GameScene_bitmask_done = true;

    for (int colour = 0; colour < 4; colour++)
    {
        for (int y = 0; y < 4; y++)
        {
            int x_offset = 0;

            for (int i = 0; i < 4; i++)
            {
                int mask = 0x00;

                for (int x = 0; x < 2; x++)
                {
                    if (CB_patterns[colour][y][x_offset + x] == 1)
                    {
                        int n = i * 2 + x;
                        mask |= (1 << (7 - n));
                    }
                }

                CB_bitmask[colour][i][y] = mask;

                x_offset ^= 2;
            }
        }
    }
}

__section__(".rare") static unsigned get_save_state_timestamp_(
    CB_GameScene* gameScene, unsigned slot
)
{
    char* path;
    playdate->system->formatString(
        &path, "%s/%s.%u.state", cb_gb_directory_path(CB_statesPath), gameScene->base_filename, slot
    );

    SDFile* file = playdate->file->open(path, kFileReadData);

    cb_free(path);

    if (!file)
    {
        return 0;
    }

    struct StateHeader header;
    int read = playdate->file->read(file, &header, sizeof(header));
    playdate->file->close(file);
    if (read < sizeof(header))
    {
        return 0;
    }
    else
    {
        return header.timestamp;
    }
}

__section__(".rare") unsigned get_save_state_timestamp(CB_GameScene* gameScene, unsigned slot)
{
    return (unsigned)call_with_main_stack_2(get_save_state_timestamp_, gameScene, slot);
}

static size_t get_save_state_size_including_script(CB_GameScene* gameScene)
{
    CB_GameSceneContext* context = gameScene->context;
    int save_size = gb_get_state_size(context->gb);
    if (save_size <= 0) return 0;
    
    if (gameScene->script)
    {
        save_size += script_query_savestate_size(gameScene->script);
    }
    return save_size;
}

// returns true if successful
__section__(".rare") static bool save_state_(CB_GameScene* gameScene, unsigned slot)
{
    if (gameScene->isCurrentlySaving)
    {
        playdate->system->logToConsole("Save state failed: another save is in progress.");
        return false;
    }

    gameScene->isCurrentlySaving = true;

    CB_GameSceneContext* context = gameScene->context;

    bool success = false;

    char* path_prefix = NULL;
    char* state_name = NULL;
    char* tmp_name = NULL;
    char* bak_name = NULL;
    char* thumb_name = NULL;
    char* buff = NULL;

    playdate->system->formatString(
        &path_prefix, "%s/%s.%u", cb_gb_directory_path(CB_statesPath), gameScene->base_filename, slot
    );

    playdate->system->formatString(&state_name, "%s.state", path_prefix);
    playdate->system->formatString(&tmp_name, "%s.tmp", path_prefix);
    playdate->system->formatString(&thumb_name, "%s.thumb", path_prefix);
    playdate->system->formatString(&bak_name, "%s.bak", path_prefix);

    // Clean up any old temp file
    playdate->file->unlink(tmp_name, false);

    int save_size = gb_get_state_size(context->gb);
    if (save_size <= 0)
    {
        playdate->system->logToConsole("Save state failed: invalid save size.");
        goto cleanup;
    }
    
    int script_size = 0;
    if (gameScene->script)
    {
        script_size = script_query_savestate_size(gameScene->script);
    }

    buff = cb_malloc(save_size + script_size);
    if (!buff)
    {
        playdate->system->logToConsole("Failed to allocate buffer for save state");
        goto cleanup;
    }

    if (gameScene->script && !script_save_state(gameScene->script, (void*)(buff + save_size)))
    {
        playdate->system->logToConsole("Script error while saving state");
        goto cleanup;
    }
    gb_state_save(context->gb, buff);

    struct StateHeader* header = (struct StateHeader*)buff;
    header->timestamp = playdate->system->getSecondsSinceEpoch(NULL);
    header->script = (preferences_script_support && context->scene->script);
    header->cgb = context->gb->is_cgb_mode;
    header->script_save_data_size = script_size;

    // Write the state to the temporary file
    SDFile* file = playdate->file->open(tmp_name, kFileWrite);
    if (!file)
    {
        playdate->system->logToConsole(
            "failed to open temp state file \"%s\": %s", tmp_name, playdate->file->geterr()
        );
    }
    else
    {
        save_size += script_size;
        
        int written = playdate->file->write(file, buff, save_size);
        playdate->file->close(file);

        // Verify that the temporary file was written correctly
        if (written != save_size)
        {
            playdate->system->logToConsole(
                "Error writing temp state file \"%s\" (wrote %d of %d bytes). "
                "Aborting.",
                tmp_name, written, save_size
            );
            playdate->file->unlink(tmp_name, false);
        }
        else
        {
            // Rename files: .state -> .bak, then .tmp -> .state
            playdate->system->logToConsole("Temp state saved, renaming files.");
            playdate->file->unlink(bak_name, false);
            playdate->file->rename(state_name, bak_name);
            if (playdate->file->rename(tmp_name, state_name) == 0)
            {
                success = true;
            }
            else
            {
                playdate->system->logToConsole(
                    "CRITICAL: Failed to rename temp state file. Restoring "
                    "backup."
                );
                playdate->file->rename(bak_name, state_name);
            }
        }
    }

    // we check playtime nonzero so that LCD has been updated at least once
    uint8_t* lcd = context->gb->lcd;
    if (success && lcd && gameScene->playtime > 1)
    {
        // save thumbnail, too
        // (inessential, so we don't take safety precautions)
        SDFile* file = playdate->file->open(thumb_name, kFileWrite);

        static const uint8_t dither_pattern[5] = {
            0b00000000 ^ 0xFF, 0b01000100 ^ 0xFF, 0b10101010 ^ 0xFF,
            0b11011101 ^ 0xFF, 0b11111111 ^ 0xFF,
        };

        if (file)
        {
            for (unsigned y = 0; y < SAVE_STATE_THUMBNAIL_H; ++y)
            {
                uint8_t* line0 = lcd + y * LCD_WIDTH_PACKED;

                u8 thumbline[(SAVE_STATE_THUMBNAIL_W + 7) / 8];
                memset(thumbline, 0, sizeof(thumbline));

                for (unsigned x = 0; x < SAVE_STATE_THUMBNAIL_W; ++x)
                {
                    // very bespoke dithering algorithm lol
                    u8 p0, p1;
                    if (context->gb->is_cgb_mode)
                    {
                        p0 = __gb_get_pixel__cgb(line0, x);
                        p1 = __gb_get_pixel__cgb(line0, x ^ 1);
                    }
                    else
                    {
                        p0 = __gb_get_pixel__dmg(line0, x);
                        p1 = __gb_get_pixel__dmg(line0, x ^ 1);
                    }

                    u8 val = p0;
                    if (val >= 2)
                        val++;
                    if (val == 1 && p1 >= 2)
                        ++val;
                    if (val == 3 && p1 < 2)
                        --val;

                    u8 pattern = dither_pattern[val];
                    if (y % 2 == 1)
                    {
                        if (val == 2)
                            pattern = (pattern >> 1) | (pattern << 7);
                        else
                            pattern = (pattern >> 2) | (pattern << 6);
                    }

                    u8 pix = (pattern >> (x % 8)) & 1;

                    thumbline[x / 8] |= pix << (7 - (x % 8));
                }

                playdate->file->write(file, thumbline, sizeof(thumbline));
            }
        }

        playdate->file->close(file);
    }

cleanup:
    if (path_prefix)
        cb_free(path_prefix);
    if (state_name)
        cb_free(state_name);
    if (tmp_name)
        cb_free(tmp_name);
    if (bak_name)
        cb_free(bak_name);
    if (thumb_name)
        cb_free(thumb_name);
    if (buff)
        cb_free(buff);

    gameScene->isCurrentlySaving = false;
    return success;
}

// returns true if successful
__section__(".rare") bool save_state(CB_GameScene* gameScene, unsigned slot)
{
    return (bool)call_with_main_stack_2(save_state_, gameScene, slot);
    gameScene->playtime = 0;
}

__section__(".rare") bool load_state_thumbnail_(
    CB_GameScene* gameScene, unsigned slot, uint8_t* out
)
{
    char* path;
    playdate->system->formatString(
        &path, "%s/%s.%u.thumb", cb_gb_directory_path(CB_statesPath), gameScene->base_filename, slot
    );

    SDFile* file = playdate->file->open(path, kFileReadData);

    cb_free(path);

    if (!file)
    {
        return 0;
    }

    int count = SAVE_STATE_THUMBNAIL_H * ((SAVE_STATE_THUMBNAIL_W + 7) / 8);
    int read = playdate->file->read(file, out, count);
    playdate->file->close(file);

    return read == count;
}

// returns true if successful
__section__(".rare") bool load_state_thumbnail(CB_GameScene* gameScene, unsigned slot, uint8_t* out)
{
    return (bool)call_with_main_stack_3(load_state_thumbnail_, gameScene, slot, out);
}

// returns true if successful
__section__(".rare") bool load_state(CB_GameScene* gameScene, unsigned slot)
{
    gameScene->playtime = 0;
    CB_GameSceneContext* context = gameScene->context;
    char* state_name;
    playdate->system->formatString(
        &state_name, "%s/%s.%u.state", cb_gb_directory_path(CB_statesPath), gameScene->base_filename, slot
    );
    bool success = false;

    int save_state_size = gb_get_state_size(context->gb);
    SDFile* file = playdate->file->open(state_name, kFileReadData);
    if (!file)
    {
        playdate->system->logToConsole(
            "failed to open save state file \"%s\": %s", state_name, playdate->file->geterr()
        );
    }
    else
    {
        playdate->file->seek(file, 0, SEEK_END);
        int file_size = playdate->file->tell(file);
        if (file_size > 0)
        {
            if (playdate->file->seek(file, 0, SEEK_SET))
            {
                playdate->system->logToConsole(
                    "Failed to seek to start of state file \"%s\": %s", state_name,
                    playdate->file->geterr()
                );
            }
            else
            {
                success = true;
                int size_remaining = file_size;
                char* buff = cb_malloc(file_size);
                if (buff == NULL)
                {
                    playdate->system->logToConsole("Failed to allocate save state buffer");
                }
                else
                {
                    char* buffptr = buff;
                    while (size_remaining > 0)
                    {
                        int read = playdate->file->read(file, buffptr, size_remaining);
                        if (read == 0)
                        {
                            playdate->system->logToConsole(
                                "Error, read 0 bytes from save file, \"%s\"\n", state_name
                            );
                            success = false;
                            break;
                        }
                        if (read < 0)
                        {
                            playdate->system->logToConsole(
                                "Error reading save file \"%s\": %s\n", state_name,
                                playdate->file->geterr()
                            );
                            success = false;
                            break;
                        }
                        size_remaining -= read;
                        buffptr += read;
                    }

                    if (success)
                    {
                        struct StateHeader* header = (struct StateHeader*)buff;
                        
                        if (header->script_save_data_size > file_size)
                        {
                            success = false;
                            CB_presentModal(
                                CB_Modal_new("Invalid script custom data size in file", NULL, NULL, NULL)->scene
                            );
                        }
                        else
                        {
                            unsigned int loaded_timestamp = header->timestamp;

                            if (loaded_timestamp > 0)
                            {
                                playdate->system->logToConsole(
                                    "Save state had been created at: %u", loaded_timestamp
                                );
                            }
                            else
                            {
                                playdate->system->logToConsole(
                                    "Save state is from an old version (no "
                                    "timestamp)."
                                );
                            }

                            const char* res = gb_state_load(context->gb, buff, file_size - header->script_save_data_size);
                            if (res)
                            {
                                success = false;
                                playdate->system->logToConsole("Error loading state! %s", res);

                                char* details = NULL;
                                playdate->system->formatString(&details, "%s", res);

                                if (details)
                                {
                                    // First modal: generic message + OK/Details
                                    CB_presentModal(CB_Modal_new(
                                                        "Failed to load state.", loadStateErrorOptions,
                                                        CB_LoadStateErrorModalCallback, details
                                    )
                                                        ->scene);
                                }
                                else
                                {
                                    // Fallback: 1-button modal
                                    CB_presentModal(
                                        CB_Modal_new("Failed to load state.", NULL, NULL, NULL)->scene
                                    );
                                }
                            }
                            else if (gameScene->script)
                            {
                                const char* scriptbuff = buff + save_state_size;
                                if (file_size - save_state_size != header->script_save_data_size)
                                {
                                    success = false;
                                    
                                    CB_presentModal(
                                        CB_Modal_new("Script custom state missing from state file.", NULL, NULL, NULL)->scene
                                    );
                                }
                                else if (!script_load_state(gameScene->script, (void*)scriptbuff, header->script_save_data_size))
                                {
                                    success = false;
                                    
                                    CB_presentModal(
                                        CB_Modal_new("Failed to load script's custom state.", NULL, NULL, NULL)->scene
                                    );
                                }
                            }
                        }
                    }

                    cb_free(buff);
                }
            }
        }
        else
        {
            playdate->system->logToConsole("Failed to determine file size");
        }

        playdate->file->close(file);
    }

    cb_free(state_name);
    return success;
}

__section__(".rare") static
bool CB_GameScene_lock(void* object)
{
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;
    
    if (preferences_lock_button != PREF_BUTTON_NONE)
    {
        gameScene->lock_button_hold_frames_remaining = 8;
        return true;
    }
    
    return false;
}

__section__(".rare") static
void CB_GameScene_event(void* object, PDSystemEvent event, uint32_t arg)
{
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;

    switch (event)
    {
    case kEventLock:
        if (CB_App->hasSystemAccess && preferences_lock_button != PREF_BUTTON_NONE) return;
        // fallthrough
    case kEventPause:
        audioGameScene = NULL;

        // Re-enable auto-lock when the system menu is open.
        playdate->system->setAutoLockDisabled(0);
        
        gameScene->lock_button_hold_frames_remaining = 0;

        // fall-through
    case kEventTerminate:
        DTCM_VERIFY();
        if (context->gb->direct.sram_dirty && gameScene->save_data_loaded_successfully)
        {
            playdate->system->logToConsole("saving (system event)");
            gb_save_to_disk(context->gb);
        }
        DTCM_VERIFY();
        break;
    case kEventUnlock:
    case kEventResume:
        // Re-apply the user's auto-lock preference on resume.
        playdate->system->setAutoLockDisabled(preferences_disable_autolock);
        if (gameScene->audioEnabled)
        {
            audioGameScene = gameScene;

            // If the buffered audio sync is the active mode upon leaving the settings,
            // we MUST reset our timing baseline. This recalibrates our sample counter
            // against the hardware clock, closing the "time gap" that was created
            // while the device was locked or the system menu was open.
            if (preferences_audio_sync == 1)
            {
                CB_reset_audio_sync_state();
            }
        }
        break;
    case kEventLowPower:
        if (context->gb->direct.sram_dirty && gameScene->save_data_loaded_successfully)
        {
            // save a recovery file
            char* recovery_filename = cb_save_filename(context->scene->rom_filename, true);
            write_cart_ram_file(recovery_filename, context->gb);
            cb_free(recovery_filename);
        }
        break;
    case kEventMirrorStarted:
        gameScene->is_mirroring = true;
        {
            int headphones;
            playdate->sound->getHeadphoneState(&headphones, NULL, CB_headphone_state_changed);
            reconfigure_audio_source(gameScene, headphones);
        }
        break;

    case kEventMirrorEnded:
        gameScene->is_mirroring = false;
        {
            int headphones;
            playdate->sound->getHeadphoneState(&headphones, NULL, CB_headphone_state_changed);
            reconfigure_audio_source(gameScene, headphones);
        }
        break;
    case kEventKeyPressed:
        playdate->system->logToConsole("Key pressed: %x\n", (unsigned)arg);

        switch (arg)
        {
        case 0x35:  // 5
            if (save_state(gameScene, 0))
            {
                playdate->system->logToConsole("Save state %d successful", 0);
            }
            else
            {
                playdate->system->logToConsole("Save state %d failed", 0);
            }
            break;
        case 0x37:  // 7
            if (load_state(gameScene, 0))
            {
                playdate->system->logToConsole("Load state %d successful", 0);
            }
            else
            {
                playdate->system->logToConsole("Load state %d failed", 0);
            }
            break;
        case 0x76: // V
            {
                __gb_dump_vram(context->gb);
            }
#if ENABLE_RENDER_PROFILER
        case 0x39:  // 9
            playdate->system->logToConsole("Profiler triggered. Will run on next frame.");
            CB_run_profiler_on_next_frame = true;
            break;
#endif
        }
    default:
        break;
    }
}

static void CB_GameScene_free(void* object)
{
    DTCM_VERIFY();
    CB_GameScene* gameScene = object;
    CB_GameSceneContext* context = gameScene->context;

    playdate->system->setAutoLockDisabled(0);

    if (audioGameScene == gameScene)
    {
        if (CB_App->soundSource != NULL)
        {
            playdate->sound->removeSource(CB_App->soundSource);
            CB_App->soundSource = NULL;
        }

        if (preferences_audio_sync == 1)
        {
            CB_reset_audio_sync_state();
            memset(g_audio_sync_buffer.left, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
            memset(g_audio_sync_buffer.right, 0, AUDIO_RING_BUFFER_SIZE * sizeof(int16_t));
        }

        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 1.0f);

        audioGameScene = NULL;
        audio_enabled = 0;
    }

    prefs_locked_by_script = 0;
    preferences_read_from_disk(CB_globalPrefsPath);
    preferences_per_game = 0;
    preferences_save_state_slot = 0;
    gb_save_to_disk(context->gb);
    preferences_save_slot = 0;

    if (gameScene->menuImage)
    {
        playdate->graphics->freeBitmap(gameScene->menuImage);
    }

    playdate->system->setMenuImage(NULL, 0);

    CB_Scene_free(gameScene->scene);

    gb_reset(context->gb, context->cgb_mode);

    cb_free(gameScene->rom_filename);
    cb_free(gameScene->save_filename);
    cb_free(gameScene->base_filename);
    cb_free(gameScene->settings_filename);
    cb_free(gameScene->name_short);

    if (context->rom)
    {
        cb_free(context->rom);
    }

    if (context->cart_ram)
    {
        cb_free(context->cart_ram);
    }

    if (preferences_script_support && gameScene->script)
    {
        script_end(gameScene->script, gameScene);
        gameScene->script = NULL;
    }

    cb_free(context);
    cb_free(gameScene);

    dtcm_deinit();

    DTCM_VERIFY();
}

__section__(".rare") void __gb_dump_vram(gb_s* gb)
{
    playdate->system->logToConsole("dumping vram to vram.bin");
    
    // reverse byte order of appropriate bytes
    for (int pass = 0; pass <= 1; ++pass)
    {
        for (int b = 0; b <= gb->is_cgb_mode; ++b)
        {
            for (int i = 0; i < 0x1800; ++i)
            {
                gb->vram[i | (0x2000 * b)] = reverse_bits_u8(gb->vram[i | (0x2000 * b)]);
            }
        }
        
        if (pass == 0)
            call_with_main_stack_3(cb_write_entire_file, "vram.bin", gb->vram, (gb->is_cgb_mode) ? VRAM_SIZE_CGB : VRAM_SIZE);
    }
}

__section__(".rare") void __gb_on_breakpoint(gb_s* gb, int breakpoint_number)
{
    CB_GameSceneContext* context = gb->direct.priv;
    CB_GameScene* gameScene = context->scene;

    CB_ASSERT(gameScene->context == context);
    CB_ASSERT(gameScene->context->scene == gameScene);
    CB_ASSERT(gameScene->context->gb->direct.priv == context);
    CB_ASSERT(gameScene->context->gb == gb);

    if (preferences_script_support && gameScene->script)
    {
        call_with_user_stack_2(script_on_breakpoint, gameScene, breakpoint_number);
    }
}

void show_game_script_info(const char* rompath, const char* name_short)
{
    ScriptInfo* info = script_get_info_by_rom_path(rompath);
    if (!info)
        return;

    if (!info->info)
    {
        script_info_free(info);
        return;
    }

    char* text = NULL;

    // Check if name_short was provided and is not an empty string
    if (name_short && name_short[0] != '\0')
    {
        text = aprintf("Script information:\n\n%s", info->info);
    }
    else
    {
        // Fallback to just the rom_name if name_short is not available
        text = aprintf("Script information:\n\n%s", info->info);
    }

    script_info_free(info);
    if (!text)
        return;

    CB_InfoScene* infoScene = CB_InfoScene_new(name_short, text);

    cb_free(text);

    CB_presentModal(infoScene->scene);
}
