# ESP32 BLE Mouse Jiggler

A Bluetooth (BLE) mouse jiggler for the ESP32. It emulates a wireless mouse and nudges the cursor at a fixed interval, so paired machines never go idle — no software or drivers needed on the computer. It just pairs like a regular mouse.

Written for the AZ-Delivery DevKit V4 (ESP32-WROOM-32) using PlatformIO, with a vendored fork of [t-vk/ESP32-BLE-Mouse](https://github.com/T-vK/ESP32-BLE-Mouse) (in `lib/BleMouse`, based on 0.3.1) and [philj404/SimpleSerialShell](https://github.com/philj404/SimpleSerialShell).

## How it works

- Emulates a standard BLE HID mouse. Every `period` milliseconds it moves the cursor by an offset on each axis, bounded by `±dist` px, corrected so the jiggle's own net displacement stays near zero — see Movement profiles. A (0, 0) report is never sent: if both axes come up 0, the offsets are redrawn until at least one axis moves, so every report moves the cursor.
- Reports a simulated battery level that drains linearly from 100% at boot to 0% when the sleep timer runs out.
- After `sleep` minutes (default 480 = 8 hours, counted from boot) it disconnects all hosts and enters deep sleep. Wake it by pressing the **Boot** (or EN/reset) button or re-plugging USB power.
- Once flashed and configured, the device only needs power — any USB port or power bank works. (Note: many power banks cut their output below a current threshold, which can end a session early.)

## Movement profiles

`set mode` chooses how the cursor is nudged (default `random`):

- `random` — one random offset per `period`, each report canceling the residual of the previous ones.
- `pulse` — short bursts of 2–4 nudges 300–600 ms apart followed by a quiet gap; each cycle is jittered to 0.75–1.25 × `period`. Same residual cancellation as `random`.
- `glide` — the cursor walks a short path in one direction and then retraces it: 6–15 steps per run at the normal `period` cadence, each step up to `dist / 4` px (at least 1 px). The return run always uses the same number of steps as the outbound run, so each out/back pair nets exactly zero.

The profiles only change *how* the movement looks, never *whether* it happens. Every profile reports at least once per cycle, and the longest silence any of them produces is max(600 ms, 1.25 × `period`) — orders of magnitude below any host's idle timeout. Keeping every machine awake while you work on the others is the point, so a profile that could risk an idle timeout would be a bug.

### No drift

Each profile keeps the net displacement *it* sends bounded on both axes: `random` and `pulse` cancel their accumulated residual on every report (our own contribution never moves the cursor more than about `dist` px from where it started), and `glide` nets exactly zero over every out/back pair. Leave the mouse in a corner of the screen and come back after 1, 2 or 6 hours — it is still roughly where you parked it, plus whatever you did with it yourself. (Only the host's screen-edge clamping can shift this, and only if the cursor was already within `dist` px of an edge.) A plain zero-*average* offset would not be enough: that still lets the cursor random-walk ≈ 25 × `dist` px over a workday.

## Multi-host support

The mouse can stay connected to up to 4 laptops simultaneously (a limit of the ESP32 Bluetooth stack). All connected hosts receive the cursor movements and battery updates, so every paired machine is kept awake at the same time.

To reduce radio contention when multiple hosts are connected, the device requests longer BLE connection intervals (30–50 ms, no slave latency, 4 s supervision timeout) after each host connects, instead of the default 7.5–11.25 ms. This is a *request* — the host may grant a different interval — but it frees enough air time on the single ESP32 radio to keep three or more hosts stable.

Pair each laptop as described below. While hosts are connected, enable pairing mode to make the device discoverable for the next one.

The device advertises whenever no host is connected, and every change in the number of connected hosts re-opens a 60-second reconnect window. A laptop that drops out — a radio hiccup, a brief trip out of range, a reboot — therefore comes back on its own instead of waiting for you to press **Boot**. A host that stays away longer than the window can only return once the other hosts disconnect, or after you turn pairing mode on.

The `get` command shows how many hosts are currently connected and whether the device is advertising right now.

## Building and flashing

Install [PlatformIO](https://platformio.org/), then from the project root:

```
pio run -t upload        # build and flash
pio run                  # build only
pio device monitor       # serial console (115200 baud)
```

The device connects via BLE, so you only need to flash it once. After that, configuration happens over the serial console.

## Pairing

To pair an additional laptop while others are connected, use pairing mode:

- Short-press the **Boot** button → pairing mode **on**: the device stays discoverable even while hosts are connected. It switches itself off again after the configured `pairing` timeout (default 60 seconds), so a forgotten press cannot leave it discoverable forever.
- Short-press it again → pairing mode **off**.

For 60 seconds after boot — and again after every change in the number of connected hosts — the device behaves as if pairing mode were **on**, so already-paired laptops can reconnect. After that it locks automatically and is no longer discoverable to new hosts while connected. Short-pressing **Boot** toggles pairing mode off and closes the window immediately.

Pairing mode does not persist across reboots and times out after the configured `pairing` duration (default 60 seconds): after a power cycle the device starts with the fixed 60-second grace window and then returns to off. A 3-second hold of the **Boot** button toggles the serial console instead. The current state is shown by the `get` status header: one `Status:` line with `[battery]`, `[hosts]`, `[pairing]` (including the seconds left while it is on), `[advertising]` and `[sleep]` left, followed by the configuration table.

If the mouse doesn't show up in the scan list, enable pairing mode with a short press of **Boot**, toggle Bluetooth on the host and search again, or briefly press the reset button on the board.

## Configuration

Configuration happens over a serial console (115200 baud) that is off by default. A 3-second hold of the **Boot** button on the board switches it on or off at any time. (A short press toggles pairing mode instead — see Pairing.) While the console is open, mouse movements are paused.

Commands:

```
  exit   - Reboots the device
  get    - Displays current configuration
  help   - Displays available commands
  load   - Loads stored configuration
  ping   - Responds with pong and device uptime
  save   - Saves current configuration
  set    - Sets parameter to a value
  uptime - Responds with pong and device uptime
```

Configurable parameters:

```
  period  - Time between movements (in ms, 100-60000)
    dist  - Max movement distance per axis (in px, 1-127)
    mode  - Movement profile: random, pulse or glide
   sleep  - Time until deep sleep (in minutes, 5-43200)
 pairing  - Pairing mode duration (in seconds, 5-600)
    name  - Advertised device name (string, 3-29 chars)
    manu  - Advertised device manufacturer (string, 3-29 chars)
```

Notes:

- `set period ...`, `set dist ...`, `set mode ...`, `set sleep ...` and `set pairing ...` take effect immediately; `set name ...` and `set manu ...` only apply after a reboot, because the BLE stack is initialized at boot.
- The sleep timer starts at boot and is not reset by configuration changes or reconnections. It also keeps running while the serial console is open.
- Unsaved changes are lost on reboot. Use `save` to persist them, and `exit` (or the reset button) to reboot.
- Defaults: period `15000`, dist `1`, mode `random`, sleep `480` (8 hours), pairing `60` (s), name `Wobbly BLE Mouse`, manufacturer `ESP32`.

Example session:

```
> set name "Wobbly BLE Mouse"
> set period 15000
> save
> exit
```

## Disclaimer

For use on machines you own or are authorized to control.
