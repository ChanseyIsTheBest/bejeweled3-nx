# Bejeweled 3 port: notes

This file records the port: what each step found, with its evidence. The project is a copy of peggle1-nx: its SDK
fork, tools, app and Renut renderer. Identifiers were renamed peggle1 -> bejeweled3 with `tools/port_rename.py`.
Peggle 1's notes (`peggle1-notes.md`) and Peggle 2's (`peggle2-notes.md`) stay as the reference for everything taken
from those ports. Comments that say "peggle1-nx" or "Peggle 2" record findings from those ports.

## The game (2026-10-21)

The user's file is `D:\peggle 2\Bejeweled 3.rar`. It was extracted to `D:\peggle 2\bejeweled 3` and holds one package:
`Bejeweled 3/584111D6/000D0000/B6CB3450...`. That is an XBLA game (content type `0x000D0000`), title **584111D6**,
with no title update and no DLC. `tools/stfs_extract.py` hash-checked every block and extracted the package to
`assets/game_root` (git-ignored): 18 files, 209.3 MiB.

- **`default.xex`** (`tools/xex_info.py`):
  - Format and layout: XEX2, original PE name `default.exe`, base `0x82000000`, entry `0x822EC320`, image size
    `0x01000000`.
  - Identity: version 0.0.0.1, media `55825998`.
  - Imports: only `xam.xex` and `xboxkrnl.exe` (2.0.20764).
  - Static libraries: XDK **2.0.20764** (XAPILIB, D3D9, D3DX9, XBOXKRNL, XNET, XONLINE, XGRAPHC, XAUDIO2, XAPOBA,
    XMCORE, XMP, XMIC, XWMAD); compiler 16.0.11886.
  - XDK placement: between Peggle 2's 21256 and Peggle 1's 7776; SA2's is 21076.
- **Sections** (from the PE header in the loaded image):

  | Section | Range |
  |---|---|
  | `.rdata` | `82000400-8228DD4C` |
  | `.pdata` | `8228DE00-822B9C30` (22,470 runtime function entries) |
  | `.text` | `822C0000-82ACD0A4` |
  | `.data` | `82AD0000-82FC21D8` |
  | `.XBMOVIE` | `82FC2200` |
  | `.idata` | `82FD0000` |
  | `.XBLD` | `82FE0000` |
  | `.reloc` | `82FE0200-83049920` (past the 16 MB image: not loaded, so no relocation data) |

  There are no `.embsec_` sections and no linker jump stubs (an image scan for `lis r11; addi r11; mtctr r11; bctr`
  finds 0). Peggle 1's `bejeweled3_stubs.cpp` was therefore left out.
- **Data:**
  - `main.rsb`, 204 MB: a PopCap resource bundle, magic `rsb1`, version 3.
  - `intro.wmv`
  - the achievement pictures, `banner.png`, `game.png`
  - `ArcadeInfo.xml`
- **Strings in the image:**
  - `properties/resources.xml`, `music.xml`, `partner.xml`, `badges.txt`, `ranks.txt`, `tips.txt`
  - `Language` / `LanguagesForCountry`; the game calls `XGetLanguage`
  - no "multiplayer" or "versus" string

## Code generation (2026-10-21)

- **Manifest:** `app/bejeweled3_manifest.toml`. The quick loop is `tools/win/codegen_bj3.ps1`; the full run is
  `tools/codegen.ps1 -PostPasses`.
- **First run:** 265 files, 135 MB of C++, 29,217 functions, and no "target not in any function". It left four
  "Unresolved conditional branch to 0x828B8104".
  - The function at `0x828B80F0` is reached only through data. The generator cut it at its first `blr`
    (`0x828B8110`), but its body runs on to the tail call at `0x828B8190`.
  - Fix: an entry `size = 0xA4` in `app/overrides.toml`.
- **Jump tables** (`tools/switch_check.py --code 822C0000:82ACD0A4`):
  - `sub_828FC5A0` is the localized-string lookup. It is a table of 2,706 cases (`cmplwi r3,2705`), each
    `lis/addi r3 = string; blr`. The generator's limit of 1,024 cut it short.
    - Fix: every case is listed in `[[switch_tables]]`.
    - The limit `REX_MAX_JUMP_TABLE_ENTRIES` is now 4096 in both codegen scripts.
  - The check's 24 "table jumps with no generated code" are false alarms with the optimised codegen (the index
    register is a C++ local). For example, `sub_82868590`'s `bctr` at `0x828686BC` is generated as
    `switch (r26.u32)`.
- **setjmp / longjmp** (found by shape, as in Peggle 1/2), declared in `app/overrides.toml`:
  - setjmp `0x825E33C0`: hook word `0x82FC21C8`, `mfcr`, `stfd f14,0(r3)`; 12 call sites.
  - longjmp `0x825E30A0`: `lwz r0,312(r3)`, `mr r7,r3`, `lfd f14,0(r7)`; 20 call sites.
- **Pointer sweep** (`tools/pointer_fixpoint.py`): 75 functions accepted in pass 1, fixpoint in pass 2. Of the 30
  rejections, the ones checked by hand are SEH scope tables: `{count, filter, handler, ...}` in `.rdata` pointing
  at `mr r8,r8` scope labels inside their functions, for example `0x8228D3A0` -> `0x822F04CC/DC/E8`.
- **CRT functions** (`tools/p2_function_matches.py`):

  | Function | Bejeweled 3 | Peggle 2 | Match |
  |---|---|---|---|
  | memcpy | `0x825DF500` | `0x825F0AE0` | byte for byte |
  | memset | `0x825DEDD0` | `0x825F10B0` | byte for byte |

  Their hand-written natives apply unchanged: `app/src/cpu/bejeweled3_hot_guest.cpp`. `tools/inline_memcalls.py`
  takes the same addresses.
- **Other addresses:**

  | What | Bejeweled 3 | How |
  |---|---|---|
  | `D3DDevice_Swap` | `sub_822F5EB0` | the only caller of `VdSwap` |
  | `_CxxThrowException` | `sub_825E0108` | 492 call sites; copies the 32-byte template at `0x8207F8A4`, calls RaiseException |
  | `RaiseException` | `sub_825F9868` | the CRT's caller of `RtlRaiseException` |
  | entry point | `0x822EC320` | XEX header (`tools/leaf_dead_stores.py`) |
- **Optimised codegen:** `app/perf_overrides.toml` is peggle1-nx's stage-1 set as shipped, including
  `non_volatile_as_local_keep = ["v64-v72"]` (the WMV decoder). `tools/post_codegen.py` runs with Bejeweled 3's
  addresses, and its lists (promotions, localized copies) start empty.

## First boots on the PC (2026-10-21, emulated GPU)

- **Build:** `tools\win\run.ps1 out\pc_configure.log "cmake --preset win-pc -S app -DBEJEWELED3_RENUT_RENDERER=OFF
  -DBEJEWELED3_PERF_POST=ON"`; Renut's renderer stays off until its hooks are at Bejeweled 3's addresses.
- **The first build re-ran codegen by itself:** its stamp file did not exist yet.
- **Boot 1:** a guest read of `0x60` on the main thread 2 s in, in `sub_822F0520` (`bejeweled3.exe+0x1C80DEA`,
  `llvm-symbolizer`).
  - `sub_822F0520` is a `__finally` funclet (`addi r31,r12,-320`: the parent's frame). Its caller `0x822F04CC` was one
    of the pointer sweep's 75 acceptances.
  - `0x822F04CC` is no function. It is a `__try` scope label (`mr r8,r8`, right after a `bl`) inside `sub_822EFCB8`
    (`.pdata`: 2,120 bytes), pointed at by the SEH scope table at `0x8228D3B0`.
  - As a function it split its parent. With registers as C++ locals, the parent's r31/r12 did not reach the
    funclet.
  - Fix: the entry is commented out. `tools/pointer_fixpoint.py` now rejects candidates that start with `mr r8,r8`.
    The other 74 acceptances all start right after a `blr`, `b`, `bctr` or padding (checked with a script).

## Renut's renderer at Bejeweled 3's addresses (2026-10-21)

A helper agent matched Renut's hooks against Bejeweled 3, from Peggle 2's tree (XDK 21256, the nearest). Its data,
method and evidence are in `out/renut_port/` (`hook_map.md` / `.json`, `device_offsets.md` / `.json`,
`device_offset_sites_bj3*.json`, `scripts/`).

- **Hooks:** 361 in all: 324 with high confidence, 37 absent (now in `#if 0`, each with its search evidence).
  - 318 of the 324 have the same shape as Peggle 2's function. 186 are identical with every immediate; the rest
    differ only in device offsets and constant addresses.
  - Anchors used for the matching:
    - the state tables, `InitializeApiState` (`sub_82310D70`) and Swap (`sub_822F5EB0`)
    - the only callers of the same kernel imports
    - aligned call sites
    - the link order of each object
  - Six hooks Peggle 2 had to leave out are live here: InsertCallback `sub_822F8510`, Acquire/ReleaseThreadOwnership
    `sub_822FEFA8` / `sub_822FEFE8`, GetDisplayMode `sub_822FAAE8`, DrawVertices `sub_82309FE8`, Present
    `sub_822F6618`.
  - Not linked:
    - DrawVerticesUP (the game batches through BeginVertices)
    - the surface / volume locks and descs
    - command buffers and tiling
    - N&B's game hooks (no XUI)
  - `tools/renut_hook_remap.py` was rewritten for Bejeweled 3. Swap is the only `_SHARED` hook.
- **The device (XDK 20764) is Peggle 2's layout with two shifts** (offsets in Peggle 2's numbering):
  - the same up to about +11527
  - 24 bytes lower from +11584 to +13560
  - 16 bytes lower from +13600 on

  Evidence: 431 offsets harvested from 559 paired functions, with deltas 0 (185), -16 (163) and -24 (76). The global
  qword `0x82AD07A0` holds build 20764 (`0x511C`).
  - `tools/renut_device_offsets_bj3.py` rewrote 168 lines and 11 text sites.
  - Key values: kPresentInterval 13984, AutoFrontBuffer 15216, Textures 12896, RenderTargets 12792, Viewport 13000,
    snapshot size `0x3308`.
  - The frame hooks' `kDeviceIntervalOffset` is 13984.
- **Guest data:**

  | What | Address |
  |---|---|
  | state tables | `0x82AD01E8` / `0x82AD06A8` |
  | format table | `0x82001620` |
  | caps | `0x82001830` |
  | HiZ owner | `0x82B113E0` |
  | export switches | `0x82B103B0` / `0x82B103B4` |
  | global qword | `0x82AD07A0` |
  | full viewport | `0x82001284` |
  | no-large-pages flag | `0x82B10380` |
  | VdGlobalDevice slot | `0x82000618` |
  | XMemFree | `sub_825F8670` |
  | GetCurrentThreadId | `sub_825F8690` |
  | Fusion flag | none (IsFusion is not linked and nothing reads it) |
- **Behaviour checked in Bejeweled 3's own D3D:**
  - Peggle 2's 32-byte block at +11028 (`sub_823119B0`)
  - the +16712 clear, at +17104 here (`sub_82311390`)
  - Peggle 2's SetTexture(NULL) fetch-type clear (`sub_82302BC8`)
  - Peggle 2's BeginExport condition (`sub_822F97A8`)
  - 41 read chunks for the snapshot deltas (N&B 39, Peggle 2 40)
  - the automatic front buffer, kept (`sub_82468CC0` leaves DisableAutoFrontBuffer at 0)
- **Signature differences, all as in Peggle 2:**
  - BlockOnFence's flag is in r7.
  - LockResource has a 64-bit r4.
  - ClearF replaces Z when `Flags & 0x10`.
  - AddRef and Release are atomic.
  - GetStreamSource reads r5 and r6.

## PC runs (2026-10-22)

- **Boot 2**, emulated GPU, no input: no crash in 60 s at 60 frames a second. Seen: the PopCap gem video
  (`intro.wmv`), the PopCap logo, and the title "PRESS START".
- **Boot 3** crashed at "Call to invalid or unregistered function at guest address 0x82725E98". Two of the sweep's
  "fragment" rejections are functions (`app/overrides.toml` has the evidence); the others are jump tables after their
  `bctr`, or C++ unwind maps.
- **Menus 2** (START, then A and DOWN), emulated GPU, all working:
  - title, main menu (Play, Help & Options, Badges, Achievements, Leaderboards, Exit Game)
  - Classic mode selection, loading
  - the Classic board: gem selection, swaps, the "Illegal Move" dialog
- **The board is random:** two runs of the same script dealt different gems.
  - `MTRand::SRand(this, seed)` is `sub_825286E0` (a seed of 0 becomes 4357, then the 0x6C078965 recurrence; 11 call
    sites).
  - `bejeweled3_test_seed` (`src/bejeweled3_debug_hooks.cpp`) replaces every seed in test runs, so both renderers see
    the same boards.
- **The reference** (`tools/win/bj3_lockstep.ps1 -Tag xenos2 -From 600 -Frames 1500`, `REX_BEJEWELED3_TEST_SEED=20261022`):
  - input: START at frame 840, A at 1200, then moves
  - covers title, main menu, Classic selection, the board loading and "GO!"
  - recorded to `out/pc/captures/lock_xenos2.rgba` (half size, 1,500 frames); contact sheet `out/renut_tests/lock_xenos2.jpg`
- **Renut on Vulkan, first run** (`renut1`): the intro, title, menus and board were drawn. The cache lacked 5 pixel
  shaders (D3DX effects compiled at run time); the run captured them and the next build added them (76 in the cache).
- **Renut crash with the fixed seed** at START: XMemCpy (`sub_825F81E0`) wrote to the unmapped `0x5ADA0070`.
  - The SDK's guest crash report (registers, the guest stack back chain), which was Switch only, now runs on Windows
    too (`sdk/src/system/xmemory.cpp`, VirtualQuery for safe reads).
  - The caller was `sub_823064F8`: `D3DPixelShader_GetFunction`, called by D3DX's effect code (`sub_826C0018`). The
    vertex shader version is `sub_82306550` (`sub_826BFED0`).
  - Both read the D3D shader object's fields: the container at `+0x28` / `+0x368`, the physical part through
    `+0x18` / `+0x20`. A shader Renut creates is its record, which has none of those fields.
  - Fix (`d3d/hooks/shader.cpp`): the record keeps its container, and the two hooks give it back. Shaders set up in
    place (XGRegister*Shader) go to the original.

## Renut against the emulated GPU (2026-10-22)

- **The setup for comparable runs:**
  - `bejeweled3_test_seed` gives both runs the same boards.
  - `clock_per_swap_hz = 60` with `vblank_per_swap = true` is the SDK's frame clock from peggle2-nx. Renut's Swap
    replaces D3D's whole Swap, so it never reached the kernel's VdSwap that advances that clock; the frame hook now
    calls `Clock::NotifyGuestSwap()` after Renut's Swap.
- **Loads still drift:** the game loads in the background while it keeps swapping, so a load takes a host-dependent
  number of frames. Renut missed a frame-timed A at the Classic menu because the menu came later than on the emulated
  GPU. The route that works presses START, then A every 120 frames (`lock_xenos4` / `lock_renut4`, 2,400 frames from
  game frame 600).
- **Result** (`tools/compare_raw.py --window 600`, each frame against its best match):
  - mean mad 2.61, median 0.64 % of pixels over the tolerance
  - both runs: title, main menu, Classic, "How to Play", the same seeded board, gem selection
  - a steady board frame differs by mad 2.07, which is the display gamma ramp: the game sets a curve Renut applies as
    a power, exponent 1.172, worst 6 of 255, as in Peggle 1; plus the selected gem's spin phase
  - the worst frames are transitions (fades, the mode gem's zoom) at another phase
- `tools/raw_sheet.py` makes contact sheets of a recording; `tools/raw_pair.py` puts A | B | diff side by side.

## Touch controls (2026-10-22)

The user asked (2026-10-21): "implement touch controls in game ... being able to tap all the UI elements and being
able to tap and swipe the gems to swap them".

- **The game's own touch path** (out/touch/notes.txt; `src/bejeweled3_touch.cpp`): the framework is PopCap's later
  SexyAppFramework, the one shared with its touch platforms, and the Xbox 360 build still links its touch code.
  - SexyAppBase's vtable slots 96 / 97 / 98 load `mWidgetManager` (+880) and jump to WidgetManager's TouchBegan
    (`sub_823FAAA0`), TouchEnded (`sub_823FACE8`) and TouchMoved (`sub_823FABD0`).
  - Each takes a SexyTouch (+8 x, +12 y, +16 / +20 previous) and finds the widget under it: `GetWidgetAt`
    `sub_823FA450`, made local through slot 15. It then calls the widget's TouchBegan / Moved / Ended (slots 61 /
    62 / 63).
  - Their default is the PC game's mouse handling: MouseDown(x, y, 1), MouseDrag, MouseUp. That gives buttons,
    dialogs and the board's click-and-drag swaps.
  - Nothing calls them on the console.
- **The port calls them** on the game's thread, between frames, from the D3DDevice_Swap hook. The touches queued
  since the previous frame come from:
  - the Switch's touch screen, polled once a frame (`hidGetTouchScreenStates`; 1280x720 panel coordinates)
  - on the PC, the left mouse button
  - for tests, `bejeweled3_touch_script` ("frame:down|move|up:x:y" in 1280x720 screen pixels)
- **The app object:** a hook on SexyAppBase's constructor, `sub_82406770` (it stores the vtable `0x8204F00C`;
  `sub_82405E10`, which names it too, is the destructor).
- **Coordinates:** the widgets live in 960x720 (the manager's +0x34 / +0x38), centred on the 1280x720 screen. The
  manager's mouse destination rectangle (+0xBC: -160, 0, 1280, 720) maps the screen to them, so a screen x is the
  game's x + 160. The first test tapped at the screen's coordinates and hit "Help & Options" instead of "Play".
- **Checked on the PC** (`out/renut_tests/touch4`, scripted touches):
  - "Play" opens the mode screen
  - the Classic gem starts Classic ("How to Play")
  - "Continue" loads the board
  - a tap selects a gem
  - a drag one cell to the right swaps; the swap made no match, so the game showed "Illegal Move"
- Keys: `bejeweled3_touch` (on), `bejeweled3_touch_log`, `bejeweled3_touch_script`.

## Language, Xbox Live menus, multiplayer (2026-10-22)

- **Language works** (`bejeweled3_language`; empty follows the system language, as Peggle 1). The game takes its
  language from XGetLanguage and reads `locales\<LL-CC>\...` from `main.rsb`.
  - PC run with `fr`: "Jouer / Aide et options / Succès / Classements / Quitter le jeu", "Classique"
    (`locales\FR-FR`).
  - With `ja`: "プレイ", "クラシック" (`locales\JA-JP`).
  - `main.rsb` holds seven string tables: en, de, es, fr, it, ja and pt. Korean and Chinese systems get English.
- **Xbox Live menus** (`out/renut_tests/live1`):
  - Achievements calls the stub `XamShowAchievementsUI` (logged as STUB): nothing opens and the menu stays usable.
  - Leaderboards shows the game's own "You need to be signed into Xbox LIVE to access the leaderboards", which OK
    closes. The SDK's `XUserReadStats` / `XamUserCreateStatsEnumerator` fixes (peggle1-nx 1.0.1) are in this SDK.
- **No multiplayer.** The main menu is Play, Help & Options, Badges, Achievements, Leaderboards, Exit Game. The
  image has no multiplayer or versus strings, and the six modes on the mode screen are single-player.

## The first Switch build (2026-10-22)

- **Shaders:**
  - Renut's XenosRecomp with `BANJO_HLSL_OVERRIDE=out/pc/renut_gpu/early_alpha_hlsl` and `BANJO_DEKO3D_SPIRV_DIR`
    turns the 76 containers into SPIR-V.
  - `deko3d_shaders.py`: 76 of 76 compile with uam (66 vertex, median 35 KB; 10 pixel).
  - `deko3d_glsl_opt.py --check`, then `deko3d_shader_package.py`: 531 KB.
  - `renut_switch_shader_cache.py`.
  - The deko3d PC tools (SPIRV-Cross, uam) were copied from peggle1-nx's `out/deko3d_tools`.
- **Build:** `tools/win/build_switch.ps1 -Pgo none -Define "BEJEWELED3_SWITCH_LTO=OFF"`, NRO 38.8 MB.
- **Ryujinx** (`emu1`, `tools/win/bj3_emu_run.sh`; SD `switch\bejeweled3-nx` is a junction to
  `out/emu_sd/bejeweled3-nx` in the Peggle data folder):
  - PopCap logo, title, menu, Classic, How to Play, the board, gem selection
  - 60.0 game frames a second, render thread 1.17 ms a frame, 22 uam variant compiles, no crash in 150 s
- **Profiling in Ryujinx** (`prof2`, 300 s):
  - `switch_profile_report = true` and `switch_profile_stacks = true` in the SD toml; the release defaults turn the
    report off.
  - Ryujinx gives the NRO no `argv[0]` path, so `RexSwitchLogDir()` falls back to `sdmc:/switch/`, and the report is
    `sdcard/switch/rex_profile.log` of the data folder. Peggle's runs share that file: copy it out right after a run.
  - CPU total 75-79 % of 400 % at 60 fps; the busy guest code is the game thread's.
  - `tools/switch_hot_functions.py --from-report 4` gives the 35 hottest guest functions; none uses lwarx/stwcx.
    They are in `app/src/cpu/hot/localized.txt`.

## Build loop fixes (2026-10-22)

- **The PC build ran the generator again after every regeneration,** and its passes rewrote `bejeweled3_pch.h` while
  the build compiled it ("has been modified since the precompiled header was built").
  - Ninja treats every generated `.cpp` as an output of the codegen command. `post_codegen.py` keeps the old time of
    a file it did not change, so those outputs stayed older than the inputs and the generator executable.
  - Each build also re-runs CMake, because the codegen command writes `codegen_profile.txt`, a configure input. That
    regenerates `version.h` and relinks `rexgluerd.exe`.
- **Fix:**
  - `tools/win/bj3_regen_build.ps1` builds the codegen target on its own, then runs `tools/regen_restat.py`, then the
    rest.
  - The script moves the generated sources the passes left alone to now. It moves their objects in `out/pc` and
    `out/nx` past them, but only objects that were up to date with the file and every generated header. Headers keep
    their times: clang refuses a precompiled header whose header changed time.
  - Checked: the build that followed did not run the generator.
- `sdk/src/system/xmemory.cpp`: the guest crash report (registers, the guest stack chain) is written on Windows too.

## The 35 hottest functions (2026-10-22)

- **The list** (`app/src/cpu/hot/localized.txt`): the Ryujinx profile's 35 hottest guest functions by self time
  (`prof2`). None uses lwarx/stwcx. `tools/localize_native.py` (run by `post_codegen.py`) made 35 copies, with smart
  sync at 45 of 134 call sites.
- **`sub_825F9040` is out.** It is a vector memset (stvx loops, 128-byte cache-line zeroing, the line size read from
  `0x82ADDEB4`).
  - With its copy, the PC boot jumped to address 0 at 2.7 s (Windows' application log: 0xC0000005, fault offset 0).
  - Bisected with the per-copy keys (`out/bisect_hot.ps1`): with only `bejeweled3_cpu_native_sub_825F9040` off, the
    game runs, and with every other copy on.
  - Not root-caused. The 36th hottest, `sub_8255EF98`, takes its place.

## Verification of the 35 copies (2026-10-22)

- **PC, verify mode** (`bejeweled3_cpu_verify = true`, every call checked; `out/renut_tests/verify2`, 4 minutes of
  scripted play with random swaps): 37 natives (35 copies, memcpy and memset) all called; about 200 M checks,
  0 differences.
- **Ryujinx, ARM, verify mode** (`verify_emu1`, 270 s): 47.8 M checks, 0 differences. 33 of the 35 copies were
  called. `sub_82A0F590` and `sub_82A28A58` did not occur on that run's random board; they did on the PC's.
- In verify mode the emulator runs at about 1 frame a second (the guarded calls cost more than the frame).

## The exact display ramp (2026-10-22)

- Renut applied the game's display ramp as a power curve (exponent 1.172, worst 6 of 255). That was the steady
  difference to the emulated GPU: mad 2.07 on a still board.
- `backend/present.cpp` now uploads the game's 256-entry table: a 256x3 RGBA8 texture, each 10-bit value as r = high
  8 bits, g = low 2 bits. `present_ps.hlsl` indexes it with round(colour x 255), as `apply_gamma_table` does
  (`bejeweled3_renut_display_ramp_table`, on).
- **PC Vulkan** (`lock_renut5` against `lock_xenos4`, best match within 600 frames):
  - mean mad 0.92 over the route (was 2.61)
  - still board frames: mad 0.27-0.30, and 103 of them identical within the tolerance
  - what remains is the selected gem's spin and the waterfall, which are animation phase
- **deko3d:** the same shader drew black frames, with a `Load` as with a point sample (Ryujinx `ramp_on`,
  `ramp_on2`); with the key off the frames are right (`ramp_off`). Not found yet. The Switch's built-in settings turn
  the table off (`app/bejeweled3_switch_defaults.toml`), so the Switch keeps the power curve.

## Touch controls, the board (2026-10-22)

- **On the console the board's mouse code treats the gamepad cursor's gem as selected.** A piece's byte +604 is its
  selection flag, and the cursor's gem carries it. So:
  - the board's MouseDown (`sub_82A1AE50`) took a click as the second gem of a swap with the cursor's gem
  - a tap next to the cursor swapped with it; any other tap gave "Illegal Move: You must select two gems that are
    adjacent to each other"
- **The board** (vtable `0x8227AB18`) holds its pieces at +204 (8x8; a piece's column at +8, row at +0xC, position
  at +0x10 / +0x14 in 128-unit cells). `sub_82A0FC20` returns the first flagged piece.
- **Its drag** (`sub_82A10BF0`) swaps the flagged piece with the neighbour in the drag's main direction, past a
  threshold of the screen size / 30 from the touch-down point (+10208 / +10212). After that swap the game flags its
  cursor's gem again every frame, and further drag events swapped that one too ("Each swap must create a row of
  three").
- **The port** (`src/bejeweled3_touch.cpp`):
  - a touch on the board is taps
  - before each tap, every flag but the touch's own earlier selection is cleared (a selection made by the mouse has
    +605 set too)
  - the touch taps its gem at once
  - a swipe of half a cell (39 of 720 lines) taps the neighbour in its main direction, which is the swap; the rest
    of the swipe is ignored
  - touches elsewhere (menus, dialogs) go to the game as they are
- **Checked** (seed 20261022; the legal move (3,1)<->(4,1) was read from the board's picture):
  - PC: a swipe down (`swipe5`) matched (75, then 150, 225, 300, "GOOD!"); two taps (`taptap2`) matched (150, 225)
  - Ryujinx (`swipe_emu1`): the same swipe matched, at 60 frames a second

## Delivered (2026-10-22, development)

- `out/dist/dev_20261022_bejeweled3/bejeweled3-nx/`: `bejeweled3.nro`, the game pack and README.txt.
  - The NRO: SHA-256 `02470e3c...e23c7840`, 39,276,688 bytes; no LTO / PGO; logs off; the exact display ramp on; the
    real 35 hottest functions (with `sub_825F9040`).
  - ELF and map: `out/nx/elf/bejeweled3_dev_top35_nolto_20261022_0435.*`.
  - Earlier: 04:05 (`15792bb9...`, `sub_8255EF98` in place of `sub_825F9040`).
  - The 03:49 delivery (`17510228...`, `..._0349.*`) had the power curve on the Switch.
- The homebrew menu entry is "Bejeweled 3" 1.0.0 with the game's own icon (`out/icon_dev.jpg`: `game.png` scaled to
  256x256, not committed). The first delivery (03:09, `b1864836...`) still said "Peggle" 1.0.1 with Peggle's icon,
  because peggle1-nx's `app/icon.jpg` and NRO name had come along in the copy.
- **The 35 copies, measured** (Ryujinx, the same 300 s script, random boards: `prof2` before, `prof3` after): CPU
  total 76.5 % -> 71.8 % of 400 %; the game thread's busy samples 3,753 -> 3,358 (-10.5 %).

## Saves, quitting, soak (2026-10-22)

- **Saves work** (PC `save4`):
  - At start the game opens `Bej3.sav` (XamContentCreate mode 3, "none" on a first start). Leaving a game
    (pause, Exit Game) creates it (mode 4): one file, `bej.sav`, 11,747 B, plus its header. The profile settings
    (0x63E83FFF) are 361 B.
  - Classic then asks "Continue Game? Continue saved game or start a new one", and continuing restored the board.
  - The pause menu: Resume Game, Help & Options, Leaderboards, Achievements, Exit Game.
- **Quitting** (main menu, Exit Game, "Quit? Do you want to quit Bejeweled 3?" Yes):
  - PC: "Execution complete" 1.3 s after Yes, exit code 0.
  - Ryujinx: "Execution complete" 1.3 s after Yes, then the applet's Exit (`ServiceAm Exit`), 1.6 s after Yes
    (`exit_emu2` to `exit_emu4`; the game's and Ryujinx's logs lined up by the deko3d device start). That is the
    SDK's exit (`__nx_applet_exit_mode = 1`, `__libnx_exit`): ISelfController::Exit, after which the system ends
    the process with all its threads (`windowed_app_main_switch.cpp` records it on the console). Ryujinx stubs
    Exit, so there the process stays until the window is closed; one thread keeps polling for SaltyNX's ports
    (`ConnectToNamedPort` NotFound once a second, the overlay FPS support). An earlier reading of 17 s here was
    wrong.
  - The Quit dialog itself stays responsive (PC, 15 s).
- **Ryujinx logs:** runs often lack the last seconds of the game's log and shots, although the log flushes every
  second. Ryujinx keeps them when the runner kills it. Ryujinx's own output (`ryu_stdout.txt`) is the record of how a
  run ended.
- **Soak** (Ryujinx `soak1`, 8 minutes of scripted play with random swaps, the delivered NRO): through level 1 into
  level 2 (the scene changes), mean 58.2 game frames a second, no crash.
- **System language** (Ryujinx's `Config.json` set to French with a backup, restored after; same hash): with
  `bejeweled3_language` empty, "[language] ... the system language (fr) gives fr", and "APPUYEZ SUR START".
- **The presentation interval** (`kDeviceIntervalOffset` 13984): the game asks for 1 (60 frames a second); the old
  N&B offset had read a counter.
- **deko3d display ramp, ruled out:**
  - the copy path (`dkCmdBufCopyBufferToImage` from the upload ring, like the overlay's textures)
  - Load against sample, and point against linear sampler: all black
  - The GLSL is right (`present_ps.deko3d.h`). Its dk header says `tex=1` while the shader samples two textures:
    the next thing to check is how the deko3d back end binds a host shader's handles.

## The display ramp on deko3d, found (2026-10-22)

- The table read nothing on deko3d because the texture handle was computed from a float (param1 = 1 + slot, converted
  in the shader). The frame's own handle, a uint read from the constant buffer as it is stored, was fine.
- Now param0 = -1 selects the table and param1 holds the slot's bits (`asuint`). Its slot, upload and table (log line
  "Display ramp table: slot 3, upload recorded") were right all along.
- Ryujinx `ramp_dbg3`, `final2`: the frames right, 58-60 game frames a second. The Switch's built-in settings no longer
  turn the table off.
- PC Vulkan with the new form (`lock_renut6` against `lock_xenos4`): mean mad 0.920, as before (0.924).
- What did not matter: a Load against a sample, point against linear sampler, the push constant's word (param0 against
  param1), the descriptor cache (plume invalidates it before the next draw after any descriptor write).

## `sub_825F9040`, found (2026-10-22)

- The store log of a checked call (`LogBefore`, `bejeweled3_hot_guest.cpp`) keeps each store's old bytes in a 32-byte
  entry (`UndoEntry::old[32]`). A `dcbz` zeroes 128 bytes (`BEJEWELED3_LOG_ZERO`), and the whole 128 were copied into
  the 32-byte field on the stack: the first checked call of the vector memset `sub_825F9040` returned to address 0.
- Only the logged form (the first-call check, verify mode) was affected, and only this function of the 35 uses `dcbz`.
- Fix: long stores are logged in 32-byte pieces. `sub_825F9040` is back in the list (the second hottest, 311 samples),
  replacing `sub_8255EF98`.
- **PC verify** (`verify3`): 37 natives all called (`sub_825F9040` 2.19 M times); 206.8 M checks, 0 differences.
- **Ryujinx verify** (`verify_emu2`): 59.8 M checks, 0 differences (`sub_825F9040` 2.87 M calls).
- **Ryujinx normal** (`final3`): plays (the saved game's level 2, the score rising), mean 53.9 game frames a second
  over the run including loading.

## Docked (2026-10-22)

- Ryujinx with `docked_mode` true (`Config.json` backed up and put back; same hash), `docked1`: a 1920x1080 swapchain,
  the game's 1280x720 render scaled to it, 50-57 game frames a second through the menus; the frames right.
- **Final soak** (Ryujinx `soak2`, the 04:35 NRO, 8 min 45 s of scripted play): mean 58.5 game frames a second over
  51 reports, no crash; Renut's heap 1,208 -> 1,295 MB, the level `soak1` settled at too.
- **All seven languages:**
  - Spanish ("Jugar / Ayuda y opciones / Clásico"), Italian ("Gioca / Guida e opzioni / Classico") and Portuguese
    ("Jogar / Ajuda e opções / Clássico") on the PC (`lang_es`, `lang_it`, `lang_pt`)
  - French and Japanese on the PC
  - German, and French from the system language, in Ryujinx

## Shaders that only appear in long play (2026-10-22)

- The long soak (`soak2`) and the docked run logged 12 shaders (8 pixel, 4 vertex) "not in the shader cache": the
  stored 71 plus the 5 compiled at start do not cover what later levels and special gems use. Renut captured them
  into the SD's `shader_capture`; until then those draws were skipped.
- They are in `out/renut_shader_dump` now (88), and the PC cache, the deko3d package (88 of 88 compiled, 70 vertex +
  18 pixel, 535 KB) and the NRO are rebuilt from it.
- Ryujinx `soak3` (the 04:56 NRO, 9 min of scripted play, the SD's capture folder emptied first): 0 shaders missing,
  nothing captured, no crash, mean 58.3 game frames a second over 51 reports; the shots right (menus, the board, gems
  matching, the score rising).
- Delivered: `out/dist/dev_20261022_bejeweled3/bejeweled3-nx/bejeweled3.nro`, SHA-256 `3eae9501...` (39,293,072
  bytes); ELF and map `out/nx/elf/bejeweled3_dev_88shaders_nolto_20261022_0456.*`.

## The level transition on all three renderers (2026-10-22)

- The 12 late shaders are the level change: "Level Complete", the board breaking up, the hyperspace warp and the zoom
  into the next level's backdrop. The fresh-save routes of the first comparison never finished a level, so it had not
  seen them. The SD's save after `soak3` (`out/pc/userdata_src/continue1`) continues a game whose board finishes
  level 1 at once: "960:START,1560:A,2160:A,2760:A" (Play, Classic, Continue) plays the whole transition.
- `tools/win/bj3_lockstep.ps1` now sets the seed (`-Seed`, default 20261022) and the frame clock itself, and
  `-SaveFrom` copies a user data folder into the run's.
- **The warp's colour follows game time**, not the seed alone: an unseeded run without the frame clock drew it blue on
  Renut and red on the emulated GPU, and Ryujinx without the frame clock drew it purple. With the seed and the frame
  clock all three draw the same red/orange tunnel.
- **PC, Renut against the emulated GPU** (`lock_renut_cont` / `lock_xenos_cont`, 900 frames from game frame 2700,
  `compare_raw.py --window 600 --step 3`): mean mad 2.97. The worst pairs (mad up to 68) are the same scenes at
  another phase: the exploding gems, the zoom bubble, the tunnel's gems.
- **Switch, deko3d in Ryujinx, against the emulated GPU** (`emu_warp2`: the same save, seed and frame clock, 600
  frames from game frame 2800 by `bejeweled3_video_from_frame`; `--first 50`): the frames pair by game frame
  (3196/3196), mean mad 3.90. The worst (mad 20-23) are tunnel frames whose rotating streaks sit a little apart; the
  picture is the same. No shader missing.

## Touch controls, the rest of the menus (2026-10-22)

- **Checked by touch alone on the PC** (`touchui2` to `touchui11`, scripted touches, the touch log on):
  - the main menu's entries, Badges and its "(B) Back", Help & Options' prompts
  - the mode screen's gems; the Quest relic screen was reached through them
  - the Settings sliders follow a drag (Music, `touchui10`)
- **The title screen** ("PRESS START") ignored taps. Its widget (vtable `0x8227F068`, built by `sub_829A3060`) is the
  title and the main menu in one, and it waits for a pad's START.
  - A tap on it now presses START on player 1 through a new synthetic pad: `input_script::Press`, the "Touch
    controls" device, merged into player 1 like the scripted pad.
  - The device is connected only while a press is due and for 6 frames after it. Otherwise player 1's state comes
    from the real pads alone, so a disconnected pad still shows as one.
  - START does nothing on the main menu (pad test `startmenu2`), so a tap beside its entries stays harmless.
- **The list dialogs** (Help & Options `0x8227D408`, the pause menu `0x82280050`; both with the MouseDown
  `sub_82A41AA0`):
  - Their entries are `Sexy::ButtonWidget`s (vtable `0x8206010C`, RTTI) parked at x = -1000. The dialog draws them
    and keeps them at +0x234 (up to five) and the highlighted one at +0x248, so taps on entries did nothing.
  - Writing +0x248 does not choose: A then opened the first entry (`touchui8`).
  - A tap on an entry's row (its +0x30 / +0x38 in the dialog) now presses DOWN or UP from the highlighted entry to
    it, then A, through the game's own navigation. The presses queue, each released before the next.
  - Checked: Settings from Help & Options (two DOWNs, A; `touchui9`); in the pause menu, Help & Options, then Resume
    Game (one UP, A; `touchui11`).
- **Not by touch:**
  - Settings' "Vibration Enabled" row: its dialog `0x8227FEC0` keeps no parked entries. Controller vibration is off
    in the port by default (`input_rumble`).
  - Opening the pause menu: the console's board has no menu button; the + button pauses.
- **Ryujinx** (`touchui_emu1`, the 06:19 NRO): the title tap started the game, and Help & Options -> Settings by taps
  (two DOWNs and A).
- **A whole session by touch on the PC** (`swipe6`, seed 20261022, fresh save): title, Play, Classic, How to Play's
  Continue, then the swipe down that matched before ("GOOD!", 730). The board's path is unchanged by the widget lookup
  now shared with the dialogs.
- **The mode screen's ring** (gems: vtable `0x8227B7A0`): the centred gem starts its mode on a tap; a side gem ignored
  taps (`touchui12`), and the pad turns the ring with LEFT / RIGHT. A tap on a gem whose middle is more than 100 of
  the game's 960 columns from the centre now presses RIGHT or LEFT once, towards it (`touchui13`: Classic ->
  Lightning -> Quest, then the centred crown opened Quest). On the Quest screen, a tap on a relic starts its quest
  (the game's own handling; "Gold Rush" from the second relic).
- **One more shader: Badges** (`ps_d9d6fad578f2e600`): captured when `touchui2` opened Badges. Without it the badges'
  icons were not drawn (empty slots in `touchui2` / `touchui3`). The PC build took it in (06:04; the dump is 89), and
  the deko3d package was made again (89 of 89; 70 vertex + 19 pixel, 538 KB). Ryujinx `touchui_emu2`: the badge
  icons drawn, nothing missing, nothing captured.
- **Ryujinx soak of the touch build** (`soak4`, the 06:19 NRO, pad script, 5.5 min): the score rising, mean 57.5 game
  frames a second over 30 reports, no shader missing, no crash.
- **Delivered:** `out/dist/dev_20261022_bejeweled3/bejeweled3-nx/bejeweled3.nro`, the 06:31 NRO, SHA-256 `0a3d82ca...`
  (39,301,264 bytes), the one tested in `touchui_emu2`. ELF and map are in
  `out/nx/elf/bejeweled3_dev_touchmenus_89shaders_nolto_20261022_0631.*`.
- **Every mode's board** (found 06:45 with Zen, `zen4`): the other modes' boards are subclasses of Classic's, with
  vtables Zen `0x82284C60`, Lightning `0x822831B0` and the Butterflies quest `0x8227A418`. The touch code knew only
  Classic's vtable, so on the other boards taps went to the game's mouse code as they were, with the cursor-gem
  double swap.
  - A board is now known by Board's MouseDown in vtable slot 53, `sub_82A1AE50`, which all of them keep.
  - Checked with seeded swipes that matched: Zen `zen5` (0 -> 748), Lightning `light2` (0 -> 1,125), Butterflies
    `quest2` (board path taken).
- **Zen's "Press RB for Zen Options" box** (the game's x 20..290, y 440..560 on Zen's board): a tap presses RB
  (`zen6`). A tap while a modal dialog is up finds no widget and does nothing (`zen5`, behind "Illegal Move").
- **Ryujinx** (`zen_emu1`, the 06:51 NRO, touch only apart from the two intro dialogs' A): three ring turns to Zen, the
  seeded swipe matched (cascades, 750), and the box opened Zen's options. No shader missing.
- **Final soaks:** `soak5` (the 06:31 NRO, pad script, 5.5 min): mean 57.4 game frames a second, nothing missing, no
  crash. The 06:51 NRO differs from it only in `bejeweled3_touch.cpp`.
- **Delivered:** the 06:51 NRO, SHA-256 `57b1f3b6...` (39,301,264 bytes), the one tested in `zen_emu1`. ELF and map are
  in `out/nx/elf/bejeweled3_dev_touchboards_nolto_20261022_0651.*`.
- **More scenes checked for missing shaders on the PC:**
  - menus by touch (`tour1`): How To Play, Controls, the scrolling Credits, Leaderboards' Xbox LIVE notice
  - Zen's intro and options (`zen3`), the Quest relic screen and Gold Rush (`touchui13`)
  - all drawn, none missing
- **Badges** (dialog vtable `0x82278F28`; its MouseDown is a no-op):
  - 20 badges in five columns: three rows, then the bottom row. The pad chooses one, with the index at +0x33C
    (row * 5 + column; 10 -> 11 on RIGHT, -> 16 on DOWN, `badges3`), and taps were ignored.
  - A tap on a badge now presses the pad's way to it. The badges' middles in the dialog (x 65 + 123 per column; y 57,
    157, 257, and 377 for the bottom row) were measured on a `badges3` shot.
  - Checked on the PC (`badges4`) and in Ryujinx (`badges_emu1`): Ice Breaker -> Bejeweler (top right) -> Elite:
    Heroes Welcome (bottom left), each named with how to earn it.
- **Delivered:** the 07:00 NRO, SHA-256 `6e98eed0...` (39,305,360 bytes), the one tested in `badges_emu1`. It differs
  from the 06:51 NRO only in the Badges branch of `bejeweled3_touch.cpp`. ELF and map are in
  `out/nx/elf/bejeweled3_dev_touchbadges_nolto_20261022_0700.*`.
- **Final checks:**
  - `soak6` (the delivered 07:00 NRO, pad script, 5.5 min): nothing missing or captured, no crash, the score rising,
    mean 55.7 game frames a second (PC builds ran beside it).
  - A rebuild from the final source differs from the delivered NRO only in the build stamp ("Release@20261022_0707")
    and the GNU build ID.
  - Settings' highlight lies outside its dialog's first 0x1600 bytes (`settings2`, `settings3`; probably the widget
    manager's focus), so its Vibration row stays pad-only.
- **Raw recordings:** `out/pc/captures` holds 6.7 GB (`lock_xenos4` / `lock_renut6`, `lock_*_cont`, `emu_warp1/2`).
  Their per-frame hashes are written (`.hash` next to each).

## The shaders beside the NRO (2026-10-22, the user's request)

- The user prefers Peggle's way: the game's shaders as a file beside the NRO, made from their copy, not built in.
- The NRO already read `deko3d_glsl.bin` beside it before its built-in package (peggle2-nx). The release layout
  comes from peggle1-nx's tools:
  - `tools/renut_release_shaders.py --glsl out/deko3d_shaders89/glsl_opt` writes `out/renut_switch_release/`: the
    shader table without its SPIR-V (17 KB) and `sd/deko3d_glsl.bin`, all 89 shaders. That file is byte for byte the
    package the tested NRO had built in.
  - The build takes `-Define "BEJEWELED3_SWITCH_LTO=OFF;BEJEWELED3_RENUT_RELEASE_SHADERS=ON"`, and the option stays
    in `out/nx`'s cache. Passing `BANJO_DEKO3D_GLSL_CPP` itself does nothing, because the cmake file sets it with
    FORCE.
  - The NRO is 38,203,536 bytes, 1.1 MB less.
- **Ryujinx:**
  - Without the file (`nofile1`), the NRO logs "no GLSL package ... no game shaders".
  - With it (`sdshaders1`): "GLSL package /switch/bejeweled3-nx/deko3d_glsl.bin: 89 shaders". From an empty shader
    cache, the 5.5-minute soak played through the level transition: nothing missing, no crash, mean 57.5 game frames
    a second.
- **For a web installer like Peggle's**, 71 of the 89 shaders are stored in `default.xex` and a page can make them.
  - The other 18 are not stored in `default.xex` or in `main.rsb` as bytes (out/default_image.bin and main.rsb
    searched for each one's microcode): the 5 of the boot, the 12 of the level change and Badges'.
  - The executable links D3DX's HLSL effect compiler (the "ID3DXEffectCompiler" texts), so the game compiles them
    while it runs, which a browser cannot do.
- **Delivered:** the 07:23 NRO (SHA-256 `0869a51b...`) and `deko3d_glsl.bin` (`2c8fd2b5...`, 551,088 bytes), both as
  tested in `sdshaders1`. ELF and map are in `out/nx/elf/bejeweled3_dev_sdshaders_nolto_20261022_0723.*`.

## A console crash in the Quest menu (2026-10-22, reported by the user)

- `rex_crash (8).log` from the user's console, "navigating menus", the 07:23 NRO:
  - a memory access fault in `sub_8296CA98`, symbolised with `out/nx/elf/..._sdshaders_..._0723.elf`
  - on the game's thread, from the widget manager's draw (`sub_823FBC48`)
  - `ldr s29, [x0, #0x148]` on guest 0x01000147
- `sub_8296CA98` is slot 27 of vtable `0x82281328`: the Quest menu's Draw. Its constructor `sub_82973350` loads
  "IMAGE_QUESTMENU_PROGRESS_%d_%d"; its destructor is `sub_82969228`.
  - The Draw reads a pointer at +1692 (set at the end of the constructor from +1660 or +1628), checks it against 0 only,
    and loads a float at +328 from it.
  - The pointer was 0x00FFFFFF, a colour value. Most likely the menu was drawn after its memory was freed and used
    again, or the field was overwritten.
- Not reproduced:
  - PC `questnav1`, pad: into Quest and out, again, the Butterflies quest started and left through the pause menu,
    back to the main menu
  - Ryujinx `questnav_emu1`, the same mostly by touch
- At the user's request, the Switch build logs again (`app/bejeweled3_switch_defaults.toml`: `log_level = "info"`,
  `log_to_file = true`, `bejeweled3_touch_log = true`).
  - Checked from a first start without a toml (`firststart_log`).
  - Delivered NRO 08:09; ELF and map are in `out/nx/elf/bejeweled3_dev_logs_nolto_20261022_0809.*`.
- **The user's logs** (08:15-08:20, the 08:09 NRO; `bejeweled3_001..004.log`, `rex_crash (9).log`):
  - The crash recurred in the Quest menu's Draw, with `+1692` = 0x0000000F (guest 0x00000157).
  - In the crashed run (`_004`): Zen twice (an achievement unlocked); out to the mode screen; a ring turn; the centre
    gem into Quest (08:20:31.5); a tap on the Quest menu's "(B) Back" prompt (vtable `0x82281488`, 08:20:35.1). The
    log ends there, 3.6 s after entering Quest.
  - **A touch bug, console only:** every release reached the game at (-160, 0), the screen's corner.
    - `PollSwitch` pushed the release as (-1, -1) for "the last position", and `Push()` clamps to 0..1.
    - The game's TouchEnded gives the release to the widget the touch began on (manager +136), at local coordinates
      far outside it. Its buttons fire on MouseUp only while the mouse is over them (+85), so taps still worked.
    - Scripted touches carry their own release point, so no test showed it.
    - Fixed: the release now has the finger's last position.
  - **What sets `+1692`:** the constructor, from one of two picture arrays (+1628, +1660), and `sub_82968C40`
    (`this[1660 + 4 * index]`).
    - Update calls `sub_82968C40` with `+320` (-1 from the constructor; nothing else in the Quest menu's code writes
      it) when a countdown at `+1456` runs out.
    - `sub_82968B38` starts that countdown (280 frames), called from `sub_8297B480` when the menu opens with an
      animation, depending on the profile.
    - What wrote 0x00FFFFFF / 0x0F is not proven. The menu object was whole: its vtable still called the Draw.
  - **Not reproduced:** PC `questback1/2` (the Back tap, the release at its place and at the corner), `zenquest1`
    (the pad script went astray), Ryujinx `questnav_emu1`; nor a replay of the user's touches (`replay1`: random
    boards made the replay drift).
  - **Guard** (`src/bejeweled3_quest_menu.cpp`, a hook on the Draw `sub_8296CA98`, which is only called through the
    function table):
    - Before each Draw, `+1692` must be a picture: aligned, mapped, and its first word a vtable in the image.
    - Otherwise it gets the constructor's picture (+1628, else +1660, else 0, which the Draw skips), and the log
      says "[quest] ..." with the relic, index and countdown.
    - Silent on a healthy Quest menu (PC `questguard1`, Ryujinx `questguard_emu1` with the Back tap).
  - Run `_001` ended 2 s in, where the renderer starts (the D3D device and the render thread are next in the other
    runs). Its file's tail holds stale profile-report text, what a system crash leaves in a file being written.
    That fits the user's "crashed Atmosphere, no crash log". Atmosphere's own report is needed
    (`sdmc:/atmosphere/fatal_reports`, `crash_reports`).
  - Delivered: the 08:42 NRO; ELF and map are in `out/nx/elf/bejeweled3_dev_questguard_nolto_20261022_0842.*`.

## Lightning's frame drops: the main thread's core (2026-10-22, the user's console)

- **The user's log** (`bejeweled3_001 (1).log`, Lightning, 43-49 game frames a second at 08:55): the game's main
  thread is the limit. Its "input -> swap" time was 14 ms a frame at 60 and 22 ms in the slow stretch. Renut's render
  thread (2.6-3.8 ms a frame) and the GPU (2.7-3.4 ms, 15 % in Status Monitor at 460.8 MHz) have room.
- In the slow windows the game does about twice the work a frame: `sub_82453938` 8,770 -> 19,645 calls a frame;
  memcpy 2.6x; `sub_8243ACF8`, `sub_822DFF80`, `sub_82528740` (MTRand) about 2x. That is the special gems' and bonus
  effects' float-heavy code.
- **Threads:** every guest thread ran at 0x3B on any core, so the game's helper threads (10 % and 5 % of a core in
  Ryujinx `cores1`) could share the main thread's core in Horizon's 10 ms slices. Renut's record worker (0x2D, above
  the game) was pinned to core 0.
  - sa2-nx's layout (`sa2_renut_core_layout`) stays off: it re-masks running threads and hung Peggle and Banjo on the
    console.
- **New: the main thread's core of its own** (`bejeweled3_main_core`, CPU group, on: 0):
  - `RexSetGuestThreadCoresAtStart` (sdk/src/system/xthread.cpp) is called in `OnPostSetup`, before the module is
    launched. It only stores the layout.
  - Each guest thread applies it to itself as it starts: the main thread core 0 (mask 0x1), the others cores 1-2
    preferring 2 (mask 0x6, sa2-nx's creation-time path). No thread is moved while it runs.
  - The record worker is now on core 2.
- **Ryujinx `cores1`** (profile report on): "Main XThread" preferred core 0, mask 0x1; the game's other threads mask
  0x6. 60 game frames a second in play, nothing missing, no crash.
- **Still on core 0 above the game:** the audio output pump (0x2B, 187.5 Hz, 1.6 %: created on the process's default
  core) and the UI thread (0.3 %).
- **Still floating** (mask 0x7): the TimerQueue (2.9 %, the game's priority), the Audio Worker (2.6 %, 0x2B), the XMA
  decoder, the log writer. Left as they are; they are created before the layout is known.
- **Delivered:** the 09:05 NRO. ELF and map are in `out/nx/elf/bejeweled3_dev_maincore_nolto_20261022_0905.*`. The
  console's measure is the user's profile.

## No SaltyNX; the Status Monitor crash; the console profile (2026-10-22)

- **The Atmosphere hang** (`01792623994_420000000007e51a.log`, the user's console, 09:06):
  - The process is `ovlloader` (420000000007E51A) with the module Status-Monitor-Overlay: an Instruction Abort at
    0x0dd40dba60, outside the module, through a function pointer taken at Status-Monitor-Overlay + 0x29d74 (x23).
    Status Monitor's own code jumped into data.
  - The game's log (`bejeweled3_002.log`) stops at 09:06:32 mid-play.
- **SaltyNX removed** (the user: "get rid of our salty nx integration for this game please entirely because no
  benefit"):
  - `include/rex/ui/switch_saltynx.h` gives every function its no-op form (`REX_SWITCH_SALTYNX 0`), and
    `src/ui/switch_saltynx.cpp` is not built.
  - No FPS or resolution published, no shared memory mapped, no SaltySD port polled, no Reverse-NX mode read: the
    console's real mode decides.
  - Ryujinx `nosalty1`: 0 `ConnectToNamedPort` calls (before, one a second), play as before.
  - Delivered: the 09:12 NRO; ELF and map are in `out/nx/elf/bejeweled3_dev_nosaltynx_nolto_20261022_0912.*`.
- **The console profile of Lightning** (`rex_profile.log`, the 08:42 NRO, stacks on; reports 15 on, 36-46 game frames
  a second):
  - "Main XThread" was at 101.8 % of a core, 1 % waiting in the kernel.
  - Its self time (`tools/switch_hot_functions.py` with the 08:42 map):
    - **`sub_824D8FF8` 24.0 %**: not among the 35. It is 770 integer instructions: it sorts three points by y, works
      in 16.16 fixed point with 64-bit divides, like a triangle's span fill.
    - then the natives `n_8245AAD8` 5.2 %, `n_82453938` 4.6 %, `sub_8251B938` 3.5 %, `n_822DFF80` 2.7 %,
      `sub_825652B0` 2.3 %, `sub_82454730` 2.0 %.
- **What `sub_824D8FF8` is:**
  - It is one of about 30 variants of PopCap's software triangle rasterizer (0x824C6C10..0x824E6C70). Their table at
    0x82B4FF10 is filled at start by `sub_82512C58`.
  - The dispatcher `sub_825126C0` picks a variant by flags and pixel format (555 / 565 / 888). It is called by
    `sub_825127C0`, from `sub_8251DE10` / `sub_8251E158`.
  - Those are DeviceImage's (and MemoryImage's) vtable slots 16 and 17 (RTTI): drawing textured triangles into an
    image.
  - DeviceImage slot 16 (`sub_8247E960`) has no GPU path: it locks the image's pixels (slot 33), rasterizes on the
    CPU, unlocks (slot 34) and marks the image for upload (`sub_82475330`). The game itself draws some effect into
    an image on the CPU, cheap on the 360's 3.2 GHz cores and expensive here.
  - On the PC, 90 s of random Lightning play called it fewer than 20,000 times (a temporary hook, removed): the
    effect comes with what the user's play produced (special gems, the bonus rounds), not with every match.
- **Diagnostic build** (09:29 NRO, delivered with the profiler toml):
  - Hooks on DeviceImage / MemoryImage's triangle methods (`sub_8247E960`, `sub_8247EAD0`, `sub_8251E068`,
    `sub_8251E408`, all reached through vtables) count each call by its game caller and image.
  - Every 10 s it logs "[swtri]" with the image's +104 / +108 words.
  - Silent in random pad play on the PC and in Ryujinx (no call). The user: the effect comes with matching 4 or 5
    in Lightning; they test on the console.

## Optimising the hot functions; the 1.0.0 release (2026-10-22)

- **The method** is docs/optimising-functions.md (and docs/hand-natives.md for the hand-written natives). In order:
  - 35 more localized copies (batch 2);
  - four hand-written natives (`sub_82453938` the curve cache, `sub_822DFF80` the 3x3 product, `sub_824D8FF8` the
    software rasterizer by `tools/hand_native_824D8FF8.py`, `sub_8255EF98` a setter);
  - write-backs of only the changed registers in every copy (`tools/hand_native_dirty_sync.py`, run by
    `localize_native.py`);
  - 313 more copies from the console profiles (`app/src/cpu/hot/localized_bulk.txt`, 8 shard TUs);
  - leaf callees inlined in the copies (`inline_callees`, `app/src/cpu/hot/inline_hot.txt`);
  - direct hook dispatch in play builds (`bejeweled3_cpu_hook_counters`, off by default).
- **The console, heavy Lightning, 1020 MHz:** 25.4 ms a frame (10:41 NRO), 20.5 ms (11:03), 15.4 ms (11:24, a lighter
  scene).
- **Verify mode with 388 natives** does not leave the loading screen in 150 s on the PC (every leaf copy's every call
  is checked during loading); the same build plays normally without it. One DIFFERENCE was a race (`sub_828AFB28`'s
  f0, an audio field another thread wrote between the native's run and the original's).
- **One console start of the 10:21 NRO hung** before the PopCap logo (the intro video). It was not reproduced, and
  the hand-written natives had not run yet. The profiler now keeps the previous run's report
  (`logs/rex/rex_profile_prev.log`).
- **The release, 1.0.0:**
  - PGO: the instrumented build (`-Pgo generate`, LTO; built in `outnx`, the use build must be there too) on the
    user's console; one session, `out/pgo_streams/console_20261022/bejeweled3_pgo_1.stream`, merged into 159 `.gcda`
    in `pgo/`. Then `-Pgo use`, LTO.
  - Logging off by default (`app/bejeweled3_switch_defaults.toml`), the "[swtri]" hooks removed, the icon is the user's
    picture (`out/icon.jpg`, outside the repository).
  - Shaders, by the user's choice: the 18 the game compiles at run time (not in any of its files) are built into the
    NRO (`tools/renut_release_shaders.py --image out/default_image.bin`: every translated shader the image does not
    store); the installer page makes the 71 stored ones (its chain: 7 byte for byte, 64 with other SPIR-V temporary
    numbers than the native chain).
  - `tools/check_switch_build.py`: Bejeweled 3's own halt (`sub_8254E850`, Xbox360AppDriver::DoExit) and streaming
    copies (`sub_825FA9B0`, `sub_825FA154`) replace Peggle 1's.
  - The installer page: `D:\peggle 2\bejeweled3-nx-installer`, from peggle1-nx-installer without the title update and
    DLC steps.

## 1.0.1: a sideways Joy-Con's stick; the cursor hidden while touching (2026-10-26)

- **A single Joy-Con held sideways** moved the cursor a quarter turn off (pushing right moved it up): the input driver
  was peggle2-nx 1.0.1's line for line. peggle2-nx 1.0.2's fix (`ec1a138`, from guwange-nx; peggle1-nx 1.0.2 has it
  too): `Poll` turns the stick a quarter, the left Joy-Con's to `{-y, x}` and the right one's to `{y, -x}`; new key
  `input_joycon_sideways_stick` (on by default, built in).
- **The board's cursor while touching** (the user: the bracket "disappears when touching the screen and comes back
  when using stick and button controls"; `bejeweled3_touch_hides_cursor`, on by default):
  - The game fades its cursor itself. `Board::Update` (`sub_82A1FFD0`) fades it out while the board's virtual
    WantsCursor (vtable slot 101, +404) returns false and in when it returns true. The board's draw (`sub_82A26FE8`)
    draws the bracket at the cursor's column and row (+11688 / +11692; -1 = none) only while that fade (+11544) is
    above 0, in one of two images by whether the cursor's gem is the selected one (`sub_82A0FBB8`).
  - Slot 101 holds `sub_82A0EA38` in Classic's and Zen's boards (and two more board classes), and `sub_82976700`,
    `sub_829F8FF0` or `sub_8297EB68` in the other 16 board classes (every vtable whose MouseDown is `sub_82A1AE50`):
    each is slot 100 ("the board takes input", `sub_82A13050` for Board) with conditions of its own. Board::Update is
    their only caller (virtual calls only; its hot copy calls through `REX_CALL_INDIRECT_FUNC` too).
  - The cursor's gem flag (+604) is set by `sub_82A13F78` from slot 100, not 101, and no drawing reads it (its other
    reader, `sub_82A2EA48`, writes the save): hiding changes the bracket only.
  - The port: a touch (any, on any screen) makes the four return false until a pad is used. `InputSystem` got a state
    observer (`SetStateObserver`, called from `GetState` with each device's state as the game reads it); the input
    script module counts presses from it (`input_script::PadPresses`: a button going down, a stick or trigger leaving
    its rest; the touch controls' own pad excluded), so a pad held still or a drifting stick cannot bring it back.
- **Checked** (seed 20261022; the touch route to Classic, then pad RIGHT at frame 3300, a swipe that matches at 3700,
  pad LEFT at 4300; `out/renut_tests/<run>/cursor_events.png` shows the board before and after each):
  - PC `cursor2`: hidden after the menu taps, back after RIGHT, hidden after the swipe, back after LEFT (moved one
    gem). The control `cursor2off` (`bejeweled3_touch_hides_cursor = false`): the bracket stays throughout; the same
    boards, swap and score. `cursor1` (a single tap, then LEFT): the tap had selected a gem, so LEFT swapped it ("Illegal
    Move"), the game's own select-then-direction rule, with or without this change.
  - Ryujinx `cursor_emu3`, the 1.0.1 release NRO: the same four steps ("GOOD!" on the swipe). The frame-timed route
    needs about 165 s there.
- 1.0.1 build: `outnx` (LTO + PGO from the 1.0.0 profile; the changed functions have none), `check_switch_build.py`
  all checks passed, the four hooks strong symbols in the ELF. NRO SHA-256 `6d1ae5bc...584b67`, 44,756,063 bytes;
  ELF and map `out/nx/elf/bejeweled3_1.0.1_lto_pgouse_20261026_1207.*`; delivered as
  `out/dist/release_1.0.1/bejeweled3-nx/` (NRO and LICENSES.txt: replace the NRO of an installed 1.0.0).
