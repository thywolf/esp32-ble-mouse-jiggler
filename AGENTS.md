# AGENTS.md

Agent guide for this repo; humans start at [README.md](README.md). Keep this file lean — it loads on every request, so details belong in `docs/` behind a link.

## Project

ESP32 BLE mouse jiggler firmware: advertises as a BLE HID mouse, jiggles the cursor on a timer, reports a simulated battery, and deep-sleeps after a configurable time. PlatformIO + Arduino for `az-delivery-devkit-v4` (ESP32-WROOM-32); app logic is in `src/main.cpp`.

## Commands

PlatformIO is not on PATH; use the penv copy:

- Windows: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`
- Linux/macOS: `~/.platformio/penv/bin/pio`

- Build: `pio run -e az-delivery-devkit-v4` — the only automated verification (no unit tests; `test/` is the untouched PlatformIO template). Must pass before committing.
- Flash: `pio run -t upload` — only on explicit request; hardware is usually not attached.
- Monitor: `pio device monitor` (115200 baud).

`pio run` can rewrite `.vscode/extensions.json`. Review what it adds and commit or revert it deliberately — never let it ride along in an unrelated commit.

## Gotchas

- `lib/BleMouse/` is a vendored fork of t-vk/ESP32-BLE-Mouse 0.3.1 patched for simultaneous multi-host connections. Do not re-add `t-vk/ESP32 BLE Mouse` to `lib_deps`, and do not "upgrade" the fork to upstream — the patches are the point.
- Toolchain versions are pinned in `platformio.ini`; bump deliberately. The BLE notes in [docs/ble-stack.md](docs/ble-stack.md) were verified against this exact version.

## Style

No formatter or linter is configured — match the surrounding code: 2-space indent, opening brace on the same line (`void setup() {`), camelCase functions/variables, SCREAMING_SNAKE_CASE constants, CRLF line endings.

## Commits

Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`). Small commits go straight onto `main`; use a branch + PR for larger features.

## Read before you touch

- [docs/ble-stack.md](docs/ble-stack.md) — Bluedroid quirks, the 4-host cap, callback traps. Read before any BLE or connection change.
- [docs/invariants.md](docs/invariants.md) — timers, movement, advertising/pairing, config bounds. Read before changing app behavior.
