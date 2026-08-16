#!/bin/sh
#
# Cross-compiles the renderer for a Raspberry Pi, with the DispmanX backend
# enabled, inside the cross image.
#
# Requires the image:
#   docker build -f docker/rpi-cross.Dockerfile -t opengl-refapp-rpi docker/
#
# This checks that the DispmanX backend compiles and links against the real
# Broadcom userland. It does not run anything -- the binary is armhf, and a
# DispmanX surface only exists on the device.

set -eu

IMAGE="${IMAGE:-opengl-refapp-rpi}"
BUILD_DIR="${BUILD_DIR:-build-rpi}"

root=$(cd "$(dirname "$0")/.." && pwd)

docker run --rm -v "${root}:/workspace" -u "$(id -u):$(id -g)" -w /workspace \
    "${IMAGE}" sh -c "
set -e
cmake -S . -B ${BUILD_DIR} \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi-armhf.cmake \
      -DENABLE_DISPMANX=ON \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build ${BUILD_DIR} --target refapp -j\"\$(nproc)\"
echo
echo '--- the cross-built binary ---'
arm-linux-gnueabihf-readelf -h ${BUILD_DIR}/refapp | grep -E 'Class:|Machine:'
echo
echo '--- shared libraries it needs ---'
arm-linux-gnueabihf-readelf -d ${BUILD_DIR}/refapp | grep NEEDED
"
