# v3.0.0 - (2026-09-02)

More performance and better Audio. Also: 日本語 is arriving.

## Japanese Language (WIP)

- Roughly 50% of the UI has been translated.
- Spotted a mistake or want to help? Reach out to us.

## Emulation

- Improved accuracy and more supported games.
- Roughly 40% performance improvement.

## Audio

- Voice Sample support!!! (Cowabunga!)
- New Latency setting for accurate sound mode.
- New High-Pass filter toggle.
- "OFF" is gone. Instead, if you mute your Playdate the normal way, CrankBoy can detect this now (it's a new OS feature! Thanks, Panic!) and give you a small perf boost.
- Improved cycle-accurate APU replay.

## Display

- New framerate settings. Instead of "30 FPS Mode", we now have 30 / 50 / 60 Framerate mode. To be clear: none of these limit emulation speed; 30 and 50 just skip some frames to keep up the pace.
- (50 is new, and it's our recommendation.)

## CGB

- Configurable grayscale **gamma correction** (with steps below 1.0, for darkening the dark).
- New Auto and Contrast grayscale modes.
- A one-time-per-game notice when a ROM is CGB-only.
- The Rewind toggle is now hidden for CGB games.

## Combo Inputs

- Fixed behaviour when Ⓐ›Ⓑ or Ⓑ›Ⓐ mappings contained Ⓐ/Ⓑ
- Added mappings for releasing Ⓑ+Ⓐ

## Performance (TCM)

- TCM mode changes apply live. No restart needed.
- TCM relocation has been hardened. No crashes in CGB mode anymore!

## Scripts

- Toggle game scripts mid-session (for supported games).

## Catalog

- Bundled Games: show/hide, to your taste. (But why would you...)
- New bundled game: Synthwave World by beatscribe (Amazing CGB and Voice Sample showcase)

## Homebrew Hub

- Moved "Get ROMs..." to the main library menu.

## Compatibility

- New mapper support: HuC1 and HuC3.
- Accelerometer support for MBC7 mapper. (It tilts 'n' tumbles now.)

## Bug Fixes

- Fixed rare glitches when loading save states from older versions of CrankBoy.
- Forgets last-selected-preference when starting or exiting a game
- A pile of crash and stability fixes.

# v2.2.1 - (2026-07-09)

## Bug Fixes

- Fixes a rare issue, where games could freeze.

# v2.2.0 - (2026-07-06)

Be kind, rewind ~ every game is now Crankin'.

## Rewind

- Rewind feature is optional and must first be enabled in the Behaviour settings. (Small perf cost, shouldn't affect most games)
- Hold Ⓑ or Up while cranking to scrub through time.
- Seekbar at the bottom of the screen shows your current time.
- Dock crank to exit rewind mode.
- VHS-style effect like it's 1989.

## Menu

- Can now map "quickly open and close the quick-menu" to start/select. (It's quick!)

## Emulation

- Audio overhauled across all sound channels. Should be a bit less crackle-y now.
- Numerous timing and accuracy fixes. Most instances where music played back at the wrong speed should be resolved now.
- Removed interlacing and legacy "fast" settings, which had poor results anyway; the accurate defaults are faster now anyway.

## Display

- "LCD Ghosting" setting mimics the slow pixel response of a real GB screen. (Just in case you find the Playdate display too crisp.) This is important for some games which have sprites that flicker rapidly to simulate transparency.

## Scripts

- New: Beatmania GB (forced to CGB mode).

## Bug Fixes

- Save states load reliably. Pretty reliably. (You should still make back-ups sometimes!)
- Various crash and stability fixes.

# v2.1.1 - (2026-06-18)

# Emulation

- We improved compatibility with more games. (You can finally slay monsters in Wizards & Warriors X)

# Bug Fixes

- Fixed a bug where MBC3 games would freeze (Pokemon Crystal is in the Clear)

---

# v2.1.0 - (2026-06-10)

Welcome to the new Changelog feature! It's the dawn of a new age...

## Startup

- CrankBoy should now start about 8x faster (Coffee break no longer required.)

## Settings

- Pagination. (Praise be -- we have over 40 settings now.)
- "Manage Roms" screen (view rom info; delete roms and save data etc.)
- Automatic word-wrap for settings descriptions. (We were doing it manually this whole time. Don't ask..)

## Catalog

- Bundled ROMs no longer self-extract, taking up unnecessary space on disk. (You can still pull them out of the PDX manually though.)
- Oh? Something else is coming... Can't quite see it yet. Please be patient.

## Performance Options (Experimental)

- Adaptive 30/60 FPS mode -- uses 60 FPS mode but switches to 30 on the fly for for difficult frames.
- TCM settings for improved perf with mild sacrifices to stability.

## CGB Support (Experimental)

- CGB support actually already existed in prior versions, but many more games should work now.
- Performance is still sub-par. It's an up-hill battle.
- Completely rewritten colour palette handling.
- Sprite and background priorities are now fully accurate. Probably.
- Frame Blending enabled by default -- uses special logic to increase the number of shades of grey available too.

## Emucore Support (Experimental)

- You can place emulation cores in `/Shared/Emulation/cores`. These allow you to run other emulators directly from CrankBoy, bringing support for other systems to CrankBoy. There are only a few out there so far.

## Scripts

- New: Trip World, World Heroes 2 Jet, Kirby's Pinball Land, Hoshi no Kirby (jp)
- Various bugfixes for some existing scripts.

## Bug Fixes

- Fixed audio glitches and missing sounds in several games.
- The HALT bug and joypad interrupt timing now fully accurate. (Um. Probably.)
- CGB double-speed mode timer fixes. Music should play at normal speed now. (Though it was kind of fun at double speed before.)
- Various crash fixes and stability improvements.
