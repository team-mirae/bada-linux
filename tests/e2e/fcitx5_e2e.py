"""End to end through Fcitx5: a fresh fcitx5 with the bada-linux addon and shout installed, driven
over its D-Bus frontend (org.fcitx.Fcitx.InputMethod1) as an app's input module drives it.

Run inside a session bus, with fcitx5 started by tools/ci.sh."""

import os

import dbus
import dbus.mainloop.glib

from common import (ALT, CTRL, KEYS, SHIFT, TextField, check, check_eq, finish, pump, read_state, wait_for)

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
SERVICE = "org.fcitx.Fcitx5"
IME = "bada-shout"

# CapabilityFlag (fcitx-utils/capabilityflags.h)
PREEDIT, PASSWORD, FORMATTED, SURROUNDING = 1 << 1, 1 << 3, 1 << 4, 1 << 6

wait_for(lambda: bus.name_has_owner(SERVICE), 20, "fcitx5 on the bus")
controller = dbus.Interface(bus.get_object(SERVICE, "/controller"), "org.fcitx.Fcitx.Controller1")
im = dbus.Interface(bus.get_object(SERVICE, "/org/freedesktop/portal/inputmethod"), "org.fcitx.Fcitx.InputMethod1")

ims = [str(entry[0]) for entry in controller.AvailableInputMethods()]
check(IME in ims, f"{IME} is listed ({ims})")

signals = []
bus.add_signal_receiver(lambda ime, js: signals.append((str(ime), str(js))), signal_name="Changed",
                        dbus_interface="me.sjun.BadaLinux.State")


class Client:
    def __init__(self, caps=PREEDIT | FORMATTED | SURROUNDING, program="e2e"):
        path, _ = im.CreateInputContext([("program", program), ("display", "x11:")])
        self.path = path
        self.ic = dbus.Interface(bus.get_object(SERVICE, path), "org.fcitx.Fcitx.InputContext1")
        self.field = TextField()
        self.caps = caps
        f = self.field
        bus.add_signal_receiver(lambda s: f.commit(str(s)), signal_name="CommitString",
                                dbus_interface="org.fcitx.Fcitx.InputContext1", path=path)
        bus.add_signal_receiver(lambda parts, cursor: f.set_preedit("".join(str(p[0]) for p in parts)),
                                signal_name="UpdateFormattedPreedit",
                                dbus_interface="org.fcitx.Fcitx.InputContext1", path=path)
        bus.add_signal_receiver(lambda offset, size: f.delete_before(int(size)), signal_name="DeleteSurroundingText",
                                dbus_interface="org.fcitx.Fcitx.InputContext1", path=path)
        self.ic.SetCapability(dbus.UInt64(caps))

    def focus(self):
        self.ic.FocusIn()
        pump()
        if str(controller.CurrentInputMethod()) != IME:
            controller.SetCurrentIM(IME)
            pump()

    def surrounding(self, text, cursor, anchor):
        self.ic.SetSurroundingText(text, dbus.UInt32(cursor), dbus.UInt32(anchor))
        pump(0.05)

    def key(self, token, state=0, release=False):
        sym, code = KEYS[token]
        eaten = bool(self.ic.ProcessKeyEvent(dbus.UInt32(sym), dbus.UInt32(code + 8), dbus.UInt32(state),
                                             release, dbus.UInt32(0)))
        pump(0.05)
        if not eaten and not release and not (state & (CTRL | ALT)):
            self.field.app_key(token, bool(state & SHIFT))
        return eaten

    def type(self, tokens):
        for t in tokens.split():
            self.key(t)
            self.key(t, release=True)


# shout: HELLO world, then muhenkan by 無変換.
a = Client()
a.focus()
check_eq(str(controller.CurrentInputMethod()), IME, "current input method")
a.type("h e l l o")
check_eq(a.field.preedit, "HELLO", "henkan preedit")
state = read_state(IME)
check(state is not None and state.get("staged") == "HELLO" and state["host"]["frontend"] == "fcitx5",
      f"state file while composing ({state})")
a.type("Space w o r l d Tab Enter")
check_eq(a.field.text, "HELLO world\n", "HELLO world")
check_eq(a.field.preedit, "", "nothing marked after Enter")
a.type("muhenkan a b c Backspace")
check_eq(a.field.text, "HELLO world\nab", "muhenkan types JIS, the app the Backspace")
state = read_state(IME)
check(state is not None and state.get("mode") == "muhenkan" and state["host"]["app"] == "e2e",
      f"state file in muhenkan ({state})")
check(any('"muhenkan"' in js for _, js in signals), "a Changed signal carried the muhenkan state")

# 変換 enters henkan; again moves in the menu (⇧: back).
a.type("henkan h i")
check_eq(a.field.preedit, "HI", "henkan again")
a.type("henkan")
check_eq(a.field.preedit, "hi", "変換 moves in the menu")
a.key("henkan", SHIFT)
a.key("henkan", SHIFT, release=True)
check_eq(a.field.preedit, "HI", "⇧変換 moves back")

# Focus out commits what's staged.
a.ic.FocusOut()
pump()
check(a.field.text.endswith("HI"), f"focus out commits ({a.field.text!r})")

# A second input context follows the session's mode; 無変換 there switches for everyone.
b = Client(program="other")
b.focus()
b.type("muhenkan x")
check_eq(b.field.text, "x", "second context in muhenkan")
a.focus()
a.type("y")
check(a.field.text.endswith("y") and a.field.preedit == "", f"first context followed to muhenkan ({a.field.text!r})")

# 半角/全角 in muhenkan shouts the selection (need_selection → surrounding text).
c = Client(program="shouter")
c.focus()
c.field.text = "quiet please"
c.surrounding("quiet please", 12, 6)
c.type("zenkaku")
check_eq(c.field.text, "quiet PLEASE", "半角/全角 shouts the selection")

# A password field: nothing composes, nothing typed is published.
p = Client(caps=PREEDIT | FORMATTED | PASSWORD, program="pw")
p.focus()
p.type("henkan s e c r e t")
check_eq(p.field.preedit, "", "nothing composes in a password field")
check_eq(p.field.text, "secret", "a password field gets plain JIS characters")
state = read_state(IME)
check(state is not None and state["host"]["secure"] and "secret" not in str(state).lower(),
      f"the state hides a password field ({state})")
check(all("secret" not in js.lower() and "SECRET" not in js for _, js in signals), "no signal carried the password")

finish("fcitx5 e2e")
