# BLE stack notes

Verified against the pinned toolchain `espressif32@6.3.2` (arduino-esp32 2.0.9, Bluedroid). Re-verify before bumping.

- `CONFIG_BT_ACL_CONNECTIONS=4` (precompiled sdkconfig): hard cap of 4 simultaneous hosts. It is a Bluedroid compile-time constant, no workaround.
- `BLEServer::handleGATTServerEvent` invokes *both* `onConnect` overloads back to back (`onConnect(this)` then `onConnect(this, param)`) and both `onDisconnect` overloads. Override exactly one of each: `BleConnectionStatus` overrides only the param-taking `onConnect` and leaves the 1-arg version as the no-op default. Implementing both double-counts `connectionCount`, which then never returns to 0 — the app believes it is still connected (no advertising, no movement gate, `[hosts]` stuck at 2×).
- BLE server callbacks run in the Bluetooth task and fire *before* the framework's `getConnectedCount()` reflects the change. Use the library's `connectionCount`, not the framework counter (one-beat lag).
- Legacy advertising stops on the first connection and the framework never restarts it. `lib/BleMouse` re-applies it from `onConnect`/`onDisconnect` whenever the app asked (`BleMouse::setAdvertising`, backed by `advertisingEnabled`); the policy lives in the app, not the fork.
- `BLEServer::getGattsIf()` is private; use the public `BLEServer::disconnect(connId)` (`BleMouse::disconnectAll()` does).
- `BLEServer::updateConnParams(remote_bda, minInterval, maxInterval, latency, timeout)` is public and queues `esp_ble_gap_update_conn_params`. It must be called from the param-taking `onConnect` callback, which carries `param->connect.remote_bda`. Intervals are in 1.25 ms units, timeout in 10 ms. The stock 7.5–11.25 ms interval starves the single radio with 3+ hosts, so the fork requests 30–50 ms (min=24, max=40, latency=0, timeout=400).
- `BLEAdvertising::start()` is fully async in this core version (no blocking semaphore waits), so it is safe to call from BLE callbacks.
