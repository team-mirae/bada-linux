#!/bin/sh
# Builds and tests bada-linux in a Linux container (tools/Dockerfile: Debian trixie, Fcitx5, IBus),
# against shout from a bada-compatible checkout ($BADA_COMPATIBLE, default ../bada-compatible).
# Runs on any machine with Docker; the container is the build machine's architecture.
#
#   tools/test.sh            build the image if needed, then tools/ci.sh in it
#   tools/test.sh shell      a shell in the same container
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
compat=$(cd "${BADA_COMPATIBLE:-$root/../bada-compatible}" && pwd)
image=bada-linux-dev
docker build -q -t "$image" -f "$root/tools/Dockerfile" "$root/tools" > /dev/null
# Built files stay in a volume, so a second run is incremental and the checkouts are only read.
set -- "${1:-ci}"
cmd="sh /src/tools/ci.sh"
[ "$1" = shell ] && cmd=bash
exec docker run --rm -i $( [ -t 0 ] && echo -t ) \
  -v "$root:/src:ro" -v "$compat:/compat:ro" \
  -v bada-linux-build:/build -v bada-linux-cargo:/root/.cargo/registry \
  "$image" $cmd
