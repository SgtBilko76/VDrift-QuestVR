# Push the staged game data to the headset (/sdcard/VDriftVR).
# Run tools/stage_data.py first.
param(
    [switch]$Full,     # re-push everything (default: only if the data dir is missing)
    [switch]$Reset     # also delete the on-device user settings (.vdrift)
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root "stage\VDriftVR"
if (-not (Test-Path $stage)) { throw "stage dir missing: run python tools/stage_data.py" }

$hasData = (adb shell "test -d /sdcard/VDriftVR/data && echo yes") -match "yes"
if ($Full -or -not $hasData) {
    Write-Host "Pushing the full data set..."
    adb shell mkdir -p /sdcard/VDriftVR
    adb push --sync "$stage\." /sdcard/VDriftVR/
} else {
    Write-Host "Refreshing settings, shaders and templates only (use -Full for everything)"
    adb push --sync "$stage\data\settings" /sdcard/VDriftVR/data/
    adb push --sync "$stage\data\shaders" /sdcard/VDriftVR/data/
    adb push --sync "$stage\templates" /sdcard/VDriftVR/
    adb push "$stage\vr.cfg" /sdcard/VDriftVR/vr.cfg
}
if ($Reset) {
    Write-Host "Removing /sdcard/VDriftVR/.vdrift"
    adb shell rm -rf /sdcard/VDriftVR/.vdrift
}
adb shell "du -sh /sdcard/VDriftVR"
