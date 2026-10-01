#include "BleConnectionStatus.h"

BleConnectionStatus::BleConnectionStatus(void) {
}

void BleConnectionStatus::onConnect(BLEServer* pServer)
{
  this->connectionCount++;
  this->connected = true;
  BLE2902* desc = (BLE2902*)this->inputMouse->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
  desc->setNotifications(true);
  // Advertising stops as soon as a host connects; re-apply it when the
  // application still wants the device to be discoverable (multi-host support)
  if (this->advertisingEnabled) {
    pServer->getAdvertising()->start();
  }
}

void BleConnectionStatus::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param)
{
  // Request longer connection intervals (30-50 ms) with no slave latency
  // and a 4 s supervision timeout. The stock 7.5-11.25 ms interval starves
  // the single ESP32 radio when 3+ hosts are connected, causing random drops.
  //   min=24 -> 30 ms, max=40 -> 50 ms, latency=0, timeout=400 -> 4000 ms
  pServer->updateConnParams(param->connect.remote_bda, 24, 40, 0, 400);
}

void BleConnectionStatus::onDisconnect(BLEServer* pServer)
{
  if (this->connectionCount > 0) {
    this->connectionCount--;
  }
  if (this->connectionCount == 0) {
    this->connected = false;
    BLE2902* desc = (BLE2902*)this->inputMouse->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(false);
  }
  // Neither the library nor the framework restarts advertising on disconnect.
  // Re-apply the application's wish, so a host that dropped mid-session can
  // come back; the application decides how long that window stays open.
  if (this->advertisingEnabled) {
    pServer->getAdvertising()->start();
  }
}
