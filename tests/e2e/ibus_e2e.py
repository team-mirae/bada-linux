"""End to end through IBus: a fresh ibus-daemon that starts bada-ibus from the installed component
XML, driven through IBus input contexts as an app's input module drives them.

Run inside a session bus, with ibus-daemon started by tools/ci.sh."""

import gi

gi.require_version("IBus", "1.0")
from gi.repository import IBus  # noqa: E402

from common import (KEYS, SHIFT, TextField, check, check_eq, finish, pump, read_state, wait_for)  # noqa: E402

IME = "bada-shout"
RELEASE = IBus.ModifierType.RELEASE_MASK

IBus.init()
bus = IBus.Bus()
wait_for(bus.is_connected, 20, "ibus-daemon")
names = [e.get_name() for e in bus.list_engines()]
check(IME in names, f"{IME} is listed ({names})")


class Client:
    def __init__(self, purpose=IBus.InputPurpose.FREE_FORM, name="e2e"):
        self.ic = bus.create_input_context(name)
        self.field = TextField()
        f = self.field
        self.ic.set_capabilities(IBus.Capabilite.PREEDIT_TEXT | IBus.Capabilite.FOCUS |
                                 IBus.Capabilite.SURROUNDING_TEXT)
        self.ic.connect("commit-text", lambda ic, t: f.commit(t.get_text()))
        self.ic.connect("update-preedit-text", lambda ic, t, cursor, visible: f.set_preedit(t.get_text() if visible else ""))
        self.ic.connect("hide-preedit-text", lambda ic: f.set_preedit(""))
        self.ic.connect("delete-surrounding-text", lambda ic, offset, n: f.delete_before(n))
        self.purpose = purpose

    def focus(self):
        # Engines are switched the way panels do it: the global engine (an input context's own
        # SetEngine is ignored by ibus-daemon 1.5).
        engine = bus.get_global_engine()
        if engine is None or engine.get_name() != IME:
            bus.set_global_engine(IME)
            pump(0.3)
        self.ic.focus_in()
        self.ic.set_content_type(self.purpose, 0)
        pump(0.3)

    def surrounding(self, text, cursor, anchor):
        self.ic.set_surrounding_text(IBus.Text.new_from_string(text), cursor, anchor)
        pump(0.05)

    def key(self, token, state=0):
        sym, code = KEYS[token]
        eaten = self.ic.process_key_event(sym, code, state)
        pump(0.05)
        if not eaten and not (state & RELEASE):
            self.field.app_key(token, bool(state & SHIFT))
        return eaten

    def type(self, tokens):
        for t in tokens.split():
            self.key(t)
            self.key(t, RELEASE)


a = Client()
a.focus()
wait_for(lambda: a.ic.get_engine() is not None and a.ic.get_engine().get_name() == IME, 20, "the engine on the context")
a.type("h e l l o")
check_eq(a.field.preedit, "HELLO", "henkan preedit")
state = read_state(IME)
check(state is not None and state.get("staged") == "HELLO" and state["host"]["frontend"] == "ibus",
      f"state file while composing ({state})")
a.type("Space w o r l d Tab Enter")
check_eq(a.field.text, "HELLO world\n", "HELLO world")
a.type("muhenkan a b c Backspace")
check_eq(a.field.text, "HELLO world\nab", "muhenkan types JIS, the app the Backspace")
state = read_state(IME)
check(state is not None and state.get("mode") == "muhenkan", f"state file in muhenkan ({state})")

a.type("henkan h i henkan")
check_eq(a.field.preedit, "hi", "変換 moves in the menu")
a.ic.focus_out()
pump()
check(a.field.text.endswith("hi"), f"focus out commits ({a.field.text!r})")

# A second input context follows the session's mode.
b = Client(name="other")
b.focus()
b.type("muhenkan x")
check_eq(b.field.text, "x", "second context in muhenkan")
a.focus()
a.type("y")
check(a.field.text.endswith("y") and a.field.preedit == "", f"first context followed to muhenkan ({a.field.text!r})")

# 半角/全角 in muhenkan shouts the selection.
c = Client(name="shouter")
c.focus()
c.field.text = "quiet please"
c.surrounding("quiet please", 12, 6)
c.type("zenkaku")
check_eq(c.field.text, "quiet PLEASE", "半角/全角 shouts the selection")

# A password field.
p = Client(purpose=IBus.InputPurpose.PASSWORD, name="pw")
p.focus()
p.type("henkan s e c r e t")
check_eq(p.field.preedit, "", "nothing composes in a password field")
check_eq(p.field.text, "secret", "a password field gets plain JIS characters")
state = read_state(IME)
check(state is not None and state["host"]["secure"] and "secret" not in str(state).lower(),
      f"the state hides a password field ({state})")

finish("ibus e2e")
