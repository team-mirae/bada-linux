#!/bin/sh
# Builds and tests everything, inside the dev container (tools/test.sh runs it there):
#   1. shout's engine from a bada-compatible checkout (/compat)
#   2. bada-linux, warnings as errors; the core tests, plain and with ASan/UBSan
#   3. installs it and shout into /usr, then the end-to-end tests through fcitx5 and ibus-daemon
set -eu
src=${SRC:-/src}
compat=${COMPAT:-/compat}
build=${BUILD:-/build}
arch=$(uname -m)

echo "== shout"
CARGO_TARGET_DIR=$build/shout cargo build --release --lib --quiet --manifest-path "$compat/Cargo.toml"
shout=$build/shout/release/libshout.so

echo "== build"
cmake -S "$src" -B "$build/cmake" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr -DBADA_WERROR=ON > /dev/null
cmake --build "$build/cmake" -j"$(nproc)"

echo "== core tests"
"$build/cmake/bl_core_tests" "$shout"
cmake -S "$src" -B "$build/asan" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBADA_WERROR=ON \
  -DBADA_FCITX5=OFF -DBADA_IBUS=OFF -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" > /dev/null
cmake --build "$build/asan" -j"$(nproc)" --target bl_core_tests
echo "(ASan/UBSan)"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/asan/bl_core_tests" "$shout"

echo "== install"
cmake --install "$build/cmake" > /dev/null
stage=$build/stage/shout
rm -rf "$stage" && mkdir -p "$stage/$arch"
cp "$src/examples/shout/ime.json" "$stage/"
cp "$shout" "$stage/$arch/"
BADA_IBUS=/usr/libexec/bada-ibus /usr/bin/bada-linux-install-ime "$stage" /usr

# A clean session for each frontend: its own home, runtime dir and session bus.
fresh() {
  export HOME=$build/home-$1 XDG_RUNTIME_DIR=$build/run-$1
  rm -rf "$HOME" "$XDG_RUNTIME_DIR" && mkdir -p "$HOME/.config" "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
  export XDG_CONFIG_HOME=$HOME/.config
  unset XDG_DATA_HOME
}
dbus-uuidgen --ensure

echo "== fcitx5 end to end"
fresh fcitx5
mkdir -p "$XDG_CONFIG_HOME/fcitx5"
cat > "$XDG_CONFIG_HOME/fcitx5/profile" <<'EOF'
[Groups/0]
Name=Default
Default Layout=jp
DefaultIM=bada-shout

[Groups/0/Items/0]
Name=keyboard-jp
Layout=

[Groups/0/Items/1]
Name=bada-shout
Layout=

[GroupOrder]
0=Default
EOF
# Fcitx5's defaults take 半角/全角 (and an Apple keyboard's 英数 / かな) as its own hotkeys, and keep
# input methods out of password fields. docs/spec.md §1.1 has users change the first; the second is
# turned on here so the IME's own password handling is tested.
cat > "$XDG_CONFIG_HOME/fcitx5/config" <<'EOF'
[Hotkey/TriggerKeys]
0=Control+space

[Hotkey/ActivateKeys]

[Hotkey/DeactivateKeys]

[Behavior]
AllowInputMethodForPassword=True
EOF
dbus-run-session -- sh -c '
  fcitx5 --disable=all --enable=dbus,dbusfrontend,keyboard,bada-linux > "$HOME/fcitx5.log" 2>&1 &
  pid=$!
  python3 "'"$src"'/tests/e2e/fcitx5_e2e.py"; status=$?
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  if [ $status -ne 0 ]; then echo "--- fcitx5 log"; tail -40 "$HOME/fcitx5.log"; fi
  exit $status'

echo "== ibus end to end"
fresh ibus
export GSETTINGS_BACKEND=memory
dbus-run-session -- sh -c '
  ibus-daemon --panel=disable --emoji-extension=disable --xim --replace --verbose > "$HOME/ibus.log" 2>&1 &
  pid=$!
  python3 "'"$src"'/tests/e2e/ibus_e2e.py"; status=$?
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  if [ $status -ne 0 ]; then echo "--- ibus log"; tail -40 "$HOME/ibus.log"; fi
  exit $status'

echo "== all passed"
