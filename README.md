<p align=center><img src="docs/icon.jpg" alt="Bejeweled 3" width="256" height="256"></p>

<h1 align=center>Bejeweled 3: Nintendo Switch port</h1>

<p align=center><b>bejeweled3-nx 1.0.2</b> · <a href="https://chanseyisthebest.github.io/bejeweled3-nx-installer/">web installer</a> · <a href="https://github.com/ChanseyIsTheBest/bejeweled3-nx/releases/tag/v1.0.2">release</a></p>

An unofficial native port of the Xbox 360 (Xbox Live Arcade) version of **Bejeweled 3**, with touch controls. Made by
static recompilation with [ReXGlue](https://github.com/rexglue/rexglue-sdk), from the
[Peggle port](https://github.com/ChanseyIsTheBest/peggle1-nx)'s code. No emulator.

This repository contains no game files. You need your own copy of the game: the Xbox Live Arcade content package from
your Xbox 360 (`584111D6`).

## Playing it on the Switch

1. Open the web installer: **https://chanseyisthebest.github.io/bejeweled3-nx-installer/**
2. Give it your game (its content package, or a folder that holds it) and press **Create bejeweled3-nx.zip**. Your
   files never leave your computer.
3. Extract `bejeweled3-nx.zip` into `sdmc:/switch/`.
4. Start it with a title override (hold **R** while starting an installed game) or a forwarder.

- Album applet mode does not give the game enough memory.
- A forwarder must be set to a **39-bit address space**.
- The first time you play, the game can stutter for a moment in new places while it compiles shaders. Later sessions
  start smoothly.

Features:

- Touch controls in handheld: tap the menus, and tap a gem and then its neighbour (or swipe) to swap them, in every
  mode. The board's cursor hides while you play by touch (`bejeweled3_touch_hides_cursor`).
- Native deko3d renderer (Renut); 1280x720 in handheld and docked, 60 fps as on the Xbox 360.
- The console's system language by default, or `bejeweled3_language`: `en`, `de`, `es`, `fr`, `it`, `ja` or `pt`.
- Face buttons work by position as on the Xbox 360 pad (`input_xbox_layout = false` for by letter); a single Joy-Con
  can be held sideways.
- Settings are in `bejeweled3.toml` next to the NRO; delete it to get the defaults back.
- A game menu: hold **A + B + X + Y + L + R** together (**+** closes it) for the settings (language, rumble, button
  layout, Joy-Con, touch, and the menu's own buttons, colour, font and background), the achievements and each player's
  controls. It saves your choices in `bejeweled3.toml`.
- Rumble: the game's own vibration, on by default (off in the menu, or `input_rumble = false`).
- A smaller install: the installer stores the game files compressed, with the sounds as Opus and the intro movie as
  H.265 (played by the Switch's video decoder).
- No log files, except `logs/rex/rex_crash.log` after a crash.

Known issue:

- Achievements and Leaderboards need Xbox LIVE (the game says so). The game has no multiplayer.

## Building

Needs Windows with LLVM clang 20 and the VS 2022 Build Tools (PC build); devkitA64, libnx and deko3d from
[devkitPro](https://devkitpro.org) (Switch build), with uam and SPIRV-Cross built for the PC
(`tools/deko3d_build_tools.sh`); Python; and your own copy of the game.

The steps (PC test build, NRO, game data) are in [docs/building.md](docs/building.md). More in
[docs/bejeweled3-notes.md](docs/bejeweled3-notes.md), [docs/optimising-functions.md](docs/optimising-functions.md),
[docs/hand-natives.md](docs/hand-natives.md) and the Peggle port's [docs/peggle1-notes.md](docs/peggle1-notes.md).

## Credits and license

- **PopCap Games**: creators of Bejeweled 3. This is an unofficial, fan-made port.
- **ChanseyIsTheBest**: this port and the Peggle ports it is made from.
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)**: the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)**: nfsmw-nx (the SDK's Switch layer and much of the method) and
  [nfsmw-nx-installer](https://github.com/StevensND/nfsmw-nx-installer), which the installer page is built on.
- **[masterspike52](https://github.com/masterspike52/reNut)**: reNut, the renderer (reused with its author's permission).
- **banjo-nx** (Banjo-Kazooie: Nuts & Bolts port): Renut's Switch port (plume's deko3d back end, the shader generator
  and its optimisations).
- **[renderbag](https://github.com/renderbag/plume)**: plume. **[fincs](https://github.com/devkitPro/uam)** and
  devkitPro: deko3d and uam.
- **[hedge-dev](https://github.com/hedge-dev/XenosRecomp)**: XenosRecomp.
- **[devkitPro](https://devkitpro.org)** and **[switchbrew](https://github.com/switchbrew/libnx)**: devkitA64 and libnx.
- The sibling ports, which share their fixes, and the libraries in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

GPL-3.0 (see [LICENSE](LICENSE)); the SDK keeps its BSD-3-Clause license (see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

No affiliation with PopCap Games, Electronic Arts, Microsoft or Nintendo. "Bejeweled" is a trademark of PopCap Games /
Electronic Arts; the icon (`docs/icon.jpg`, also the NRO's icon) is the game's artwork, © PopCap Games. Apart from that
icon, this repository contains no assets or program code from the original game: you provide your own legally obtained
copy, and everything made from it is made on your own computer. Running homebrew requires custom firmware, which
violates Nintendo's ToS and can get a console banned.
