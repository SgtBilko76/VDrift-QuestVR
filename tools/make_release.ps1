# Build a self-contained release APK: the app with the staged game data inside it.
#
# The data is packed as assets/gamedata.zip and unpacked to /sdcard/VDriftVR on
# first run (VDriftVRActivity.unpackGameData). It cannot simply live inside the
# app: the game reaches its data through ordinary paths, and keeping it outside
# means it survives reinstalling and can be added to with adb.
#
# What goes in is whatever tools\stage_data.py staged. The whole VDrift data set
# is about 1.7 GB; an APK is a zip that Android's package parser reads without
# zip64, so it tops out near 4 GB. Stage with --minimal or --cars/--tracks for a
# small download.
#
#   tools\make_release.ps1              -> dist\VDriftVR-<version>.apk

param(
    [switch]$SkipBuild    # repack the data only; gradle does not rebuild unchanged code anyway
)

$ErrorActionPreference = "Stop"
$root    = Split-Path -Parent $PSScriptRoot
$stage   = Join-Path $root "stage\VDriftVR"
$appDir  = Join-Path $root "android\app"
$zipDir  = Join-Path $appDir "build\bundled-assets"
$zipPath = Join-Path $zipDir "gamedata.zip"
$apk     = Join-Path $appDir "build\outputs\apk\release\app-release.apk"

if (-not (Test-Path $stage)) { throw "stage dir missing: run python tools\stage_data.py" }

$gradle  = Get-Content (Join-Path $appDir "build.gradle") -Raw
$version = ([regex]::Match($gradle, "versionName\s+'([^']+)'")).Groups[1].Value
if (-not $version) { throw "could not read versionName from android\app\build.gradle" }

$name = "VDriftVR-$version"
Write-Host "Building $name"

# --- the data zip ------------------------------------------------------------
Write-Host "  data   packing $stage ..."
New-Item -ItemType Directory -Force -Path $zipDir | Out-Null
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$stageLen = $stage.Length
$archive = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')
try {
    $files = 0
    foreach ($file in Get-ChildItem -Recurse -Force -File $stage) {
        # Zip entries use forward slashes and are relative to the data dir.
        $entry = $file.FullName.Substring($stageLen).TrimStart('\').Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, $entry, [System.IO.Compression.CompressionLevel]::Fastest) | Out-Null
        $files++
    }
} finally {
    $archive.Dispose()
}
$zipMB = [math]::Round((Get-Item $zipPath).Length / 1MB)
Write-Host "  data   $files files, $zipMB MB"

# --- the APK -----------------------------------------------------------------
Push-Location (Join-Path $root "android")
try {
    & .\gradlew assembleRelease -PvdvrBundleData=true -q
    if ($LASTEXITCODE -ne 0) { throw "gradle build failed" }
} finally { Pop-Location }

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$out = Join-Path $dist "$name.apk"
Copy-Item -Force $apk $out
$apkMB = [math]::Round((Get-Item $out).Length / 1MB)
Write-Host "  apk    $out ($apkMB MB)"
