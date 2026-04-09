## CrankBoy

[![Nightly Build](https://github.com/CrankBoyHQ/crankboy-app/actions/workflows/nightly.yml/badge.svg)](https://github.com/CrankBoyHQ/crankboy-app/actions/workflows/nightly.yml)
[![Forum Thread](https://img.shields.io/badge/Forum_Thread-yellow?logo=discourse&label=PlayDate)](https://devforum.play.date/t/60-fps-gameboy-emulation/22865)
[![Matrix](<https://img.shields.io/matrix/crankboy-dev%3Amatrix.org?logo=element&label=Chat%20(Matrix%2FElement)>)](https://matrix.to/#/!ULiZfDdeDpdQkmZgtc:matrix.org?via=matrix.org)
[![Discord](https://img.shields.io/discord/675983554655551509?logo=discord&logoColor=white&color=7289DA)](https://discord.com/channels/675983554655551509/1378119815641694278)

A full-speed Game Boy® emulator for Playdate™. CrankBoy is a fork of [PlayGB](https://github.com/risolvipro/PlayGB)
and based on [Peanut-GB](https://github.com/deltabeard/Peanut-GB), a header-only C Game Boy emulator by
[deltabeard](https://github.com/deltabeard).

<p align="center">
<img src="Source/launcher/card.png?raw=true">
</p>

## Features <!--userguide-->

- Stable, full-speed Game Boy emulation (on both Rev A and Rev B devices)
- You can download cover art for your library from within CrankBoy.
- Cartridge data saves automatically.
- 44.1 kHz audio
- Multiple save state slots per game.
- Settings to fine-tune performance, visual appearance, and crank controls
- Support for softpatching `.bps`, `.ips` & `.ups` patch files. Instead of making a bunch of copies of a ROM for all the different ROM hacks you'd like to apply to it, you can use a single clean ROM and several patch files, each of which you can toggle from the settings. [Instructions below](#softpatching).
    - Conveniently browse and download ROM hacks directly from within CrankBoy.
- ROMs can access Playdate features [via IO registers](./gb-extensions.md) and are also [scriptable with C](src/cscripts/kirby_dreamland.c) -- you can add native crank controls to a game if you have the technical know-how.
- <!--no-userguide--> Can be installed in "bundle" mode, containing just a single ROM. This lets you have your ROM(s) visible directly from the Playdate menu, instead of having to open the emulator. You can also **release your own Game Boy ROM as a Playdate game** this way. See "[Bundle Mode](#bundle-mode)," below.

## Limitations <!--userguide-->

- CrankBoy is not 100% stable. A responsible gamer makes back-ups of their save files now and then.
- Currently, **Game Boy Color games are not supported** in general. However, many Game Boy Color games are able to run on the DMG (original Game Boy) -- CrankBoy should be able to play those games fine. (There is now limited support for running CGB games, but it still works with only very few games.)
- Some games don't work correctly. Please report any broken games.
- Audio is not accurate to sub-frame precision, so audio clips (like in _Pokémon Yellow_ or _The Chessmaster_) will often be unrecognizable or silent.
- Link Cable (and other peripherals) are not supported.
- The Playdate's screen cannot fully refresh at a consistent 60 frames per second. CrankBoy has a variety of options to work around this. By default, the display will only update at 30 Hz (though the game will still run at full speed). It's quite hard to notice the difference on the Playdate screen. Games which don't have scrolling backgrounds should be able to run at 60 fps just fine, though you'll need to enable that in the options. 60 fps interlaced is also possible.
- <!--no-userguide--> Although CrankBoy will notify you if an update is available, updates are not downloaded automatically. CrankBoy checks if one is available at most once per day, and this behaviour can be disabled by revoking network privileges from the Playdate's native settings menu.

## Installing

<a href="https://github.com/CrankBoyHQ/crankboy-app/releases/latest"><img src="assets/playdate-badge-download.png?raw=true" width="200"></a>

First, download the zip for the [latest release](https://github.com/CrankBoyHQ/crankboy-app/releases/latest), or the [latest unstable nightly build](https://github.com/CrankBoyHQ/crankboy-app/actions/workflows/nightly.yml) (you must be logged into GitHub to access the nightly).

### Installing CrankBoy

- **Web sideload**
    1. Open the [Web sideload page](https://play.date/account/sideload/) and login to your account.
    2. Upload the `pdx` or `zip` file.
    3. Wait for your Playdate to download and install CrankBoy.
- **USB sideload**
    1. Connect your Playdate to a computer (or other device) by USB and unlock it.
    2. Put the Playdate into Data Disk mode (hold LEFT + MENU + POWER for 10 seconds).
    3. Copy the `pdx`to the `Games` folder.
- **Simulator sideload**
    1. Download the Simulator build for your OS (Linux or macOS -- Windows is [not yet supported](https://github.com/CrankBoyHQ/crankboy-app/issues/43).)
    2. Connect your Playdate to your computer via USB and unlock it.
    3. Open the `pdx` in the Simulator.
    4. Press `Alt+U` on Linux or `⌘+U` on macOS.

### Installing ROMs <!--userguide:##-->

There are two methods for installing ROMs on CrankBoy. Choose whichever is more convenient for you. You can even mix and match.

#### USB

- Connect your Playdate to a computer (or another device) by USB, press and hold `LEFT` + `MENU` + `LOCK` at the same time for 5 seconds. Or from the app launcher, go to `Settings > System > Reboot to Data Disk`.
- Place the ROMs in this directory: `/Shared/Emulation/gb/games/`
- ROM filenames must end with `.gb`, `.gbc`, or (if [compressed](#tips)) `.gbz`
- Cover art can be placed manually in `/Shared/Emulation/gb/covers/`. The file name should match that of the corresponding ROM except for the file extension, which should be one of `.png`, `.jpg`, `.bmp`, or `.pdi`. The resolution should be 240x240 pixels. CrankBoy will automatically convert the image to a Playdate-format `.pdi` image the next time it is launched.

#### PDX

- Add your ROMs (`.gb` or `.gbc`) to the PDX zip file.
    - On **macOS**, _control-/right-click_ on `CrankBoy.pdx.zip` and select `Show Package Contents`; macOS will
      open the PDX as a folder and you can then drag and drop ROMs into it.
    - On **Linux** based operating systems like Linux Mint, you may be able to simply drag the ROMs into `CrankBoy.pdx.zip`.
    - On **Windows** you may need to extract the PDX zip, copy the ROMs into the extracted directory, then re-zip the directory.
- Cover art can be added this way as well (see above for accepted formats)
- Install the PDX onto your Playdate as normal. Then, on first launch, the ROMs will be copied automatically from the PDX to the data directory.

Please note that the copy of the files in the PDX will not be deleted, so this could waste some disk space on your Playdate unnecessarily. However, even if you then re-install a fresh copy of CrankBoy without any additions to the PDX, the ROMs will still be present (and any new ROMs will be copied in).

Also note that ROMs and cover art cannot be _replaced_ or _deleted_ through this method, as it will not overwrite a previously-copied ROM from the PDX.

## Softpatching <!--userguide-->

CrankBoy can apply patches (i.e. ROM hacks) to your games for you, and you can select which patches to apply before launching the game. This means you don't need to make multiple copies of a ROM for each combination of hacks to apply.

You can download hacks mirrored from [romhacking.net](https://www.romhacking.net/) directly through CrankBoy, for convenience.

Alternatively, using the USB method described above, create a folder in the game's data directory, in the `patches/`
subdirectory, matching the associated ROM name without extension. For instance, given a ROM called `Squid Game Boy.gb`,
create the directory `patches/Squid Game Boy/`, and place your various `.ips` patch files in this directory.
(If you go to `⊙ > settings > Patch` from the main library within CrankBoy, this directory will be
automatically created for you.)

Then, you can enable, disable, and reorder your patches by going to `⊙ > settings > Patch` while the appropriate game is selected on main game library screen. Please note that the patches are applied in the order given; this matters if different patches conflict. In the case of a conflict, no warning message will be displayed.

## Tips <!--userguide-->

- You can delete cover art from the library view by holding Ⓑ for 5 seconds.
- Some games require a simultanious press of `Start + Select`, this can be done by either selecting `button->Both` from the Playdate's menu or, if the _Crank_ preference is set to `Start/Select`, by rotating the crank to 6 o'clock (i.e. straight down).
    - Be careful -- many games use Start+Select+A+B as a shortcut to reset the game. Try not to keep the crank in this position long term.
- You can compress ROMs to a special ".gbz format" by using [this python script](./scripts/compress_rom.py). Note that [gzip](https://en.wikipedia.org/wiki/Gzip) must be available from the command line to run the script. (Currently, there is no way to compress roms from within CrankBoy.)

## Bundle Mode

Bundling a ROM allows you to have a Game Boy ROM appear directly on the Playdate OS main menu along
with your other non-game-boy games and apps. The primary reason for this is to allow Game Boy
developers to release their games directly as playdate games. However, you can also use it if you'd
simply like for one or more Game Boy games to appear directly in the Playdate OS main menu.

There are two steps to enabling Bundle mode. Step 1 is to modify the [launcher assets](./Source/launcher/)
and [pdxinfo](./Source/pdxinfo) to suit your application. You **must** change the `bundleID` field
to something other than `app.crankboyhq.crankboy`.

Step 2 is to create a file called `bundle.json` and place it in the root of the PDX.
It should be a standard `JSON` file like so (replace the fields marked by `< >` and remove the `// comments`):

```
{
    // (required)
    "rom": "<path to rom file>",

    // (optional -- set to 'CGB' to launch with experimental CGB support)
    "device": "DMG"

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

Additionally, it's also strongly recommended that you add a [C script](src/cscripts/kirby_dreamland.c) and/or [native crank support](./gb-extensions.md) to your ROM in order to maximize playdate-friendliness.

For developers new to Playdate, please be aware that you will need to [compile CrankBoy](https://sdk.play.date/2.7.6/Inside%20Playdate%20with%20C.html#_prerequisites) (§2, §4.2) yourself if you want to run it with the Simulator, as the simulator cannot run a device-only build.

## Contributions

Come chat with us on the [Playdate Developer Forum](https://devforum.play.date/t/60-fps-gameboy-emulation/22865) or on [Discord](https://discord.com/channels/675983554655551509/1378119815641694278). Even if you're not an expert at emulation coding, we could still use some visual assets, look-and-feel, UI, UX, and so on to make the app feel more cute and at-home on a cozy device like Playdate.

For coders: we could use help with setting up a [Windows simulator build CI](https://github.com/CrankBoyHQ/crankboy-app/issues/43).

CrankBoy uses a heavily modified version of Peanut-GB. Various [advanced optimization techniques](https://devforum.play.date/t/dirty-optimization-secrets-c-for-playdate/23011) were used to tailor the performance to the Playdate. If you wish to work on adding features to the emulator core itself, you may want to glance at those optimization techniques since it explains some of the unusual design choices made.

#### Tips

You can use the command line arg `rom=/Shared/Emulation/gb/games/<rom-file>` to quickly launch a rom while testing.

### Project Setup

After cloning the repository, please enable the clang-format git hook by running this command from the project root:

```bash
git config core.hooksPath githooks
```

For convenience, you can use the CLI arg `rom=<path/to/rom>` (where path is relative to the game's data directory) to launch a rom in bundled mode directly.

## Legal <!--userguide-->

**CrankBoy** is an independent, community-led project and is not affiliated with, authorized, sponsored, or endorsed by Nintendo Co., Ltd.

- **Game Boy®** is a registered trademark of Nintendo Co., Ltd.
- **Playdate™** is a trademark of Panic Inc.

This software is an emulator designed for the playback of legally acquired ROM files and homebrew software. The developers of CrankBoy do not provide, host, or distribute unlicensed ROM files. All other trademarks are the property of their respective owners.

CrankBoy relies on certain open source 3rd-party libraries. The credits and legal information regarding these can be viewed in-app or [here](./Source/credits.json).