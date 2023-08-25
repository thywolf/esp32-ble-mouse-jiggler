#include <Arduino.h>
#include "esp32-hal-cpu.h"
#include <BleConnectionStatus.h>
#include <BleMouse.h>
#include <Preferences.h>
#include <SimpleSerialShell.h>
#include <quotedTokenizer.h>

Preferences preferences;

int batteryLevel = 50;
unsigned long previousMillis = 0;
unsigned long interval;
unsigned long loopwait;
std::string mouseName;
std::string mouseManu;

BleMouse *bleMouse;

int getRandomDirection();
int getBatteryLevel();
void loadPreferences();
int savePreferences(int /*argc*/ , char ** /*argv*/ );
int getConfig(int /*argc*/ , char ** /*argv*/ );
int setPeriod(int argc, char **argv);
int setDelay(int argc, char **argv);
int setName(int argc, char **argv);
int setManu(int argc, char **argv);
int doReboot(int /*argc*/ , char ** /*argv*/);

void setup() {
  // Board setup
  setCpuFrequencyMhz(80);
  int seedValue = analogRead(A0);
  randomSeed(seedValue);

  // Preferences setup
  preferences.begin("ble-mouse", false);
  loadPreferences();

  // Shell setup
  Serial.begin(115200);
  shell.attach(Serial);
  shell.addCommand(F("show"), getConfig);
  shell.addCommand(F("save"), savePreferences);
  shell.addCommand(F("setperiod"), setPeriod);
  shell.addCommand(F("setdelay"), setDelay);
  shell.addCommand(F("setname"), setName);
  shell.addCommand(F("setmanu"), setManu);
  shell.addCommand(F("reboot"), doReboot);
  shell.setTokenizer(quotedTokenizer);

  //Mouse setup
  bleMouse = new BleMouse(mouseName, mouseManu, batteryLevel);
  bleMouse->begin();
}

void loop() {
  shell.executeIfInput();
  if(bleMouse->isConnected()) {
    if (millis() - previousMillis >= interval) {
      bleMouse->setBatteryLevel(getBatteryLevel());
      bleMouse->move(getRandomDirection(), getRandomDirection());
      previousMillis = millis();
    }
  }
  delay(loopwait);
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

void loadPreferences() {
  interval = preferences.getULong("period", 15000);
  loopwait = preferences.getULong("delay", 2000);
  mouseName = std::string(preferences.getString("name", "Razer Orochi LE").c_str());
  mouseManu = std::string(preferences.getString("manu", "Razer").c_str());
}

int savePreferences(int /*argc*/ , char ** /*argv*/) {
  preferences.putULong("period", interval);
  preferences.putULong("delay", loopwait);
  preferences.putString("name", mouseName.c_str());
  preferences.putString("manu", mouseManu.c_str());
  return EXIT_SUCCESS;
}

int getConfig(int /*argc*/ , char ** /*argv*/) {
  shell.print("Movement period: ");
  shell.print(interval);
  shell.println("ms");
  shell.print("Loop delay: ");
  shell.print(loopwait);
  shell.println("ms");
  shell.print("Mouse name: ");
  shell.println(mouseName.c_str());
  shell.print("Mouse manufacturer: ");
  shell.println(mouseManu.c_str());
  return EXIT_SUCCESS;
}

int doReboot(int /*argc*/ , char ** /*argv*/) {
  ESP.restart();
  return EXIT_SUCCESS;
}

int setPeriod(int argc, char **argv)
{
  if (argc != 2) {
      shell.println("Bad argument count");
      return -1;
  }
  interval = strtoul(argv[1], NULL, 10);
  shell.println("OK");
  return EXIT_SUCCESS;
}

int setDelay(int argc, char **argv)
{
  if (argc != 2) {
      shell.println("Bad argument count");
      return -1;
  }
  loopwait = strtoul(argv[1], NULL, 10);
  shell.println("OK");
  return EXIT_SUCCESS;
}

int setName(int argc, char **argv) { 
  if (argc != 2) {
    shell.println("Bad argument count");
    return -1;
  }
  mouseName = argv[1];
  shell.println("OK");
  return EXIT_SUCCESS;
}

int setManu(int argc, char **argv) { 
  if (argc != 2) {
    shell.println("Bad argument count");
    return -1;
  }
  mouseManu = argv[1];
  shell.println("OK");
  return EXIT_SUCCESS;
}