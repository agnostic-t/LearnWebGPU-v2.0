#!/usr/bin/env bash

set -euo pipefail

MAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
mkdir -p "$MAIN_DIR/libs/glfw3"

ROOT="$MAIN_DIR/libs/glfw3"

SOURCE_DIR="$ROOT/.src/glfw3"
BUILD_DIR="$ROOT/.build/glfw3"
INSTALL_DIR="$ROOT"

rm -rf "$SOURCE_DIR" "$BUILD_DIR"
rm -rf "$INSTALL_DIR/include" "$INSTALL_DIR/lib" "$INSTALL_DIR/bin"
mkdir -p "$ROOT/.src" "$ROOT/.build"

git clone --depth 1 https://github.com/glfw/glfw "$SOURCE_DIR"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DGLFW_BUILD_WAYLAND=ON \
    -DGLFW_BUILD_X11=ON \
    -DGLFW_LIBRARY_TYPE=STATIC

cmake --build "$BUILD_DIR" -j "$(nproc)"
cmake --install "$BUILD_DIR"

rm -rf "$ROOT/.src" "$ROOT/.build"

echo
echo "GLFW3 installed into: $INSTALL_DIR"
