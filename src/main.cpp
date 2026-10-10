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
int moveDist = 1;
unsigned long sleepMinutes;
std::string mouseName;
std::string mouseManu;

// Movement profiles. Every profile keeps the net displacement *we* send
// bounded, so the cursor stays roughly where the user left it no matter how
// long a session runs: `random`/`pulse` cancel their accumulated residual on
// every report (|net| <= dist in steady state), `glide` retraces each
// outbound run with an equal and opposite return run. Every profile also
// reports at least once per cycle, and the longest silence any of them
// produces is max(600 ms, 1.25 x period) - far below any host's idle
// timeout, so no machine can drift toward sleep while another one is busy.
enum MoveMode { MODE_RANDOM, MODE_PULSE, MODE_GLIDE };
const char *const MODE_NAMES[] = {"random", "pulse", "glide"};
MoveMode moveMode = MODE_RANDOM;
// Residual net offset (our own reports only) that the next random/pulse
// report cancels. The host clamps at screen edges and the user's own
// movements are invisible to us, so this is our contribution only - which is
// exactly what needs to net to zero.
int64_t netX = 0;
int64_t netY = 0;
// Pulse profile state: a cycle is a burst of 2-4 reports pulseStepLen ms
// apart followed by pulseGapLen of quiet; the cycle length is jittered to
// [0.75, 1.25] x period. pulseNextDelay is the gate value in pulse mode.
unsigned long pulseBurstLeft = 0;
unsigned long pulseStepLen = 300;
unsigned long pulseGapLen = 0;
unsigned long pulseNextDelay = 0;
// Glide profile state: alternating outbound/return runs of glideStepsLeft
// same-direction steps at the normal period cadence; the return run reuses
// glideOutSteps so each out/back pair nets exactly (0, 0).
unsigned long glideStepsLeft = 0;
unsigned long glideOutSteps = 0;
int glideDx = 0;
int glideDy = 0;
bool glideReturnNext = false;

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

// Advertising is closed while hosts are served, but re-opened for this window
// after boot and after every change of the connected-host count. That lets
// hosts which dropped mid-session (out of range, reboot, radio hiccup) come
// back on their own, while the device still hides itself once everything
// settles. Pairing mode runs on the same clock: after pairingTimeout it
// switches itself off, so a forgotten press cannot leave the device
// discoverable forever.
const unsigned long ADVERTISE_GRACE_MS = 60000;
// pairing timeout is configurable via the serial shell (set pairing <seconds>);
// defaults to ADVERTISE_GRACE_MS (60 s) at boot
unsigned long pairingTimeout = ADVERTISE_GRACE_MS;
bool pairingMode = false;
unsigned long pairingModeEnd = 0;
unsigned long advertiseGraceEnd = 0;
int lastHostCount = 0;
// kept in sync by the advertising policy in loop(); BleMouse::begin() starts
// advertising, so this matches the boot state
bool advertiseWanted = true;

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

int randomSignedOffset(int dist);
int getBatteryLevel();
int loadPreferences(int /*argc*/ , char ** /*argv*/);
int savePreferences(int /*argc*/ , char ** /*argv*/ );
int getConfig(int /*argc*/ , char ** /*argv*/ );
int setConfig(int argc, char **argv);
int doReboot(int /*argc*/ , char ** /*argv*/);
int doPing(int /*argc*/ , char ** /*argv*/);
void setPairingMode(bool enable);
void enterDeepSleep(void);
MoveMode modeFromName(const char *name);
void resetMoveProfile();
void armPulseCycle();
void armGlideRun();
unsigned long glideAmp();
int correctAxis(int sample, int64_t net);

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
  // app boots; swallow that press so its release doesn't toggle pairing mode
  suppressNextRelease = digitalRead(bootButton.PIN) == LOW;
  attachInterrupt(bootButton.PIN, isr, CHANGE);

  // Shell setup
  shell.addCommand(F("get \t- Displays current configuration"), getConfig);
  shell.addCommand(F("set \t- Sets parameter to a value"), setConfig);
  shell.addCommand(F("load \t- Loads stored configuration"), loadPreferences);
  shell.addCommand(F("save \t- Saves current configuration"), savePreferences);
  shell.addCommand(F("ping \t- Responds with pong and device uptime"), doPing);
  shell.addCommand(F("uptime \t- Responds with pong and device uptime"), doPing);
  shell.addCommand(F("exit \t- Reboots the device"), doReboot);
  shell.setTokenizer(quotedTokenizer);

  //Mouse setup
  bleMouse = new BleMouse(mouseName, mouseManu, 100);
  // open the reconnect window before any host can connect, so already-bonded
  // hosts can come back while the stack comes up
  advertiseGraceEnd = millis() + ADVERTISE_GRACE_MS;
  bleMouse->begin();
}

void loop() {
  if (millis() - bootMillis >= sleepMinutes * 60000UL) {
    enterDeepSleep();
  }
  // Long press (3 s hold) opens/closes the serial console.
  if (buttonHeld && !longPressHandled && millis() - pressStart >= LONG_PRESS_MS) {
    longPressHandled = true;
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
  // Short click toggles pairing mode; it switches itself off after
  // pairingTimeout so a forgotten press can't leave the device discoverable.
  if (shortPressPending) {
    shortPressPending = false;
    setPairingMode(!pairingMode);
  }
  // A forgotten pairing mode switches itself off, like the grace window does.
  if (pairingMode && millis() >= pairingModeEnd) {
    setPairingMode(false);
  }
  // Advertising policy: the device is discoverable while it has no hosts, while
  // pairing mode is on, and for a grace window after every connection change;
  // otherwise it stays hidden while it serves its hosts. Without the window a
  // host that dropped mid-session could only come back by hand (short press).
  int hosts = bleMouse->getConnectedHosts();
  if (hosts != lastHostCount) {
    lastHostCount = hosts;
    advertiseGraceEnd = millis() + ADVERTISE_GRACE_MS;
  }
  bool wantAdvertising = pairingMode
      || hosts == 0
      || (hosts < bleMouse->getMaxHosts() && millis() < advertiseGraceEnd);
  if (wantAdvertising != advertiseWanted) {
    advertiseWanted = wantAdvertising;
    bleMouse->setAdvertising(wantAdvertising);
  }
  switch(appState) {
    case APP_SERIAL: // serial is switched on, mouse not updating
      shell.executeIfInput();
      break;
    case APP_BLE: // serial is switched off, mouse is updating
      if(bleMouse->isConnected()) {
          // random and glide read `period` live so `set period` applies at
          // once; pulse carries its own gate through the burst/gap cycle
          unsigned long due = moveMode == MODE_PULSE ? pulseNextDelay : period;
          if (millis() - previousMillis >= due) {
            if (moveMode == MODE_PULSE && pulseBurstLeft == 0) {
              armPulseCycle();
            } else if (moveMode == MODE_GLIDE && glideStepsLeft == 0) {
              armGlideRun();
            }
            bleMouse->setBatteryLevel(getBatteryLevel());
            int x;
            int y;
            if (moveMode == MODE_GLIDE) {
              // same-direction step; never (0, 0): the heading is never
              // (0, 0) and the amplitude is at least 1. The equal-length
              // return run cancels it, so no correction is needed here.
              x = glideDx * (int)glideAmp();
              y = glideDy * (int)glideAmp();
            } else {
              // cancel our accumulated residual on every report so the net
              // displacement we send stays bounded (|net| <= dist in steady
              // state) and the cursor is roughly where the user left it.
              // Redraw until at least one axis of the corrected report is
              // non-zero: a (0, 0) report would move nothing.
              do {
                x = correctAxis(randomSignedOffset(moveDist), netX);
                y = correctAxis(randomSignedOffset(moveDist), netY);
              } while (x == 0 && y == 0);
            }
            bleMouse->move(x, y);
            netX += x;
            netY += y;
            previousMillis = millis();
            if (moveMode == MODE_PULSE) {
              pulseBurstLeft--;
              pulseNextDelay = pulseBurstLeft > 0 ? pulseStepLen : pulseGapLen;
            } else if (moveMode == MODE_GLIDE) {
              glideStepsLeft--;
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

int randomSignedOffset(int dist) {
  // random value in [-dist, dist], inclusive
  return (int)random(2 * dist + 1) - dist;
}

MoveMode modeFromName(const char *name) {
  if (strcmp(name, "pulse") == 0) {
    return MODE_PULSE;
  }
  if (strcmp(name, "glide") == 0) {
    return MODE_GLIDE;
  }
  // unknown values (including a corrupt NVS read) fall back to the legacy profile
  return MODE_RANDOM;
}

void resetMoveProfile() {
  // zeroed state re-arms on the next report, so a mode switch starts clean
  pulseBurstLeft = 0;
  pulseNextDelay = 0;
  glideStepsLeft = 0;
  glideReturnNext = false;
}

void armPulseCycle() {
  // one cycle: a burst of 2-4 nudges 300-600 ms apart, then a quiet gap
  // that fills out a cycle length jittered to [0.75, 1.25] x period
  pulseStepLen = 300 + random(301);
  unsigned long cycleLen = period / 4 * 3 + random(period / 2 + 1);
  unsigned long n = 2 + random(3);
  if ((n - 1) * pulseStepLen >= cycleLen) {
    // period too short to fit a burst: one report per jittered cycle
    n = 1;
    pulseGapLen = cycleLen;
  } else {
    pulseGapLen = cycleLen - (n - 1) * pulseStepLen;
    if (pulseGapLen < 50) {
      pulseGapLen = 50;
    }
  }
  pulseBurstLeft = n;
}

void armGlideRun() {
  if (!glideReturnNext) {
    // fresh heading: a direction in {-1, 0, 1}^2, never (0, 0)
    do {
      glideDx = (int)random(3) - 1;
      glideDy = (int)random(3) - 1;
    } while (glideDx == 0 && glideDy == 0);
    glideReturnNext = true;
    glideOutSteps = 6 + random(10); // 6-15 steps outbound at the period cadence
    glideStepsLeft = glideOutSteps;
  } else {
    // retrace the outbound path back with the same step count, so the
    // out/back pair nets exactly (0, 0) on both axes
    glideDx = -glideDx;
    glideDy = -glideDy;
    glideReturnNext = false;
    glideStepsLeft = glideOutSteps;
  }
}

unsigned long glideAmp() {
  // per-axis step size: at least 1 px (moveDist >= 1), at most moveDist
  unsigned long amp = (unsigned long)moveDist / 4;
  return amp < 1 ? 1 : amp;
}

int correctAxis(int sample, int64_t net) {
  // cancel the accumulated residual, capped at +/- dist so one report can
  // never overshoot: net after this report equals the sampled offset whenever
  // |sample - net| <= dist, so |net| stays <= dist in steady state, and any
  // larger residual (e.g. an interrupted glide run) shrinks by dist per report
  int64_t v = (int64_t)sample - net;
  if (v > moveDist) {
    return moveDist;
  }
  if (v < -(int64_t)moveDist) {
    return -moveDist;
  }
  return (int)v;
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
  pairingMode = enable;
  // pairing mode runs on a timer like the reconnect window, so a forgotten
  // short press cannot leave the device discoverable forever
  pairingModeEnd = enable ? millis() + pairingTimeout : 0;
  if (!enable) {
    // locking by hand closes the reconnect window at once; the next connection
    // change opens it again
    advertiseGraceEnd = 0;
  }
  // the advertising policy in loop() applies the change on the next iteration
  if (appState == APP_SERIAL) {
    if (enable) {
      shell.printf("Pairing mode on - the device stays discoverable for %lu s or until it is switched off again.\n", pairingTimeout / 1000UL);
    } else {
      shell.println("Pairing mode off - the device only advertises while no host is connected and for a minute after a disconnect.");
    }
  }
}

int loadPreferences(int /*argc*/ , char ** /*argv*/) {
  period = preferences.getULong("period", 15000);
  moveDist = preferences.getUChar("dist", 1);
  if (moveDist < 1 || moveDist > 127) {
    moveDist = 1;
  }
  sleepMinutes = preferences.getULong("sleep", 480);
  if (sleepMinutes < 5 || sleepMinutes > 43200) {
    sleepMinutes = 480;
  }
  // pairing timeout is stored in seconds, converted to ms at load time
  pairingTimeout = preferences.getULong("pairing", 60) * 1000UL;
  if (pairingTimeout < 5000UL || pairingTimeout > 600000UL) {
    pairingTimeout = ADVERTISE_GRACE_MS;
  }
  // unknown mode strings fall back to the legacy profile
  moveMode = modeFromName(preferences.getString("mode", "random").c_str());
  resetMoveProfile();
  mouseName = std::string(preferences.getString("name", "Wobbly BLE Mouse").c_str());
  mouseManu = std::string(preferences.getString("manu", "ESP32").c_str());
  return EXIT_SUCCESS;
}

int savePreferences(int /*argc*/ , char ** /*argv*/) {
  preferences.putULong("period", period);
  preferences.putUChar("dist", (uint8_t)moveDist);
  preferences.putULong("sleep", sleepMinutes);
  preferences.putULong("pairing", pairingTimeout / 1000UL);
  preferences.putString("mode", MODE_NAMES[moveMode]);
  preferences.putString("name", mouseName.c_str());
  preferences.putString("manu", mouseManu.c_str());
  return EXIT_SUCCESS;
}

int getConfig(int /*argc*/ , char ** /*argv*/) {
  unsigned long elapsed = millis() - bootMillis;
  unsigned long remaining = 0;
  if (elapsed < sleepMinutes * 60000UL) {
    remaining = (sleepMinutes * 60000UL - elapsed) / 60000UL;
  }
  // one-line status header: the live values at a glance, config table follows
  shell.printf("Status: [battery] %d%% [hosts] %d [pairing] ",
      getBatteryLevel(), bleMouse->getConnectedHosts());
  if (pairingMode) {
    unsigned long left = pairingModeEnd - millis();
    shell.printf("on (%lus left)", (unsigned long)(left / 1000UL));
  } else {
    shell.print("off");
  }
  shell.printf(" [advertising] %s [sleep] %lu min left\n",
      bleMouse->isAdvertisingEnabled() ? "on" : "off", remaining);
  shell.println();
  shell.printf("Movement [period]: %lu ms\n", period);
  shell.printf("Movement [dist]: %d px\n", moveDist);
  shell.printf("Movement [mode]: %s\n", MODE_NAMES[moveMode]);
  shell.printf("Deep [sleep]: %lu min\n", sleepMinutes);
  shell.printf("Mouse [name]: %s\n", mouseName.c_str());
  shell.printf("Mouse [manu]facturer: %s\n", mouseManu.c_str());
  shell.printf("Pairing [timeout]: %lu s\n", pairingTimeout / 1000UL);
  return EXIT_SUCCESS;
}

int doReboot(int /*argc*/ , char ** /*argv*/) {
  ESP.restart();
  return EXIT_SUCCESS;
}

int doPing(int /*argc*/ , char ** /*argv*/ ) {
  unsigned long uptime = (millis() - bootMillis) / 1000UL;
  shell.printf("pong %lus\n", uptime);
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
    } else if (strcmp(argv[1], "dist") == 0) {
      unsigned long value;
      if (parseUnsigned(argv[2], value) && value >= 1 && value <= 127) {
        moveDist = (int)value;
        return EXIT_SUCCESS;
      } else {
        shell.printf("Invalid dist '%s'. Allowed values: 1-127 px.\n", argv[2]);
      }
    } else if (strcmp(argv[1], "mode") == 0) {
      if (strcmp(argv[2], "random") == 0 || strcmp(argv[2], "pulse") == 0
          || strcmp(argv[2], "glide") == 0) {
        moveMode = modeFromName(argv[2]);
        resetMoveProfile();
        return EXIT_SUCCESS;
      } else {
        shell.printf("Invalid mode '%s'. Allowed values: random, pulse, glide.\n", argv[2]);
      }
    } else if (strcmp(argv[1], "sleep") == 0) {
      unsigned long value;
      if (parseUnsigned(argv[2], value) && value >= 5 && value <= 43200) {
        sleepMinutes = value;
        return EXIT_SUCCESS;
      } else {
        shell.printf("Invalid sleep '%s'. Allowed values: 5-43200 minutes.\n", argv[2]);
      }
    } else if (strcmp(argv[1], "pairing") == 0) {
      unsigned long value;
      if (parseUnsigned(argv[2], value) && value >= 5 && value <= 600) {
        pairingTimeout = value * 1000UL;
        return EXIT_SUCCESS;
      } else {
        shell.printf("Invalid pairing '%s'. Allowed values: 5-600 seconds.\n", argv[2]);
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
  shell.println("    dist - Max distance per axis (in px, 1-127)");
  shell.println("    mode - Movement profile: random, pulse or glide");
  shell.println("   sleep - Time until deep sleep (in minutes, 5-43200)");
  shell.println(" pairing - Pairing mode duration (in seconds, 5-600)");
  shell.println("    name - Advertised device name (string, 3-29 chars)");
  shell.println("    manu - Advertised device manufacturer (string, 3-29 chars)");
  shell.println();
  shell.println("Example:");
  shell.println("  set name \"Generic BLE Mouse\"");
  return -1;
}
