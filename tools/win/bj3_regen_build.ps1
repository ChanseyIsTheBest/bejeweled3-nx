# Regenerates the game code with the post-codegen passes and builds a tree (default out\pc), in that order.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\bj3_regen_build.ps1 [-BuildDir out\pc] [-Jobs 14]
#       [-NoBuild]
#
# The build runs the generator itself (generated/rexglue.cmake: when an input of codegen.d or one of the generated
# .cpp files is newer) and then the passes (BEJEWELED3_PERF_POST: tools/post_codegen.py). The precompiled header is no
# output of that step, so a full build compiled it while the passes still rewrote it (2026-10-21: "has been modified
# since the precompiled header was built"). Here the codegen target is built on its own first, after the manifest is
# touched so that it runs; then tools\regen_restat.py moves the generated sources the passes left alone (they keep
# their old times) past the inputs, so the full build does not run the generator again; then the rest.
param(
    [string]$BuildDir = 'out\pc',
    [int]$Jobs = 14,
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$sw = [Diagnostics.Stopwatch]::StartNew()
(Get-Item (Join-Path $root 'app\bejeweled3_manifest.toml')).LastWriteTime = Get-Date
powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\run.ps1 "$BuildDir`_codegen.log" "cmake --build $BuildDir --target bejeweled3_codegen" |
    Select-Object -Last 3
python -P tools\regen_restat.py
"codegen + passes: $([int]$sw.Elapsed.TotalSeconds) s"
if ($NoBuild) { return }
powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\run.ps1 "$BuildDir`_build.log" "cmake --build $BuildDir -j $Jobs" |
    Select-Object -Last 3
"total: $([int]$sw.Elapsed.TotalSeconds) s"
