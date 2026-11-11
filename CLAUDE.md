# bada-linux

- The Linux (Fcitx5 + IBus) host for bada-compatible engines. Source of truth: `docs/spec.md`
  (behaviour), `docs/api.md` (the state for other programs). If code and docs disagree, fix one in
  the same change. The engine contract is team-mirae/bada-compatible's `docs/bada-compatible-v1.md`
  (v1.1); keep up with it, don't fork it.
- One input method per IME, both modes inside it (spec §3). Don't split it into two.
- `src/core/` started as bada-windows' core and is **Linux's own**: don't keep it in step with
  bada-windows or port changes back and forth; follow bada-compatible. It stays free of Fcitx5 and
  IBus headers, so its tests run anywhere; key policy goes in `host.*`, session policy in `session.*`,
  not in the frontends.
- Mark facts measured (with the date) / e2e / **[measure]**; don't implement **[decide]** items
  silently. The Right Alt tap stays off by default (owner, 2026-11-11).
- `linux.id` in manifests is permanent.
- Names: bada (lowercase); muhenkan / henkan mode; bada-linux.
- License: CC0, all of it.

## Building

- Build and test in Docker, never on the host: `tools/test.sh` (shout from `../bada-compatible` or
  `$BADA_COMPATIBLE`; core tests plain and with ASan/UBSan; Fcitx5 and IBus end to end). All of it
  must pass. `tools/test.sh shell` for a shell in the container.
- A change to how keys or text move through a frontend needs an end-to-end check in
  `tests/e2e/` (both frameworks where it applies).
