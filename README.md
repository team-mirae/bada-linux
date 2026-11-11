# bada-linux

A Linux input method for **any bada-compatible engine**: a Fcitx5 addon and an IBus engine that load
an IME engine speaking [bada-compatible](https://github.com/team-mirae/bada-compatible)'s C ABI
(v1.1, bada ime 0.2.0's plus `P_state`) and host it the way
[bada-windows](https://github.com/team-mirae/bada-windows) does on Windows: JIS language keys,
muhenkan and henkan modes, henkan profiles, optional Caps Lock handling, the candidate menu, and the
text in every app. The engine is a `.so` named in a small manifest; nothing is linked, so one build
hosts any engine.

The example IME is **shout**, bada-compatible's template IME: henkan mode types CAPITALS, Tab or 変換
offer lowercase, 半角/全角 in muhenkan shouts the selection.

> [!NOTE]
> Built and tested in a container against real Fcitx5 5.1.12 and IBus 1.5.32 (core tests, and
> end-to-end tests that drive both frameworks over D-Bus as apps do). Not yet tried on a desktop with
> a real JIS keyboard: see [docs/spec.md §10](docs/spec.md#10-open-questions).

## One input method, two modes

Each IME is **one** input method in Fcitx5 or IBus, and switches muhenkan and henkan itself, as
bada-windows does. macOS's bada ime has two input sources because macOS sends 英数 and かな to
different sources. Linux sends them nowhere in particular, so a second input method would only add a
deactivate and activate, and a commit, at every switch ([docs/spec.md §3](docs/spec.md#3-one-input-method-two-modes)).

| | macOS (bada ime) | Windows (bada-windows) | Linux (bada-linux) |
|---|---|---|---|
| Input methods | Two: Bada Muhenkan, Bada Henkan | One TSF profile | **One** Fcitx5 / IBus input method |
| Muhenkan / henkan | 英数 / かな | 無変換 / 変換 | **無変換 / 変換** (and an Apple keyboard's 英数 / かな) |
| `mac-kana` on a PC keyboard | | none (Right Alt never reaches a TIP) | **Right Alt tap**, off by default |
| Caps Lock | bada's | optional | optional; the jp layout's Caps Lock key is 英数 |
| State for other programs | file + notification | shared memory + window message | **file + D-Bus signal** ([docs/api.md](docs/api.md)) |

## Build and try it

```sh
tools/test.sh      # in Docker: builds shout and bada-linux, core tests, Fcitx5 and IBus end to end
```

On a desktop: [docs/testing.md](docs/testing.md#on-a-desktop). Then, for Fcitx5, take
`Zenkaku_Hankaku` and `Hangul` / `Hangul_Hanja` out of its hotkeys ([docs/spec.md §1.1](docs/spec.md#11-setting-up)).

## Make a Linux IME from a bada-compatible engine

1. Make the engine (bada-compatible's README, "Make a new IME"). Export `state` for the API.
2. Make a folder with `ime.json` (copy `examples/shout/ime.json`): `name`, `library`
   (`{arch}/lib<engine>.so`), `prefix`, `data` (copied next to it), `settings`, and under `linux` a
   new **`id`** (permanent: it's in users' settings), the language and the indicator's labels.
3. Build the engine as a `cdylib` and put it in `<folder>/<arch>/` (`aarch64`, `x86_64`).
4. `bada-linux-install-ime <folder> /usr`, then restart Fcitx5 or IBus.

## Layout

| Path | What |
|---|---|
| `src/core/` | C++20, no framework headers: JSON, Outputs, keys, manifests, the engine loader, **the host logic**, settings, the session |
| `src/common/` | Finding installed IMEs; publishing the state |
| `src/fcitx5/` | The Fcitx5 addon |
| `src/ibus/` | The IBus engine |
| `examples/shout/` | The example IME's manifest |
| `tests/` | Core tests; end-to-end tests through Fcitx5 and IBus |
| `tools/` | `test.sh` (Docker), `ci.sh` (in it), `install-ime.sh`, `Dockerfile` |
| `docs/` | [spec](docs/spec.md), [api](docs/api.md), [testing](docs/testing.md) |

## License

[CC0 1.0](LICENSE), all of it, the parts that started in bada-windows included. Engines keep their own
licenses.
