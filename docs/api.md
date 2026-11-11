# The state, for other programs (API version 1)

bada-compatible v1.1 lets an engine report its state (`P_state`, §12 there) and asks its wrapper to
expose it, read-only, so users can script around the IME. bada-linux publishes the state of the
input context in focus, per IME, as a **file** and a **D-Bus signal**: both readable with standard
tools (`cat`, `jq`, `inotifywait`, `gdbus monitor`, `busctl`), no plugin. Nothing a reader does
changes the IME.

## What's published

The engine's `P_state` object as it is, with bada-linux's fields under `host`:

```json
{"format": "bada-compatible-state/1", "mode": "henkan", "profile": null,
 "staged": "HELLO", "preedit": "HELLO", "menu": ["HELLO", "hello"], "selected": 0, "zero": 0,
 "horizontal": false, "need_selection": null,
 "host": {"name": "bada-linux", "version": "0.1.0", "frontend": "fcitx5", "ime": "shout",
          "app": "gedit", "mode": "henkan", "profile": null, "caps_lock": false, "secure": false,
          "off": false}}
```

- Top-level fields are the engine's (bada-compatible §12): `staged` is exactly what a flush would
  commit. An engine without `P_state` gets a stand-in from its last Output, with `staged: null`.
- `host.mode` / `host.profile` are the session's (docs/spec.md §3); `host.frontend` is `fcitx5` or
  `ibus`; `host.app` the program, as Fcitx5 names it (`""` under IBus, which doesn't say);
  `host.off`: the IME is off there.
- **Secure input**: in a password field (docs/spec.md §8) `staged`, `preedit` and `menu` are empty and
  `host.secure` is `true`. What's typed there is never published.
- `host.caps_lock` is always `false` for now: the frontends don't read the lock state outside a key.
- It's published after every key the IME handles, every mode or profile switch, and every focus
  in, only when the JSON changed. With `publish_state: false` (docs/spec.md §9) the file is removed
  and a `{}` signal sent.

## The file

`$XDG_RUNTIME_DIR/bada-linux/<id>/state.json` (else `/tmp/bada-linux-<uid>/<id>/state.json`), e.g.
`/run/user/1000/bada-linux/bada-shout/state.json`. Rewritten atomically (a temporary file in the same
folder, then a rename), so a reader sees a whole state. Mode 0600, folders 0700: only the user reads
it.

```sh
jq -r .mode "$XDG_RUNTIME_DIR/bada-linux/bada-shout/state.json"
inotifywait -m -e moved_to "$XDG_RUNTIME_DIR/bada-linux/bada-shout"   # each change
```

## The signal

On the session bus, from the Fcitx5 or bada-ibus process:

| | |
|---|---|
| Object | `/me/sjun/BadaLinux` |
| Interface | `me.sjun.BadaLinux.State` |
| Signal | `Changed(s id, s json)`: the IME's `linux.id`, and the JSON above |

```sh
gdbus monitor --session --object-path /me/sjun/BadaLinux
dbus-monitor --session "type='signal',interface='me.sjun.BadaLinux.State'"
```

```python
# The mode, in a status bar (python-dbus)
import json, dbus, dbus.mainloop.glib
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
def changed(ime, js):
    s = json.loads(js)
    print(ime, "가" if s.get("mode") == "henkan" else "A")
dbus.SessionBus().add_signal_receiver(changed, "Changed", "me.sjun.BadaLinux.State")
GLib.MainLoop().run()
```

The session bus is the user's: other users' programs can't listen. A sandboxed (Flatpak) reader
doesn't see the signal through the bus proxy: give it `--socket=session-bus`, or have it read the file.
