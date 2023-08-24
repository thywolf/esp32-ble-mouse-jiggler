#include <Arduino.h>
#include "esp32-hal-cpu.h"
#include <BleConnectionStatus.h>
#include <BleMouse.h>

int batteryLevel = 50;
const unsigned long interval = 30000;
unsigned long previousMillis = 0;

BleMouse bleMouse("Razer Orochi LE", "Razer", batteryLevel);

int getRandomDirection();
int getBatteryLevel();

void setup() {
  setCpuFrequencyMhz(80);
  int seedValue = analogRead(A0);
  randomSeed(seedValue);
  bleMouse.begin();
}

void loop() {
  if(bleMouse.isConnected()) {
    if (millis() - previousMillis >= interval) {
      bleMouse.setBatteryLevel(getBatteryLevel());
      bleMouse.move(getRandomDirection(), getRandomDirection());
      previousMillis = millis();
    }
  }
  delay(2000);
}

int getRandomDirection() {
  int randomNumber = random(3);
  return randomNumber - 1;
}

int getBatteryLevel() {
  int randomNumber = random(61)+20;
  if(batteryLevel<=randomNumber) {
    batteryLevel=batteryLevel+1;
  } else {
    batteryLevel=batteryLevel-1;
  }
  return batteryLevel;
}