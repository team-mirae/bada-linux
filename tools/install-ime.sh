#!/bin/sh
# Installs one IME (docs/spec.md §1): a folder with ime.json and what it names (the engine library
# under <arch>/, its data), copied to <prefix>/share/bada-linux/imes/<name>/, plus the IBus
# component XML that starts bada-ibus for it. Fcitx5 finds it by itself (restart Fcitx5).
#
#   install-ime.sh <folder> [prefix]     prefix: /usr/local (default), /usr, or ~/.local
#   BADA_IBUS=<path to bada-ibus>         default: <prefix>/libexec/bada-ibus, else found on PATH
set -eu
src=${1:?usage: install-ime.sh <folder> [prefix]}
prefix=${2:-/usr/local}
manifest=$src/ime.json
[ -f "$manifest" ] || { echo "no ime.json in $src" >&2; exit 1; }
name=$(sed -n 's/^ *"name": *"\([^"]*\)".*/\1/p' "$manifest" | head -1)
[ -n "$name" ] || { echo "no name in $manifest" >&2; exit 1; }
dest=$prefix/share/bada-linux/imes/$name
mkdir -p "$dest"
cp -R "$src"/. "$dest"/
ibus=${BADA_IBUS:-$prefix/libexec/bada-ibus}
[ -x "$ibus" ] || ibus=$(command -v bada-ibus || true)
if [ -n "$ibus" ] && [ -x "$ibus" ]; then
  mkdir -p "$prefix/share/ibus/component"
  "$ibus" --xml --manifest "$dest/ime.json" --exec "$ibus" > "$prefix/share/ibus/component/bada-linux-$name.xml"
  echo "IBus: $prefix/share/ibus/component/bada-linux-$name.xml (then: ibus restart)"
fi
echo "installed $name in $dest"
