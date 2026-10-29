#!/usr/bin/env bash

set -euo pipefail

MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
mkdir -p "$MAIN_DIR/libs/dawn"

ROOT="$MAIN_DIR/libs/dawn"

SOURCE_DIR="$ROOT/.src/dawn"
BUILD_DIR="$ROOT/.build/dawn"
INSTALL_DIR="$ROOT"

rm -rf "$SOURCE_DIR"
rm -rf "$BUILD_DIR"
rm -rf "$INSTALL_DIR/include" "$INSTALL_DIR/lib" "$INSTALL_DIR/bin"

mkdir -p "$ROOT/.src" "$ROOT/.build" "$INSTALL_DIR"

git clone --depth 1 \
    https://dawn.googlesource.com/dawn \
    "$SOURCE_DIR"

python "$SOURCE_DIR/tools/fetch_dawn_dependencies.py" \
    -d "$SOURCE_DIR"

test -f "$SOURCE_DIR/third_party/abseil-cpp/CMakeLists.txt" || {
    echo "ERROR: Dawn deps were not fetched" >&2
    exit 1
}

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=OFF \
    -DDAWN_FETCH_DEPENDENCIES=OFF \
    -DDAWN_ENABLE_INSTALL=ON \
    -DDAWN_USE_GLFW=ON \
    -DDAWN_USE_X11=ON \
    -DDAWN_USE_WAYLAND=ON \
    -DDAWN_BUILD_SAMPLES=OFF \
    -DDAWN_BUILD_TESTS=OFF \
    -DDAWN_BUILD_BENCHMARKS=OFF \
    -DDAWN_BUILD_NODE_BINDINGS=OFF

cmake --build "$BUILD_DIR" -j "$(nproc)"
cmake --install "$BUILD_DIR"
cmake --build "$BUILD_DIR" \
    --target dawn_glfw \
    -j "$(nproc)"

install -Dm644 \
    "$SOURCE_DIR/include/webgpu/webgpu_glfw.h" \
    "$INSTALL_DIR/include/webgpu/webgpu_glfw.h"

DAWN_GLFW_LIB="$(
    find "$BUILD_DIR" \
        -type f \
        -name 'libdawn_glfw.a' \
        -print \
        -quit
)"

if [[ -z "$DAWN_GLFW_LIB" ]]; then
    echo "ERROR: libdawn_glfw.a was not built" >&2
    exit 1
fi

install -Dm644 \
    "$DAWN_GLFW_LIB" \
    "$INSTALL_DIR/lib/libdawn_glfw.a"

git -C "$SOURCE_DIR" rev-parse HEAD \
    > "$INSTALL_DIR/DAWN_COMMIT"

rm -rf "$ROOT/.src" "$ROOT/.build"

echo
echo "Dawn installed into: $INSTALL_DIR"
echo "Dawn commit: $(cat "$INSTALL_DIR/DAWN_COMMIT")"
