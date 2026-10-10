# Behavioral invariants

Rules that break silently if not preserved. Read before changing app behavior.

## Timers

- All timers anchor to `bootMillis`, captured at the top of `setup()`: movement ticks, the deep-sleep countdown, and the simulated battery (linear 100% → 0% over the sleep window). Keep them correlated.
- `sleep` is capped at 43200 minutes so `minutes * 60000` stays within 32-bit `millis()` arithmetic.
- The sleep timer starts at boot and is not reset by configuration changes or reconnections; it also keeps running while the serial console is open. This is by design — the device has a hard "off" time, not a "last activity" timeout.

## Movement

- A HID report of (0, 0) is never sent: in `random`/`pulse` the *corrected* offsets are rejection-resampled until at least one axis is non-zero — the correction happens inside the redraw loop, so it can never be skipped; `glide` steps are non-zero by construction (heading never (0, 0), amplitude at least 1).
- Zero-net invariant: the device's own net displacement must stay bounded on each axis, so the cursor is roughly where the user left it after any length of session. A zero-*mean* offset is not sufficient — that still random-walks (σ ≈ 0.58 × dist × √n ≈ 25 × dist over a workday). `random`/`pulse` enforce it per report via `correctAxis`: cancel the accumulated residual, capped at ±dist, so net after a report equals the sampled offset whenever |sample − net| ≤ dist (hence |net| ≤ dist in steady state) and any larger residual — e.g. from an interrupted glide run — shrinks by at least dist per report. `glide` enforces it structurally: the return run must reuse the outbound run's step count (`glideOutSteps`), so each out/back pair nets exactly (0, 0).
- Profiles (`set mode`, default `random`): the `random`/`glide` gate reads `period` live so `set period` keeps applying immediately. `pulse` and `glide` may vary cadence and direction but must never leave any host silent for longer than max(600 ms, 1.25 × period): the pulse cycle is jittered to [0.75, 1.25] × period, intra-burst steps are 300–600 ms, and a period too short to fit a burst degrades to one report per cycle. Keeping every host awake is the device's core job — a profile that could risk an idle timeout is a bug.

## Advertising and pairing

- Advertising is decided once per loop in `src/main.cpp`: on while no host is connected, on while pairing mode is on, and on for a grace window (`ADVERTISE_GRACE_MS`, 60 s) after boot and after *every* change of the connected-host count. That window is what lets a host which dropped mid-session reconnect by itself, and it requires a free slot (`< CONFIG_BT_ACL_CONNECTIONS`).
- Pairing mode is itself timed: `setPairingMode` sets `pairingModeEnd` from `pairingTimeout` (`set pairing <seconds>`, default 60 s, aliased to `ADVERTISE_GRACE_MS`), and `loop()` calls `setPairingMode(false)` when it elapses, so a forgotten press cannot leave the device discoverable forever. `setPairingMode(false)` from any path also closes the reconnect window at once (`advertiseGraceEnd = 0`).
- Boot button: short press (acted on release) toggles pairing mode; turning it off closes the window immediately. A 3 s hold toggles the serial console.

## Configuration

- Parameter bounds live in three places that must stay in sync: `setConfig` in `src/main.cpp`, the usage text it prints, and `README.md`. Bounds: period 100–60000 ms, dist 1–127 px, mode random/pulse/glide, sleep 5–43200 min, pairing 5–600 s, name/manu 3–29 chars.
- Parsing goes through `parseUnsigned`, which rejects signs (`strtoul` would wrap negatives into huge values).
- `period`/`dist`/`sleep`/`pairing`/`mode` apply immediately; `name`/`manu` only apply after a reboot (the BLE stack is initialized in `setup()`). NVS namespace `ble-mouse` (keys: `period`, `dist`, `sleep`, `pairing`, `name`, `manu`, `mode`). An unknown/corrupt `mode` value falls back to `random`.
- Unsaved changes are lost on reboot; use `save` to persist them.
