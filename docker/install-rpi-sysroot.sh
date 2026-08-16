#!/bin/sh
#
# Provisions a Raspberry Pi cross-compilation environment: an armhf toolchain
# plus the Broadcom userland that the DispmanX backend needs (FR-127).
#
# The Broadcom userland is not packaged for Debian. It lives in the Raspberry
# Pi firmware repository, prebuilt, under hardfp/opt/vc -- headers and shared
# objects both. We take that tree and nothing else: a full clone is gigabytes
# of firmware blobs and kernel images we have no use for.
#
# Pinned to a tag, and not to master, because **master no longer has this
# tree**: the prebuilt userland was removed once the legacy stack stopped
# being the default. 1.20230405 is a known-good tag that still carries it.
# This is also why pinning is right rather than merely tidy -- the thing we
# depend on is frozen history, not a moving branch.
#
# Target is 32-bit armhf, not arm64, because the legacy graphics stack that
# provides DispmanX is a 32-bit userland.

set -eu

FIRMWARE_REF="${FIRMWARE_REF:-1.20230405}"

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install --no-install-recommends --yes \
    crossbuild-essential-armhf \
    make \
    cmake \
    git \
    ca-certificates \
    pkg-config

# Shallow, blobless, sparse: fetch only the tree we need.
git clone --depth 1 --filter=blob:none --sparse \
    --branch "${FIRMWARE_REF}" \
    https://github.com/raspberrypi/firmware.git /tmp/firmware
git -C /tmp/firmware sparse-checkout set hardfp/opt/vc

test -d /tmp/firmware/hardfp/opt/vc || {
    echo "hardfp/opt/vc is missing from firmware ref '${FIRMWARE_REF}'." >&2
    echo "The prebuilt userland was removed from master; use a tag that still" >&2
    echo "carries it, such as 1.20230405." >&2
    exit 1
}

mkdir -p /opt/vc
cp -a /tmp/firmware/hardfp/opt/vc/. /opt/vc/
rm -rf /tmp/firmware

apt-get clean
rm -rf /var/lib/apt/lists/*

echo "--- toolchain ---"
arm-linux-gnueabihf-gcc --version | head -1
arm-linux-gnueabihf-g++ --version | head -1
cmake --version | head -1

echo "--- Broadcom userland ---"
for header in bcm_host.h EGL/egl.h GLES2/gl2.h interface/vmcs_host/vc_dispmanx.h; do
    test -f "/opt/vc/include/${header}" ||
        { echo "missing header: /opt/vc/include/${header}" >&2; exit 1; }
    echo "found: include/${header}"
done

for library in libbcm_host libbrcmEGL libbrcmGLESv2; do
    ls /opt/vc/lib/${library}.so >/dev/null 2>&1 ||
        ls /opt/vc/lib/${library}.so.* >/dev/null 2>&1 ||
        { echo "missing library: ${library}" >&2; exit 1; }
    echo "found: lib/${library}"
done

echo "--- sysroot size ---"
du -sh /opt/vc
