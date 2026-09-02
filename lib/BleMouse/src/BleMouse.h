#ifndef ESP32_BLE_MOUSE_H
#define ESP32_BLE_MOUSE_H
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)

/*
 * Vendored fork of t-vk/ESP32-BLE-Mouse v0.3.1 (https://github.com/T-vK/ESP32-BLE-Mouse),
 * modified to support simultaneous connections from multiple BLE hosts
 * (up to CONFIG_BT_ACL_CONNECTIONS, which is 4 in the stock espressif32 Arduino core).
 */

#include "BleConnectionStatus.h"
#include "BLEHIDDevice.h"
#include "BLECharacteristic.h"

#define MOUSE_LEFT 1
#define MOUSE_RIGHT 2
#define MOUSE_MIDDLE 4
#define MOUSE_BACK 8
#define MOUSE_FORWARD 16
#define MOUSE_ALL (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE) # For compatibility with the Mouse library

class BleMouse {
private:
  uint8_t _buttons;
  BleConnectionStatus* connectionStatus;
  BLEHIDDevice* hid;
  BLECharacteristic* inputMouse;
  void buttons(uint8_t b);
  void rawAction(uint8_t msg[], char msgSize);
  static void taskServer(void *pvParameter);
public:
  BleMouse(std::string deviceName = "ESP32 Bluetooth Mouse", std::string deviceManufacturer = "Espressif", uint8_t batteryLevel = 100);
  void begin(void);
  void end(void);
  void click(uint8_t b = MOUSE_LEFT);
  void move(signed char x, signed char y, signed char wheel = 0, signed char hWheel = 0);
  void press(uint8_t b = MOUSE_LEFT);   // press LEFT by default
  void release(uint8_t b = MOUSE_LEFT); // release LEFT by default
  bool isPressed(uint8_t b = MOUSE_LEFT); // check LEFT by default
  bool isConnected(void);
  int getConnectedHosts(void);
  void setAdvertiseWhileConnected(bool enable);
  void startAdvertising(void);
  void stopAdvertising(void);
  void disconnectAll(void);
  void setBatteryLevel(uint8_t level);
  uint8_t batteryLevel;
  std::string deviceManufacturer;
  std::string deviceName;
protected:
  virtual void onStarted(BLEServer *pServer) { };
};

#endif // CONFIG_BT_ENABLED
#endif // ESP32_BLE_MOUSE_H
