//
//  game_scene.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 14/05/22.
//  Maintained and developed by the CrankBoy dev team.
//

#define PGB_IMPL

#include "../minigb_apu/minigb_apu.h"
#include "../peanut_gb/peanut_gb.h"
#include "app.h"
#include "credits_scene.h"
#include "dtcm.h"
#include "info_scene.h"
#include "library_scene.h"
#include "modal.h"
#include "preferences.h"
#include "revcheck.h"
#include "script.h"
#include "settings_scene.h"
#include "userstack.h"
#include "utility.h"

#include <stdlib.h>
#include <string.h>

// clang-format off
#include "game_scene.h"
// clang-format on

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

PGB_GameScene* audioGameScene = NULL;

static void PGB_GameScene_selector_init(PGB_GameScene* gameScene);
static void PGB_GameScene_update(void* object, uint32_t u32enc_dt);
static void PGB_GameScene_menu(void* object);
static void PGB_GameScene_generateBitmask(void);
static void PGB_GameScene_free(void* object);
static void PGB_GameScene_event(void* object, PDSystemEvent event, uint32_t arg);

static uint8_t* read_rom_to_ram(
    const char* filename, PGB_GameSceneError* sceneError, size_t* o_rom_size
);

// returns 0 if no pre-existing save data;
// returns 1 if data found and loaded, but not RTC
// returns 2 if data and RTC loaded
// returns -1 on error
static int read_cart_ram_file(
    const char* save_filename, struct gb_s* gb, unsigned int* last_save_time
);
static void write_cart_ram_file(const char* save_filename, struct gb_s* gb);

static void gb_error(struct gb_s* gb, const enum gb_error_e gb_err, const uint16_t val);
static void gb_save_to_disk(struct gb_s* gb);

static const char* startButtonText = "start";
static const char* selectButtonText = "select";

static uint8_t PGB_dither_lut_row0[256];
static uint8_t PGB_dither_lut_row1[256];

const uint16_t PGB_dither_lut_c0[] = {
    (0b1111 << 0) | (0b0111 << 4) | (0b0001 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),

    // L
    (0b1111 << 0) | (0b0111 << 4) | (0b0101 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),

    // D
    (0b1111 << 0) | (0b0101 << 4) | (0b0001 << 8) | (0b0000 << 12),
    (0b1111 << 0) | (0b0101 << 4) | (0b0101 << 8) | (0b0000 << 12),
};

const uint16_t PGB_dither_lut_c1[] = {
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
    uint32_t dither_lut = PGB_dither_lut_c0[preferences_dither_pattern] |
                          ((uint32_t)PGB_dither_lut_c1[preferences_dither_pattern] << 16);

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
        PGB_dither_lut_row0[orgpixels_int] = p0;

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
        PGB_dither_lut_row1[orgpixels_int] = p1;
    }
}

static uint8_t PGB_bitmask[4][4][4];
static bool PGB_GameScene_bitmask_done = false;

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
static bool PGB_run_profiler_on_next_frame = false;
#endif

#if ITCM_CORE
void* core_itcm_reloc = NULL;

__section__(".rare") void itcm_core_init()
{
    // ITCM seems to crash Rev B (not anymore it seems), so we leave this is an option
    if (!dtcm_enabled() || !preferences_itcm)
    {
        // just use original non-relocated code
        core_itcm_reloc = (void*)&__itcm_start;
        playdate->system->logToConsole("itcm_core_init but dtcm not enabled");
        return;
    }

    if (core_itcm_reloc == (void*)&__itcm_start)
        core_itcm_reloc = NULL;

    if (core_itcm_reloc != NULL)
        return;

    // paranoia
    int MARGIN = 4;

    // make region to copy instructions to; ensure it has same cache alignment
    core_itcm_reloc = dtcm_alloc_aligned(itcm_core_size + MARGIN, (uintptr_t)&__itcm_start);
    DTCM_VERIFY();
    memcpy(core_itcm_reloc, __itcm_start, itcm_core_size);
    DTCM_VERIFY();
    playdate->system->logToConsole(
        "itcm start: %x, end %x: run_frame: %x", &__itcm_start, &__itcm_end, &gb_run_frame
    );
    playdate->system->logToConsole(
        "core is 0x%X bytes, relocated at 0x%X", itcm_core_size, core_itcm_reloc
    );
    playdate->system->clearICache();
}
#else
void itcm_core_init(void)
{
}
#endif

// Helper function to generate the config file path for a game
char* pgb_game_config_path(const char* rom_filename)
{
    char* basename = pgb_basename(rom_filename, true);
    char* path;
    playdate->system->formatString(&path, "%s/%s.json", PGB_settingsPath, basename);
    pgb_free(basename);
    return path;
}

static LCDBitmap* numbers_bmp = NULL;
static uint32_t last_fps_digits;
static uint8_t fps_draw_timer;

PGB_GameScene* PGB_GameScene_new(const char* rom_filename, char* name_short)
{
    playdate->system->logToConsole("ROM: %s", rom_filename);

    if (!numbers_bmp)
    {
        numbers_bmp = playdate->graphics->loadBitmap("fonts/numbers", NULL);
    }

    if (!DTCM_VERIFY_DEBUG())
        return NULL;

    PGB_Scene* scene = PGB_Scene_new();

    PGB_GameScene* gameScene = pgb_malloc(sizeof(PGB_GameScene));
    memset(gameScene, 0, sizeof(*gameScene));
    gameScene->scene = scene;
    scene->managedObject = gameScene;

    scene->update = PGB_GameScene_update;
    scene->menu = PGB_GameScene_menu;
    scene->free = PGB_GameScene_free;
    scene->event = PGB_GameScene_event;
    scene->use_user_stack = 0;  // user stack is slower

    scene->preferredRefreshRate = 30;

    gameScene->rom_filename = string_copy(rom_filename);
    gameScene->name_short = string_copy(name_short);
    gameScene->save_filename = NULL;

    gameScene->state = PGB_GameSceneStateError;
    gameScene->error = PGB_GameSceneErrorUndefined;

    gameScene->model = (PGB_GameSceneModel){.state = PGB_GameSceneStateError,
                                            .error = PGB_GameSceneErrorUndefined,
                                            .selectorIndex = 0,
                                            .empty = true};

    gameScene->audioEnabled = (preferences_sound_mode > 0);
    gameScene->audioLocked = false;
    gameScene->button_hold_mode = 1;  // None
    gameScene->button_hold_frames_remaining = 0;

    gameScene->crank_turbo_accumulator = 0.0f;
    gameScene->crank_turbo_a_active = false;
    gameScene->crank_turbo_b_active = false;
    gameScene->crank_was_docked = playdate->system->isCrankDocked();

    gameScene->interlace_tendency_counter = 0;
    gameScene->interlace_lock_frames_remaining = 0;
    gameScene->previous_scale_line_index = -1;

    gameScene->isCurrentlySaving = false;

    gameScene->menuImage = NULL;

    gameScene->staticSelectorUIDrawn = false;

    gameScene->save_data_loaded_successfully = false;
    
    prefs_locked_by_script = 0;

    // Global settings are loaded by default. Check for a game-specific file.
    gameScene->settings_filename = pgb_game_config_path(rom_filename);

    if (!PGB_App->bundled_rom)
    {
        // Try loading game-specific preferences
        preferences_per_game = 0;
        call_with_user_stack_1(preferences_read_from_disk, gameScene->settings_filename);

        // we always use the per-game save slot, even if global settings are enabled
        void* stored_save_slot = preferences_store_subset(PREFBIT_save_state_slot);

        // If the game-specific settings explicitly says "use Global"
        // (or there is no game-specific settings file),
        // load the global preferences file instead.
        if (preferences_per_game == 0)
        {
            call_with_user_stack_1(preferences_read_from_disk, PGB_globalPrefsPath);
        }

        if (stored_save_slot)
        {
            preferences_restore_subset(stored_save_slot);
            pgb_free(stored_save_slot);
        }
    }
    else
    {
        // bundled ROMs always use global preferences
        call_with_user_stack_1(preferences_read_from_disk, PGB_globalPrefsPath);
    }

    PGB_GameScene_generateBitmask();

    generate_dither_luts();

    PGB_GameScene_selector_init(gameScene);

#if PGB_DEBUG && PGB_DEBUG_UPDATED_ROWS
    int highlightWidth = 10;
    gameScene->debug_highlightFrame = PDRectMake(
        PGB_LCD_X - 1 - highlightWidth, 0, highlightWidth, playdate->display->getHeight()
    );
#endif

#if ITCM_CORE
    core_itcm_reloc = NULL;
#endif
    dtcm_deinit();
    dtcm_init();

    DTCM_VERIFY();

    PGB_GameSceneContext* context = pgb_malloc(sizeof(PGB_GameSceneContext));
    struct gb_s* gb;
    static struct gb_s gb_fallback;  // use this gb struct if dtcm alloc not available
    if (dtcm_enabled())
    {
        gb = dtcm_alloc(sizeof(struct gb_s));
    }
    else
    {
        gb = &gb_fallback;
    }

    DTCM_VERIFY();

    itcm_core_init();

    memset(gb, 0, sizeof(struct gb_s));
    DTCM_VERIFY();

    if (PGB_App->soundSource == NULL)
    {
        PGB_App->soundSource = playdate->sound->addSource(audio_callback, &audioGameScene, 0);
    }
    audio_enabled = 1;
    context->gb = gb;
    context->scene = gameScene;
    context->rom = NULL;
    context->cart_ram = NULL;

    gameScene->context = context;

    PGB_GameSceneError romError;
    size_t rom_size;
    uint8_t* rom = read_rom_to_ram(rom_filename, &romError, &rom_size);
    DTCM_VERIFY();
    if (rom)
    {
        playdate->system->logToConsole("Opened ROM.");
        context->rom = rom;

        static uint8_t lcd[LCD_SIZE];
        memset(lcd, 0, sizeof(lcd));

        enum gb_init_error_e gb_ret = gb_init(
            context->gb, context->wram, context->vram, lcd, rom, rom_size, gb_error, context
        );

        if (PGB_App->bootRomData && preferences_bios)
        {
            gb_init_boot_rom(context->gb, PGB_App->bootRomData);
        }

        gb_reset(context->gb);

        playdate->system->logToConsole(
            "Interrupts detected: Joypad=%d\n", context->gb->joypad_interrupt
        );

        if (gb_ret == GB_INIT_NO_ERROR)
        {
            playdate->system->logToConsole("Initialized gb context.");
            char* save_filename = pgb_save_filename(rom_filename, false);
            gameScene->save_filename = save_filename;

            gameScene->base_filename = pgb_basename(rom_filename, true);

            gameScene->cartridge_has_battery = context->gb->cart_battery;
            playdate->system->logToConsole(
                "Cartridge has battery: %s", gameScene->cartridge_has_battery ? "Yes" : "No"
            );

            //      _             ____
            //     / \           /    \,
            //    / ! \         | STOP |
            //   /_____\         \____/
            //      |              |
            //      |              |
            // WARNING -- SEE MESSAGE [7700] IN "game_scene.h" BEFORE ALTERING
            // THIS LINE           |
            //      |              |
            gameScene->save_states_supported = !gameScene->cartridge_has_battery;
            ;

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
                playdate->system->logToConsole(
                    "Error loading save data. To protect your data, the game "
                    "will not start."
                );

                PGB_presentModal(PGB_Modal_new(
                                     "Error loading save data. To protect your "
                                     "data, the game will not start.",
                                     NULL, NULL, NULL
                )
                                     ->scene);

                audioGameScene = NULL;

                if (context->gb && context->gb->gb_cart_ram)
                {
                    pgb_free(context->gb->gb_cart_ram);
                    context->gb->gb_cart_ram = NULL;
                }

                // Now, free the scene and context.
                PGB_GameScene_free(gameScene);
                return NULL;
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
                    playdate->system->logToConsole(
                        "Loaded RTC state and timestamp from save file."
                    );

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

            playdate->system->logToConsole("Initializing audio...");

            DTCM_VERIFY();

            audio_init(&gb->audio);
            PGB_GameScene_apply_settings(gameScene, true);

            if (gameScene->audioEnabled)
            {
                playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 0.2f);
                context->gb->direct.sound = 1;
                audioGameScene = gameScene;
            }

            gb_init_lcd(context->gb);
            memset(context->previous_lcd, 0, sizeof(context->previous_lcd));
            gameScene->state = PGB_GameSceneStateLoaded;

            playdate->system->logToConsole("gb context initialized.");
        }
        else
        {
            gameScene->state = PGB_GameSceneStateError;
            gameScene->error = PGB_GameSceneErrorFatal;

            playdate->system->logToConsole(
                "%s:%i: Error initializing gb context", __FILE__, __LINE__
            );
        }
    }
    else
    {
        playdate->system->logToConsole("Failed to open ROM.");
        gameScene->state = PGB_GameSceneStateError;
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
    script_info_free(scriptInfo);

    if (preferences_script_support && gameScene->script_available)
    {
        char name[17];
        gb_get_rom_name(context->gb->gb_rom, name);
        playdate->system->logToConsole("ROM name: \"%s\"", name);
        gameScene->script = script_begin(name, gameScene);
        gameScene->prev_dt = 0;
        if (!gameScene->script)
        {
            playdate->system->logToConsole("Associated script failed to load or not found.");
        }
    }
#endif
    DTCM_VERIFY();

    PGB_ASSERT(gameScene->context == context);
    PGB_ASSERT(gameScene->context->scene == gameScene);
    PGB_ASSERT(gameScene->context->gb->direct.priv == context);

    return gameScene;
}

void PGB_GameScene_apply_settings(PGB_GameScene* gameScene, bool audio_settings_changed)
{
    PGB_GameSceneContext* context = gameScene->context;

    generate_dither_luts();

    // Reset the audio system to ensure its state is consistent with the new settings.
    if (audio_settings_changed)
    {
        audio_init(&context->gb->audio);
    }

    // Apply sound on/off and sound mode
    bool desiredAudioEnabled = (preferences_sound_mode > 0);
    const char* mode_labels[] = {"Off", "Fast", "Accurate"};
    playdate->system->logToConsole("Audio mode setting: %s", mode_labels[preferences_sound_mode]);
    gameScene->audioEnabled = desiredAudioEnabled;

    if (desiredAudioEnabled)
    {
        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 0.2f);
        context->gb->direct.sound = 1;
        audioGameScene = gameScene;
    }
    else
    {
        playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 0.0f);
        context->gb->direct.sound = 0;
        audioGameScene = NULL;
    }
}

static void PGB_GameScene_selector_init(PGB_GameScene* gameScene)
{
    int startButtonWidth = playdate->graphics->getTextWidth(
        PGB_App->labelFont, startButtonText, strlen(startButtonText), kUTF8Encoding, 0
    );
    int selectButtonWidth = playdate->graphics->getTextWidth(
        PGB_App->labelFont, selectButtonText, strlen(selectButtonText), kUTF8Encoding, 0
    );

    int width = 18;
    int height = 46;

    int startSpacing = 3;
    int selectSpacing = 6;

    int labelHeight = playdate->graphics->getFontHeight(PGB_App->labelFont);

    int containerHeight = labelHeight + startSpacing + height + selectSpacing + labelHeight;

    int containerWidth = width;
    containerWidth = PGB_MAX(containerWidth, startButtonWidth);
    containerWidth = PGB_MAX(containerWidth, selectButtonWidth);

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
    gameScene->selector.deadAngle = 20;
    gameScene->selector.index = 0;
    gameScene->selector.startPressed = false;
    gameScene->selector.selectPressed = false;
}

/**
 * Returns a pointer to the allocated space containing the ROM. Must be freed.
 */
static uint8_t* read_rom_to_ram(
    const char* filename, PGB_GameSceneError* sceneError, size_t* o_rom_size
)
{
    *sceneError = PGB_GameSceneErrorUndefined;

    SDFile* rom_file = playdate->file->open(filename, kFileReadDataOrBundle);

    if (rom_file == NULL)
    {
        const char* fileError = playdate->file->geterr();
        playdate->system->logToConsole(
            "%s:%i: Can't open rom file %s", __FILE__, __LINE__, filename
        );
        playdate->system->logToConsole("%s:%i: File error %s", __FILE__, __LINE__, fileError);

        *sceneError = PGB_GameSceneErrorLoadingRom;

        if (fileError)
        {
            char* fsErrorCode = pgb_extract_fs_error_code(fileError);
            if (fsErrorCode)
            {
                if (strcmp(fsErrorCode, "0709") == 0)
                {
                    *sceneError = PGB_GameSceneErrorWrongLocation;
                }
            }
        }
        return NULL;
    }

    playdate->file->seek(rom_file, 0, SEEK_END);
    int rom_size = playdate->file->tell(rom_file);
    *o_rom_size = rom_size;
    playdate->file->seek(rom_file, 0, SEEK_SET);

    uint8_t* rom = pgb_malloc(rom_size);

    if (playdate->file->read(rom_file, rom, rom_size) != rom_size)
    {
        playdate->system->logToConsole(
            "%s:%i: Can't read rom file %s", __FILE__, __LINE__, filename
        );

        pgb_free(rom);
        playdate->file->close(rom_file);
        *sceneError = PGB_GameSceneErrorLoadingRom;
        return NULL;
    }

    playdate->file->close(rom_file);
    return rom;
}

static int read_cart_ram_file(
    const char* save_filename, struct gb_s* gb, unsigned int* last_save_time
)
{
    *last_save_time = 0;

    const size_t sram_len = gb_get_save_size(gb);

    PGB_GameSceneContext* context = gb->direct.priv;
    PGB_GameScene* gameScene = context->scene;

    gb->gb_cart_ram = (sram_len > 0) ? pgb_malloc(sram_len) : NULL;
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

static void write_cart_ram_file(const char* save_filename, struct gb_s* gb)
{
    // Get the size of the save RAM from the gb context.
    const size_t sram_len = gb_get_save_size(gb);
    PGB_GameSceneContext* context = gb->direct.priv;
    PGB_GameScene* gameScene = context->scene;

    // If there is no battery, exit.
    if (!gameScene->cartridge_has_battery)
    {
        return;
    }

    // Generate .tmp and .bak filenames
    size_t len = strlen(save_filename);
    char* tmp_filename = pgb_malloc(len + 2);
    char* bak_filename = pgb_malloc(len + 2);

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

    if (gameScene->cartridge_has_battery)
    {
        playdate->file->write(f, gb->cart_rtc, sizeof(gb->cart_rtc));
        unsigned int now = playdate->system->getSecondsSinceEpoch(NULL);
        gameScene->last_save_time = now;
        playdate->file->write(f, &now, sizeof(now));
    }

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
        pgb_free(tmp_filename);
    if (bak_filename)
        pgb_free(bak_filename);
}

static void gb_save_to_disk_(struct gb_s* gb)
{
    DTCM_VERIFY_DEBUG();

    PGB_GameSceneContext* context = gb->direct.priv;
    PGB_GameScene* gameScene = context->scene;

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

static void gb_save_to_disk(struct gb_s* gb)
{
    call_with_main_stack_1(gb_save_to_disk_, gb);
}

/**
 * Handles an error reported by the emulator. The emulator context may be used
 * to better understand why the error given in gb_err was reported.
 */
static void gb_error(struct gb_s* gb, const enum gb_error_e gb_err, const uint16_t val)
{
    PGB_GameSceneContext* context = gb->direct.priv;

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
        playdate->system->logToConsole("Invalid read: addr %04x", val);
    }
    else if (gb_err == GB_INVALID_WRITE)
    {
        playdate->system->logToConsole("Invalid write: addr %04x", val);
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
            char* recovery_filename = pgb_save_filename(context->scene->rom_filename, true);
            write_cart_ram_file(recovery_filename, context->gb);
            pgb_free(recovery_filename);
        }

        // TODO: write recovery savestate

        context->scene->state = PGB_GameSceneStateError;
        context->scene->error = PGB_GameSceneErrorFatal;

        PGB_Scene_refreshMenu(context->scene->scene);
    }

    return;
}

typedef typeof(playdate->graphics->markUpdatedRows) markUpdateRows_t;

__core_section("fb") void update_fb_dirty_lines(
    uint8_t* restrict framebuffer, uint8_t* restrict lcd,
    const uint16_t* restrict line_changed_flags, markUpdateRows_t markUpdatedRows,
    unsigned dither_preference, int scy, bool stable_scaling_enabled, uint8_t* restrict dither_lut0,
    uint8_t* restrict dither_lut1
)
{
    framebuffer += (PGB_LCD_X / 8);
    unsigned fb_y_playdate_current_bottom = PGB_LCD_Y + PGB_LCD_HEIGHT;

    if (stable_scaling_enabled)
    {
        // --- STABILIZED PATH ---

        for (int y_gb = LCD_HEIGHT; y_gb-- > 0;)
        {
            int world_y = y_gb + scy;

            // Row height is correctly determined by world_y to prevent rows from changing size.
            int row_height_on_playdate = 2;
            if ((world_y + dither_preference) % 3 == 2)
            {
                row_height_on_playdate = 1;
            }

            unsigned int current_line_pd_top_y =
                fb_y_playdate_current_bottom - row_height_on_playdate;

            // Skip rendering if the source GB line has not changed.
            if (((line_changed_flags[y_gb / 16] >> (y_gb % 16)) & 1) == 0)
            {
                fb_y_playdate_current_bottom = current_line_pd_top_y;
                continue;
            }

            fb_y_playdate_current_bottom = current_line_pd_top_y;
            uint8_t* restrict gb_line_data = &lcd[y_gb * LCD_WIDTH_PACKED];
            uint8_t* restrict pd_fb_line_top_ptr =
                &framebuffer[current_line_pd_top_y * PLAYDATE_ROW_STRIDE];

            // --- STABLE DITHER LOGIC ---
            // Dither phase is now based on the parity of the world-space Y coordinate.
            // This ensures any given line of GB content is always dithered with the same
            // high-frequency alternating pattern, stabilizing both uniform and non-uniform
            // textures.
            bool is_world_y_even = ((world_y + dither_preference) % 2 == 0);

            uint8_t* restrict dither_lut_top = is_world_y_even ? dither_lut0 : dither_lut1;
            uint8_t* restrict dither_lut_bottom = is_world_y_even ? dither_lut1 : dither_lut0;

            for (int x_packed_gb = 0; x_packed_gb < LCD_WIDTH_PACKED; x_packed_gb++)
            {
                uint8_t orgpixels = gb_line_data[x_packed_gb];
                pd_fb_line_top_ptr[x_packed_gb] = dither_lut_top[orgpixels];
                if (row_height_on_playdate == 2)
                {
                    uint8_t* restrict pd_fb_line_bottom_ptr =
                        pd_fb_line_top_ptr + PLAYDATE_ROW_STRIDE;
                    pd_fb_line_bottom_ptr[x_packed_gb] = dither_lut_bottom[orgpixels];
                }
            }

            markUpdatedRows(
                current_line_pd_top_y, current_line_pd_top_y + row_height_on_playdate - 1
            );
        }
    }
    else
    {
        // --- NORMAL PATH ---

        int scale_index = dither_preference;
        uint8_t* restrict dither_lut0_ptr = dither_lut0;
        uint8_t* restrict dither_lut1_ptr = dither_lut1;

        for (int y_gb = LCD_HEIGHT; y_gb-- > 0;)
        {
            int row_height_on_playdate = 2;
            if (scale_index++ == 2)
            {
                scale_index = 0;
                row_height_on_playdate = 1;

                uint8_t* restrict temp_ptr = dither_lut0_ptr;
                dither_lut0_ptr = dither_lut1_ptr;
                dither_lut1_ptr = temp_ptr;
            }

            unsigned int current_line_pd_top_y =
                fb_y_playdate_current_bottom - row_height_on_playdate;

            if (((line_changed_flags[y_gb / 16] >> (y_gb % 16)) & 1) == 0)
            {
                // Line has not changed, just update the position for the
                // next line and skip drawing.
                fb_y_playdate_current_bottom = current_line_pd_top_y;
                continue;
            }

            // Line has changed, draw it.
            fb_y_playdate_current_bottom = current_line_pd_top_y;

            uint8_t* restrict gb_line_data = &lcd[y_gb * LCD_WIDTH_PACKED];
            uint8_t* restrict pd_fb_line_top_ptr =
                &framebuffer[current_line_pd_top_y * PLAYDATE_ROW_STRIDE];

            for (int x_packed_gb = 0; x_packed_gb < LCD_WIDTH_PACKED; x_packed_gb++)
            {
                uint8_t orgpixels = gb_line_data[x_packed_gb];

                // Get the pre-calculated dithered byte for the top row.
                pd_fb_line_top_ptr[x_packed_gb] = dither_lut0_ptr[orgpixels];

                if (row_height_on_playdate == 2)
                {
                    uint8_t* restrict pd_fb_line_bottom_ptr =
                        pd_fb_line_top_ptr + PLAYDATE_ROW_STRIDE;

                    // Get the pre-calculated dithered byte for the bottom row.
                    pd_fb_line_bottom_ptr[x_packed_gb] = dither_lut1_ptr[orgpixels];
                }
            }

            markUpdatedRows(
                current_line_pd_top_y, current_line_pd_top_y + row_height_on_playdate - 1
            );
        }
    }
}

static void save_check(struct gb_s* gb);

static __section__(".text.tick") void display_fps(void)
{
    if (!numbers_bmp)
        return;

    if (++fps_draw_timer % 4 != 0)
        return;

    float fps;
    if (PGB_App->avg_dt <= 1.0f / 98.5f)
    {
        fps = 99.9f;
    }
    else
    {
        fps = 1.0f / PGB_App->avg_dt;
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

__section__(".text.tick") __space
    static void crank_update(PGB_GameScene* gameScene, float* progress)
{
    PGB_GameSceneContext* context = gameScene->context;

    float angle = fmaxf(0, fminf(360, playdate->system->getCrankAngle()));

    if (preferences_crank_mode == CRANK_MODE_START_SELECT)
    {
        if (angle <= (180 - gameScene->selector.deadAngle))
        {
            if (angle >= gameScene->selector.triggerAngle)
            {
                gameScene->selector.startPressed = true;
            }

            float adjustedAngle = fminf(angle, gameScene->selector.triggerAngle);
            *progress = 0.5f - adjustedAngle / gameScene->selector.triggerAngle * 0.5f;
        }
        else if (angle >= (180 + gameScene->selector.deadAngle))
        {
            if (angle <= (360 - gameScene->selector.triggerAngle))
            {
                gameScene->selector.selectPressed = true;
            }

            float adjustedAngle = fminf(360.0f - angle, gameScene->selector.triggerAngle);
            *progress = 0.5f + adjustedAngle / gameScene->selector.triggerAngle * 0.5f;
        }
        else
        {
            gameScene->selector.startPressed = true;
            gameScene->selector.selectPressed = true;
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

__section__(".text.tick") __space static void PGB_GameScene_update(void* object, uint32_t u32enc_dt)
{
    // This prevents flicker when transitioning to the Library Scene.
    if (PGB_App->pendingScene)
    {
        return;
    }

    setCrankSoundsEnabled(
        !preferences_crank_dock_button && !preferences_crank_undock_button &&
        preferences_crank_mode != CRANK_MODE_START_SELECT
    );

    float dt = UINT32_AS_FLOAT(u32enc_dt);
    PGB_GameScene* gameScene = object;
    PGB_GameSceneContext* context = gameScene->context;

    PGB_Scene_update(gameScene->scene, dt);

    float progress = 0.5f;

    // Check whether drawing transparent pixels is enabled.
    context->gb->direct.transparency_enabled = preferences_transparency;

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

    context->gb->direct.joypad_interrupts =
        preferences_joypad_interrupts && context->gb->joypad_interrupt;

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
        gameScene->button_hold_frames_remaining = 10;
    }
    if (preferences_crank_dock_button && !gameScene->crank_was_docked &&
        playdate->system->isCrankDocked())
    {
        if (preferences_crank_dock_button == PREF_BUTTON_START)
        {
            gameScene->button_hold_mode = 2;
        }
        else if (preferences_crank_dock_button == PREF_BUTTON_SELECT)
            gameScene->button_hold_mode = 0;
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

    bool gbScreenRequiresFullRefresh = false;
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

    if (gameScene->state == PGB_GameSceneStateLoaded)
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

        PGB_GameSceneContext* context = gameScene->context;

        PDButtons current_pd_buttons = PGB_App->buttons_down;

        bool gb_joypad_start_is_active_low = !(gameScene->selector.startPressed);
        bool gb_joypad_select_is_active_low = !(gameScene->selector.selectPressed);

        context->gb->direct.joypad_bits.start = gb_joypad_start_is_active_low;
        context->gb->direct.joypad_bits.select = gb_joypad_select_is_active_low;

        context->gb->direct.joypad_bits.a =
            !((current_pd_buttons & kButtonA) || gameScene->crank_turbo_a_active);
        context->gb->direct.joypad_bits.b =
            !((current_pd_buttons & kButtonB) || gameScene->crank_turbo_b_active);
        context->gb->direct.joypad_bits.left = !(current_pd_buttons & kButtonLeft);
        context->gb->direct.joypad_bits.up = !(current_pd_buttons & kButtonUp);
        context->gb->direct.joypad_bits.right = !(current_pd_buttons & kButtonRight);
        context->gb->direct.joypad_bits.down = !(current_pd_buttons & kButtonDown);

        context->gb->overclock = (unsigned)(preferences_overclock);
        if (context->gb->gb_bios_enable)
            context->gb->overclock = 0;  // overclocked boot ROM is glitchy

        if (gbScreenRequiresFullRefresh)
        {
            playdate->graphics->clear(kColorBlack);
        }

#if PGB_DEBUG && PGB_DEBUG_UPDATED_ROWS
        memset(gameScene->debug_updatedRows, 0, LCD_ROWS);
#endif

        context->gb->direct.sram_updated = 0;

#ifndef NOLUA
        if (preferences_script_support && context->scene->script)
        {
            script_tick(context->scene->script, gameScene);
        }
#endif

        PGB_ASSERT(context == context->gb->direct.priv);

        struct gb_s* tmp_gb = context->gb;

#ifdef TARGET_SIMULATOR
        pthread_mutex_lock(&audio_mutex);
#endif

        // copy gb to stack (DTCM) temporarily only if dtcm not enabled
        int stack_gb_size = 1;
        if (!dtcm_enabled())
        {
            stack_gb_size = sizeof(struct gb_s);
        }
        char stack_gb_data[stack_gb_size];
        if (!dtcm_enabled())
        {
            gameScene->audioLocked = 1;
            memcpy(stack_gb_data, tmp_gb, sizeof(struct gb_s));
            context->gb = (void*)stack_gb_data;
            gameScene->audioLocked = 0;
        }

        gameScene->playtime += 1 + preferences_frame_skip;
        PGB_App->avg_dt_mult =
            (preferences_frame_skip && preferences_display_fps == 1) ? 0.5f : 1.0f;
        for (int frame = 0; frame <= preferences_frame_skip; ++frame)
        {
            context->gb->direct.frame_skip = preferences_frame_skip != frame;
#ifdef DTCM_ALLOC
            DTCM_VERIFY_DEBUG();
            ITCM_CORE_FN(gb_run_frame)(context->gb);
            DTCM_VERIFY_DEBUG();
#else
            gb_run_frame(context->gb);
#endif
        }

        if (!dtcm_enabled())
        {
            gameScene->audioLocked = 1;
            memcpy(tmp_gb, context->gb, sizeof(struct gb_s));
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

        int check_val = stable_scaling_enabled ? scy : dither_preference;

        if (gameScene->previous_scale_line_index != check_val)
        {
            gbScreenRequiresFullRefresh = true;
            gameScene->previous_scale_line_index = check_val;
        }

#if TENDENCY_BASED_ADAPTIVE_INTERLACING
        int updated_playdate_lines = 0;
        int scale_index_for_calc = dither_preference;
#endif

        if (memcmp(current_lcd, previous_lcd, LCD_SIZE) != 0)
        {
            for (int y = 0; y < LCD_HEIGHT; y++)
            {
                if (memcmp(
                        &current_lcd[y * LCD_WIDTH_PACKED], &previous_lcd[y * LCD_WIDTH_PACKED],
                        LCD_WIDTH_PACKED
                    ) != 0)
                {
                    line_has_changed[y / 16] |= (1 << (y % 16));

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

        // Determine if drawing is actually needed based on changes or
        // forced display
        bool actual_gb_draw_needed = true;

#if ENABLE_RENDER_PROFILER
        if (PGB_run_profiler_on_next_frame)
        {
            PGB_run_profiler_on_next_frame = false;

            for (int i = 0; i < LCD_HEIGHT / 16; i++)
            {
                line_has_changed[i] = 0xFFFF;
            }

            float startTime = playdate->system->getElapsedTime();

            ITCM_CORE_FN(update_fb_dirty_lines)(
                playdate->graphics->getFrame(), current_lcd, line_has_changed,
                playdate->graphics->markUpdatedRows, dither_preference, scy, stable_scaling_enabled,
                PGB_dither_lut_row0, PGB_dither_lut_row1
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

        if (actual_gb_draw_needed)
        {
            if (gbScreenRequiresFullRefresh)
            {
                for (int i = 0; i < LCD_HEIGHT / 16; i++)
                {
                    line_has_changed[i] = 0xFFFF;
                }
            }

            ITCM_CORE_FN(update_fb_dirty_lines)(
                playdate->graphics->getFrame(), current_lcd, line_has_changed,
                playdate->graphics->markUpdatedRows, dither_preference, scy, stable_scaling_enabled,
                PGB_dither_lut_row0, PGB_dither_lut_row1
            );

            ITCM_CORE_FN(gb_fast_memcpy_64)(
                context->previous_lcd, current_lcd, LCD_WIDTH_PACKED * LCD_HEIGHT
            );
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

        if (!gameScene->staticSelectorUIDrawn || gbScreenRequiresFullRefresh)
        {
            // Clear the right sidebar area before redrawing any static UI.
            // This ensures that when we disable Turbo mode, the old text
            // disappears.
            const int rightBarX = 40 + 320;
            const int rightBarWidth = 40;
            playdate->graphics->fillRect(
                rightBarX, 0, rightBarWidth, playdate->display->getHeight(), kColorBlack
            );

            if (shouldDisplayStartSelectUI)
            {
                playdate->graphics->setFont(PGB_App->labelFont);
                playdate->graphics->setDrawMode(kDrawModeFillWhite);
                playdate->graphics->drawText(
                    startButtonText, pgb_strlen(startButtonText), kUTF8Encoding,
                    gameScene->selector.startButtonX, gameScene->selector.startButtonY
                );
                playdate->graphics->drawText(
                    selectButtonText, pgb_strlen(selectButtonText), kUTF8Encoding,
                    gameScene->selector.selectButtonX, gameScene->selector.selectButtonY
                );
            }

            if (preferences_crank_mode == CRANK_MODE_TURBO_CW ||
                preferences_crank_mode == CRANK_MODE_TURBO_CCW)
            {
                // Draw the Turbo indicator on the right panel
                playdate->graphics->setFont(PGB_App->labelFont);
                playdate->graphics->setDrawMode(kDrawModeFillWhite);

                const char* line1 = "Turbo";
                const char* line2 = (preferences_crank_mode == CRANK_MODE_TURBO_CW) ? "A/B" : "B/A";

                int fontHeight = playdate->graphics->getFontHeight(PGB_App->labelFont);
                int lineSpacing = 2;
                int paddingBottom = 6;

                int line1Width = playdate->graphics->getTextWidth(
                    PGB_App->labelFont, line1, strlen(line1), kUTF8Encoding, 0
                );
                int line2Width = playdate->graphics->getTextWidth(
                    PGB_App->labelFont, line2, strlen(line2), kUTF8Encoding, 0
                );

                const int rightBarX = 40 + 320;
                const int rightBarWidth = 40;

                int bottomEdge = playdate->display->getHeight();
                int y2 = bottomEdge - paddingBottom - fontHeight;
                int y1 = y2 - fontHeight - lineSpacing;

                int x1 = rightBarX + (rightBarWidth - line1Width) / 2;
                int x2 = rightBarX + (rightBarWidth - line2Width) / 2;

                // 4. Draw the text.
                playdate->graphics->drawText(line1, strlen(line1), kUTF8Encoding, x1, y1);
                playdate->graphics->drawText(line2, strlen(line2), kUTF8Encoding, x2, y2);

                playdate->graphics->setDrawMode(kDrawModeCopy);
            }

            playdate->graphics->setDrawMode(kDrawModeCopy);
        }

        gameScene->staticSelectorUIDrawn = true;

        if (animatedSelectorBitmapNeedsRedraw && shouldDisplayStartSelectUI)
        {
            LCDBitmap* bitmap;
            // Use gameScene->selector.index, which is the most current
            // calculated frame
            if (gameScene->selector.index < 0)
            {
                bitmap = PGB_App->startSelectBitmap;
            }
            else
            {
                bitmap = playdate->graphics->getTableBitmap(
                    PGB_App->selectorBitmapTable, gameScene->selector.index
                );
            }
            playdate->graphics->drawBitmap(
                bitmap, gameScene->selector.x, gameScene->selector.y, kBitmapUnflipped
            );
        }

#if PGB_DEBUG && PGB_DEBUG_UPDATED_ROWS
        PDRect highlightFrame = gameScene->debug_highlightFrame;
        playdate->graphics->fillRect(
            highlightFrame.x, highlightFrame.y, highlightFrame.width, highlightFrame.height,
            kColorBlack
        );

        for (int y = 0; y < PGB_LCD_HEIGHT; y++)
        {
            int absoluteY = PGB_LCD_Y + y;

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
    else if (gameScene->state == PGB_GameSceneStateError)
    {
        // Check for pushed A or B button to return to the library
        PDButtons pushed;
        playdate->system->getButtonState(NULL, &pushed, NULL);

        if ((pushed & kButtonA) || (pushed & kButtonB))
        {
            PGB_GameScene_didSelectLibrary(gameScene);
            return;
        }

        gameScene->scene->preferredRefreshRate = 30;

        if (gbScreenRequiresFullRefresh)
        {
            char* errorTitle = "Oh no!";

            int errorMessagesCount = 1;
            char* errorMessages[4];

            errorMessages[0] = "A generic error occurred";

            if (gameScene->error == PGB_GameSceneErrorLoadingRom)
            {
                errorMessages[0] = "Can't load the selected ROM";
            }
            else if (gameScene->error == PGB_GameSceneErrorWrongLocation)
            {
                errorTitle = "Wrong location";
                errorMessagesCount = 2;
                errorMessages[0] = "Please move the ROM to";
                errorMessages[1] = "/Data/*.crankboy/games/";
            }
            else if (gameScene->error == PGB_GameSceneErrorFatal)
            {
                errorMessages[0] = "A fatal error occurred";
            }

            errorMessages[errorMessagesCount++] = "";
            errorMessages[errorMessagesCount++] = "Press Ⓐ or Ⓑ to return to Library";

            playdate->graphics->clear(kColorWhite);

            int titleToMessageSpacing = 6;

            int titleHeight = playdate->graphics->getFontHeight(PGB_App->titleFont);
            int lineSpacing = 2;
            int messageHeight = playdate->graphics->getFontHeight(PGB_App->bodyFont);
            int messagesHeight =
                messageHeight * errorMessagesCount + lineSpacing * (errorMessagesCount - 1);

            int containerHeight = titleHeight + titleToMessageSpacing + messagesHeight;

            int titleX =
                (float)(playdate->display->getWidth() -
                        playdate->graphics->getTextWidth(
                            PGB_App->titleFont, errorTitle, strlen(errorTitle), kUTF8Encoding, 0
                        )) /
                2;
            int titleY = (float)(playdate->display->getHeight() - containerHeight) / 2;

            playdate->graphics->setFont(PGB_App->titleFont);
            playdate->graphics->drawText(
                errorTitle, strlen(errorTitle), kUTF8Encoding, titleX, titleY
            );

            int messageY = titleY + titleHeight + titleToMessageSpacing;

            for (int i = 0; i < errorMessagesCount; i++)
            {
                char* errorMessage = errorMessages[i];
                int messageX = (float)(playdate->display->getWidth() -
                                       playdate->graphics->getTextWidth(
                                           PGB_App->bodyFont, errorMessage, strlen(errorMessage),
                                           kUTF8Encoding, 0
                                       )) /
                               2;

                playdate->graphics->setFont(PGB_App->bodyFont);
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

__section__(".text.tick") __space static void save_check(struct gb_s* gb)
{
    static uint32_t frames_since_sram_update;

    // save SRAM under some conditions
    // TODO: also save if menu opens, playdate goes to sleep, app closes, or
    // powers down
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
        if (frames_since_sram_update >= PGB_IDLE_FRAMES_BEFORE_SAVE)
        {
            playdate->system->logToConsole("Saving (idle detected)");
            gb_save_to_disk(gb);
        }
    }
}

void PGB_LibraryConfirmModal(void* userdata, int option)
{
    PGB_GameScene* gameScene = userdata;

    if (option == 1)
    {
        call_with_user_stack(PGB_goToLibrary);
    }
    else
    {
        gameScene->button_hold_frames_remaining = 0;
        gameScene->button_hold_mode = 1;
        gameScene->audioLocked = false;
    }
}

__section__(".rare") void PGB_GameScene_didSelectLibrary_(void* userdata)
{
    PGB_GameScene* gameScene = userdata;
    gameScene->audioLocked = true;

    // if playing for more than 1 minute, ask confirmation
    if (gameScene->playtime >= 60 * 60)
    {
        const char* options[] = {"No", "Yes", NULL};
        PGB_presentModal(
            PGB_Modal_new("Quit game?", quitGameOptions, PGB_LibraryConfirmModal, gameScene)->scene
        );
    }
    else
    {
        call_with_user_stack(PGB_goToLibrary);
    }
}

__section__(".rare") void PGB_GameScene_didSelectLibrary(void* userdata)
{
    DTCM_VERIFY();

    call_with_user_stack_1(PGB_GameScene_didSelectLibrary_, userdata);

    DTCM_VERIFY();
}

__section__(".rare") static void PGB_GameScene_showSettings(void* userdata)
{
    PGB_GameScene* gameScene = userdata;
    PGB_SettingsScene* settingsScene = PGB_SettingsScene_new(gameScene);
    PGB_presentModal(settingsScene->scene);

    // We need to set this here to None in case the user selected any button.
    // The menu automatically falls back to 0 and the selected button is never
    // pushed.
    playdate->system->setMenuItemValue(buttonMenuItem, 1);
    gameScene->button_hold_mode = 1;
}

__section__(".rare") void PGB_GameScene_buttonMenuCallback(void* userdata)
{
    PGB_GameScene* gameScene = userdata;
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

static void PGB_GameScene_menu(void* object)
{
    PGB_GameScene* gameScene = object;

    if (gameScene->menuImage != NULL)
    {
        playdate->graphics->freeBitmap(gameScene->menuImage);
        gameScene->menuImage = NULL;
    }

    gameScene->scene->forceFullRefresh = true;

    playdate->system->removeAllMenuItems();

    if (gameScene->state == PGB_GameSceneStateError)
    {
        if (!PGB_App->bundled_rom)
        {
            playdate->system->addMenuItem("Library", PGB_GameScene_didSelectLibrary, gameScene);
        }
        return;
    }

    if (gameScene->menuImage == NULL)
    {
        PGB_LoadedCoverArt cover_art = {.bitmap = NULL};
        char* actual_cover_path = NULL;

        // --- Get Cover Art ---

        bool has_cover_art = false;
        if (PGB_App->coverArtCache.rom_path &&
            strcmp(PGB_App->coverArtCache.rom_path, gameScene->rom_filename) == 0 &&
            PGB_App->coverArtCache.art.status == PGB_COVER_ART_SUCCESS &&
            PGB_App->coverArtCache.art.bitmap != NULL)
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
        if (gameScene->save_states_supported)
        {
            for (int i = 0; i < SAVE_STATE_SLOT_COUNT; ++i)
            {
                last_state_save_time =
                    MAX(last_state_save_time, get_save_state_timestamp(gameScene, i));
            }
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

                    PGB_LoadedCoverArt* cached_art = &PGB_App->coverArtCache.art;

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
                    playdate->graphics->setFont(PGB_App->labelFont);
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
                        pgb_free(human_time);
                    }

                    int font_height = playdate->graphics->getFontHeight(PGB_App->labelFont);
                    int line1_width = playdate->graphics->getTextWidth(
                        PGB_App->labelFont, line1, strlen(line1), kUTF8Encoding, 0
                    );
                    int line2_width = playdate->graphics->getTextWidth(
                        PGB_App->labelFont, line2, strlen(line2), kUTF8Encoding, 0
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

                        int box_width = PGB_MAX(line1_width, line2_width) + (padding_x * 2);
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
    if (!PGB_App->bundled_rom)
    {
        playdate->system->addMenuItem("Library", PGB_GameScene_didSelectLibrary, gameScene);
    }
    if (preferences_bundle_hidden != (preferences_bitfield_t)-1)
    {
        playdate->system->addMenuItem("Settings", PGB_GameScene_showSettings, gameScene);
    }
    else
    {
        playdate->system->addMenuItem("About", PGB_showCredits, gameScene);
    }

    buttonMenuItem = playdate->system->addOptionsMenuItem(
        "Button", buttonMenuOptions, 4, PGB_GameScene_buttonMenuCallback, gameScene
    );
    playdate->system->setMenuItemValue(buttonMenuItem, gameScene->button_hold_mode);
}

static void PGB_GameScene_generateBitmask(void)
{
    if (PGB_GameScene_bitmask_done)
    {
        return;
    }

    PGB_GameScene_bitmask_done = true;

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
                    if (PGB_patterns[colour][y][x_offset + x] == 1)
                    {
                        int n = i * 2 + x;
                        mask |= (1 << (7 - n));
                    }
                }

                PGB_bitmask[colour][i][y] = mask;

                x_offset ^= 2;
            }
        }
    }
}

__section__(".rare") static unsigned get_save_state_timestamp_(
    PGB_GameScene* gameScene, unsigned slot
)
{
    char* path;
    playdate->system->formatString(
        &path, "%s/%s.%u.state", PGB_statesPath, gameScene->base_filename, slot
    );

    SDFile* file = playdate->file->open(path, kFileReadData);

    pgb_free(path);

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

__section__(".rare") unsigned get_save_state_timestamp(PGB_GameScene* gameScene, unsigned slot)
{
    return (unsigned)call_with_main_stack_2(get_save_state_timestamp_, gameScene, slot);
}

// returns true if successful
__section__(".rare") static bool save_state_(PGB_GameScene* gameScene, unsigned slot)
{
    playdate->system->logToConsole("save state %p", __builtin_frame_address(0));

    if (gameScene->isCurrentlySaving)
    {
        playdate->system->logToConsole("Save state failed: another save is in progress.");
        return false;
    }

    gameScene->isCurrentlySaving = true;

    PGB_GameSceneContext* context = gameScene->context;
    bool success = false;

    char* path_prefix = NULL;
    char* state_name = NULL;
    char* tmp_name = NULL;
    char* bak_name = NULL;
    char* thumb_name = NULL;
    char* buff = NULL;

    playdate->system->formatString(
        &path_prefix, "%s/%s.%u", PGB_statesPath, gameScene->base_filename, slot
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

    buff = pgb_malloc(save_size);
    if (!buff)
    {
        playdate->system->logToConsole("Failed to allocate buffer for save state");
        goto cleanup;
    }

    gb_state_save(context->gb, buff);

    struct StateHeader* header = (struct StateHeader*)buff;
    header->timestamp = playdate->system->getSecondsSinceEpoch(NULL);
    header->script = (preferences_script_support && context->scene->script);

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
                    u8 p0 = __gb_get_pixel(line0, x);
                    u8 p1 = __gb_get_pixel(line0, x ^ 1);

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
        pgb_free(path_prefix);
    if (state_name)
        pgb_free(state_name);
    if (tmp_name)
        pgb_free(tmp_name);
    if (bak_name)
        pgb_free(bak_name);
    if (thumb_name)
        pgb_free(thumb_name);
    if (buff)
        pgb_free(buff);

    gameScene->isCurrentlySaving = false;
    return success;
}

// returns true if successful
__section__(".rare") bool save_state(PGB_GameScene* gameScene, unsigned slot)
{
    return (bool)call_with_main_stack_2(save_state_, gameScene, slot);
    gameScene->playtime = 0;
}

__section__(".rare") bool load_state_thumbnail_(
    PGB_GameScene* gameScene, unsigned slot, uint8_t* out
)
{
    char* path;
    playdate->system->formatString(
        &path, "%s/%s.%u.thumb", PGB_statesPath, gameScene->base_filename, slot
    );

    SDFile* file = playdate->file->open(path, kFileReadData);

    pgb_free(path);

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
__section__(".rare") bool load_state_thumbnail(
    PGB_GameScene* gameScene, unsigned slot, uint8_t* out
)
{
    return (bool)call_with_main_stack_3(load_state_thumbnail_, gameScene, slot, out);
}

// returns true if successful
__section__(".rare") bool load_state(PGB_GameScene* gameScene, unsigned slot)
{
    gameScene->playtime = 0;
    PGB_GameSceneContext* context = gameScene->context;
    char* state_name;
    playdate->system->formatString(
        &state_name, "%s/%s.%u.state", PGB_statesPath, gameScene->base_filename, slot
    );
    bool success = false;

    int save_size = gb_get_state_size(context->gb);
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
        int save_size = playdate->file->tell(file);
        if (save_size > 0)
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
                int size_remaining = save_size;
                char* buff = pgb_malloc(save_size);
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
                        unsigned int timestamp = 0;

                        unsigned int loaded_timestamp = header->timestamp;

                        if (timestamp > 0)
                        {
                            playdate->system->logToConsole("Save state created at: %u", timestamp);
                        }
                        else
                        {
                            playdate->system->logToConsole(
                                "Save state is from an old version (no "
                                "timestamp)."
                            );
                        }

                        const char* res = gb_state_load(context->gb, buff, save_size);
                        if (res)
                        {
                            success = false;
                            playdate->system->logToConsole("Error loading state! %s", res);
                        }
                    }

                    pgb_free(buff);
                }
            }
        }
        else
        {
            playdate->system->logToConsole("Failed to determine file size");
        }

        playdate->file->close(file);
    }

    pgb_free(state_name);
    return success;
}

__section__(".rare") static void PGB_GameScene_event(
    void* object, PDSystemEvent event, uint32_t arg
)
{
    PGB_GameScene* gameScene = object;
    PGB_GameSceneContext* context = gameScene->context;

    switch (event)
    {
    case kEventLock:
    case kEventPause:
        audioGameScene = NULL;

        DTCM_VERIFY();
        if (gameScene->cartridge_has_battery)
        {
            call_with_user_stack_1(PGB_GameScene_menu, gameScene);
        }
        // fallthrough
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
        if (gameScene->audioEnabled)
        {
            audioGameScene = gameScene;
        }
        break;
    case kEventLowPower:
        if (context->gb->direct.sram_dirty && gameScene->save_data_loaded_successfully)
        {
            // save a recovery file
            char* recovery_filename = pgb_save_filename(context->scene->rom_filename, true);
            write_cart_ram_file(recovery_filename, context->gb);
            pgb_free(recovery_filename);
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
#if ENABLE_RENDER_PROFILER
        case 0x39:  // 9
            playdate->system->logToConsole("Profiler triggered. Will run on next frame.");
            PGB_run_profiler_on_next_frame = true;
            break;
#endif
        }
    default:
        break;
    }
}

static void PGB_GameScene_free(void* object)
{
    DTCM_VERIFY();
    PGB_GameScene* gameScene = object;
    PGB_GameSceneContext* context = gameScene->context;
    
    prefs_locked_by_script = 0;

    preferences_read_from_disk(PGB_globalPrefsPath);
    preferences_per_game = 0;
    preferences_save_state_slot = 0;

    if (PGB_App->soundSource != NULL)
    {
        playdate->sound->removeSource(PGB_App->soundSource);
        PGB_App->soundSource = NULL;
    }

    playdate->sound->channel->setVolume(playdate->sound->getDefaultChannel(), 1.0f);

    audioGameScene = NULL;
    audio_enabled = 0;

    if (gameScene->menuImage)
    {
        playdate->graphics->freeBitmap(gameScene->menuImage);
    }

    playdate->system->setMenuImage(NULL, 0);

    PGB_Scene_free(gameScene->scene);

    gb_save_to_disk(context->gb);

    gb_reset(context->gb);

    pgb_free(gameScene->rom_filename);
    pgb_free(gameScene->save_filename);
    pgb_free(gameScene->base_filename);
    pgb_free(gameScene->settings_filename);
    pgb_free(gameScene->name_short);

    if (context->rom)
    {
        pgb_free(context->rom);
    }

    if (context->cart_ram)
    {
        pgb_free(context->cart_ram);
    }

    if (preferences_script_support && gameScene->script)
    {
        script_end(gameScene->script, gameScene);
        gameScene->script = NULL;
    }

    pgb_free(context);
    pgb_free(gameScene);

    dtcm_deinit();
    DTCM_VERIFY();
}

__section__(".rare") void __gb_on_breakpoint(struct gb_s* gb, int breakpoint_number)
{
    PGB_GameSceneContext* context = gb->direct.priv;
    PGB_GameScene* gameScene = context->scene;

    PGB_ASSERT(gameScene->context == context);
    PGB_ASSERT(gameScene->context->scene == gameScene);
    PGB_ASSERT(gameScene->context->gb->direct.priv == context);
    PGB_ASSERT(gameScene->context->gb == gb);

    if (preferences_script_support && gameScene->script)
    {
        call_with_user_stack_2(script_on_breakpoint, gameScene, breakpoint_number);
    }
}

void show_game_script_info(const char* rompath)
{
    ScriptInfo* info = script_get_info_by_rom_path(rompath);
    if (!info)
        return;

    if (!info->info)
    {
        script_info_free(info);
        return;
    }

    char* text = aprintf("\"%s\"\nScript information:\n\n%s", info->rom_name, info->info);
    script_info_free(info);
    if (!text)
        return;

    PGB_InfoScene* infoScene = PGB_InfoScene_new(text);

    pgb_free(text);

    PGB_presentModal(infoScene->scene);
}
