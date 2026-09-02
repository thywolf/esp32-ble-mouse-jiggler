# ESP32 BLE Mouse Jiggler

A Bluetooth (BLE) mouse jiggler for the ESP32. It emulates a wireless mouse and nudges the cursor at a fixed interval, so paired machines never go idle — no software or drivers needed on the computer. It just pairs like a regular mouse.

Written for the AZ-Delivery DevKit V4 (ESP32-WROOM-32) using PlatformIO, with a vendored fork of [t-vk/ESP32-BLE-Mouse](https://github.com/T-vK/ESP32-BLE-Mouse) (in `lib/BleMouse`, based on 0.3.1) and [philj404/SimpleSerialShell](https://github.com/philj404/SimpleSerialShell).

## How it works

- Emulates a standard BLE HID mouse. Every `period` milliseconds it moves the cursor by a random offset of ±1 pixel on each axis.
- Reports a simulated battery level that drains linearly from 100% at boot to 0% when the sleep timer runs out.
- After `sleep` minutes (default 480 = 8 hours, counted from boot) it disconnects all hosts and enters deep sleep. Wake it by pressing the **Boot** (or EN/reset) button or re-plugging USB power.
- Once flashed and configured, the device only needs power — any USB port or power bank works. (Note: many power banks cut their output below a current threshold, which can end a session early.)

## Multi-host support

The mouse can stay connected to up to 4 laptops simultaneously (a limit of the ESP32 Bluetooth stack). All connected hosts receive the cursor movements and battery updates, so every paired machine is kept awake at the same time.

Pair each laptop as described below. While hosts are connected, enable pairing mode to make the device discoverable for the next one. Whenever the device is idle, advertising runs automatically, so paired laptops reconnect after disconnects, reboots, or being out of range. The `get` command shows how many hosts are currently connected.

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

- Press and hold the **Boot** button for 3 seconds → pairing mode **on**: the device stays discoverable even while hosts are connected.
- Press and hold it again for 3 seconds → pairing mode **off**.

For the first 60 seconds after boot, the device behaves as if pairing mode were **on**, so all already-paired laptops can reconnect at once. After that it locks automatically and is no longer discoverable to new hosts while connected. Holding **Boot** for 3 seconds during that window locks it immediately.

Pairing mode does not persist across reboots: after a power cycle the device starts with the 60-second grace window and then returns to off. A short press of the **Boot** button (under 3 seconds) toggles the serial console instead. The current state is shown by the `get` command (`Pairing [mode]`).

If the mouse doesn't show up in the scan list, enable pairing mode with a long press of **Boot**, toggle Bluetooth on the host and search again, or briefly press the reset button on the board.

## Configuration

Configuration happens over a serial console (115200 baud) that is off by default. A short press of the **Boot** button on the board switches it on or off at any time. (A 3-second hold toggles pairing mode instead — see Pairing.) While the console is open, mouse movements are paused.

Commands:

```
  exit  - Reboots the device
  get   - Displays current configuration
  help  - Displays available commands
  load  - Loads stored configuration
  save  - Saves current configuration
  set   - Sets parameter to a value
```

Configurable parameters:

```
  period - Time between movements (in ms, 100-60000)
   sleep - Time until deep sleep (in minutes, 5-43200)
    name - Advertised device name (string, 3-29 chars)
    manu - Advertised device manufacturer (string, 3-29 chars)
```

Notes:

- `set period ...` and `set sleep ...` take effect immediately; `set name ...` and `set manu ...` only apply after a reboot, because the BLE stack is initialized at boot.
- The sleep timer starts at boot and is not reset by configuration changes or reconnections. It also keeps running while the serial console is open.
- Unsaved changes are lost on reboot. Use `save` to persist them, and `exit` (or the reset button) to reboot.
- Defaults: period `15000`, sleep `480` (8 hours), name `Wobbly BLE Mouse`, manufacturer `ESP32`.

Example session:

```
> set name "Wobbly BLE Mouse"
> set period 15000
> save
> exit
```

## Disclaimer

For use on machines you own or are authorized to control.
