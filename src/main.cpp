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
unsigned long period;
std::string mouseName;
std::string mouseManu;

enum APPState {
  APP_BLE,
  APP_SERIAL,
  APP_SERIAL_OPEN,
  APP_SERIAL_CLOSE
};

APPState appState = APP_BLE;
bool wasConnected = false;

struct Button {
  const uint8_t PIN;
  unsigned long button_time;  
  unsigned long last_button_time; 
};

Button bootButton = {0, 0, 0};

void IRAM_ATTR isr() {
  bootButton.button_time = millis();
  if (bootButton.button_time - bootButton.last_button_time > 250)
  {
    switch(appState) {
      case APP_SERIAL:
        appState = APP_SERIAL_CLOSE;
        break;
      case APP_BLE:
        appState = APP_SERIAL_OPEN;
        break;
    }
    bootButton.last_button_time = bootButton.button_time;
  }
}

BleMouse *bleMouse;

int getRandomDirection();
int getBatteryLevel();
void loadPreferences();
int savePreferences(int /*argc*/ , char ** /*argv*/ );
int getConfig(int /*argc*/ , char ** /*argv*/ );
int setConfig(int argc, char **argv);
int doReboot(int /*argc*/ , char ** /*argv*/);

void setup() {
  // Board setup
  setCpuFrequencyMhz(80);
  int seedValue = analogRead(A0);
  randomSeed(seedValue);

  // Preferences setup
  preferences.begin("ble-mouse", false);
  loadPreferences();

  // button interrupt setup
  attachInterrupt(bootButton.PIN, isr, FALLING);

  // Shell setup
  shell.addCommand(F("get \t- Displays current configuration"), getConfig);
  shell.addCommand(F("set \t- Sets parameter to a value"), setConfig);
  shell.addCommand(F("save \t- Saves current configuration"), savePreferences);
  shell.addCommand(F("exit \t- Reboots the device"), doReboot);
  shell.setTokenizer(quotedTokenizer);

  //Mouse setup
  bleMouse = new BleMouse(mouseName, mouseManu, batteryLevel);
  bleMouse->begin();
}

void loop() {
  switch(appState) {
    case APP_SERIAL: // serial is switched on, mouse not updating
      shell.executeIfInput();
      break;
    case APP_BLE: // serial is switched off, mouse is updating
      if(appState == APP_BLE) {
        if(bleMouse->isConnected()) {
          if (millis() - previousMillis >= period) {
            bleMouse->setBatteryLevel(getBatteryLevel());
            bleMouse->move(getRandomDirection(), getRandomDirection());
            previousMillis = millis();
            if (!wasConnected) wasConnected = true;
          }
        } else {
          if (millis() - previousMillis >= period) {
            if (wasConnected) ESP.restart();
          }
        }
      }
      break;
    case APP_SERIAL_OPEN: // switching Serial ON
      // Init Serial
      Serial.begin(115200);
      // Attach shell
      shell.attach(Serial);
      shell.execute("help");
      appState = APP_SERIAL;
      break;
    case APP_SERIAL_CLOSE: // switching Serial OFF
      // Stop Serial
      shell.println("Goodbye...");
      shell.flush();
      Serial.end();
      appState = APP_BLE;
      break;
  }
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
  period = preferences.getULong("period", 15000);
  mouseName = std::string(preferences.getString("name", "Razer Orochi LE").c_str());
  mouseManu = std::string(preferences.getString("manu", "Razer").c_str());
}

int savePreferences(int /*argc*/ , char ** /*argv*/) {
  preferences.putULong("period", period);
  preferences.putString("name", mouseName.c_str());
  preferences.putString("manu", mouseManu.c_str());
  return EXIT_SUCCESS;
}

int getConfig(int /*argc*/ , char ** /*argv*/) {
  shell.printf("Movement [period]: %d ms\n", period);
  shell.printf("Mouse [name]: %s\n", mouseName.c_str());
  shell.printf("Mouse [manu]facturer: %s\n", mouseManu.c_str());
  return EXIT_SUCCESS;
}

int doReboot(int /*argc*/ , char ** /*argv*/) {
  ESP.restart();
  return EXIT_SUCCESS;
}

int setConfig(int argc, char **argv)
{
  if (argc != 3) {
    shell.println("Bad argument count.");
  } else {
    if (strcmp(argv[1], "period") == 0) {
      period = strtoul(argv[2], NULL, 10);
      return EXIT_SUCCESS;
    } else if (strcmp(argv[1], "name") == 0) {
      mouseName = argv[2];
      return EXIT_SUCCESS;
    } else if (strcmp(argv[1], "manu") == 0) {
      mouseManu = argv[2];
      return EXIT_SUCCESS;
    } else {
      shell.println("Unrecognized parameter.");
    }
  }

  shell.println();
  shell.println("Usage: set <parameter> <value>");
  shell.println("Parameters: name of the parameter to set. Available parameters:");
  shell.println("  period - Time between movements (in ms)");
  shell.println("    name - Advertised device name (string)");
  shell.println("    manu - Advertised device manufacturer (string)");
  shell.println();
  shell.println("Example:");
  shell.println("  set name \"Generic BLE Mouse\"");
  return -1;
}
