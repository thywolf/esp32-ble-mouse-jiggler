#include <Arduino.h>
#include "esp32-hal-cpu.h"
#include <esp_sleep.h>
#include <BleConnectionStatus.h>
#include <BleMouse.h>
#include <Preferences.h>
#include <SimpleSerialShell.h>
#include <quotedTokenizer.h>

Preferences preferences;

unsigned long bootMillis = 0;
unsigned long previousMillis = 0;
unsigned long period;
unsigned long sleepMinutes;
std::string mouseName;
std::string mouseManu;

enum APPState {
  APP_BLE,
  APP_SERIAL,
  APP_SERIAL_OPEN,
  APP_SERIAL_CLOSE
};

APPState appState = APP_BLE;

const unsigned long LONG_PRESS_MS = 3000;

struct Button {
  const uint8_t PIN;
  unsigned long last_button_time;
};

Button bootButton = {0, 0};

// Button state shared with the ISR. Holding GPIO0 while the firmware runs is
// just a press; entering the bootloader requires holding it across a reset.
volatile bool buttonHeld = false;
volatile unsigned long pressStart = 0;
volatile bool longPressHandled = false;
volatile bool shortPressPending = false;
volatile bool suppressNextRelease = false;

// Pairing mode starts on for a grace window after boot so all already-paired
// hosts can reconnect at once (legacy advertising stops on the first
// connection), then turns off automatically unless toggled manually.
const unsigned long PAIRING_GRACE_MS = 60000;
bool pairingMode = true;
bool pairingGracePending = true;
unsigned long pairingGraceStart = 0;

void IRAM_ATTR isr() {
  unsigned long now = millis();
  if (now - bootButton.last_button_time < 50) {
    return;
  }
  bootButton.last_button_time = now;
  if (digitalRead(bootButton.PIN) == LOW) {
    buttonHeld = true;
    pressStart = now;
    longPressHandled = false;
  } else {
    buttonHeld = false;
    if (suppressNextRelease) {
      // release of a press that spans the boot (deep sleep wake) is not a
      // config press
      suppressNextRelease = false;
    } else if (!longPressHandled) {
      shortPressPending = true;
    }
  }
}

BleMouse *bleMouse;

int getRandomDirection();
int getBatteryLevel();
int loadPreferences(int /*argc*/ , char ** /*argv*/);
int savePreferences(int /*argc*/ , char ** /*argv*/ );
int getConfig(int /*argc*/ , char ** /*argv*/ );
int setConfig(int argc, char **argv);
int doReboot(int /*argc*/ , char ** /*argv*/);
void setPairingMode(bool enable);
void enterDeepSleep(void);

void setup() {
  // every timer (movement, sleep) is counted from this point
  bootMillis = millis();

  // Board setup
  setCpuFrequencyMhz(80);
  randomSeed(esp_random());

  // Preferences setup
  preferences.begin("ble-mouse", false);
  loadPreferences(0, NULL);

  // button interrupt setup
  pinMode(bootButton.PIN, INPUT_PULLUP);
  // waking from deep sleep via the Boot button leaves the pin low while the
  // app boots; swallow that press so its release doesn't open the console
  suppressNextRelease = digitalRead(bootButton.PIN) == LOW;
  attachInterrupt(bootButton.PIN, isr, CHANGE);

  // Shell setup
  shell.addCommand(F("get \t- Displays current configuration"), getConfig);
  shell.addCommand(F("set \t- Sets parameter to a value"), setConfig);
  shell.addCommand(F("load \t- Loads stored configuration"), loadPreferences);
  shell.addCommand(F("save \t- Saves current configuration"), savePreferences);
  shell.addCommand(F("exit \t- Reboots the device"), doReboot);
  shell.setTokenizer(quotedTokenizer);

  //Mouse setup
  bleMouse = new BleMouse(mouseName, mouseManu, 100);
  // sync the advertising gate with the boot grace window before any host
  // can connect
  pairingGraceStart = millis();
  bleMouse->setAdvertiseWhileConnected(pairingMode);
  bleMouse->begin();
}

void loop() {
  if (millis() - bootMillis >= sleepMinutes * 60000UL) {
    enterDeepSleep();
  }
  if (pairingGracePending && millis() - pairingGraceStart >= PAIRING_GRACE_MS) {
    pairingGracePending = false;
    if (pairingMode) {
      setPairingMode(false);
    }
  }
  if (buttonHeld && !longPressHandled && millis() - pressStart >= LONG_PRESS_MS) {
    longPressHandled = true;
    setPairingMode(!pairingMode);
  }
  if (shortPressPending) {
    shortPressPending = false;
    switch(appState) {
      case APP_SERIAL:
        appState = APP_SERIAL_CLOSE;
        break;
      case APP_BLE:
        appState = APP_SERIAL_OPEN;
        break;
      default:
        break;
    }
  }
  switch(appState) {
    case APP_SERIAL: // serial is switched on, mouse not updating
      shell.executeIfInput();
      break;
    case APP_BLE: // serial is switched off, mouse is updating
      if(bleMouse->isConnected()) {
          if (millis() - previousMillis >= period) {
            bleMouse->setBatteryLevel(getBatteryLevel());
            bleMouse->move(getRandomDirection(), getRandomDirection());
            previousMillis = millis();
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

void enterDeepSleep(void) {
  bleMouse->disconnectAll();
  delay(250); // give the hosts a moment to register the disconnect
  // wake when the Boot button pulls GPIO0 low; it idles high via pull-up
  esp_sleep_enable_ext0_wakeup(gpio_num_t(bootButton.PIN), 0);
  esp_deep_sleep_start();
}

int getBatteryLevel() {
  // simulated battery: drains linearly from 100% at boot to 0% when the
  // sleep timer runs out
  unsigned long elapsed = millis() - bootMillis;
  unsigned long total = sleepMinutes * 60000UL;
  if (elapsed >= total) {
    return 0;
  }
  return 100 - (int)(((uint64_t)elapsed * 100ULL) / total);
}

void setPairingMode(bool enable) {
  pairingGracePending = false;
  pairingMode = enable;
  bleMouse->setAdvertiseWhileConnected(enable);
  if (enable) {
    bleMouse->startAdvertising();
  } else if (bleMouse->getConnectedHosts() > 0) {
    bleMouse->stopAdvertising();
  }
  if (appState == APP_SERIAL) {
    shell.println(enable ? "Pairing mode on - the device stays discoverable while hosts are connected."
                         : "Pairing mode off - the device is only discoverable while no host is connected.");
  }
}

int loadPreferences(int /*argc*/ , char ** /*argv*/) {
  period = preferences.getULong("period", 15000);
  sleepMinutes = preferences.getULong("sleep", 480);
  if (sleepMinutes < 5 || sleepMinutes > 43200) {
    sleepMinutes = 480;
  }
  mouseName = std::string(preferences.getString("name", "Wobbly BLE Mouse").c_str());
  mouseManu = std::string(preferences.getString("manu", "ESP32").c_str());
  return EXIT_SUCCESS;
}

int savePreferences(int /*argc*/ , char ** /*argv*/) {
  preferences.putULong("period", period);
  preferences.putULong("sleep", sleepMinutes);
  preferences.putString("name", mouseName.c_str());
  preferences.putString("manu", mouseManu.c_str());
  return EXIT_SUCCESS;
}

int getConfig(int /*argc*/ , char ** /*argv*/) {
  shell.printf("Movement [period]: %lu ms\n", period);
  shell.printf("Deep [sleep]: %lu min\n", sleepMinutes);
  shell.printf("Mouse [name]: %s\n", mouseName.c_str());
  shell.printf("Mouse [manu]facturer: %s\n", mouseManu.c_str());
  shell.printf("Battery [level]: %d %%\n", getBatteryLevel());
  unsigned long elapsed = millis() - bootMillis;
  unsigned long remaining = 0;
  if (elapsed < sleepMinutes * 60000UL) {
    remaining = (sleepMinutes * 60000UL - elapsed) / 60000UL;
  }
  shell.printf("Sleep [left]: %lu min\n", remaining);
  shell.printf("Connected [hosts]: %d\n", bleMouse->getConnectedHosts());
  shell.printf("Pairing [mode]: %s\n", pairingMode ? "on" : "off");
  return EXIT_SUCCESS;
}

int doReboot(int /*argc*/ , char ** /*argv*/) {
  ESP.restart();
  return EXIT_SUCCESS;
}

bool parseUnsigned(const char* str, unsigned long& out) {
  // reject empty input and signs: strtoul would wrap negatives into huge values
  if (str == 0 || *str == '\0' || *str == '-' || *str == '+') {
    return false;
  }
  char* endptr = nullptr;
  unsigned long value = strtoul(str, &endptr, 10);
  if (endptr == str || *endptr != '\0') {
    return false;
  }
  out = value;
  return true;
}

int setConfig(int argc, char **argv)
{
  if (argc != 3) {
    shell.println("Bad argument count.");
  } else {
    if (strcmp(argv[1], "period") == 0) {
      unsigned long value;
      if (parseUnsigned(argv[2], value) && value >= 100 && value <= 60000) {
        period = value;
        return EXIT_SUCCESS;
      } else {
        shell.printf("Invalid period '%s'. Allowed values: 100-60000 ms.\n", argv[2]);
      }
    } else if (strcmp(argv[1], "sleep") == 0) {
      unsigned long value;
      if (parseUnsigned(argv[2], value) && value >= 5 && value <= 43200) {
        sleepMinutes = value;
        return EXIT_SUCCESS;
      } else {
        shell.printf("Invalid sleep '%s'. Allowed values: 5-43200 minutes.\n", argv[2]);
      }
    } else if (strcmp(argv[1], "name") == 0) {
      if (strlen(argv[2]) >= 3 && strlen(argv[2]) <= 29) {
        mouseName = argv[2];
        return EXIT_SUCCESS;
      } else {
        shell.println("Invalid name length. Allowed 3-29 chars.");
      }
    } else if (strcmp(argv[1], "manu") == 0) {
      if (strlen(argv[2]) >= 3 && strlen(argv[2]) <= 29) {
        mouseManu = argv[2];
        return EXIT_SUCCESS;
      } else {
        shell.println("Invalid manufacturer length. Allowed 3-29 chars.");
      }
    } else {
      shell.printf("Unrecognized parameter '%s'.\n", argv[1]);
    }
  }

  shell.println();
  shell.println("Usage: set <parameter> <value>");
  shell.println("Parameters:");
  shell.println("  period - Time between movements (in ms, 100-60000)");
  shell.println("   sleep - Time until deep sleep (in minutes, 5-43200)");
  shell.println("    name - Advertised device name (string, 3-29 chars)");
  shell.println("    manu - Advertised device manufacturer (string, 3-29 chars)");
  shell.println();
  shell.println("Example:");
  shell.println("  set name \"Generic BLE Mouse\"");
  return -1;
}
