<h1 align=center>Bejeweled 3 — Nintendo Switch port</h1>

A native Nintendo Switch port of the Xbox 360 (Xbox Live Arcade) version of **Bejeweled 3** (2011), with touch
controls.

It's not an emulator. The PowerPC code of the game's `default.xex` is statically recompiled to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch, and the game is drawn by Renut's
Direct3D-level renderer ([reNut](https://github.com/masterspike52/reNut) by masterspike52, as banjo-nx carries it)
directly on the Switch's own graphics API, deko3d. It is made from the
[Peggle port](https://github.com/ChanseyIsTheBest/peggle1-nx)'s code.

> [!NOTE]
> You need your own copy of the game, from your own Xbox 360: the **Xbox Live Arcade** game's content package
> (`Content/0000000000000000/584111D6/000D0000/B6CB3450D57A979840BC9E33BE0BB1CA9FC3AE1958`).

## How to install

1. Open the installer page: **https://chanseyisthebest.github.io/bejeweled3-nx-installer/**
2. Choose your game (its content package, or a folder that holds it) and press **Create bejeweled3-nx.zip**.
   Everything runs in your browser and the game files never leave your computer: the page checks your copy, downloads
   the build, and makes the shaders and the game pack from your files.
3. Extract the downloaded `bejeweled3-nx.zip` into `sdmc:/switch/`.

Your `bejeweled3-nx` folder should look like this:

```text
/switch/bejeweled3-nx/
  bejeweled3.nro
  LICENSES.txt
  bejeweled3.toml           (your settings; written at the first start when it is missing)
  deko3d_glsl.bin           (the game's shaders for the renderer, made from your copy)
  game_root.nxpack          (the game, in one file, made from your copy)
  game_root.nxidx
```

Launch it with a game override (hold **R** while starting an installed title) or a forwarder. Album applet mode does
not give the game enough memory. If you use a forwarder, set it to a **39-bit address space**.

To build everything yourself instead, see [docs/building.md](docs/building.md).

> [!CAUTION]
> **The first time you play**, the game compiles the shader variants it has not seen yet, so **it can stutter for a
> moment** in new places. They are saved in `deko3d_shaders.bin` next to the NRO and later sessions start smoothly.

## Controls

**Touch screen** (handheld): tap the title screen to start, and tap the menus, the list entries (Help & Options, the
pause menu), the badges and the button prompts; drag the volume sliders. On the mode screen, tap a side gem to turn
the ring and the middle gem to play. On the board, in every mode, tap a gem and then a neighbouring gem to swap them,
or swipe a gem towards its neighbour. In Zen, tap the "Press RB for Zen Options" box for its options. **+** pauses.
While you play by touch the board's cursor (the bracket around a gem) is hidden; it comes back when you use a
controller's stick or buttons.

**Controllers:** by default the face buttons work **by position, as on the Xbox 360 pad** the game was made for: the
bottom button is the Xbox's A, the right one the Xbox's B, and so on.

<div align="center">

| Switch | Xbox 360 |
| --- | --- |
| B (bottom) | A |
| A (right) | B |
| Y (left) | X |
| X (top) | Y |
| ZR / ZL | RT / LT |
| R / L | RB / LB |
| + | Start |
| − | Back |
| Sticks / D-pad | Sticks / D-pad |

</div>

To use the buttons by letter instead (the Switch's A acts as A), set `input_xbox_layout = false` in `bejeweled3.toml`.

The game has no multiplayer. Achievements and Leaderboards need Xbox LIVE, and the game says so.

## Settings

`bejeweled3.toml`, next to the NRO, holds only your options. Everything else is built into the NRO.

<div align="center">

| Setting | What it does |
| --- | --- |
| `bejeweled3_language` | Empty (default): the console's system language (English when the game does not have it). Or `en`, `de`, `es`, `fr`, `it`, `ja` or `pt` |
| `input_xbox_layout` | Face buttons by position (`true`, default) or by letter (`false`) |
| `input_joycon_sideways` | A single Joy-Con held sideways (`true`, default) |
| `input_joycon_sideways_stick` | With it, a single Joy-Con's stick turned for the sideways grip (`true`, default) |
| `bejeweled3_touch_hides_cursor` | A touch hides the board's cursor until a controller is used (`true`, default) |

</div>

If you delete `bejeweled3.toml`, the game writes the default one again at the next start.

## Resolution and performance

The game draws **1280x720** in handheld and docked mode and runs at **60 FPS**, as on the Xbox 360. No log files are
kept; only a crash writes `logs/rex/rex_crash.log`.

## How it works

- **The game's code is translated, not emulated.** [ReXGlue](https://github.com/rexglue/rexglue-sdk) translates every
  function of the game's program to C++, which is then compiled for the Switch. ReXGlue also gives the game what it
  expects from an Xbox 360: its system, files, saves, audio and controllers.
- **Renut's renderer on deko3d.** The game's Direct3D calls are drawn directly by Renut's Direct3D-level renderer
  through [plume](https://github.com/renderbag/plume)'s deko3d back end. On the PC (Vulkan) its frames match the
  emulated Xbox 360 GPU's, with the game's own display gamma table.
- **Touch.** Bejeweled 3's framework is the one PopCap shared with its touch platforms, and the Xbox 360 build still
  has its touch code: the port feeds the touch screen to it.
- **Shaders translated beforehand.** The game stores 71 of its 89 GPU programs in its executable: the installer
  translates them with [XenosRecomp](https://github.com/hedge-dev/XenosRecomp), DXC and SPIRV-Cross into deko3d's GLSL
  in your browser (`deko3d_glsl.bin`). The other 18 the game compiles while it runs, with the Xbox's effect compiler,
  so no installer can make them from the game's files: the NRO carries them. The console compiles each shader variant
  once with [uam](https://github.com/devkitPro/uam) and keeps it in `deko3d_shaders.bin`.
- **Optimization.** The 383 busiest game functions (from console profiles of Lightning) run as translated native
  copies that keep the game's registers in C++ locals and write back only the registers that changed, with the small
  functions they call inlined; a few of the hottest are written by hand (see
  [docs/optimising-functions.md](docs/optimising-functions.md)). The release NRO is built with link-time optimisation
  and profile-guided optimisation from a console session.

## Documentation

<div align="center">

| Document | What it explains |
| --- | --- |
| [docs/building.md](docs/building.md) | How to build the PC test build, the NRO and the game data |
| [docs/bejeweled3-notes.md](docs/bejeweled3-notes.md) | Notes on the game's code and every change of the port, each with its evidence |
| [docs/optimising-functions.md](docs/optimising-functions.md) | How the game's hot functions are made faster, measured and checked |
| [docs/hand-natives.md](docs/hand-natives.md) | The rules for hand-written native functions |
| [docs/peggle1-notes.md](docs/peggle1-notes.md) | The Peggle port's notes: the code this port is made from |

</div>

## Build

Windows, LLVM clang 20 and the VS 2022 Build Tools for the PC build; devkitA64, libnx and deko3d from
[devkitPro](https://devkitpro.org) for the Switch, with uam and SPIRV-Cross built for the PC
(`tools/deko3d_build_tools.sh`); Python; and your own copy of the game. The whole process is in
[docs/building.md](docs/building.md).

## Credits

- **PopCap Games** — creators of Bejeweled 3. This is an unofficial, fan-made port with no affiliation.
- **ChanseyIsTheBest** — this port and the Peggle ports it is made from.
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)** — the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)** — nfsmw-nx: the Switch layer of the SDK and much of the
  method; and [nfsmw-nx-installer](https://github.com/StevensND/nfsmw-nx-installer), the installer page this port's
  is built on.
- **[masterspike52](https://github.com/masterspike52/reNut)** — reNut: the Direct3D-level renderer the port draws
  with (its reuse in this port permitted by its author).
- **banjo-nx** (Banjo-Kazooie: Nuts & Bolts Switch port) — Renut's Switch port: the deko3d back end of plume, the
  shader generator and its optimisations.
- **[renderbag](https://github.com/renderbag/plume)** — plume; **[fincs](https://github.com/devkitPro/uam)** and
  devkitPro — deko3d and uam.
- **[hedge-dev](https://github.com/hedge-dev/XenosRecomp)** — XenosRecomp, the Xenos shader translator.
- **[devkitPro](https://devkitpro.org) and [switchbrew](https://github.com/switchbrew/libnx)** — devkitA64 and libnx.
- The sibling ports, which share their fixes with this one, and the libraries listed in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

No affiliation with PopCap Games, Electronic Arts, Microsoft or Nintendo. "Bejeweled" is a trademark of PopCap Games /
Electronic Arts; the NRO's icon is the game's artwork, © PopCap Games. This repository contains no assets or program
code from the original game. Users must provide their
own legally obtained copy, and everything made from it (`assets/`, `app/generated/`, the shader package, the game
pack) is made on their own computer. Running homebrew requires custom firmware, which violates Nintendo's ToS and can
get a console banned — your call.

Source code is provided under the GPL-3.0 License (see [LICENSE](LICENSE)), like the ports it builds on. The SDK keeps
its BSD-3-Clause license (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
