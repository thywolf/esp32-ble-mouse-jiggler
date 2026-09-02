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
- Legacy advertising stops on the first connection and the framework never restarts it. `lib/BleMouse` restarts advertising in `onConnect`/`onDisconnect`, gated by `advertiseWhileConnected` (the pairing-mode switch).
- BLE server callbacks run in the Bluetooth task and fire *before* the framework's `getConnectedCount()` reflects the change. Rely on the library's own `connectionCount`, not the framework counter — there's a one-beat lag.
- `BLEAdvertising::start()` is fully async in this core version (no blocking semaphore waits), so calling it from BLE callbacks is safe.
- `BLEServer::getGattsIf()` is private; use the public `BLEServer::disconnect(connId)` instead (`BleMouse::disconnectAll()` does).

## Behavioral invariants

- All timers anchor to `bootMillis`, captured at the top of `setup()`: movement ticks, the deep-sleep countdown, and the simulated battery (linear 100% → 0% over the sleep window). Keep them correlated when touching any one of them.
- `sleep` is capped at 43200 minutes because `minutes * 60000` must stay within 32-bit `millis()` arithmetic. (Trust me, I've seen millis() overflow bugs. They're Saturday-night-debugging material.)
- Parameter bounds live in three places that must stay in sync: `setConfig` in `src/main.cpp`, the usage text it prints, and `README.md`. Bounds: period 100–60000 ms, sleep 5–43200 min, name/manu 3–29 chars. Parsing goes through `parseUnsigned`, which rejects signs (strtoul would wrap negatives into huge values).
- `period`/`sleep` apply immediately; `name`/`manu` only apply after a reboot (BLE stack initialized in `setup()`). NVS namespace is `ble-mouse` (keys: `period`, `sleep`, `name`, `manu`).
- Pairing mode is ON for the first 60 s after boot (`PAIRING_GRACE_MS`, lets already-bonded hosts reconnect since legacy advertising stops on the first connection), then OFF. Boot button: short press (acted on release) toggles the serial console, 3 s hold toggles pairing mode.
- The sleep timer starts at boot and is not reset by configuration changes or reconnections. It also keeps running while the serial console is open. (This is by design — the device has a hard "off" time, not a "last activity" timeout.)
- Unsaved changes are lost on reboot. Use `save` to persist them.
