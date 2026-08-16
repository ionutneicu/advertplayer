# CMake toolchain for cross-compiling to a Raspberry Pi running the legacy
# graphics stack (32-bit armhf, Broadcom userland in /opt/vc).
#
#   cmake -S . -B build-rpi \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi-armhf.cmake \
#         -DENABLE_DISPMANX=ON
#
# Intended to be used inside the image built from docker/rpi-cross.Dockerfile,
# which provides both the toolchain and /opt/vc.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Where to look for the target's headers and libraries. The Broadcom userland
# is the interesting part; the rest comes from the cross toolchain's own
# sysroot.
set(REFAPP_RPI_SYSROOT "/opt/vc" CACHE PATH "Broadcom userland location")
list(APPEND CMAKE_FIND_ROOT_PATH "${REFAPP_RPI_SYSROOT}")

# Programs come from the host; everything else must come from the target, or
# the build silently picks up an x86-64 library and fails at link with an
# architecture mismatch that is hard to read.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The Broadcom libraries carry no rpath and are not on the default search
# path, on the device or here.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-rpath-link,${REFAPP_RPI_SYSROOT}/lib")
