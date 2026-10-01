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
  // Set by the application. When true the callbacks re-apply advertising after
  // a connection change (legacy advertising stops on the first connection);
  // when false the device stays hidden while it is serving hosts.
  volatile bool advertisingEnabled = true;
  void onConnect(BLEServer* pServer);
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param);
  void onDisconnect(BLEServer* pServer);
  BLECharacteristic* inputMouse;
  BLEServer* pServer = 0;
};

#endif // CONFIG_BT_ENABLED
#endif // ESP32_BLE_CONNECTION_STATUS_H
