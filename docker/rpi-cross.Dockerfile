# Cross-compilation image for the Raspberry Pi DispmanX backend.
#
#   docker build -f docker/rpi-cross.Dockerfile -t opengl-refapp-rpi docker/
#   docker/build-rpi.sh
#
# What this does and does not prove:
#
#   It proves the DispmanX backend **compiles and links** against the real
#   Broadcom headers and libraries. That is worth having, because nothing else
#   in CI touches that code at all.
#
#   It does not prove anything **runs**. Cross-compiled binaries are not
#   executed here, and a DispmanX surface can only be brought up on the device.
#   Acceptance criterion 1 of story S-02 still needs a Raspberry Pi.
#
# Target is armhf: the legacy graphics stack providing DispmanX is a 32-bit
# userland.

FROM debian:trixie-slim

COPY install-rpi-sysroot.sh /tmp/install-rpi-sysroot.sh
RUN sh /tmp/install-rpi-sysroot.sh && rm -f /tmp/install-rpi-sysroot.sh

WORKDIR /workspace

CMD ["/bin/bash"]
