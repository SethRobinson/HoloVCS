# holo_auto.ps1 - control a running HoloVCS instance through the in-game automation harness (no window focus needed).
# The game watches <SavedDir>\Automation\commands.txt and appends results to <SavedDir>\Automation\ai_log.txt.
# See docs/automation_workflow.md for the command list.
#
# Examples:
#   tools\holo_auto.ps1 -Cmd "shot overworld" -WaitShot overworld
#   tools\holo_auto.ps1 -Cmd "press up 120"
#   tools\holo_auto.ps1 -Cmd "press start 8","dump"
#   tools\holo_auto.ps1 -Cmd "video 120 clip1"
#   tools\holo_auto.ps1 -Log
param(
    [string[]]$Cmd,
    [string]$SavedDir = "f:\Unreal\HoloVCS_UE56\Saved",
    [string]$WaitShot,           # name passed to a 'shot' command; waits for the png and prints its path
    [string]$WaitFile,           # or any explicit file path to wait for
    [int]$TimeoutSec = 10,
    [switch]$Log,                # print the tail of ai_log.txt afterward
    [string]$CropQuilt,          # path to a quilt png (..._qsCxRaA.AA.png); extracts ONE tile as <name>_tileN.png
    [int]$Tile = -1              # tile index for -CropQuilt, row-major from top-left; default = center view
)

$ai = Join-Path $SavedDir "Automation"

if ($CropQuilt) {
    # Pull a single view out of a quilt screenshot - much cheaper to feed to vision than all 48 tiles.
    if (-not (Test-Path $CropQuilt)) { Write-Output "ERROR: $CropQuilt not found"; exit 1 }
    if ($CropQuilt -notmatch '_qs(\d+)x(\d+)a') { Write-Output "ERROR: filename lacks _qsCxRa tiling suffix"; exit 1 }
    $cols = [int]$Matches[1]; $rows = [int]$Matches[2]
    if ($Tile -lt 0) { $Tile = [int](($cols * $rows) / 2) + [int]($cols / 2) }  # middle row, middle column = straight-on view
    Add-Type -AssemblyName System.Drawing
    $src = [System.Drawing.Bitmap]::FromFile($CropQuilt)
    $tw = [int]($src.Width / $cols); $th = [int]($src.Height / $rows)
    $col = $Tile % $cols; $row = [int][math]::Floor($Tile / $cols)
    $rect = New-Object System.Drawing.Rectangle ($col * $tw), ($row * $th), $tw, $th
    $dst = $src.Clone($rect, $src.PixelFormat)
    $outFile = $CropQuilt -replace '\.png$', ("_tile{0}.png" -f $Tile)
    $dst.Save($outFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $src.Dispose(); $dst.Dispose()
    Write-Output "tile ${Tile} (col $col row $row, ${tw}x${th}) -> $outFile"
    exit 0
}

if ($Cmd) {
    $cf = Join-Path $ai "commands.txt"
    $deadline = (Get-Date).AddSeconds(5)
    while ((Test-Path $cf) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
    if (Test-Path $cf) { Write-Output "ERROR: game is not consuming commands (is it running?)"; exit 1 }

    $tmp = Join-Path $ai ("cmd_" + [guid]::NewGuid().ToString("n") + ".tmp")
    Set-Content -Path $tmp -Value ($Cmd -join "`n") -Encoding ascii
    Move-Item -Path $tmp -Destination $cf -Force
    Write-Output ("sent: " + ($Cmd -join " | "))
}

if ($WaitShot) { $WaitFile = Join-Path $ai ("shots\" + $WaitShot + ".png") }

if ($WaitFile) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not (Test-Path $WaitFile) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if (Test-Path $WaitFile) {
        Start-Sleep -Milliseconds 200  # let the engine finish flushing the png
        Write-Output "file ready: $WaitFile"
    } else {
        Write-Output "TIMEOUT waiting for $WaitFile"
    }
}

if ($Log) {
    $lf = Join-Path $ai "ai_log.txt"
    if (Test-Path $lf) { Get-Content $lf -Tail 15 }
}
