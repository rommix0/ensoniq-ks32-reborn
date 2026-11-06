#!/usr/bin/env bash
# Linux counterpart of tools/dist.ps1: builds the KS-32 Standalone and VST3 (GCC x64, Release, static
# libstdc++ and libgcc) in build-dist-linux and assembles dist/KS-32-<version>-linux: KS-32, KS-32.vst3,
# README.md, LICENSE and the app guide. --rom-folder copies the six ROM files the app needs into its
# roms folder, which the Standalone takes on its first start. The ROMs are Ensoniq's: a dist with them
# is for your own use only.
#
#   tools/dist.sh [--rom-folder <folder with the ROMs>]
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD

rom_folder=
if [ "${1:-}" = --rom-folder ]; then rom_folder=${2:?--rom-folder needs a folder}; fi

version=$(grep -oP 'VERSION \K\d+\.\d+\.\d+' app/CMakeLists.txt | head -1)
[ -n "$version" ] || { echo 'No VERSION in app/CMakeLists.txt' >&2; exit 1; }

if [ ! -f build-dist-linux/CMakeCache.txt ]; then
    cfg=(-S . -B build-dist-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON
         "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"     # runs without this GCC's libstdc++
         "-DCMAKE_MODULE_LINKER_FLAGS=-static-libstdc++ -static-libgcc")
    if [ -d build-juce/juce-src ]; then cfg+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$root/build-juce/juce-src"); fi
    cmake "${cfg[@]}"
fi
cmake --build build-dist-linux --target KS32_Standalone KS32_VST3

art=build-dist-linux/app/KS32_artefacts/Release
out=dist/KS-32-$version-linux
rm -rf "$out"
mkdir -p "$out"
cp "$art/Standalone/KS-32" "$out/"
cp -r "$art/VST3/KS-32.vst3" "$out/"
cp README.md app_preview.png LICENSE "$out/"
cp docs/ks32app.md "$out/APP-GUIDE.md"

if [ -n "$rom_folder" ]; then
    for f in ks32/ks32v301upper.bin ks32/ks32v301lower.bin sq1-u25.bin sq1-u26.bin rom2.u39 rom3.u38; do
        [ -f "$rom_folder/$f" ] || { echo "ROM missing: $rom_folder/$f" >&2; exit 1; }
        mkdir -p "$(dirname "$out/roms/$f")"
        cp "$rom_folder/$f" "$out/roms/$f"
    done
fi
echo "dist: $root/$out"
