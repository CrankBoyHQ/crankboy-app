/*
 * format: PREF(name, default value)
 */

PREF(per_game, 0)         // (never visible in Bundle mode)
PREF(save_slot, 0)        // (never visible in Bundle mode; only visible in library view)
PREF(save_state_slot, 0)  // (has two corresponding settings)

/* --- audio ---
 * sound_mode:       0=Off^, 1=Fast, 2=Accurate
 * sample_rate:      0=High, 1=Medium, 2=Low
 * audio_latency:    0=Low, 1=Buffered (accurate mode only)
 * headphone_audio:  0=Mono, 1=Stereo
 * high_pass_filter: 0=Off, 1=On
 *
 * ^: not accessible to user
 */
PREF(sound_mode, 2)
PREF(sample_rate, 0)
PREF(audio_latency, 0)
PREF(headphone_audio, 1)
PREF(high_pass_filter, 1)

/* --- display ---
 *  framerate:       0=30FPS, 1=50FPS, 2=60FPS
 *  blend_frames:    0=Off, 1=On
 *  dither_pattern:  0=Staggered, 1=Grid, 2=Staggered (L), 3=Grid (L), 4=Staggered (D), 5=Grid (D)
 *  dither_line:     0 = Off (used for scripting), 1 = Line 1, 2= Line 2, 3 = Line 3
 *  dither_stable:   0=Off, 1=On
 *  ghosting:        0=Off, 1=On (DMG only)
 */
PREF(framerate, 0)
PREF(blend_frames, 0)
PREF(dither_pattern, 0)
PREF(dither_line, 2)
PREF(dither_stable, 1)
PREF(ghosting, 0)

/* --- input ---
 * crank_mode:           0=Start/Select, 1=Turbo A/B, 2=Turbo B/A, 3=None
 * crank_down_action:    0=None, 1=Select+Start
 * crank_undock_button:  0=None, 1=Start, 2=Select, 3=Start+Select
 * crank_dock_button:    same as above
 * hold_a_press_b:       0=Default, 1=Start, 2=Select,3=Start+Select, 4=Start+A, 5=Select+A,
 *                       6=Start+Select+A, 7=Start+B, 8=Select+B, 9=Start+Select+B, 10=Start+A+B,
 *                       11=Select+A+B, 12=All
 * hold_b_press_a:       same as above
 * press_a_b:            same as above
 * menu_button:          0=Off, 1=Start, 2=Select, 3=Start+Select
 * lock_button:          0=None, 1=Start, 2=Select, 3=Start+Select, 4=A, 5=B
 */
PREF(crank_mode, CRANK_MODE_START_SELECT)
PREF(crank_down_action, 0)
PREF(crank_undock_button, PREF_BUTTON_NONE)
PREF(crank_dock_button, PREF_BUTTON_NONE)
PREF(press_a_b, PREF_BUTTON_HP_DEFAULT)
PREF(hold_a_press_b, PREF_BUTTON_HP_DEFAULT)
PREF(hold_b_press_a, PREF_BUTTON_HP_DEFAULT)
PREF(hold_ab_release_a, PREF_BUTTON_ABR_DEFAULT)
PREF(hold_ab_release_b, PREF_BUTTON_ABR_DEFAULT)
PREF(lock_button, PREF_BUTTON_NONE)
PREF(menu_button, 0)

/* --- cgb ---
 * cgb_speed:       0=Default, 1=Force slow mode
 * hle:             0=Off, 1=On
 * cgb_blend_bias:  0=Darker, 1=Dark, 2=Neutral, 3=Bright, 4=Brighter
 * cgb_bias_auto:   0=Manual, 1=Auto, 2=Contrast (ignores cgb_blend_bias)
 * cgb_gamma:       0..12 -> gamma 0.6..2.6 (0.1 steps below 1.0, 0.2 above; default 4 = 1.0)
 */
PREF(cgb_speed, 0)
PREF(hle, 1)
PREF(cgb_blend_bias, 2)
PREF(cgb_bias_auto, 1)
PREF(cgb_gamma, 4)

/* --- behaviour ---
 * overclock:          0=Off, 1=x2, 2=x4
 * script_support:     0=Off, 1=On
 * rewind_enabled:     0=Off, 1=On (DMG only)
 */
PREF(overclock, 0)
PREF(script_support, !!(CB_App->bundled_rom))
PREF(disable_autolock, 0)
PREF(rewind_enabled, 0)

/* --- library ---
 * display_name_mode:          0=Short, 1=Detailed, 2=Filename
 * display_article:            0=Leading, 1=As-is
 * display_sort:               0=Filename, 1=Database, 2=DB (w/article), 3=File (w/article)
 * library_remember_selection: 0=Off, 1=On
 * prompt_if_cgb_optional:     0=No, 1=Yes, 2=Always
 * library_launch_animation:   0=Off, 1=On
 * show_bundled_games:         0=Hide, 1=Show (catalog only)
 * library_view_mode:          0=List, 1=Cover Flow
 */
PREF(display_name_mode, 0)
PREF(display_article, 0)
PREF(display_sort, 1)
PREF(library_view_mode, 0)
PREF(library_remember_selection, 1)
PREF(prompt_if_cgb_optional, 0)
PREF(library_launch_animation, 1)
PREF(show_bundled_games, 1)

/* --- misc ---
 * itcm:                           0=Off, 1=Both, 2=Core, 3=Draw
 * uncap_fps:                      0=Off, 1=On
 * display_fps:                    0=Off, 1=On, 2=Playdate
 * disable_autolock:               0=Off, 1=On
 * ui_sounds:                      0=Off, 1=On
 * boot_fade:                      0=Off, 1=Short, 2=Long, 3=Short (W), 4=Long (W)
 */
PREF(itcm, (pd_rev == PD_REV_A))
PREF(uncap_fps, 0)
PREF(display_fps, 0)
PREF(ui_sounds, 1)
PREF(boot_fade, 1)

/* --- phony ---
 * script_has_prompted:            0=No, 1=Yes (not a real setting)
 * recommended_settings_ignored:   0=No, 1=Yes (not a real setting)
 * cgb_only_has_prompted:          0=No, 1=Yes (not a real setting)
 */
PREF(script_has_prompted, 0)
PREF(recommended_settings_ignored, 0)
PREF(cgb_only_has_prompted, 0)

/*
 * scripts can use these arbitrarily (see script_custom_setting_add),
 * but should never assume that the value is within any given bound.
 */
PREF(script_A, 0)
PREF(script_B, 0)
PREF(script_C, 0)
#undef PREF
