#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "==> Configuring miquland ($BUILD_TYPE)..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_INSTALL_PREFIX="/usr" "$@"

echo "==> Building miquland..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Build finished successfully! Binary is at $BUILD_DIR/miquland"

# If invoked with sudo/root, automatically install to system
if [ "${EUID}" -eq 0 ]; then
    echo "==> Installing miquland to system (/usr/bin, /usr/share/miquland, /usr/share/wayland-sessions)..."
    cmake --install "$BUILD_DIR"
    echo "==> miquland successfully installed! You can select 'miquland' in your display manager or launch it via 'miquland'."
fi
