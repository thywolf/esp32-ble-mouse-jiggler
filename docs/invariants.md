# Behavioral invariants

Rules that break silently if not preserved. Read before changing app behavior.

## Timers

- All timers anchor to `bootMillis`, captured at the top of `setup()`: movement ticks, the deep-sleep countdown, and the simulated battery (linear 100% → 0% over the sleep window). Keep them correlated.
- `sleep` is capped at 43200 minutes so `minutes * 60000` stays within 32-bit `millis()` arithmetic.
- The sleep timer starts at boot and is not reset by configuration changes or reconnections; it also keeps running while the serial console is open. This is by design — the device has a hard "off" time, not a "last activity" timeout.

## Movement

- A HID report of (0, 0) is never sent: movement offsets are rejection-resampled until at least one axis is non-zero.

## Advertising and pairing

- Advertising is decided once per loop in `src/main.cpp`: on while no host is connected, on while pairing mode is on, and on for a grace window (`ADVERTISE_GRACE_MS`, 60 s) after boot and after *every* change of the connected-host count. That window is what lets a host which dropped mid-session reconnect by itself, and it requires a free slot (`< CONFIG_BT_ACL_CONNECTIONS`).
- Pairing mode is itself timed: `setPairingMode` sets `pairingModeEnd` from `pairingTimeout` (`set pairing <seconds>`, default 60 s, aliased to `ADVERTISE_GRACE_MS`), and `loop()` calls `setPairingMode(false)` when it elapses, so a forgotten press cannot leave the device discoverable forever. `setPairingMode(false)` from any path also closes the reconnect window at once (`advertiseGraceEnd = 0`).
- Boot button: short press (acted on release) toggles pairing mode; turning it off closes the window immediately. A 3 s hold toggles the serial console.

## Configuration

- Parameter bounds live in three places that must stay in sync: `setConfig` in `src/main.cpp`, the usage text it prints, and `README.md`. Bounds: period 100–60000 ms, dist 1–127 px, sleep 5–43200 min, pairing 5–600 s, name/manu 3–29 chars.
- Parsing goes through `parseUnsigned`, which rejects signs (`strtoul` would wrap negatives into huge values).
- `period`/`dist`/`sleep`/`pairing` apply immediately; `name`/`manu` only apply after a reboot (the BLE stack is initialized in `setup()`). NVS namespace `ble-mouse` (keys: `period`, `dist`, `sleep`, `pairing`, `name`, `manu`).
- Unsaved changes are lost on reboot; use `save` to persist them.
