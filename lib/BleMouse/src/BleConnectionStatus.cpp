#include "BleConnectionStatus.h"

BleConnectionStatus::BleConnectionStatus(void) {
}

void BleConnectionStatus::onConnect(BLEServer* pServer)
{
  this->connectionCount++;
  this->connected = true;
  BLE2902* desc = (BLE2902*)this->inputMouse->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
  desc->setNotifications(true);
  // Advertising stops on connect; restart it so further hosts can connect
  // while this one stays connected (multi-host support)
  if (this->advertiseWhileConnected) {
    pServer->getAdvertising()->start();
  }
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
  // Neither the library nor the framework restarts advertising on disconnect;
  // do it here so remaining or bonded hosts can (re)connect. With pairing
  // gated, wait until the last host is gone.
  if (this->advertiseWhileConnected || this->connectionCount == 0) {
    pServer->getAdvertising()->start();
  }
}
