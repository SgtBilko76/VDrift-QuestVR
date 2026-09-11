# Build (optional), install, launch VDrift VR and stream its logcat.
param(
    [switch]$Build,
    [switch]$Release,
    [switch]$NoLog
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$variant = if ($Release) { "release" } else { "debug" }
if ($Build) {
    Push-Location (Join-Path $root "android")
    try { & .\gradlew ("assemble" + $variant.Substring(0,1).ToUpper() + $variant.Substring(1)) -q } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "gradle build failed" }
}
$apk = Join-Path $root "android\app\build\outputs\apk\$variant\app-$variant.apk"
adb install -r $apk
adb shell appops set com.vdriftvr MANAGE_EXTERNAL_STORAGE allow
adb logcat -c
adb shell am start -n com.vdriftvr/.VDriftVRActivity
if (-not $NoLog) {
    adb logcat -s VDriftVR:V TBXR:V AndroidRuntime:E DEBUG:E
}
