# ESP32 BLE Mouse Jiggler

A configurable Bluetooth (BLE) mouse jiggler for the ESP32. The device emulates a wireless mouse and nudges the cursor at a fixed interval, so paired machines never go idle — no software or drivers needed on the computer, it just pairs like a regular mouse.

Written for the AZ-Delivery DevKit V4 (ESP32-WROOM-32) using PlatformIO, with a vendored fork of [t-vk/ESP32-BLE-Mouse](https://github.com/T-vK/ESP32-BLE-Mouse) (in `lib/BleMouse`, based on 0.3.1) and [philj404/SimpleSerialShell](https://github.com/philj404/SimpleSerialShell).

## How it works

- Emulates a standard BLE HID mouse. Every `period` milliseconds it moves the cursor by a random offset of ±1 pixel on each axis.
- Reports a simulated battery level (starts at 50 %, random walk within 20–80 %) to look like a real wireless mouse.
- Once flashed and configured, the device only needs power — any USB port or power bank works.

## Multi-host support

The mouse can stay connected to up to 4 laptops simultaneously (a limit of the ESP32 Bluetooth stack). All connected hosts receive the cursor movements and battery updates, so every paired machine is kept awake at the same time.

Pair each laptop as described below. Advertising restarts automatically after disconnects and reboots, so any paired laptop can (re)connect at any time. The `get` command displays the number of currently connected hosts.

## Building and flashing

Install [PlatformIO](https://platformio.org/), then from the project root:

```
pio run -t upload        # build and flash over USB
pio device monitor       # serial monitor at 115200 baud
```

The project uses the `huge_app` partition table to fit the Bluetooth stack.

## Pairing

The device boots straight into mouse mode and starts advertising. Pair it in your operating system's Bluetooth settings like any other mouse — the advertised name defaults to `Wobbly BLE Mouse`:

- **Windows:** Settings → Bluetooth & devices → Add device → Bluetooth, then select the mouse.
- **macOS:** System Settings → Bluetooth, then click **Connect** when the mouse appears.
- **Linux:** the Bluetooth panel of your desktop environment, or `bluetoothctl`:

  ```
  scan on
  pair <device-mac>
  trust <device-mac>
  ```

You can pair additional laptops while others are connected — repeat the steps on each machine, up to 4 in total. Once paired, hosts reconnect automatically after a reboot or after being out of range.

If the mouse doesn't show up in the scan list, toggle Bluetooth on the host and search again, or briefly press the reset button on the board.

## Configuration

Configuration is done over a serial console (115200 baud) that is off by default: press the **Boot** button on the board to switch it on or off at any time. While the console is open, mouse movements are paused.

Commands available are:
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
  period - Time between movements (in ms, min. 100)
    name - Advertised device name (string, 3-29 chars)
    manu - Advertised device manufacturer (string, 3-29 chars)
```

Notes:

- `set period ...` takes effect immediately; `set name ...` and `set manu ...` only apply after a reboot, because the BLE stack is initialized at boot.
- Unsaved changes are lost on reboot. Use `save` to persist them, and `exit` (or the reset button) to reboot.
- Defaults: period `15000`, name `Wobbly BLE Mouse`, manufacturer `ESP32`.

Example session:

```
> set name "Wobbly BLE Mouse"
> set period 15000
> save
> exit
```

## Disclaimer

For use on machines you own or are authorized to control.
