## CrankBoy

[![Nightly Build](https://github.com/CrankBoyHQ/crankboy-app/actions/workflows/nightly.yml/badge.svg)](https://github.com/CrankBoyHQ/crankboy-app/actions/workflows/nightly.yml)
[![Forum Thread](https://img.shields.io/badge/Forum_Thread-yellow?logo=discourse&label=PlayDate)](https://devforum.play.date/t/60-fps-gameboy-emulation/22865)
[![Discord](https://img.shields.io/discord/675983554655551509?logo=discord&logoColor=white&color=7289DA)](https://discord.com/channels/675983554655551509/1378119815641694278)

A full-speed Game Boy emulator for Playdate. CrankBoy is a fork of [PlayGB](https://github.com/risolvipro/PlayGB)
and based on [Peanut-GB](https://github.com/deltabeard/Peanut-GB), a header-only C Game Boy emulator by
[deltabeard](https://github.com/deltabeard).

<p align="center">
<img src="Source/launcher/card.png?raw=true">
</p>

## Features

- Stable, full-speed Game Boy emulation (on both Rev A and Rev B devices)
- Cartridge data saves automatically
- 44.1 kHz audio; not perfectly accurate but this shouldn't matter for most games
- Settings to fine-tune performance, visual appearance, and crank controls
- Multiple Save State slots per game (note: not currently available if game has its own save data. TBA.)
- You can download cover art for your library from within CrankBoy.
- Checks for an update once a day (you can disable this by revoking the networking permission for CrankBoy in your Playdate's settings)
- Support for softpatching `.bps`, `.ips` & `.ups` patch files. Instead of making a bunch of copies of a ROM for all the different ROMhacks you'd like to apply to it, you can use a single clean ROM and several patch files, each of which you can toggle from the settings. [Instructions below](#softpatching).
- ROMs can access Playdate features [via IO registers](./gb-extensions.md) and are also [scriptable with Lua](./lua-docs.md) or [with C](src/cscripts/kirby_dreamland.c) -- you can add native crank controls to a game if you have the technical know-how.
- Can be installed in "bundle" mode, containing just a single ROM. This lets you have your ROM(s) visible directly from the Playdate menu, instead of having to open the emulator. You can also **release your own Game Boy ROM as a Playdate game** this way. See "[Bundle Mode](#bundle-mode)," below.

## Installing

<a href="https://github.com/CrankBoyHQ/crankboy-app/releases/latest"><img src="assets/playdate-badge-download.png?raw=true" width="200"></a>

**Currently, we do not provide stable releases, since CrankBoy is still under heavy development.**

First, download the zip for the [latest release](https://github.com/CrankBoyHQ/crankboy-app/releases/latest), or the [latest unstable nightly build](https://github.com/CrankBoyHQ/crankboy-app/actions/workflows/nightly.yml) (you must be logged into GitHub to access the nightly).

There are two methods for installing ROMs on CrankBoy. Choose whichever is more convenient for you. You can even mix and match.

### Installing ROMs (USB)

- Copy the pdx through the [Web sideload](https://play.date/account/sideload/) or USB.
- Launch the app at least once.
- Connect Playdate to a computer, press and hold `LEFT` + `MENU` + `LOCK` at the same time in the home screen. Or go to `Settings > System > Reboot to Data Disk`.
- Place the ROMs in the app data folder; the folder name depends on the sideload method.
    - For Web sideload: `/Data/user.*.app.crankboyhq.crankboy/games/`
    - For USB: `/Data/app.crankboyhq.crankboy/games/`
- ROM filenames must end with `.gb` or `.gbc`
- Cover art can be placed manually in the `covers/` directory. The file name should match that of the corresponding ROM except for the file extension, which should be one of `.png`, `.jpg`, or `.bmp`. The resolution should be 240x240 pixels. CrankBoy will automatically convert the image to a Playdate-format `.pdi` image the next time it is launched.

### Installing ROMs (PDX)

- Add your ROMs (`.gb` or `.gbc`) to the PDX zip file.
    - On **macOS**, _control-/right-click_ on the `CrankBoy.pdx` and select `Show Package Contents`; macOS will
      open the PDX as a folder and you can then drag and drop ROMs into it.
    - On **Linux** based operating systems like Linux Mint, simply drag the ROMs into the `CrankBoy.pdx` file.
    - On **Windows** you may need to extract the PDX zip, copy the ROMs into the extracted directory, then re-zip the directory.
- Cover art can be added this way as well (see above for accepted formats)
- Install the PDX onto your Playdate as normal. Then, on first launch, the ROMs will be copied automatically from the PDX to the data directory.

Please note that the copy of the files in the PDX will not be deleted, so this could waste some disk space on your Playdate unnecessarily. However, even if you then re-install a fresh copy of CrankBoy without any additions to the PDX, the ROMs will still be present (and any new ROMs will be copied in).

Also note that ROMs and cover art cannot be _replaced_ or _deleted_ through this method, as it will not overwrite a previously-copied ROM from the PDX.

## Softpatching

Using the USB method described above, create a folder in the game's data directory, in the `patches/`
subdirectory, matching the associated ROM name without extension. For instance, given a ROM `Squid Game Boy.gb`,
create the directory `patches/Squid Game Boy/`, and place your various `.ips` patch files in this directory.
(If you go to `⊙ > settings > Patch...` from the main library within CrankBoy, this directory will be
automatically created for you.)

Then, you can enable, disable, and reorder your patches by going to `⊙ > settings > Patch` while the appropriate game is selected on main game library screen. Please note that the patches are applied in the order given; this matters if different patches conflict. In the case of a conflict, no warning message will be displayed.

## Bundle Mode

Bundling a ROM allows you to have a Game Boy ROM appear directly on the Playdate OS main menu along
with your other non-game-boy games and apps. The primary reason for this is to allow Game Boy
developers to release their games directly as playdate games. However, you can also use it if you'd
simply like for one or more ROMs to appear directly in the Playdate OS main menu.

There are two steps to enabling Bundle mode. Step 1 is to modify the [launcher assets](./Source/launcher/)
and [pdxinfo](./Source/pdxinfo) to suit your application. You **must** change the `bundleID` field
to something other than `app.crankboyhq.crankboy`.

Step 2 is to create a file called `bundle.json` and place it in the root of the PDX.
It should be a standard `JSON` file like so (replace the fields marked by `< >` and remove the `// comments`):

```
{
    // (required)
    "rom": "<path to rom file>",

    // (optional)
    "default": {
        // default values for preferences, e.g.:
        "dither_pattern": 1
    },

    // (optional)
    "hidden": [
        // list of preferences not to show, e.g.:
        "save_state_slot",
        "uncap_fps"
    ]
}
```

The value for each preference under `"default"` must be a non-negative integer, i.e. 0 or higher. If the value is out of range for the preference in question, it might crash the game or cause other glitches.

As an alternative to marking preferences as hidden, you can instead whitelist preferences that you wish to be exposed to the user by using `"visible"` instead of `"hidden"`. If you wish for the preferences menu to be hidden _entirely_, simply use `"visible": []`. A list of preferences and their names can be found [here](./src/prefs.x).

Additionally, if you are releasing your own game that is not licensed by Nintendo, please remove `dmg_boot.bin`. It's also strongly recommended that you add a [Lua script](./lua-docs.md) and/or [C script](src/cscripts/kirby_dreamland.c) and/or [native crank support](./gb-extensions.md) to your ROM in order to maximize playdate-friendliness. Note that Lua scripts are very slow because an unoptimized local version of Lua is used; you can likely increase the speed if you switch to using the Lua core provided by the Playdate firmware, but there is not currently support for this in CrankBoy. (Or just write the script in C instead.)

## Contributions

Come chat with us on the [Playdate Developer Forum](https://devforum.play.date/t/60-fps-gameboy-emulation/22865) or on [Discord](https://discord.com/channels/675983554655551509/1378119815641694278). Even if you're not an expert at emulation coding, we could still use some visual assets, look-and-feel, UI, UX, and so on to make the app feel more cute and at-home on a cozy device like Playdate.

CrankBoy uses a heavily modified version of Peanut-GB. Various [advanced optimization techniques](https://devforum.play.date/t/dirty-optimization-secrets-c-for-playdate/23011) were used to tailor the performance to the Playdate. If you wish to work on adding features to the emulator core itself, you may want to glance at those optimization techniques since it explains some of the unusual design choices made.

### Project Setup

After cloning the repository, please enable the clang-format git hook by running this command from the project root:

```bash
git config core.hooksPath githooks
```

For convenience, you can use the CLI arg `rom=<path/to/rom>` (where path is relative to the game's data directory) to launch a rom in bundled mode directly.
