# make_holo_gif.ps1 - looping 3D GIFs from a RUNNING 3DS game in multiview mode
# (LKG build, or a flat build launched with -holomultiview). Works on any 3DS game.
# See docs/automation_workflow.md ("3DS multiview GIF maker").
#
# The frames are the core's own per-view renders at native 3DS resolution (400x240 per
# view), pulled with the holo_quilt_request.txt debug dump, so no window capture, no
# focus, and no status text in the frames.
#
#   sweep    one quilt -> seamless left-right-left view sweep (parallax wigglegram)
#   cutaway  N quilts while holo.Cutaway ramps 0 -> MaxCut -> 0 in per-frame increments
#            (smoothstep + hold, no visible steps), view sweep running on top
#   buildup  starts fully cut away, BUILDS the scene to cut 0 (decelerating - the near
#            content lands last and is the interesting part; camera static), then the
#            head-move sweep, a still pause, and a ~1s teardown back to full cut so the
#            loop wraps seamlessly
#
# Examples (game already running and unpaused on the scene to shoot):
#   tools\make_holo_gif.ps1 -Mode sweep   -Out Media\metroid_title_sweep.gif
#   tools\make_holo_gif.ps1 -Mode cutaway -Out Media\metroid_title_cutaway.gif -Seconds 8
#   tools\make_holo_gif.ps1 -Mode buildup -Out Media\metroid_ruins_buildup.gif -MaxCut 0.95
param(
    [ValidateSet("sweep", "cutaway", "buildup")]
    [string]$Mode = "sweep",
    [Parameter(Mandatory = $true)]
    [string]$Out,
    # Harness dir of the running instance (staged LKG build: C:\Users\<user>\AppData\Local\HoloVCS\Saved)
    [string]$SavedDir = "f:\Unreal\HoloVCS_UE56\Saved",
    # The game PROCESS's working directory, where holo_quilt_request.txt is polled.
    # Editor-binary runs: the ENGINE Binaries\Win64 dir. Staged exe runs: the exe's dir.
    [string]$DumpDir = "F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64",
    [double]$Fps = 15,
    [double]$Seconds = 6,
    [int]$Scale = 1,          # nearest-neighbor upscale of the 400x240 views (1 = native 3DS res)
    [int]$Colors = 256,
    [double]$Cycles = 0,      # view-sweep sine cycles per loop; 0 = default (sweep 1, cutaway 2, buildup 1)
    [double]$MaxCut = 1.0,    # cutaway/buildup: deepest plane reached
    [double]$HoldFrac = 0.2,  # cutaway: fraction of the loop held at full cut
    # buildup phase lengths (seconds) and pacing
    [double]$BuildSeconds = 4.5,
    [double]$SweepSeconds = 3,
    [double]$PauseSeconds = 2,
    [double]$TeardownSeconds = 1,
    [double]$BuildPower = 3,  # cut = MaxCut*(1-t)^p: higher = more of the build spent near 0
    [switch]$Keep             # keep the intermediate bmp frames next to the gif
)

$ErrorActionPreference = "Stop"
$holoAuto = Join-Path $PSScriptRoot "holo_auto.ps1"
$holoGifPy = Join-Path $PSScriptRoot "holo_gif.py"
if ($Cycles -le 0) { $Cycles = if ($Mode -eq "cutaway") { 2 } else { 1 } }
$script:frameIndex = 0

function Send-Harness([string[]]$commands) {
    $result = & $holoAuto -Cmd $commands -SavedDir $SavedDir
    if ($result -match "ERROR") { throw "harness: $result" }
}

# Drop holo_quilt_request.txt and wait for the last packed quilt of the burst (the core
# writes 3 raw + 3 packed per request; the LAST packed is the most settled after a live
# holo.Cutaway change). Returns its path; leaves the other five deleted.
function Capture-Quilt {
    Remove-Item (Join-Path $DumpDir "holo_quilt*.bmp") -Force -ErrorAction SilentlyContinue
    Set-Content -Path (Join-Path $DumpDir "holo_quilt_request.txt") -Value "" -Encoding ascii
    $target = Join-Path $DumpDir "holo_quiltpk_02.bmp"
    $deadline = (Get-Date).AddSeconds(15)
    while (-not (Test-Path $target)) {
        if ((Get-Date) -gt $deadline) {
            throw "timed out waiting for $target - is the game running, UNPAUSED, and in 3DS multiview mode (LKG build or -holomultiview)? Is -DumpDir the game process's working directory?"
        }
        Start-Sleep -Milliseconds 40
    }
    # the write is synchronous but cheap insurance: wait for the size to hold still
    do {
        $size = (Get-Item $target).Length
        Start-Sleep -Milliseconds 60
    } while ((Get-Item $target).Length -ne $size)
    return $target
}

# the core can still hold the bmp open for a moment after the size settles
function Move-WithRetry([string]$from, [string]$to) {
    $deadline = (Get-Date).AddSeconds(5)
    while ($true) {
        try { Move-Item $from $to -Force; return }
        catch {
            if ((Get-Date) -gt $deadline) { throw }
            Start-Sleep -Milliseconds 120
        }
    }
}

# set the cutaway, capture one quilt, and file it as the next sequential frame
function Capture-CutFrame([double]$cut, [string]$workDir, [string]$label) {
    Send-Harness @(("exec holo.Cutaway {0:0.####}" -f $cut))
    Start-Sleep -Milliseconds 80   # let the push reach the core before the dump frames
    $quilt = Capture-Quilt
    Move-WithRetry $quilt (Join-Path $workDir ("quilt_{0:0000}.bmp" -f $script:frameIndex))
    $script:frameIndex++
    Write-Output ("{0}  cutaway {1:0.000}" -f $label, $cut)
}

# smoothstep ramp up / hold / ramp down, 0 at both ends = seamless loop
function Get-CutValue([int]$k, [int]$total) {
    $t = $k / [double]$total
    $ramp = (1.0 - $HoldFrac) / 2.0
    if ($t -lt $ramp) { $x = $t / $ramp }
    elseif ($t -gt 1.0 - $ramp) { $x = (1.0 - $t) / $ramp }
    else { $x = 1.0 }
    return $MaxCut * ($x * $x * (3.0 - 2.0 * $x))
}

$workDir = Join-Path $SavedDir ("Automation\gif\" + [IO.Path]::GetFileNameWithoutExtension($Out))
New-Item -ItemType Directory -Force $workDir | Out-Null
Remove-Item (Join-Path $workDir "quilt_*.bmp") -Force -ErrorAction SilentlyContinue
$outDir = Split-Path -Parent $Out
if ($outDir) { New-Item -ItemType Directory -Force $outDir | Out-Null }

if ($Mode -eq "sweep") {
    $quilt = Capture-Quilt
    $frame = Join-Path $workDir "quilt_0000.bmp"
    Move-WithRetry $quilt $frame
    python $holoGifPy --quilt $frame --out $Out --fps $Fps --seconds $Seconds `
        --cycles $Cycles --scale $Scale --colors $Colors
    if (-not $Keep) { Remove-Item $frame -Force }
}
elseif ($Mode -eq "cutaway") {
    $total = [int][math]::Round($Fps * $Seconds)
    # prewarm: the first cutaway use lazily compiles shader variants; do that hitch now
    # so the early ramp frames don't capture uncut
    Send-Harness @(("exec holo.Cutaway {0:0.####}" -f ($MaxCut / 2)))
    Start-Sleep -Milliseconds 800
    Send-Harness @("exec holo.Cutaway 0")
    Start-Sleep -Milliseconds 300
    for ($k = 0; $k -lt $total; $k++) {
        Capture-CutFrame (Get-CutValue $k $total) $workDir ("frame {0}/{1}" -f ($k + 1), $total)
    }
    Send-Harness @("exec holo.Cutaway 0")
    python $holoGifPy --framedir $workDir --out $Out --fps $Fps `
        --cycles $Cycles --scale $Scale --colors $Colors
    if (-not $Keep) { Remove-Item (Join-Path $workDir "quilt_*.bmp") -Force }
}
else {
    # buildup: [static camera] full cut -> 0 decelerating, then (assembled from the final
    # cut-0 capture, no extra dumps) the head-move sweep and still pause, then a quick
    # live teardown back to full cut. First frame = last frame's cut = seamless loop.
    $nBuild = [int][math]::Round($Fps * $BuildSeconds)
    $nTear = [int][math]::Round($Fps * $TeardownSeconds)
    # prewarm at full cut (also the first frame's value, so nothing pops mid-capture)
    Send-Harness @(("exec holo.Cutaway {0:0.####}" -f $MaxCut))
    Start-Sleep -Milliseconds 1000
    for ($k = 0; $k -lt $nBuild; $k++) {
        $t = $k / [double]($nBuild - 1)
        $cut = $MaxCut * [math]::Pow(1.0 - $t, $BuildPower)
        Capture-CutFrame $cut $workDir ("build {0}/{1}" -f ($k + 1), $nBuild)
    }
    for ($k = 1; $k -le $nTear; $k++) {
        $x = $k / [double]$nTear
        $cut = $MaxCut * ($x * $x * (3.0 - 2.0 * $x))
        Capture-CutFrame $cut $workDir ("teardown {0}/{1}" -f $k, $nTear)
    }
    Send-Harness @("exec holo.Cutaway 0")
    python $holoGifPy --framedir $workDir --out $Out --fps $Fps `
        --cycles $Cycles --scale $Scale --colors $Colors --build-frames $nBuild `
        --sweep-seconds $SweepSeconds --pause-seconds $PauseSeconds
    if (-not $Keep) { Remove-Item (Join-Path $workDir "quilt_*.bmp") -Force }
}
Remove-Item (Join-Path $DumpDir "holo_quilt*.bmp") -Force -ErrorAction SilentlyContinue
