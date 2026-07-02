# v2.2.0 - (2026-07-02)

It's Rewind Time.

## Rewind

- Crank backwards to rewind gameplay! (DMG games only for now.)
- VHS-style effects make it feel like you're actually scrubbing a tape.
- Can be enabled in the Behaviour settings.

## Menu

- Quick-press the Menu button to act as Start/Select

## Emulation

- More games work correctly now.
- Audio emulation has been improved.
- Better performance all around -- we removed some settings you don't need to worry about.

## Scripts

- New: Beatmania GB (forced to CGB mode for better compatibility).

## Bug Fixes

- Save states load reliably now.

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
