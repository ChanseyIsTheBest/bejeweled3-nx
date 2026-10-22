# Building bejeweled3-nx

Everything below runs on Windows. You need your own copy of the Xbox Live Arcade game; nothing made from it is
committed (`assets/`, `app/generated/` and `out/` are git-ignored).

## Tools

- VS 2022 Build Tools (MSVC libraries + Windows SDK), LLVM clang 20 (portable is fine; by default `..\_tools\llvm`),
  Python 3 with `xxhash` and Pillow, Git. CMake and Ninja come from the VS Build Tools. Paths can be overridden, see
  `tools/win/env.ps1`. Configure and build through `tools\win\run.ps1` (it loads that environment): from a plain
  Git Bash a re-configure finds the wrong compiler and Ninja.
- For the Switch: devkitPro (devkitA64, libnx, deko3d), and uam and SPIRV-Cross built for the PC
  (`bash tools/deko3d_build_tools.sh`, into `out/deko3d_tools`). No Vulkan driver is needed: the NRO draws with deko3d.

## 1. The game files

```powershell
# The game (verifies the STFS hash tables, writes nothing to the package)
python tools\stfs_extract.py "<...>\584111D6\000D0000\B6CB3450D57A979840BC9E33BE0BB1CA9FC3AE1958" assets\game_root
```

## 2. The code generator and the PC build

```powershell
# Third-party sources of the SDK (only the files this fork changed are kept in sdk/thirdparty)
python tools\fetch_thirdparty.py

# The code generator (-march=x86-64-v2 is required: the SDK uses SSSE3 intrinsics)
powershell -File tools\win\run.ps1 out\host_configure.log "cmake -S sdk -B out/host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2"
powershell -File tools\win\run.ps1 out\host_build.log "cmake --build out/host --target rexglue"

# Translate the game to C++ with the optimised code generation and its post steps (tools/post_codegen.py; it writes
# the decrypted executable to out\default_image.bin, which step 3 reads)
powershell -File tools\codegen.ps1 -PostPasses

# The PC build (Renut's renderer on Vulkan, or the emulated GPU for the reference)
python tools\renut_shader_dump.py --image out\default_image.bin --out out\renut_shader_dump
powershell -File tools\win\run.ps1 out\pc_configure.log "cmake --preset win-pc -S app -DBEJEWELED3_RENUT_RENDERER=ON -DBEJEWELED3_PERF_POST=ON"
powershell -File tools\win\run.ps1 out\pc_build.log "cmake --build out/pc"
```

After a change of the code generation's inputs (overrides, hooks, the hot list), regenerate and build with
`tools\win\bj3_regen_build.ps1`: it builds the codegen target first and keeps the build from running the generator a
second time (`tools/regen_restat.py`). A new hook needs the post-codegen passes again: they make direct calls to
functions no hook was known for.

Runs of the PC build (muted and minimized by default; log and shots in `out\renut_tests\<name>`):

```powershell
powershell -File tools\win\bj3_run.ps1 -Name boot1 -Seconds 60
powershell -File tools\win\bj3_run.ps1 -Name menus -InputScript "840:START,1200:A" -Env "REX_BEJEWELED3_RENDERER=renut;REX_RENUT_GPU_BACKEND=vulkan"
```

- `-InputScript` drives a scripted pad (`app/src/bejeweled3_input_script.h`; `tools/random_play_script.py` writes one
  that plays a board at random after a menu prefix); `REX_BEJEWELED3_TOUCH_SCRIPT` ("frame:down|move|up:x:y" in
  1280x720 screen pixels) scripted touches, and `REX_BEJEWELED3_TOUCH_LOG=true` logs the widget each touch lands on.
- `REX_BEJEWELED3_CPU_VERIFY=true` checks every call of the native functions against the recompiled code.
- `REX_BEJEWELED3_TEST_SEED=<n>` gives every run the same boards.
- `tools\win\bj3_lockstep.ps1` records a run raw, with the test seed and the frame clock (`-SaveFrom` starts it from a
  save, e.g. one that continues a game into the level transition); `tools/compare_raw.py` compares two recordings
  (the emulated GPU against Renut); `tools/raw_sheet.py` and `tools/raw_pair.py` show them.

## 3. The shaders

The renderer's shader cache comes from the game's 71 stored containers and the 5 it compiles at run time (the PC
build captures the ones its cache lacks into `out\pc\shader_capture`, and the next build adds them). For the Switch:

```powershell
$env:BANJO_HLSL_OVERRIDE = "$PWD\out\pc\renut_gpu\early_alpha_hlsl"
$env:BANJO_DEKO3D_SPIRV_DIR = "$PWD\out\deko3d_shaders\spirv"
out\pc\xenos_recomp\build\XenosRecomp\XenosRecomp.exe out\renut_shader_dump out\deko3d_shaders\scratch_cache.cpp app\thirdparty\reblue-XenosRecomp\XenosRecomp\shader_common.h
Remove-Item env:BANJO_HLSL_OVERRIDE, env:BANJO_DEKO3D_SPIRV_DIR
python tools\deko3d_shaders.py --spirv out\deko3d_shaders\spirv --out out\deko3d_shaders
python tools\deko3d_glsl_opt.py --in out\deko3d_shaders\glsl --out out\deko3d_shaders\glsl_opt --check
python tools\deko3d_shader_package.py --glsl out\deko3d_shaders\glsl_opt --out out\renut_switch
python tools\renut_switch_shader_cache.py
```

## 4. The NRO

The game's shaders go beside the NRO, as in the Peggle ports, not into it: `tools/renut_release_shaders.py` writes the
shader table without its SPIR-V and `sd/deko3d_glsl.bin` (every translated shader) into `out\renut_switch_release`,
and `BEJEWELED3_RENUT_RELEASE_SHADERS=ON` builds from there (the NRO then has no game shaders of its own).

```powershell
python -P tools\renut_release_shaders.py --glsl out\deko3d_shaders\glsl_opt
powershell -File tools\win\build_switch.ps1 -Jobs 12 -BuildDir out\nx -Log out\nx_build.log -Pgo none -Define "BEJEWELED3_SWITCH_LTO=OFF;BEJEWELED3_RENUT_RELEASE_SHADERS=ON"
```

The Switch build's settings are compiled in: `app/bejeweled3_switch_defaults.toml` (Renut on deko3d, no log, 720p) is
applied before the SD card's `bejeweled3.toml`, and `app/bejeweled3_switch.toml` (language, controllers) is written as
`bejeweled3.toml` at the first start when there is none. A test run turns the log on in its `bejeweled3.toml`
(`log_level = "info"`, `log_to_file = true`).

## 5. The SD folder

```powershell
python tools\build_asset_pack.py assets\game_root --out-dir out\pack --verify
```

`sdmc:/switch/bejeweled3-nx/` takes `bejeweled3.nro`, `deko3d_glsl.bin` from `out\renut_switch_release\sd`, and
`game_root.nxpack` + `game_root.nxidx` from `out\pack`.

In Ryujinx, `tools/win/bj3_emu_run.sh <name> <seconds> [key = value ...]` runs the NRO from `out/emu_sd/bejeweled3-nx`
(see the script for the data folder it expects) and collects its log and shots. The Switch profiler
(`switch_profile_report = true`, `switch_profile_stacks = true`) writes `rex_profile.log`; in Ryujinx, which gives the
NRO no path of its own, that is `sdmc:/switch/rex_profile.log`. `tools/switch_hot_functions.py` with
`out/nx/bejeweled3.map` lists the hottest functions.
