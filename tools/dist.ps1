# Builds the KS-32 Standalone and VST3 (MSVC x64, Release, static C++ runtime) in build-dist and
# assembles dist\KS-32-<version>: KS-32.exe, KS-32.vst3, README.md, LICENSE and the app guide.
# -RomFolder copies the six ROM files the app needs into its roms folder, which the Standalone
# takes on its first start. The ROMs are Ensoniq's: a dist with them is for your own use only.
#
#   powershell -ExecutionPolicy Bypass -File tools\dist.ps1 [-RomFolder <folder with the ROMs>]
param([string]$RomFolder)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$version = [regex]::Match((Get-Content app\CMakeLists.txt -Raw), 'VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw 'No VERSION in app\CMakeLists.txt' }

if (-not (Test-Path build-dist\CMakeCache.txt)) {
    $cfg = @('-S', '.', '-B', 'build-dist', '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DENS_BUILD_APP=ON',
             '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')  # no VC++ redistributable needed
    if (Test-Path build-juce\juce-src) { $cfg += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$root\build-juce\juce-src" }
    & cmake @cfg
    if ($LASTEXITCODE) { throw 'configure failed' }
}
& cmake --build build-dist --config Release --target KS32_Standalone KS32_VST3
if ($LASTEXITCODE) { throw 'build failed' }

$art = 'build-dist\app\KS32_artefacts\Release'
$out = "dist\KS-32-$version"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$art\Standalone\KS-32.exe" $out
Copy-Item -Recurse "$art\VST3\KS-32.vst3" $out
Copy-Item README.md, app_preview.png, LICENSE $out
Copy-Item docs\ks32app.md "$out\APP-GUIDE.md"

if ($RomFolder) {
    $files = 'ks32\ks32v301upper.bin', 'ks32\ks32v301lower.bin', 'sq1-u25.bin', 'sq1-u26.bin', 'rom2.u39', 'rom3.u38'
    foreach ($f in $files) {
        $src = Join-Path $RomFolder $f
        if (-not (Test-Path $src)) { throw "ROM missing: $src" }
        $dst = Join-Path "$out\roms" $f
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item $src $dst
    }
}
Write-Host "dist: $root\$out"
