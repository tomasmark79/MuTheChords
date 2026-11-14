#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

if [ -z "${PROJECT_NIX_SHELL:-}" ] || [ "$PROJECT_NIX_SHELL" != "default" ]; then
    if command -v nix >/dev/null 2>&1; then
        echo "⚠️  Entering nix develop (default)..."
        exec nix develop "$PROJECT_ROOT/nix" --command env PROJECT_NIX_SHELL=default "$0" "$@"
    fi
fi

platform=${1:-linux}
case "$platform" in
  linux) extra=() ;;
  windows) extra=(-DCMAKE_TOOLCHAIN_FILE=cmake/windows-clang.cmake -DBUILD_TESTING=OFF) ;;
  *) echo 'Usage: scripts/build-plugin.sh [linux|windows]' >&2; exit 2 ;;
esac

build_dir="build/plugin-$platform"
library="$PWD/dist/$platform/MuTheChords.patterns.json"
if [[ -f "$library" ]]; then
    extra+=(-DCHORD_EMBED_PRIVATE_PATTERNS=ON "-DCHORD_PRIVATE_PATTERN_LIBRARY=$library")
else
    extra+=(-DCHORD_EMBED_PRIVATE_PATTERNS=OFF)
fi

if [[ "$platform" == windows ]]; then build_dir="build/plugin-windows-msvc"; fi

cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release "${extra[@]}"
cmake --build "$build_dir" --target MuTheChords_VST3 MuTheChords_CLAP --parallel "${BUILD_JOBS:-8}"

if [[ "$platform" == linux ]]; then
    cmake --build "$build_dir" --target \
        muthechords_performance_processor_tests \
        muthechords_profile_tests \
        muthechords_processor_tests \
        muthechords_clap_smoke \
        muthechords_vst_smoke \
        muthechords_vst_midi_monitor_tests \
        --parallel "${BUILD_JOBS:-8}"
    if [[ -f "$library" ]]; then
        export MUTHECHORDS_PATTERN_LIBRARY="$library"
    fi
    ctest --test-dir "$build_dir" --output-on-failure
fi

mkdir -p "dist/$platform"
rm -rf "dist/$platform/MuTheChords.vst3"
cp -a "$build_dir/MuTheChords_artefacts/Release/VST3/MuTheChords.vst3" "dist/$platform/"
cp "$build_dir/MuTheChords_artefacts/Release/CLAP/MuTheChords.clap" "dist/$platform/"
echo "✅ Built MuTheChords ($platform) -> dist/$platform/"
