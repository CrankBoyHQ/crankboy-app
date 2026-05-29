/*
 * format: PREF(name, default value)
 */

/* --- general --- */
PREF(per_game, 0)         // 0: Global, 1: Per-game (never visible in Bundle mode)
PREF(save_slot, 0)        // 0: Slot A .. 9: Slot K (note: never visible in Bundle mode;
                          //                         only visible in library view)
PREF(save_state_slot, 0)  // 0: Slot A .. 9: Slot K (note: has two corresponding settings)

/* --- audio --- */
PREF(sound_mode, 2)       // 0: Off, 1: Fast, 2: Accurate
PREF(audio_sync, 0)       // 0: Fast, 1: Accurate
PREF(sample_rate, 0)      // 0: High, 1: Medium, 2: Low
PREF(headphone_audio, 1)  // 0: Mono, 1: Stereo

/* --- display --- */
PREF(frame_skip, true)   // 0: Off, 1: On, 2: Adaptive
PREF(blend_frames, 0)    // 0: Off, 1: On
PREF(dither_pattern, 0)  // 0: Staggered, 1: Grid, 2: Staggered (L),
                         // 3: Grid (L), 4: Staggered (D), 5: Grid (D)

PREF(dither_line, 2)
PREF(dither_stable, (pd_rev != PD_REV_A))  // 0: Off, 1: On
PREF(dynamic_rate, DYNAMIC_RATE_OFF)       // 0: Off, 1: On, 2: Auto

/* --- input --- */
PREF(crank_mode, CRANK_MODE_START_SELECT)  // 0: Start/Select, 1: Turbo A/B,
                                           // 2: Turbo B/A, 3: None

PREF(crank_down_action, 0)                   // 0: None, 1: Select+Start
PREF(crank_undock_button, PREF_BUTTON_NONE)  // 0: None, 1: Start, 2: Select, 3: Start+Select
PREF(crank_dock_button, PREF_BUTTON_NONE)    // same as above

PREF(hold_a_press_b, PREF_BUTTON_HP_DEFAULT)  // 0: Default, 1: Start, 2: Select,
                                              // 3: Start+Select, 4: Start+A, 5: Select+A,
                                              // 6: Start+Select+A,
                                              // 7: Start+B, 8: Select+B, 9: Start+Select+B,
                                              // 10: Start+A+B, 11: Select+A+B, 12: All

PREF(hold_b_press_a, PREF_BUTTON_HP_DEFAULT)  // same as above
PREF(press_a_b, PREF_BUTTON_HP_DEFAULT)       // same as above
PREF(lock_button, PREF_BUTTON_NONE)  // 0: None, 1: Start, 2: Select, 3: Start+Select, 4: A, 5: B

/* --- behaviour --- */
PREF(ppu_timing, 1)                            // 0: Fast (fixed), 1: Accurate (dynamic)
PREF(batching, 0)                              // 0: Off (batch 1), 1: On (batch 3)
PREF(overclock, 0)                             // 0: Off, 1: x2, 2: x4
PREF(script_support, !!(CB_App->bundled_rom))  // 0: Off, 1: On
PREF(disable_autolock, 0)                      // 0: Off, 1: On

/* --- library --- */
PREF(display_name_mode, 0)  // 0: Short, 1: Detailed, 2: Filename
PREF(display_article, 0)    // 0: Leading, 1: As-is
PREF(display_sort, 1)       // 0: Filename, 1: Database, 2: DB (w/article), 3: File (w/article)
PREF(library_remember_selection, 1)  // 0: Off, 1: On
PREF(prompt_if_cgb_optional, 0)      // 0: No, 1: Yes, 2: Always
PREF(library_launch_animation, 1)    // 0: Off, 1: On

/* --- misc --- */
PREF(itcm, (pd_rev == PD_REV_A))       // 0: Off, 1: On (note: only effective on RevA)
PREF(tcm_lcd, 0)                       // 0: Off, 1: On
PREF(uncap_fps, 0)                     // 0: Off, 1: On
PREF(display_fps, 0)                   // 0: Off, 1: On, 2: Playdate
PREF(ui_sounds, 1)                     // 0: Off, 1: On
PREF(boot_fade, 1)                     // 0: Off, 1: Short, 2: Long, 3: Short (W), 4: Long (W)
PREF(script_has_prompted, 0)           // 0: No, 1: Yes (not a real setting)
PREF(recommended_settings_ignored, 0)  // 0: No, 1: Yes (not a real setting)

/* --- cgb --- */
PREF(cgb_speed, 0)  // 0: Default, 1: Force slow mode
PREF(hle, 1)        // 0: Off, 1: On

/*
 * scripts can use these arbitrarily (see script_custom_setting_add),
 * but should never assume that the value is within any given bound.
 */
PREF(script_A, 0)
PREF(script_B, 0)
PREF(script_C, 0)
#undef PREF
