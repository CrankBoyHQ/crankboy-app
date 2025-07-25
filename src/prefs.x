// format: PREF(name, default value)

PREF(per_game, 0)         // (note: never visible in Bundle mode)
PREF(save_state_slot, 0)  // (note: has two corresponding settings)

// audio
PREF(sound_mode, 2)
PREF(sample_rate, (pd_rev == PD_REV_A) ? 1 : 0)

// display
PREF(frame_skip, true)
PREF(dither_pattern, rand() % 2)
PREF(dither_line, 2)
PREF(dither_stable, (pd_rev != PD_REV_A))
PREF(dynamic_rate, DYNAMIC_RATE_OFF)
PREF(dynamic_level, 5)
PREF(transparency, 0)

// behaviour
PREF(crank_mode, CRANK_MODE_START_SELECT)
PREF(crank_undock_button, PREF_BUTTON_NONE)
PREF(crank_dock_button, PREF_BUTTON_NONE)
PREF(overclock, 0)
PREF(bios, !(CB_App->bundled_rom))
PREF(script_support, !!(CB_App->bundled_rom))
PREF(script_has_prompted, false)  // (not a real setting)

// library
PREF(display_name_mode, 0)  // 0: Short, 1: Detailed, 2: Filename
PREF(display_article, 0)    // 0: leading article; 1: article as-is
PREF(display_sort, 1)       // 0: by filename; 1: by detailed name; 2 by detailed name (with leading
                            // article); 3 by filename (with leading article)
PREF(library_remember_selection, 1)

// misc
PREF(itcm, (pd_rev == PD_REV_A))
PREF(uncap_fps, false)
PREF(display_fps, 0)
PREF(ui_sounds, 1)
PREF(experimental_gbc_mode, 0)

#undef PREF
