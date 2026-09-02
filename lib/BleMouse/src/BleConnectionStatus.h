#ifndef ESP32_BLE_CONNECTION_STATUS_H
#define ESP32_BLE_CONNECTION_STATUS_H
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)

#include <BLEServer.h>
#include "BLE2902.h"
#include "BLECharacteristic.h"

class BleConnectionStatus : public BLEServerCallbacks
{
public:
  BleConnectionStatus(void);
  // Written from the Bluetooth callback task, read from the Arduino loop task
  volatile int connectionCount = 0;
  volatile bool connected = false;
  // When false, advertising is only started again once the last host
  // disconnects, hiding the device from new hosts while it is serving one
  volatile bool advertiseWhileConnected = true;
  void onConnect(BLEServer* pServer);
  void onDisconnect(BLEServer* pServer);
  BLECharacteristic* inputMouse;
  BLEServer* pServer = 0;
};

#endif // CONFIG_BT_ENABLED
#endif // ESP32_BLE_CONNECTION_STATUS_H
