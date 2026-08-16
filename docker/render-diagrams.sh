#!/bin/sh
#
# Regenerates docs/architecture/diagrams/*.svg from the Mermaid blocks in
# docs/architecture/diagrams.md.
#
# The markdown is the source of record; the .mmd and .svg files beside it are
# generated. Run this after editing any diagram.
#
# Requires the renderer image:
#   docker build -f docker/mermaid.Dockerfile -t opengl-refapp-mermaid docker/

set -eu

IMAGE="${IMAGE:-opengl-refapp-mermaid}"

root=$(cd "$(dirname "$0")/.." && pwd)
doc="${root}/docs/architecture/diagrams.md"
out="${root}/docs/architecture/diagrams"

test -f "${doc}" || { echo "missing ${doc}" >&2; exit 1; }
mkdir -p "${out}"

# Split the markdown into one .mmd per diagram, named from its heading.
python3 - "${doc}" "${out}" <<'PY'
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])

pattern = r'^#### (\d+)\. (.+?)$.*?^```mermaid\n(.*?)^```'
for number, title, body in re.findall(pattern, source, re.M | re.S):
    slug = re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-')
    (out / f"{int(number):02d}-{slug}.mmd").write_text(body)
PY

# Chromium in a container: no sandbox, no shared-memory assumptions, and no
# crash reporter -- crashpad refuses to start without a writable database
# directory, which the calling user does not have inside the image.
cat > "${out}/puppeteer.json" <<'EOF'
{
  "args": [
    "--no-sandbox",
    "--disable-setuid-sandbox",
    "--disable-dev-shm-usage",
    "--disable-gpu",
    "--disable-crash-reporter",
    "--disable-crashpad"
  ]
}
EOF

failed=0
for mmd in "${out}"/*.mmd; do
    name=$(basename "${mmd}" .mmd)
    printf '%-48s' "${name}"
    # -u keeps the SVGs owned by the caller; the HOME and XDG overrides give
    # that uid somewhere writable, since it has no home directory in the image.
    if docker run --rm -v "${out}:/data" -u "$(id -u):$(id -g)" \
            -e HOME=/tmp -e XDG_CONFIG_HOME=/tmp -e XDG_CACHE_HOME=/tmp \
            "${IMAGE}" \
            --input "/data/${name}.mmd" \
            --output "/data/${name}.svg" \
            --backgroundColor transparent \
            --puppeteerConfigFile /data/puppeteer.json >/dev/null 2>&1; then
        echo "ok"
    else
        echo "FAILED"
        failed=$((failed + 1))
    fi
done

rm -f "${out}/puppeteer.json"

if [ "${failed}" -ne 0 ]; then
    echo "${failed} diagram(s) failed to render" >&2
    exit 1
fi

echo "all diagrams rendered into ${out}"
