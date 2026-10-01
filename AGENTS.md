# AGENTS.md

Guidance for AI coding agents working in this repository. This doc exists so that the next session (whether it's you or another agent) doesn't have to rediscover the same ESP32 BLE stack quirks the hard way.

## Project

ESP32 BLE mouse jiggler firmware: the device advertises as a BLE HID mouse, jiggles the cursor on a timer, reports a simulated battery, and enters deep sleep after a configurable time. Built with PlatformIO (Arduino framework) for the `az-delivery-devkit-v4` board (ESP32-WROOM-32). Work happens on feature branches, PRs into `main`.

## Commands

The PlatformIO CLI is not on PATH; use the penv copy:

- Windows: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`
- Linux/macOS: `~/.platformio/penv/bin/pio`

- Build: `pio run -e az-delivery-devkit-v4`
- Flash: `pio run -t upload` — only on explicit request; hardware is usually not attached
- Serial monitor: `pio device monitor` (115200 baud)

The build is the only automated verification; there are no unit tests (`test/` is the untouched PlatformIO template). `pio run` must pass before committing. Note that building rewrites `.vscode/extensions.json` (line endings only, no content change) — revert it instead of committing.

## Layout

- `src/main.cpp` — application: `APP_BLE`/`APP_SERIAL` state machine, Boot-button ISR with short/long-press gestures, sleep timer, simulated battery, serial shell commands (`get`/`set`/`save`/`load`/`exit`)
- `src/quotedTokenizer.{h,cpp}` — `strtok_r`-compatible tokenizer honoring double quotes
- `lib/BleMouse/` — vendored fork of t-vk/ESP32-BLE-Mouse v0.3.1 (MIT) patched for simultaneous multi-host connections. Do not re-add `t-vk/ESP32 BLE Mouse` to `lib_deps` and do not "upgrade" the fork to upstream; the patches are the point.

## BLE stack facts (espressif32 6.3.2 / arduino-esp32 2.0.x, Bluedroid)

Verified against the framework sources. These are the things that bite when you assume the BLE stack works the way the spec implies:

- `CONFIG_BT_ACL_CONNECTIONS=4` in the precompiled sdkconfig: hard cap of 4 simultaneous BLE hosts. No workaround, it's a Bluedroid compile-time constant.
- Legacy advertising stops on the first connection and the framework never restarts it. `lib/BleMouse` re-applies advertising in `onConnect`/`onDisconnect` whenever the application asked for it (`BleMouse::setAdvertising`, backed by `advertisingEnabled`); the policy itself lives in the app, not the fork.
- BLE server callbacks run in the Bluetooth task and fire *before* the framework's `getConnectedCount()` reflects the change. Rely on the library's own `connectionCount`, not the framework counter — there's a one-beat lag.
- `BLEAdvertising::start()` is fully async in this core version (no blocking semaphore waits), so calling it from BLE callbacks is safe.
- `BLEServer::getGattsIf()` is private; use the public `BLEServer::disconnect(connId)` instead (`BleMouse::disconnectAll()` does).

## Behavioral invariants

- All timers anchor to `bootMillis`, captured at the top of `setup()`: movement ticks, the deep-sleep countdown, and the simulated battery (linear 100% → 0% over the sleep window). Keep them correlated when touching any one of them.
- A HID movement report of (0, 0) is never sent: with both axes at 0 nothing would move on the hosts, so movement offsets are rejection-resampled until at least one axis is non-zero. Keep this invariant when touching movement code.
- `sleep` is capped at 43200 minutes because `minutes * 60000` must stay within 32-bit `millis()` arithmetic. (Trust me, I've seen millis() overflow bugs. They're Saturday-night-debugging material.)
- Parameter bounds live in three places that must stay in sync: `setConfig` in `src/main.cpp`, the usage text it prints, and `README.md`. Bounds: period 100–60000 ms, dist 1–127 px, sleep 5–43200 min, name/manu 3–29 chars. Parsing goes through `parseUnsigned`, which rejects signs (strtoul would wrap negatives into huge values).
- `period`/`dist`/`sleep` apply immediately; `name`/`manu` only apply after a reboot (BLE stack initialized in `setup()`). NVS namespace is `ble-mouse` (keys: `period`, `dist`, `sleep`, `name`, `manu`).
- Advertising is decided once per loop in `src/main.cpp`: on while no host is connected, on while pairing mode is on, and on for a grace window (`ADVERTISE_GRACE_MS`, 60 s) after boot and after *every* change of the connected-host count. That window is what lets a host which dropped mid-session reconnect by itself — without it the device would sit unreachable until someone pressed **Boot**, and it also requires a free slot (`< CONFIG_BT_ACL_CONNECTIONS`). Boot button: short press (acted on release) toggles pairing mode (turning it off closes the window at once), 3 s hold toggles the serial console.
- Pairing mode is itself timed: `pairingModeEnd` is set in `setPairingMode` (`PAIRING_TIMEOUT_MS`, 60 s, aliased to `ADVERTISE_GRACE_MS`), and `loop()` calls `setPairingMode(false)` when it elapses — so a forgotten short press cannot leave the device discoverable forever. `setPairingMode(false)` from any path also closes the reconnect window at once (`advertiseGraceEnd = 0`).
- The sleep timer starts at boot and is not reset by configuration changes or reconnections. It also keeps running while the serial console is open. (This is by design — the device has a hard "off" time, not a "last activity" timeout.)
- Unsaved changes are lost on reboot. Use `save` to persist them.
