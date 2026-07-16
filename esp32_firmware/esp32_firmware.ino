#include <Arduino.h>
#include <esp_dmx.h>
#include "BluetoothSerial.h"

// Überprüfen, ob Bluetooth im ESP32 Core aktiviert ist
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;

// --- DEV MODE KONFIGURATION ---
const bool DEV_MODE = true;

// DMX Hardware Pins
const int transmitPin = 2; 
const int receivePin = -1; 
const int enablePin = -1;  

const dmx_port_t dmxPort = 1;
uint8_t dmxData[DMX_PACKET_SIZE]; 

// --- GEMEINSAME KANAL-KONFIGURATION ---
const int amberStartChannel = 62; // Blinder
const int amberEndChannel = 160;

// --- BLINDER VARIABLEN ---
enum BlinderState { STATE_OFF, STATE_ATTACK, STATE_ON, STATE_DECAY };
BlinderState blinderState = STATE_OFF;
float currentBrightness = 0.0; 
unsigned long lastBlinderUpdate = 0;
const int blinderUpdateInterval = 20; 

// --- STROBOSKOP VARIABLEN ---
bool strobeActive = false;
bool strobeIsLit = false;
unsigned long lastStrobeToggle = 0;

// Die Zeiten sind jetzt dynamisch veränderbar
int strobeOnTime = 20;   
int strobeOffTime = 80;  

// --- HILFSFUNKTIONEN FÜR DUAL-DEBUGGING ---
void debugPrint(String msg) {
  if (DEV_MODE) {
    Serial.print(msg);
    SerialBT.print(msg);
  }
}

void debugPrintln(String msg) {
  if (DEV_MODE) {
    Serial.println(msg);
    SerialBT.println(msg);
  }
}

// --- BLINDER FUNKTIONEN ---
void blinderKeyDown() {
  if (blinderState == STATE_OFF || blinderState == STATE_DECAY) {
    blinderState = STATE_ATTACK;
    debugPrintln("[DEV] Blinder: KeyDown");
  }
}

void blinderKeyUp() {
  if (blinderState == STATE_ON || blinderState == STATE_ATTACK) {
    blinderState = STATE_DECAY;
    debugPrintln("[DEV] Blinder: KeyUp");
  }
}

// --- STROBOSKOP FUNKTIONEN ---
void updateStrobeDMX(uint8_t val) {
  for (int ch = amberStartChannel; ch <= amberEndChannel; ch += 2) {
    dmxData[ch - 1] = val; 
  }
}

void strobeKeyDown() {
  if (!strobeActive) {
    strobeActive = true;
    strobeIsLit = true;
    lastStrobeToggle = millis();
    updateStrobeDMX(255);
    debugPrintln("[DEV] Strobe: Aktiviert");
  }
}

void strobeKeyUp() {
  if (strobeActive) {
    strobeActive = false;
    strobeIsLit = false;
    updateStrobeDMX(0);
    debugPrintln("[DEV] Strobe: Deaktiviert");
  }
}

void setup() {
  Serial.begin(115200);
  // Starte Bluetooth mit dem Gerätenamen "ESP32_DMX_Debug"
  SerialBT.begin("ESP32_DMX_Debug"); 
  delay(1000);
  
  if (DEV_MODE) {
    debugPrintln("\n--- DEV MODE AKTIV ---");
    debugPrintln("Bluetooth ist aktiv! Verbinde dich mit 'ESP32_DMX_Debug'");
    debugPrintln("Befehle für Blinder: 'B_ON' / 'B_OFF'");
    debugPrintln("Befehle für Strobe:  'S_ON' / 'S_OFF'");
    debugPrintln("Strobe Zeiten setzen: 'T_ON:20' / 'T_OFF:80' (Werte in ms)");
  }

  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = { {1, "Default Personality"} };
  
  dmx_driver_install(dmxPort, &config, personalities, 1);
  dmx_set_pin(dmxPort, transmitPin, receivePin, enablePin);
  memset(dmxData, 0, DMX_PACKET_SIZE);
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. DEV MODE: Serielle & Bluetooth Befehle einlesen
  if (DEV_MODE) {
    String input = "";
    
    // Prüfen, ob Daten über USB-Serial kommen
    if (Serial.available() > 0) {
      input = Serial.readStringUntil('\n');
    } 
    // Prüfen, ob Daten über Bluetooth kommen
    else if (SerialBT.available() > 0) {
      input = SerialBT.readStringUntil('\n');
    }

    // Wenn ein Befehl empfangen wurde (egal über welchen Weg)
    if (input != "") {
      input.trim();
      input.toUpperCase(); 

      // Befehle auswerten
      if (input == "B_ON") {
        blinderKeyDown();
      } 
      else if (input == "B_OFF") {
        blinderKeyUp();
      } 
      else if (input == "S_ON") {
        strobeKeyDown();
      } 
      else if (input == "S_OFF") {
        strobeKeyUp();
      } 
      // Anschaltzeit setzen (Sucht nach dem Präfix "T_ON:")
      else if (input.startsWith("T_ON:")) {
        strobeOnTime = input.substring(5).toInt();
        debugPrint("[DEV] Strobe ON-Zeit geändert auf: ");
        debugPrint(String(strobeOnTime));
        debugPrintln(" ms");
      }
      // Ausschaltzeit setzen (Sucht nach dem Präfix "T_OFF:")
      else if (input.startsWith("T_OFF:")) {
        strobeOffTime = input.substring(6).toInt();
        debugPrint("[DEV] Strobe OFF-Zeit geändert auf: ");
        debugPrint(String(strobeOffTime));
        debugPrintln(" ms");
      }
      else {
        debugPrintln("[DEV] Unbekannter Befehl!");
      }
    }
  }

  // 2. BLINDER LOGIK
  if (currentMillis - lastBlinderUpdate >= blinderUpdateInterval) {
    lastBlinderUpdate = currentMillis;
    bool blinderChanged = false;

    if (blinderState == STATE_ATTACK) {
      currentBrightness += 40.0; 
      if (currentBrightness >= 255.0) {
        currentBrightness = 255.0;
        blinderState = STATE_ON;
      }
      blinderChanged = true;
    } 
    else if (blinderState == STATE_DECAY) {
      currentBrightness *= 0.92; 
      if (currentBrightness < 1.0) {
        currentBrightness = 0.0;
        blinderState = STATE_OFF;
      }
      blinderChanged = true;
    }

    if (blinderChanged || blinderState == STATE_ON) {
      uint8_t dmxVal = (uint8_t)currentBrightness;
      for (int ch = amberStartChannel; ch <= amberEndChannel; ch += 2) {
        dmxData[ch] = dmxVal;
      }
    }
  }

  // 3. STROBOSKOP LOGIK
  if (strobeActive) {
    if (strobeIsLit && (currentMillis - lastStrobeToggle >= strobeOnTime)) {
      strobeIsLit = false;
      lastStrobeToggle = currentMillis;
      updateStrobeDMX(0);
    } 
    else if (!strobeIsLit && (currentMillis - lastStrobeToggle >= strobeOffTime)) {
      strobeIsLit = true;
      lastStrobeToggle = currentMillis;
      updateStrobeDMX(255);
    }
  }

  // 4. DMX Signal kontinuierlich ausgeben
  dmx_write(dmxPort, dmxData, DMX_PACKET_SIZE);
  dmx_send(dmxPort);
  dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);
}