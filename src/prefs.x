// format: PREF(name, default value)

PREF(per_game, 0)         // (note: never visible in Bundle mode)
PREF(save_slot, 0)        // (note: never visible in Bundle mode; only visible in library view)
PREF(save_state_slot, 0)  // (note: has two corresponding settings)

// audio
PREF(sound_mode, 2)  // 0: Off, 1: Fast, 2: Accurate
PREF(audio_sync, 0)  // 0: Fast, 1: Accurate
PREF(sample_rate, (pd_rev == PD_REV_A) ? 1 : 0)
PREF(headphone_audio, 1)  // 0: Mono, 1: Stereo

// display
PREF(frame_skip, true)
PREF(blend_frames, 0)  // 0: Off, 1: On
PREF(dither_pattern, rand() % 2)
PREF(dither_line, 2)
PREF(dither_stable, (pd_rev != PD_REV_A))
PREF(dynamic_rate, DYNAMIC_RATE_OFF)

// input
PREF(crank_mode, CRANK_MODE_START_SELECT)
PREF(crank_down_action, 0)
PREF(crank_undock_button, PREF_BUTTON_NONE)
PREF(crank_dock_button, PREF_BUTTON_NONE)
PREF(hold_a_press_b, PREF_BUTTON_HP_DEFAULT)
PREF(hold_b_press_a, PREF_BUTTON_HP_DEFAULT)
PREF(press_a_b, PREF_BUTTON_HP_DEFAULT)
PREF(lock_button, PREF_BUTTON_NONE)

// behaviour
PREF(ppu_timing, 1)  // 0: Fast (fixed), 1: Accurate (dynamic)
PREF(batching, 0)  // 0: off (batch 1), 1: on (batch 3)
PREF(overclock, 0)
PREF(script_support, !!(CB_App->bundled_rom))
PREF(disable_autolock, 0)

// library
PREF(display_name_mode, 0)  // 0: Short, 1: Detailed, 2: Filename
PREF(display_article, 0)    // 0: leading article; 1: article as-is
PREF(display_sort, 1)       // 0: by filename; 1: by detailed name; 2 by detailed name (with leading
                            // article); 3 by filename (with leading article)
PREF(library_remember_selection, 1)
PREF(prompt_if_cgb_optional, 0)
PREF(library_launch_animation, 1)

// misc
PREF(itcm, (pd_rev == PD_REV_A))
PREF(tcm_lcd, true)
PREF(uncap_fps, false)
PREF(display_fps, 0)
PREF(ui_sounds, 1)
PREF(script_has_prompted, false)  // (not a real setting)
PREF(recommended_settings_ignored, 0) // (not a real setting)
PREF(boot_fade, 1)

// cgb
PREF(cgb_speed, 0) // 0: default; 1: force slow mode
PREF(cgb_brightness, 1) // 0: Bright; 1: Normal; 2: Dark
PREF(hle, 1)

// scripts can use these arbitrarily (see script_custom_setting_add),
// but should never assume that the value is within any given bound.
PREF(script_A, 0)
PREF(script_B, 0)
PREF(script_C, 0)
#undef PREF
