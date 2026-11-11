"""What both end-to-end tests share: key tokens as (keysym, evdev code), a text field that takes
commits, preedits and keys the IME didn't eat, and the checks."""

import json
import os
import sys
import time

from gi.repository import GLib

# token -> (keysym, evdev code)
KEYS = {
    **{c: (ord(c), code) for c, code in zip("qwertyuiop", range(16, 26))},
    **{c: (ord(c), code) for c, code in zip("asdfghjkl", range(30, 39))},
    **{c: (ord(c), code) for c, code in zip("zxcvbnm", range(44, 51))},
    "Space": (0x20, 57), "Enter": (0xFF0D, 28), "Tab": (0xFF09, 15), "Backspace": (0xFF08, 14),
    "Escape": (0xFF1B, 1), "Period": (ord("."), 52),
    "muhenkan": (0xFF22, 94), "henkan": (0xFF23, 92), "katakana": (0xFF27, 93),
    "zenkaku": (0xFF2A, 41), "ralt": (0xFFEA, 100), "Shift": (0xFFE1, 42),
}
SHIFT, CTRL, ALT, RELEASE_IBUS, RELEASE_X = 1, 4, 8, 1 << 30, None

failures = 0
checks = 0


def check(cond, what):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print(f"FAIL {what}", flush=True)


def check_eq(got, want, what):
    check(got == want, f"{what}: got {got!r}, want {want!r}")


def pump(seconds=0.15):
    """Runs the main loop until nothing has happened for a while."""
    ctx = GLib.MainContext.default()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if ctx.iteration(False):
            end = time.monotonic() + seconds
        else:
            time.sleep(0.005)


def wait_for(predicate, seconds=10.0, what="condition"):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return True
        pump(0.05)
    check(False, f"timed out waiting for {what}")
    return False


class TextField:
    """A plain text field (bada-compatible §7.1, §7.3), as an app would keep it."""

    def __init__(self):
        self.text = ""
        self.preedit = ""
        self.commits = []
        self.forwarded = []

    def commit(self, s):
        self.commits.append(s)
        self.text += s

    def set_preedit(self, s):
        self.preedit = s

    def delete_before(self, n):
        self.text = self.text[: len(self.text) - n] if n else self.text

    def app_key(self, token, shift=False):
        """What the app does with a key the IME didn't eat."""
        if token == "Space":
            self.text += " "
        elif token == "Enter":
            self.text += "\n"
        elif token == "Tab":
            self.text += "\t"
        elif token == "Backspace":
            self.text = self.text[:-1]
        elif len(token) == 1:
            self.text += token.upper() if shift else token


def state_file(ime_id):
    return os.path.join(os.environ["XDG_RUNTIME_DIR"], "bada-linux", ime_id, "state.json")


def read_state(ime_id):
    try:
        with open(state_file(ime_id), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def finish(name):
    print(f"{name}: {checks} checks, {failures} failed", flush=True)
    sys.exit(1 if failures else 0)
