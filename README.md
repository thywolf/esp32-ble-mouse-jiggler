**ESP32 BLE Mouse Jiggler**

A configurable Bluetooth mouse jiggler written for az-delivery-devkit-v4 using t-vk/ESP32 BLE Mouse@^0.3.1 and philj404/SimpleSerialShell@^1.0.0 libraries.

Configuration can be done via serial USB (115200, not active by default, but can be turned on/off during runtime by pressing the Boot button on the board).

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
