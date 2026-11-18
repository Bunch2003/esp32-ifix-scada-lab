/*
  ESP32 Modbus TCP tank simulator
  --------------------------------
  Runs the same control logic as plc/tank_pump.st and serves the same Modbus
  map (docs/io-map.md, with holding-register offset N = 0), but as a plain
  Arduino sketch with a simulated tank. Use it to:
    - prove WiFi + Modbus + IGS + iFIX before OpenPLC is set up
    - develop iFIX screens with no breadboard wired at all

  Library: "modbus-esp8266" by Alexander Emelianov (Arduino Library Manager).
  Board:   "ESP32 Dev Module" (Espressif ESP32 core).
  WiFi:    copy secrets.h.example to secrets.h and fill in your network
           (optionally a static IP there too).
*/

#include <WiFi.h>
#include <ModbusIP_ESP8266.h>
#include "secrets.h"

// ---------- Options ----------
#define USE_POT_FOR_LEVEL false   // true = level comes from the potentiometer
                                  // false = level is simulated from pump/valve

// ---------- Pins (simulator only; OpenPLC uses its own pin map) ----------
const int PIN_START = 25;   // button to 3.3V (internal pull-down used)
const int PIN_STOP  = 26;
const int PIN_LSHH  = 27;
const int PIN_LEVEL = 34;   // ADC1, pot wiper
const int PIN_PUMP  = 16;   // LED + 220R to GND
const int PIN_VALVE = 17;
const int PIN_HORN  = 18;

// ---------- Modbus map (0-based) ----------
// Coils
const uint16_t C_PUMP = 0, C_VALVE = 1, C_HORN = 2;
const uint16_t C_HMI_START = 8, C_HMI_STOP = 9, C_HMI_AUTO = 10,
               C_HMI_VALVE = 11, C_HMI_ACK = 12;
const uint16_t C_LAH = 13, C_LAL = 14, C_FAULT = 15;
// Discrete inputs
const uint16_t DI_START = 0, DI_STOP = 1, DI_LSHH = 2;
// Holding registers
const uint16_t HR_LEVEL = 0, HR_SP_ON = 1, HR_SP_OFF = 2, HR_SP_HI = 3,
               HR_SP_LO = 4, HR_STARTS = 5, HR_RUNSEC = 6, HR_HEARTBEAT = 7;
const uint16_t NUM_HREGS = 8;
// Input registers
const uint16_t IR_LEVEL_RAW = 0;

// ---------- Simulation ----------
const float FILL_RATE_PCT_S  = 2.0;   // valve open
const float DRAIN_RATE_PCT_S = 3.0;   // pump running
const unsigned long PUMP_MAX_RUN_MS = 60000;
const unsigned long SCAN_MS = 50;

ModbusIP mb;

float level = 50.0;
bool runLatch = false, hornLatch = false, fault = false, prevAnyAlarm = false;
bool prevPump = false;
unsigned long pumpOnSince = 0, lastScan = 0, runMsAccum = 0;
unsigned long lastWifiAttempt = 0;
bool wifiWasUp = false;

// ---------- WiFi (never blocks the scan) ----------
// A real PLC keeps controlling when the network drops; so does this sketch.
const unsigned long WIFI_RETRY_MS = 10000;

void wifiBegin() {
#ifdef STATIC_IP
  WiFi.config(IPAddress(STATIC_IP), IPAddress(STATIC_GATEWAY),
              IPAddress(STATIC_SUBNET), IPAddress(STATIC_DNS));
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
}

void wifiMaintain() {
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !wifiWasUp) {
    Serial.print("WiFi up. Modbus TCP server at ");
    Serial.print(WiFi.localIP());
    Serial.println(":502");
  } else if (!up && wifiWasUp) {
    Serial.println("WiFi lost; logic keeps running, reconnecting...");
  }
  wifiWasUp = up;
  if (!up && millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
    WiFi.disconnect();
    wifiBegin();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_START, INPUT_PULLDOWN);
  pinMode(PIN_STOP,  INPUT_PULLDOWN);
  pinMode(PIN_LSHH,  INPUT_PULLDOWN);
  pinMode(PIN_PUMP,  OUTPUT);
  pinMode(PIN_VALVE, OUTPUT);
  pinMode(PIN_HORN,  OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);             // steadier Modbus response times
  Serial.println("Connecting to WiFi (logic starts now, comms when connected)");
  wifiBegin();

  mb.server();                      // port 502
  mb.addCoil(0, false, 16);         // coils 0-15
  mb.addIsts(0, false, 3);          // discrete inputs 0-2
  mb.addHreg(0, 0, NUM_HREGS);      // holding registers 0-7
  mb.addIreg(0, 0, 1);              // input register 0

  mb.Hreg(HR_SP_ON, 70);
  mb.Hreg(HR_SP_OFF, 30);
  mb.Hreg(HR_SP_HI, 90);
  mb.Hreg(HR_SP_LO, 10);
}

void loop() {
  mb.task();
  wifiMaintain();

  unsigned long now = millis();
  if (now - lastScan < SCAN_MS) return;
  float dt = (now - lastScan) / 1000.0;
  lastScan = now;

  // ----- Read inputs -----
  bool pbStart = digitalRead(PIN_START);
  bool pbStop  = digitalRead(PIN_STOP);
  bool lshh    = digitalRead(PIN_LSHH);
  uint16_t raw = analogRead(PIN_LEVEL);          // 0-4095
  mb.Ists(DI_START, pbStart);
  mb.Ists(DI_STOP, pbStop);
  mb.Ists(DI_LSHH, lshh);
  mb.Ireg(IR_LEVEL_RAW, raw);

  // ----- Level -----
  if (USE_POT_FOR_LEVEL) {
    level = raw * 100.0 / 4095.0;
  } else {
    if (mb.Coil(C_VALVE)) level += FILL_RATE_PCT_S * dt;
    if (mb.Coil(C_PUMP))  level -= DRAIN_RATE_PCT_S * dt;
    level = constrain(level, 0.0, 100.0);
  }
  uint16_t pct = (uint16_t)(level + 0.5);
  mb.Hreg(HR_LEVEL, pct);

  uint16_t spOn = mb.Hreg(HR_SP_ON), spOff = mb.Hreg(HR_SP_OFF);
  uint16_t spHi = mb.Hreg(HR_SP_HI), spLo = mb.Hreg(HR_SP_LO);

  // ----- Alarms -----
  bool lah = (pct >= spHi) || lshh;
  bool lal = pct <= spLo;
  bool pumpOn = mb.Coil(C_PUMP);
  if (pumpOn && now - pumpOnSince >= PUMP_MAX_RUN_MS) fault = true;

  // ----- Start / stop -----
  bool autoMode = mb.Coil(C_HMI_AUTO);
  bool stopReq = pbStop || mb.Coil(C_HMI_STOP);
  bool startReq;
  if (autoMode) {
    startReq = pct >= spOn;
    if (pct <= spOff) runLatch = false;
  } else {
    startReq = pbStart || mb.Coil(C_HMI_START);
  }
  if (startReq) runLatch = true;
  if (stopReq || lal || fault) runLatch = false;

  // ----- Valve -----
  bool valve = mb.Coil(C_HMI_VALVE) && !lah;

  // ----- Horn -----
  bool anyAlarm = lah || lal || fault;
  if (anyAlarm && !prevAnyAlarm) hornLatch = true;
  prevAnyAlarm = anyAlarm;
  if (mb.Coil(C_HMI_ACK)) {
    hornLatch = false;
    fault = false;
  }

  // ----- Counters -----
  if (runLatch && !prevPump) {
    pumpOnSince = now;
    mb.Hreg(HR_STARTS, mb.Hreg(HR_STARTS) + 1);
  }
  if (runLatch) {
    runMsAccum += (unsigned long)(dt * 1000);
    while (runMsAccum >= 1000) {
      runMsAccum -= 1000;
      mb.Hreg(HR_RUNSEC, mb.Hreg(HR_RUNSEC) + 1);
    }
  }
  prevPump = runLatch;

  // ----- Write outputs -----
  mb.Coil(C_PUMP, runLatch);
  mb.Coil(C_VALVE, valve);
  mb.Coil(C_HORN, hornLatch);
  mb.Coil(C_LAH, lah);
  mb.Coil(C_LAL, lal);
  mb.Coil(C_FAULT, fault);
  digitalWrite(PIN_PUMP, runLatch);
  digitalWrite(PIN_VALVE, valve);
  digitalWrite(PIN_HORN, hornLatch);

  // Heartbeat: +1 every scan (wraps at 65535). If iFIX sees it stop
  // changing, the data on screen is stale -> comm-loss alarm.
  mb.Hreg(HR_HEARTBEAT, (uint16_t)(mb.Hreg(HR_HEARTBEAT) + 1));

  // Clear momentary commands
  mb.Coil(C_HMI_START, false);
  mb.Coil(C_HMI_STOP, false);
  mb.Coil(C_HMI_ACK, false);
}
