#!/bin/sh
#
# Provisions the opengl-refapp build image.
#
# Kept out of the Dockerfile so the dependency list stays readable, reviewable
# and shellcheck-able on its own.
#
# Flags, passed as environment variables, mirror the CMake options of the same
# name (docs/architecture/requirements.md FR-6, FR-9):
#
#   WITH_X11=0            omit the X11 surface backend's development packages
#   WITH_WAYLAND=0        omit the Wayland surface backend's development packages
#   WITH_PYTHON_PLUGIN=0  omit the Python plugin boilerplate's dependencies
#
# All default to 1. Omitting a surface backend produces a smaller image that
# can only build the other one.

set -eu

WITH_X11="${WITH_X11:-1}"
WITH_WAYLAND="${WITH_WAYLAND:-1}"
WITH_PYTHON_PLUGIN="${WITH_PYTHON_PLUGIN:-1}"

if [ "${WITH_X11}" = "0" ] && [ "${WITH_WAYLAND}" = "0" ]; then
    echo "WITH_X11 and WITH_WAYLAND are both 0: no surface backend to build" >&2
    exit 1
fi

# build-essential -> g++, make, libc headers
# cmake           -> >= 3.20, required by CMakeLists.txt
# python3-dev     -> Python.h, libpython AND the python3 interpreter.
#
# python3-dev rather than libpython3-dev: the latter carries only the
# development files, with no /usr/bin/python3. CMakeLists.txt asks for
# COMPONENTS Interpreter Development.Embed, so a dev-files-only image fails
# configure with "Could NOT find Python3 (missing: Python3_EXECUTABLE
# Interpreter)" even though it reports having found a suitable version.
PACKAGES="build-essential cmake python3-dev"

# libegl-dev  -> EGL, the surface layer for both backends (FR-5)
# libgles-dev -> OpenGL ES 2.0 headers (FR-10)
PACKAGES="${PACKAGES} libegl-dev libgles-dev"

if [ "${WITH_X11}" = "1" ]; then
    PACKAGES="${PACKAGES} libx11-dev libxext-dev"
fi

if [ "${WITH_WAYLAND}" = "1" ]; then
    PACKAGES="${PACKAGES} libwayland-dev libwayland-egl-backend-dev"
    PACKAGES="${PACKAGES} wayland-protocols libxkbcommon-dev"
fi

# Python plugin boilerplate (FR-64) and the Cairo clock example (FR-66).
# python3-dev itself stays unconditional above: the core CMakeLists.txt still
# requires Python3 today. Once the interpreter moves out of the core binary it
# can move under this flag too.
if [ "${WITH_PYTHON_PLUGIN}" = "1" ]; then
    PACKAGES="${PACKAGES} python3-cairo libcairo2-dev"
fi

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install --no-install-recommends --yes ${PACKAGES}

# Package lists are useless at run time and cost ~40 MB.
apt-get clean
rm -rf /var/lib/apt/lists/*

# Fail the image build now, loudly, rather than at cmake time later. Each
# check maps to something CMakeLists.txt requires.
echo "--- toolchain ---"
cmake --version
g++ --version

# Python3::Interpreter
python3 --version

# Python3::Development.Embed -- headers plus a linkable libpython.
python3 - <<'EOF'
import os.path
import sys
import sysconfig

include_dir = sysconfig.get_paths()["include"]
print(f"python include dir: {include_dir}")

if not os.path.isfile(os.path.join(include_dir, "Python.h")):
    sys.exit("Python.h missing: the Python development files are not installed")
EOF

# Graphics headers. Missing ones would otherwise surface as a confusing
# find_package failure much later.
echo "--- graphics ---"
for header in EGL/egl.h GLES2/gl2.h; do
    test -f "/usr/include/${header}" ||
        { echo "missing header: ${header}" >&2; exit 1; }
    echo "found: ${header}"
done

if [ "${WITH_X11}" = "1" ]; then
    test -f /usr/include/X11/Xlib.h ||
        { echo "missing header: X11/Xlib.h" >&2; exit 1; }
    echo "found: X11/Xlib.h"
fi

if [ "${WITH_WAYLAND}" = "1" ]; then
    test -f /usr/include/wayland-client.h ||
        { echo "missing header: wayland-client.h" >&2; exit 1; }
    echo "found: wayland-client.h"
fi

if [ "${WITH_PYTHON_PLUGIN}" = "1" ]; then
    python3 -c 'import cairo; print(f"pycairo {cairo.version}")'
fi
