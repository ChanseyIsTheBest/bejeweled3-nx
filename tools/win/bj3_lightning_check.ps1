# One deterministic Lightning run on the PC for the hand-written natives (docs/hand-natives.md): a fresh save, the test
# seed and the frame clock (as tools\win\bj3_lockstep.ps1), the Lightning pad script (out\lightning_lockstep_script.txt)
# and, with -Autoplay N, a board move every N frames (bejeweled3_autoplay). -Verify turns on verify mode (every call of
# a leaf native checked); -NativesOff runs the generated code alone (the baseline); -Record records game frames
# -From .. -From + -Frames and keeps only their hashes under the tag (out\pc\captures\light_<Tag>.hash / .frames /
# .txt; the raw recording stays video_f<From>.rgba, which the next recording overwrites).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\bj3_lightning_check.ps1 -Tag off1 -NativesOff -Record
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [switch]$Verify,
    [switch]$NativesOff,
    [switch]$Record,
    [int]$Autoplay = 0,
    [int]$AutoplayColor = -1,
    [int]$From = 3600,
    [int]$Frames = 1800,
    [int]$Seconds = 150,
    [uint32]$Seed = 20261022
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$script = (Get-Content (Join-Path $root 'out\lightning_lockstep_script.txt') -Raw).Trim()
$userdata = Join-Path $root "out\pc\test_userdata\light_$Tag"
if (Test-Path $userdata) { Remove-Item -Recurse -Force $userdata }
New-Item -ItemType Directory -Force $userdata | Out-Null
$vars = @{
    REX_USER_DATA_ROOT = $userdata; REX_BEJEWELED3_TEST_SEED = "$Seed"; REX_CLOCK_PER_SWAP_HZ = '60';
    REX_VBLANK_PER_SWAP = 'true'; REX_BEJEWELED3_RENDERER = 'native'; REX_BEJEWELED3_AUTOPLAY = "$Autoplay";
    REX_BEJEWELED3_AUTOPLAY_COLOR = "$AutoplayColor"
}
if ($Verify) { $vars.REX_BEJEWELED3_CPU_VERIFY = 'true' }
if ($NativesOff) { $vars.REX_BEJEWELED3_CPU_NATIVES = 'false' }
foreach ($k in $vars.Keys) { Set-Item "env:$k" $vars[$k] }
try {
    if ($Record) {
        powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 -VideoFromFrame $From -VideoFrames $Frames -GameFps 60 -InputScript $script -Tail 2 | Select-Object -First 2
    } else {
        powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\boot_test.ps1 -Seconds $Seconds -InputScript $script -Tail 2 | Select-Object -First 2
    }
} finally {
    foreach ($k in $vars.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
}
$log = (Get-ChildItem out\pc\logs\bejeweled3_*.log | Sort-Object LastWriteTime | Select-Object -Last 1).FullName
"LOG $log"
if ($Record) {
    $cap = Join-Path $root 'out\pc\captures'
    python tools\compare_hashes.py --hash (Join-Path $cap "video_f$From.rgba")
    foreach ($e in 'hash', 'frames', 'txt') { Copy-Item -Force (Join-Path $cap "video_f$From.$e") (Join-Path $cap "light_$Tag.$e") }
    "kept out\pc\captures\light_$Tag.hash / .frames / .txt"
}
