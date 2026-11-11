# bada-linux: spec

The Linux host for **bada-compatible** engines: a Fcitx5 addon and an IBus engine that load any
engine speaking the C ABI of
[bada-compatible v1.1](https://github.com/team-mirae/bada-compatible/blob/main/docs/bada-compatible-v1.md)
(bada ime 0.2.0's, plus the optional `P_state`) and do on Linux what bada ime's macOS shell and
bada-windows do: language keys, modes, profiles, Caps Lock, the menu, and the text in the app.

Facts are marked **measured** (on Linux, by someone, with the date), **e2e** (checked by the
end-to-end tests against real fcitx5 5.1.12 / ibus 1.5.32 over D-Bus, tools/ci.sh), or
**[measure]** (not yet checked on a real desktop and keyboard). **[decide]** marks a choice the owner
hasn't made.

- [1. Shape](#1-shape)
- [2. The manifest](#2-the-manifest)
- [3. One input method, two modes](#3-one-input-method-two-modes)
- [4. Language keys](#4-language-keys)
- [5. Caps Lock](#5-caps-lock)
- [6. Keys the engine passes on](#6-keys-the-engine-passes-on)
- [7. Text out](#7-text-out)
- [8. Privacy](#8-privacy)
- [9. Settings](#9-settings)
- [10. Open questions](#10-open-questions)

## 1. Shape

| Part | Where | What |
|---|---|---|
| The core | `src/core/` | C++20, no framework headers: JSON, the Output object, evdev codes → `KeyboardEvent.code`, the manifest, loading an engine (`dlopen`), the **host logic** (`host.*`), settings, and the **session** (`session.*`: one mode for the whole session, one engine per input context). Unit-tested against shout |
| Common | `src/common/` | Finding installed manifests; publishing the state (a file and a D-Bus signal, docs/api.md) |
| Fcitx5 | `src/fcitx5/` | `libbada-linux.so`, one addon that lists **one input method per installed manifest** |
| IBus | `src/ibus/` | `bada-ibus`, one process per IME, started by ibus-daemon from a component XML |
| An IME | its own folder | `ime.json` and the engine library. `examples/shout/` is the template's: bada-compatible's shout, unchanged |

All typing behaviour is the engine's; all key policy is the core's; the frontends only move keys in
and text out. The engine is **not linked**: it's loaded at run time from the manifest, so one build
hosts any engine, and several IMEs install side by side.

The core started as a copy of bada-windows' `src/core` (41241ec, 2026-11-11) and is **Linux's own**
from there: it isn't kept in step with bada-windows. Both follow bada-compatible, not each other.

### Installed layout

```
<prefix>/lib/<triplet>/fcitx5/libbada-linux.so     the Fcitx5 addon (loaded at Fcitx5's start)
<prefix>/share/fcitx5/addon/bada-linux.conf
<prefix>/libexec/bada-ibus                          the IBus engine
<prefix>/bin/bada-linux-install-ime                 installs an IME folder (below)
<prefix>/share/bada-linux/imes/<name>/
  ime.json                                          the manifest
  <arch>/lib<engine>.so                             the engine (aarch64, x86_64)
  <data files the manifest lists>
<prefix>/share/ibus/component/bada-linux-<name>.xml  written by bada-linux-install-ime
```

Manifests are found in `$BADA_LINUX_IMES` (colon-separated, for tests), then
`$XDG_DATA_HOME/bada-linux/imes/*/ime.json`, then each `$XDG_DATA_DIRS/bada-linux/imes/*/ime.json`.
The first with a given id wins, so a user's copy overrides the system's. IBus reads component XML
only from `/usr/share/ibus/component` (or `$IBUS_COMPONENT_PATH`): install system-wide for IBus.

### 1.1 Setting up

**Fcitx5**: add the IME to a group (`fcitx5-configtool`), after a keyboard layout. Then, in
Global Options → Hotkey, **remove `Zenkaku_Hankaku` from Trigger Input Method, and `Hangul` /
`Hangul_Hanja` from Activate / Deactivate**: they're Fcitx5's defaults, and Fcitx5 takes them before
any input method sees them (e2e: with them on, 半角/全角 never reached the engine). 半角/全角 is the
engine's (§4), and an Apple keyboard's 英数 / かな arrive as `Hangul_Hanja` / `Hangul` (§4).

**IBus**: add the IME in Settings → Keyboard (GNOME) or `ibus-setup`. GNOME's Super+Space switches
between it and other input sources.

## 2. The manifest

superkeyboard's manifest (`team-mirae/superkeyboard`, `docs/ime-contract.md` §8; bada-compatible
§9), unchanged, plus an optional `linux` object:

| Field | | |
|---|---|---|
| `name`, `description`, `library`, `prefix`, `data`, `settings`, `mode`, `keyboard` | superkeyboard's | `library` takes `{arch}` (`aarch64`, `x86_64`) besides `{ext}` (`so`); relative paths start at `root` (default: the manifest's folder). `data` is loaded once per process. `settings` are every engine's starting settings. `mode` is the mode a session starts in. `keyboard: "us"` makes the system's layout type passed-on keys by default (§6) |
| `linux.id` | `bada-<name>` | The input method's name in Fcitx5 and IBus. **Permanent**: it's in users' Fcitx5 profiles and GNOME's input sources. A lowercase letter, then letters, digits, `-` `_` `.` |
| `linux.display_name` | `name` | The name in the input method list |
| `linux.language` | `"ja"` | The language it's listed under |
| `linux.layout` | `"jp"` | The XKB layout IBus puts under it. Fcitx5 uses its group's layout |
| `linux.labels` | `{"henkan": "あ", "muhenkan": "A"}` | The indicator's text per mode |
| `linux.icon` | `input-keyboard` | An icon name or an absolute path |

### 2.2 Why Japanese, and the jp layout

Under the **jp** XKB layout the JIS keys have their own keysyms (`Muhenkan`, `Henkan_Mode`,
`Hiragana_Katakana`, `Zenkaku_Hankaku`, `Eisu_toggle` on the unshifted Caps Lock key) and type JIS
symbols when they reach the app. bada-linux reads keys by evdev code anyway (§4), so another layout
works too; with `us`, set `keyboard: "us"` or `pass_on: "system"` (§6).

## 3. One input method, two modes

bada ime on macOS registers two input sources (Bada Muhenkan, Bada Henkan) because macOS itself
routes 英数 to the Roman source and かな to the Japanese one. Neither Fcitx5 nor IBus routes language
keys to an input method, so the IME catches them itself either way, and a second input method would
only cost a deactivate / activate (and a commit) at every switch, and on GNOME a fight with the Shell
over which source is selected. **bada-linux registers one input method per IME, which handles both
modes itself**, as bada-windows does.

- The mode is the IME's state, **one per session**: every input context shares it (held in the
  Fcitx5 addon, or the bada-ibus process; never stored). A session starts in the manifest's `mode`.
  A switch in one app commits what's staged there; the others follow when they next see a key or
  focus, each committing its own (`Host::Follow`; e2e).
- With the mode the session also keeps the henkan **profile** (§4.2).
- **The indicator** shows the manifest's label for the mode: Fcitx5's sub-mode label (and its
  input method popup at each switch), IBus' `InputMode` property symbol, which GNOME Shell shows.
  Clicking the IBus property switches mode.
- Every switch calls `P_set_mode` (which commits what's staged), then `P_set_profile` with the current
  profile if the engine has it (bada-compatible §7.4).

## 4. Language keys

The host handles these itself (bada-compatible §7.4); they reach the engine only as `Lang1`, "next
menu entry" (⇧: previous). They're recognised by **evdev code** (Fcitx5's XKB keycode − 8; IBus'
keycode), so a key works whatever layout is loaded; by keysym only for synthetic keys without a code.

| Key | evdev code | What it does |
|---|---|---|
| 無変換 | `KEY_MUHENKAN` 94 | Muhenkan mode (bada's 英数) |
| 変換 | `KEY_HENKAN` 92 | Henkan mode, profile `henkan`. In henkan: §4.2 |
| カタカナ/ひらがな | `KEY_KATAKANAHIRAGANA` 93 (also 90, 91) | Henkan mode, profile `katakana`. In henkan: §4.2 |
| 英数 (an Apple JIS keyboard's) | `KEY_HANJA` 123 **[measure]** | Muhenkan mode |
| かな (an Apple JIS keyboard's) | `KEY_HANGEUL` 122 **[measure]** | Henkan mode, profile `mac-kana` (setting `kana_key`; `none` passes it to the app) |
| The Caps Lock key | `KEY_CAPSLOCK` 58 | §5 |
| Right Alt, tapped | `KEY_RIGHTALT` 100 | **Off by default** (owner, 2026-11-11). Setting `right_alt_tap`: a profile (`mac-kana`) |
| ⌃⌥⇧ (MEH) + a profile key | | The other mode (bada's ⌃⌥⇧かな) |

- Held down, a language key acts once (auto-repeats are eaten and ignored; a press with no release
  since the last one is a repeat).
- A profile key with ⌃, ⌥ or Super (but not all of ⌃⌥⇧) is eaten and does nothing.
- 半角/全角 (`KEY_GRAVE` 41, or `KEY_ZENKAKUHANKAKU` 85) is not a language key: it's the engine's
  `Backquote` (bada's full-width / half-width; shout's "shout the selection"; e2e).
- Apple JIS 英数 / かな are HID LANG2 / LANG1, which Linux names after the Korean keys; under the
  jp layout they give `Hangul_Hanja` / `Hangul`, Fcitx5's default hotkeys (§1.1).

### 4.2 Henkan profiles

bada's profiles (bada `docs/spec.md` §6.6) are `mac-kana` (かな), `henkan` (変換) and `katakana`
(カタカナ). **On a PC keyboard 変換 is the home key and `henkan` the default profile**, as on Windows.

- In muhenkan, a profile key enters henkan in its profile.
- In henkan, the key of the profile in use moves to the next menu entry (⇧: previous); so does 変換
  in another profile while a menu of more than one row shows. Otherwise a profile key switches to its
  profile, committing what's staged.
- **`mac-kana` on a PC keyboard**: unlike a Windows TIP, Fcitx5 and IBus see a modifier's release,
  so a **Right Alt tap** (pressed and released with nothing between, no ⇧ ⌃ Super) can be its key.
  It's **off by default**; `right_alt_tap: "mac-kana"` turns it on. The press and release still
  reach the app (a modifier's press is never eaten), so an app that acts on a lone Alt tap (a menu
  bar) may act too **[measure]**.
- An engine without `P_set_profile` (shout) has one henkan: every profile key enters it, and in
  henkan moves in the menu (e2e).

## 5. Caps Lock

Caps Lock is the host's (bada-compatible v1.1 §7.7): the engine never sees it. **Handling it is
optional, and off by default** (setting `caps_lock`):

| Setting | Caps Lock | ⇧ ⌃ ⌥ + Caps Lock |
|---|---|---|
| `respect` (**default**) | the system's own | the system's own |
| `toggle` | Switches mode | Caps Lock, in muhenkan only; in henkan nothing |
| `transient` | Caps Lock in muhenkan; in henkan nothing | as Caps Lock |

- **Under the jp layout the unshifted Caps Lock key is 英数** (`Eisu_toggle`): no lock changes, and
  Caps Lock itself is ⇧ + it. So with `respect` that key is 英数 (muhenkan), and with `toggle` it
  switches mode without Caps Lock ever turning on.
- **An input method can't set the lock** on Linux (Wayland has no way; X11 would need the XKB
  extension from inside the framework). Where Windows turns Caps Lock back off, bada-linux can't:
  under a us layout, `toggle` still toggles the lock. Use the jp layout, or an XKB option that makes
  the key not lock (`caps:none` and the like) **[measure]**.
- **What Caps Lock types** (§6): with it on, passed-on letters are capitals, ⇧ or not
  (bada-compatible v1.1 §7.3). `caps_rule: "uppersnake"` (bada's UPPER_SNAKE, muhenkan only): Space
  flushes and types `_`, ⌥Space a space, without `P_key`.

## 6. Keys the engine passes on

A key the engine doesn't consume reaches the app after the commit (bada-compatible §7.2: commit, then
act). A plain or ⇧ character key (the 48 JIS positions) is then **typed by the host from its JIS
table** (bada-compatible §7.3), whatever layout is loaded: the key is eaten and its character
committed. With `pass_on: "system"` (or the manifest's `keyboard: "us"`) the app gets the key and
the system's layout types it. Shortcuts (⌃, ⌥, Super), Space, Enter, Tab, Backspace, arrows, F keys
and the keypad always go to the app (e2e).

A modifier pressed on its own never reaches the engine (it belongs to the next key and must not
commit). Keys without a `KeyboardEvent.code` the core knows go as `"Unidentified"`.

## 7. Text out

Each Output is applied as bada-compatible §7.1 says: `commit` is committed (with `replace_selection`,
the selection is deleted first through surrounding text); `preedit` becomes the preedit, underlined
(Fcitx5: highlighted, IBus: double underline, while the menu shows an entry other than entry 0).
Then:

- **`need_selection`**: the host reads the selected text from the field's surrounding text (`""`
  when the app doesn't give it, or while something is marked) and answers with `P_host_selection`,
  once (§7.5 there; e2e in both). IBus sends surrounding text only to an engine that asked once, so
  bada-ibus asks at every focus in.
- **The menu**: the framework's candidate window (a Wayland client can't place one of its own).
  Rows above `zero` (negative entries) are labelled with their entries (−2, −1); the rest have no
  labels; `notes` are the candidates' comments (IBus: after the text); `horizontal` is the layout
  hint / orientation. Choosing a row does nothing (the contract has none). **Lost** against
  Windows: the separating line, and `japanese` rows in a Japanese font.
- **Focus** (e2e): leaving a field commits what's staged. Fcitx5: the engine flushes on `reset`.
  IBus: the preedit is shown in `PREEDIT_COMMIT` mode and the client commits it, because ibus-daemon
  detaches a global engine from a context as focus leaves and a commit sent then is lost (e2e,
  ibus 1.5.32); the engine flushes into nothing. Where the engine's `staged` differs from what shows,
  that preedit is shown in `CLEAR` mode instead.
- An app's reset (a click in the text) commits what's staged (bada-compatible §7.6).

## 8. Privacy

- **Password fields** (Fcitx5: capability `Password` or `Sensitive`; IBus: purpose `PASSWORD` /
  `PIN`, hint `PRIVATE`): nothing composes there (only §6's JIS characters are typed), and nothing
  typed is published (docs/api.md: `host.secure`). Language keys still switch mode (e2e). **Fcitx5
  keeps input methods out of password fields** unless Global Options → Behavior → Allow Input Method
  in the password field is on; then the IME's own handling applies.
- **Off in a program** (`off_in`, by program name): nothing composes there. Fcitx5 only: IBus doesn't
  tell an engine which program it's in.
- The menu window can't be kept out of screen capture on Linux (it's the framework's window, and
  neither X11 nor Wayland has such a flag).
- Nothing is logged except load errors (Fcitx5's log, bada-ibus' stderr), never typed text.

## 9. Settings

`$XDG_CONFIG_HOME/bada-linux/<id>.json` (else `~/.config/…`), a JSON object, re-read at focus in when
it changed. Missing or invalid fields take their defaults.

| Field | Default | |
|---|---|---|
| `caps_lock` | `"respect"` | `respect`, `toggle`, `transient` (§5) |
| `caps_rule` | `"allcaps"` | `allcaps`, `uppersnake` (§5) |
| `pass_on` | `"jis"` (`"system"` if the manifest's keyboard is `us`) | who types passed-on character keys (§6) |
| `kana_key` | `"mac-kana"` | the profile an Apple keyboard's かな key brings up, or `"none"` (§4.2) |
| `right_alt_tap` | `"none"` | the profile a Right Alt tap brings up (§4.2) |
| `engine_settings` | | a JSON object; its top-level fields replace the manifest's `settings` (superkeyboard's rule) |
| `publish_state` | `true` | publish the state for other programs (docs/api.md) |
| `off_in` | `[]` | program names nothing composes in (Fcitx5) |
| `dictionary` | | a user dictionary file, for engines with `P_dictionary_load`; default `$XDG_DATA_HOME/bada-linux/<id>/dictionary.json` |

The dictionary is read once per process, when the engine loads. Settings stay a JSON file for now,
with no UI (owner, 2026-11-11).

## 10. Open questions

- **[measure]** On real hardware: an Apple JIS keyboard's 英数 / かな codes; the Right Alt tap in
  apps that act on Alt; the indicator in GNOME Shell and KDE; Caps Lock with `toggle` under a us
  layout and XKB options; the jp layout's Caps Lock key on X11 and Wayland.
- **[measure]** Which apps give surrounding text (GTK and Qt do; terminals, Electron, XWayland
  through XIM mostly don't), so `need_selection` answers `""` there.
- **[decide]** Packages (deb, rpm, AUR, a Flatpak extension for Fcitx5).
- Not built: an x86_64 build is untested here (the code doesn't block it: `{arch}` is `x86_64`).
