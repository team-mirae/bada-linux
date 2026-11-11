# Testing

Everything builds and runs in a Linux container: `tools/test.sh`. It needs Docker and a
bada-compatible checkout next to this one (or `BADA_COMPATIBLE=…`), and runs `tools/ci.sh` in the
image from `tools/Dockerfile` (Debian trixie: Fcitx5 5.1.12, IBus 1.5.32, Rust 1.85). The container
has the build machine's architecture (aarch64 on Apple silicon). Built files stay in the
`bada-linux-build` volume, so a second run is incremental; the checkouts are mounted read-only.

| Step | What |
|---|---|
| shout | bada-compatible's example engine, `cargo build --release --lib` |
| build | bada-linux, warnings as errors |
| core tests | `tests/core_tests.cpp`: JSON, Outputs, the evdev key map, manifests, settings, the publisher, the host and session logic typed into shout. Twice: plain and with ASan/UBSan |
| install | into the container's `/usr`, and shout with `bada-linux-install-ime` |
| fcitx5 e2e | `tests/e2e/fcitx5_e2e.py`: a fresh `fcitx5` with the addon, driven over its D-Bus frontend as an app's input module drives it (keys as keysym + XKB keycode, commits, preedits, surrounding text, password capability) |
| ibus e2e | `tests/e2e/ibus_e2e.py`: a fresh `ibus-daemon` that starts `bada-ibus` from the installed component XML, driven through IBus input contexts |

`tools/test.sh shell` opens a shell in the same container (`sh /src/tools/ci.sh` from there).

GitHub Actions runs `tools/test.sh` on every push and pull request, on x86_64 and arm64 runners
(`.github/workflows/test.yml`), with bada-compatible's `main` checked out next to it.

The end-to-end tests check what a framework does with the addon; they can't check a real keyboard,
a desktop's indicator, or apps. Those are the **[measure]** items in docs/spec.md §10: test on a
desktop (a VM with GNOME for IBus, KDE for Fcitx5), and add what you measure to the spec with the date.

## On a desktop

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr && cmake --build build && sudo cmake --install build
# an IME folder: ime.json and <arch>/lib<engine>.so
mkdir -p stage/shout/$(uname -m)
cp examples/shout/ime.json stage/shout/
cp ../bada-compatible/target/release/libshout.so stage/shout/$(uname -m)/
sudo bada-linux-install-ime stage/shout /usr
fcitx5 -r     # or: ibus restart
```

Build dependencies (Debian/Ubuntu): `cmake pkg-config libdbus-1-dev`, for Fcitx5
`libfcitx5core-dev libfcitx5utils-dev libfcitx5config-dev`, for IBus `libibus-1.0-dev`. Either
frontend can be left out: `-DBADA_FCITX5=OFF` / `-DBADA_IBUS=OFF`.
