# Build environment for opengl-refapp: Debian minimal plus the C++ and CPython
# toolchain. The image carries no project source -- the working tree is bind
# mounted at run time, so a rebuild of the code never rebuilds the image.
#
# Build context is this directory:
#   docker build -t opengl-refapp-build docker/

FROM debian:trixie-slim

# Provisioning lives in install-deps.sh rather than in RUN directives, so the
# dependency list stays readable and shellcheck-able on its own.
#
# BuildKit's `RUN --mount=type=bind` would run the script without ever putting
# it in a layer, but it requires the buildx component. COPY keeps this file
# buildable by the legacy builder too; the script is removed in the same step,
# and at ~1 kB what remains in the COPY layer is immaterial.
# Surface backends to provision development packages for. These mirror the
# CMake options of the same name; set one to 0 for a smaller single-backend
# image:
#   docker build --build-arg WITH_WAYLAND=0 -t opengl-refapp-build docker/
ARG WITH_X11=1
ARG WITH_WAYLAND=1
ARG WITH_PYTHON_PLUGIN=1

COPY install-deps.sh /tmp/install-deps.sh
RUN WITH_X11="${WITH_X11}" WITH_WAYLAND="${WITH_WAYLAND}" \
    WITH_PYTHON_PLUGIN="${WITH_PYTHON_PLUGIN}" \
    sh /tmp/install-deps.sh && rm -f /tmp/install-deps.sh

# Bind mount point for the host working tree.
WORKDIR /workspace

CMD ["/bin/bash"]
