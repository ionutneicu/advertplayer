# Renderer for the architecture diagrams: Mermaid CLI on Debian, using the
# distribution's Chromium.
#
# Built separately from the application image because it shares nothing with
# it and is needed only when regenerating docs/architecture/diagrams/*.svg.
#
#   docker build -f docker/mermaid.Dockerfile -t opengl-refapp-mermaid docker/
#   docker/render-diagrams.sh
#
# The published minlag/mermaid-cli image is not usable: it is Alpine-based
# (musl) but ships a glibc build of Chrome, so the browser fails to relocate
# __memcpy_chk and similar and never starts. Using Debian's own Chromium
# avoids the mismatch entirely.

FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install --no-install-recommends --yes \
        chromium \
        nodejs \
        npm \
        ca-certificates \
        fonts-dejavu-core && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Puppeteer must not fetch its own Chrome; the system one is already correct
# for this libc.
ENV PUPPETEER_SKIP_DOWNLOAD=true \
    PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium

RUN npm install --global --no-fund --no-audit @mermaid-js/mermaid-cli

WORKDIR /data

ENTRYPOINT ["mmdc"]
