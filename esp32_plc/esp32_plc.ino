/*
 * ESP32 "Mini PLC" — Tank & Pump Simulator for SCADA (iFIX) practice
 * ------------------------------------------------------------------
 * Behaves like a small PLC / RTU on the network:
 *   - Fixed scan cycle: READ INPUTS -> RUN LOGIC -> WRITE OUTPUTS
 *   - Modbus TCP server (slave) on port 502
 *   - Runs on its own: SCADA monitors and commands, it does NOT control
 *
 * Process being simulated:
 *   A tank fills from an inlet valve (the potentiometer = valve position).
 *   A pump drains it. In AUTO the pump starts at a high level and stops
 *   at a low level. In MANUAL the operator (SCADA or local buttons) runs it.
 *
 * Library: "modbus-esp8266" by Alexander Emelianov (v4.1+, supports ESP32)
 *   Arduino IDE -> Library Manager -> search "modbus-esp8266"
 *
 * Board: "ESP32 Dev Module" (any ESP32 DevKit / WROOM board)
 *
 * SAFETY NOTE: real E-stops are hardwired and never depend on software or
 * a network. The E-stop here is a simulation for alarm/interlock practice.
 */

#include <WiFi.h>
#include <ModbusIP_ESP8266.h>   // same header name on ESP32

// =====================================================================
//  USER CONFIG — WiFi credentials and network settings live in secrets.h
//  (git-ignored). First time: copy secrets.example.h -> secrets.h and edit.
// =====================================================================
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Missing secrets.h - copy secrets.example.h to secrets.h and fill in your WiFi details."
#endif

const char* WIFI_SSID = SECRET_WIFI_SSID;
const char* WIFI_PASS = SECRET_WIFI_PASS;

// Static IP so iFIX/IGS always finds the "PLC" (values from secrets.h)
IPAddress PLC_IP     (PLC_IP_ADDR);
IPAddress PLC_GATEWAY(PLC_GATEWAY_ADDR);
IPAddress PLC_SUBNET (PLC_SUBNET_MASK);

const uint32_t SCAN_MS = 100;            // PLC scan time (10 scans/sec)

// =====================================================================
//  I/O MAP (physical pins)
// =====================================================================
// Analog input — MUST be an ADC1 pin (GPIO32-39). ADC2 is unusable with WiFi on.
const int PIN_AI_INLET_VALVE = 34;       // potentiometer wiper

// Digital inputs (buttons to GND, internal pull-ups -> pressed = LOW)
const int PIN_DI_START  = 25;
const int PIN_DI_STOP   = 26;
const int PIN_DI_ESTOP  = 27;

// Digital outputs
const int PIN_DO_PUMP   = 16;            // LED, PWM brightness = pump speed
const int PIN_DO_ALARM  = 17;            // LED: flash = unacked, solid = acked & active
const int PIN_DO_STATUS = 2;             // onboard LED: blink = running, fast = no WiFi

// =====================================================================
//  MODBUS REGISTER MAP  (0-based offsets; classic address in comments)
//  See README.md for the full table and iFIX/IGS addressing notes.
// =====================================================================
// ---- Coils (0x) : SCADA -> PLC commands (read/write)
enum Coils : uint16_t {
  C_PUMP_START     = 0,   // 00001  momentary: PLC clears after accepting
  C_PUMP_STOP      = 1,   // 00002  momentary
  C_MODE_AUTO      = 2,   // 00003  maintained: 1 = AUTO, 0 = MANUAL
  C_ALARM_ACK      = 3,   // 00004  momentary: acknowledge all alarms
  C_ESTOP_RESET    = 4,   // 00005  momentary
  C_SIM_PUMP_FAULT = 5,   // 00006  maintained: inject a pump fault (training)
  NUM_COILS        = 6
};

// ---- Discrete inputs (1x) : PLC status bits (read-only)
enum Ists : uint16_t {
  I_PUMP_RUNNING   = 0,   // 10001
  I_MODE_AUTO      = 1,   // 10002
  I_ESTOP_ACTIVE   = 2,   // 10003
  I_ALM_LEVEL_HH   = 3,   // 10004  high-high level (overflow risk)
  I_ALM_LEVEL_LL   = 4,   // 10005  low-low level (pump dry-run risk)
  I_ALM_PUMP_FAULT = 5,   // 10006
  I_ALM_UNACKED    = 6,   // 10007  any alarm not yet acknowledged
  I_LOCAL_START_PB = 7,   // 10008  raw button state (for I/O check screens)
  I_LOCAL_STOP_PB  = 8,   // 10009
  I_LOCAL_ESTOP_PB = 9,   // 10010
  NUM_ISTS         = 10
};

// ---- Input registers (3x) : process values (read-only)
enum Iregs : uint16_t {
  IR_TANK_LEVEL    = 0,   // 30001  0-1000 = 0.0-100.0 %
  IR_INLET_VALVE   = 1,   // 30002  0-1000 = 0.0-100.0 %
  IR_PUMP_SPEED_FB = 2,   // 30003  0-1000 = 0.0-100.0 % (actual)
  IR_PUMP_RUN_MIN  = 3,   // 30004  pump run time, minutes (wraps at 65535)
  IR_PUMP_STARTS   = 4,   // 30005  start counter
  IR_HEARTBEAT     = 5,   // 30006  +1 every scan; SCADA uses it to detect stale comms
  IR_STATE         = 6,   // 30007  0=STOPPED 1=RUNNING 2=FAULT 3=ESTOP
  IR_WIFI_RSSI     = 7,   // 30008  signed dBm (two's complement)
  IR_SCAN_TIME_MS  = 8,   // 30009  last measured scan time
  NUM_IREGS        = 9
};

// ---- Holding registers (4x) : setpoints (read/write)
enum Hregs : uint16_t {
  HR_SP_PUMP_START = 0,   // 40001  AUTO: start pump at/above this level (x10 %)
  HR_SP_PUMP_STOP  = 1,   // 40002  AUTO: stop pump at/below this level
  HR_SP_ALM_HH     = 2,   // 40003  high-high alarm limit
  HR_SP_ALM_LL     = 3,   // 40004  low-low alarm limit
  HR_PUMP_SPEED_SP = 4,   // 40005  pump (VFD) speed reference 0-1000
  NUM_HREGS        = 5
};

// Defaults (x10 %)
const uint16_t DEF_SP_START = 800, DEF_SP_STOP = 200;
const uint16_t DEF_ALM_HH   = 950, DEF_ALM_LL  = 50;
const uint16_t DEF_SPEED    = 1000;

// Process tuning: % of tank per second at 100% valve / 100% pump speed
const float FILL_RATE_PCT_S  = 2.0f;     // full valve fills empty tank in ~50 s
const float DRAIN_RATE_PCT_S = 3.0f;     // full-speed pump drains it in ~33 s
const float ALARM_DEADBAND   = 2.0f;     // % hysteresis so alarms don't chatter

// =====================================================================
//  INTERNAL STATE
// =====================================================================
ModbusIP mb;

enum PlcState : uint16_t { ST_STOPPED = 0, ST_RUNNING = 1, ST_FAULT = 2, ST_ESTOP = 3 };

struct Button {
  int pin; bool stable = false; bool lastRaw = false; uint32_t changedAt = 0;
  bool rose = false;                      // true for one scan on press
};
Button btnStart{PIN_DI_START}, btnStop{PIN_DI_STOP}, btnEstop{PIN_DI_ESTOP};

struct Alarm { bool active = false; bool unacked = false; };
Alarm almHH, almLL, almFault;

float    tankLevel   = 50.0f;            // %  (process "physics")
float    valveFilt   = 0.0f;             // %  filtered pot reading
bool     pumpCmd     = false;            // logic output: pump should run
bool     estopLatched = false;
bool     faultLatched = false;
uint16_t pumpStarts  = 0;
uint32_t pumpRunMs   = 0;
uint16_t heartbeat   = 0;
uint32_t lastScan    = 0;
uint16_t lastScanTime = 0;
uint32_t lastWifiTry = 0;

// =====================================================================
//  HELPERS
// =====================================================================
static inline uint16_t pctToReg(float pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (uint16_t)(pct * 10.0f + 0.5f);
}
static inline uint16_t clampReg(uint16_t v, uint16_t hi) { return v > hi ? hi : v; }
static inline float regToPct(uint16_t r) { return (r > 1000 ? 1000 : r) / 10.0f; }

// PWM output, compatible with ESP32 Arduino core 2.x and 3.x
const int PWM_FREQ = 5000, PWM_BITS = 10, PWM_CH_PUMP = 0;
void pwmSetup() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_DO_PUMP, PWM_FREQ, PWM_BITS);
#else
  ledcSetup(PWM_CH_PUMP, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_DO_PUMP, PWM_CH_PUMP);
#endif
}
void pwmWrite(uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_DO_PUMP, duty);
#else
  ledcWrite(PWM_CH_PUMP, duty);
#endif
}

void readButton(Button& b) {
  bool raw = (digitalRead(b.pin) == LOW); // pressed = LOW (pull-up)
  uint32_t now = millis();
  if (raw != b.lastRaw) { b.lastRaw = raw; b.changedAt = now; }
  b.rose = false;
  if ((now - b.changedAt) >= 30 && raw != b.stable) {   // 30 ms debounce
    b.stable = raw;
    if (raw) b.rose = true;
  }
}

// Momentary coil: returns true once if SCADA wrote 1, then clears it
bool takeCoil(uint16_t c) {
  if (mb.Coil(c)) { mb.Coil(c, false); return true; }
  return false;
}

// Alarm with hysteresis; any new activation re-arms "unacked"
void evalAlarm(Alarm& a, bool tripCond, bool clearCond) {
  if (!a.active && tripCond)      { a.active = true; a.unacked = true; }
  else if (a.active && clearCond) { a.active = false; }
}

void wifiMaintain() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiTry < 5000) return; // retry every 5 s, never block the scan
  lastWifiTry = millis();
  Serial.println("[WiFi] (re)connecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32 Mini PLC: Tank & Pump ===");

  pinMode(PIN_DI_START, INPUT_PULLUP);
  pinMode(PIN_DI_STOP,  INPUT_PULLUP);
  pinMode(PIN_DI_ESTOP, INPUT_PULLUP);
  pinMode(PIN_DO_ALARM, OUTPUT);
  pinMode(PIN_DO_STATUS, OUTPUT);
  pwmSetup();
  analogReadResolution(12);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                  // lower, steadier Modbus latency
#if USE_STATIC_IP
  WiFi.config(PLC_IP, PLC_GATEWAY, PLC_SUBNET);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiTry = millis();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected. PLC IP = "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Not connected yet; PLC logic runs anyway, will keep retrying.");
  }

  mb.server();                           // Modbus TCP server on port 502

  for (uint16_t i = 0; i < NUM_COILS; i++) mb.addCoil(i, false);
  for (uint16_t i = 0; i < NUM_ISTS;  i++) mb.addIsts(i, false);
  for (uint16_t i = 0; i < NUM_IREGS; i++) mb.addIreg(i, 0);
  mb.addHreg(HR_SP_PUMP_START, DEF_SP_START);
  mb.addHreg(HR_SP_PUMP_STOP,  DEF_SP_STOP);
  mb.addHreg(HR_SP_ALM_HH,     DEF_ALM_HH);
  mb.addHreg(HR_SP_ALM_LL,     DEF_ALM_LL);
  mb.addHreg(HR_PUMP_SPEED_SP, DEF_SPEED);

  mb.Coil(C_MODE_AUTO, true);            // power up in AUTO
  valveFilt = analogRead(PIN_AI_INLET_VALVE) * 100.0f / 4095.0f;
  lastScan = millis();
}

// =====================================================================
//  SCAN CYCLE
// =====================================================================
void loop() {
  mb.task();                             // service Modbus between scans
  wifiMaintain();

  uint32_t now = millis();
  if (now - lastScan < SCAN_MS) return;
  uint32_t dtMs = now - lastScan;
  lastScan = now;
  uint32_t scanStart = micros();
  float dt = dtMs / 1000.0f;

  // ---------------------------------------------------------------
  // 1) READ INPUTS
  // ---------------------------------------------------------------
  readButton(btnStart); readButton(btnStop); readButton(btnEstop);
  float valveRaw = analogRead(PIN_AI_INLET_VALVE) * 100.0f / 4095.0f;
  valveFilt += 0.2f * (valveRaw - valveFilt);          // simple low-pass filter

  bool cmdStart  = takeCoil(C_PUMP_START);
  bool cmdStop   = takeCoil(C_PUMP_STOP);
  bool cmdAck    = takeCoil(C_ALARM_ACK);
  bool cmdReset  = takeCoil(C_ESTOP_RESET);
  bool modeAuto  = mb.Coil(C_MODE_AUTO);
  bool simFault  = mb.Coil(C_SIM_PUMP_FAULT);

  // Validate setpoints written by SCADA (clamp, keep STOP below START)
  uint16_t spStart = clampReg(mb.Hreg(HR_SP_PUMP_START), 1000);
  uint16_t spStop  = clampReg(mb.Hreg(HR_SP_PUMP_STOP),  1000);
  if (spStop >= spStart) { spStop = (spStart > 10) ? spStart - 10 : 0; mb.Hreg(HR_SP_PUMP_STOP, spStop); }
  uint16_t spHH    = clampReg(mb.Hreg(HR_SP_ALM_HH), 1000);
  uint16_t spLL    = clampReg(mb.Hreg(HR_SP_ALM_LL), 1000);
  uint16_t spSpeed = clampReg(mb.Hreg(HR_PUMP_SPEED_SP), 1000);
  float levelStart = regToPct(spStart), levelStop = regToPct(spStop);
  float limHH = regToPct(spHH), limLL = regToPct(spLL);

  // ---------------------------------------------------------------
  // 2) LOGIC
  // ---------------------------------------------------------------
  // E-stop: latches on press, needs explicit reset (and button released)
  if (btnEstop.rose) estopLatched = true;
  if (cmdReset && !btnEstop.stable) estopLatched = false;

  // Pump fault: injected via training coil while pump is running; latches
  if (simFault && pumpCmd) faultLatched = true;

  // Run permissive: no E-stop, no fault, and not dry (LL protects the pump)
  bool permissive = !estopLatched && !faultLatched;
  bool dryRunTrip = (tankLevel <= limLL);

  bool wasRunning = pumpCmd;
  if (!permissive) {
    pumpCmd = false;
  } else if (modeAuto) {
    // AUTO: level control with hysteresis between START and STOP setpoints
    if (tankLevel >= levelStart) pumpCmd = true;
    if (tankLevel <= levelStop)  pumpCmd = false;
    if (btnStop.rose || cmdStop) pumpCmd = false;     // stop is always honored
  } else {
    // MANUAL: SCADA or local buttons. Stop wins over start.
    if (cmdStart || btnStart.rose) pumpCmd = true;
    if (cmdStop  || btnStop.rose)  pumpCmd = false;
  }
  if (dryRunTrip) pumpCmd = false;

  // Fault reset: alarm ack clears the fault latch once the injected fault is removed
  if (cmdAck && !simFault) faultLatched = false;

  if (pumpCmd && !wasRunning) pumpStarts++;
  if (pumpCmd) pumpRunMs += dtMs;

  // Process simulation ("the physics")
  float pumpSpeed = pumpCmd ? regToPct(spSpeed) : 0.0f;
  tankLevel += (valveFilt / 100.0f * FILL_RATE_PCT_S
              - pumpSpeed / 100.0f * DRAIN_RATE_PCT_S) * dt;
  if (tankLevel < 0)   tankLevel = 0;
  if (tankLevel > 100) tankLevel = 100;   // overflowing...

  // Alarms (with deadband)
  evalAlarm(almHH,    tankLevel >= limHH, tankLevel < limHH - ALARM_DEADBAND);
  evalAlarm(almLL,    tankLevel <= limLL, tankLevel > limLL + ALARM_DEADBAND);
  evalAlarm(almFault, faultLatched,       !faultLatched);
  if (cmdAck) { almHH.unacked = almLL.unacked = almFault.unacked = false; }
  bool anyActive  = almHH.active || almLL.active || almFault.active || estopLatched;
  bool anyUnacked = almHH.unacked || almLL.unacked || almFault.unacked;

  PlcState state = estopLatched ? ST_ESTOP : faultLatched ? ST_FAULT
                 : pumpCmd ? ST_RUNNING : ST_STOPPED;

  // ---------------------------------------------------------------
  // 3) WRITE OUTPUTS (physical + Modbus image)
  // ---------------------------------------------------------------
  pwmWrite((uint32_t)(pumpSpeed / 100.0f * ((1 << PWM_BITS) - 1)));

  bool flash = (now / 250) % 2;
  digitalWrite(PIN_DO_ALARM, anyUnacked ? flash : anyActive);

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  digitalWrite(PIN_DO_STATUS, wifiOk ? ((now / 500) % 2) : ((now / 100) % 2));

  mb.Ists(I_PUMP_RUNNING,   pumpCmd);
  mb.Ists(I_MODE_AUTO,      modeAuto);
  mb.Ists(I_ESTOP_ACTIVE,   estopLatched);
  mb.Ists(I_ALM_LEVEL_HH,   almHH.active);
  mb.Ists(I_ALM_LEVEL_LL,   almLL.active);
  mb.Ists(I_ALM_PUMP_FAULT, almFault.active);
  mb.Ists(I_ALM_UNACKED,    anyUnacked);
  mb.Ists(I_LOCAL_START_PB, btnStart.stable);
  mb.Ists(I_LOCAL_STOP_PB,  btnStop.stable);
  mb.Ists(I_LOCAL_ESTOP_PB, btnEstop.stable);

  mb.Ireg(IR_TANK_LEVEL,    pctToReg(tankLevel));
  mb.Ireg(IR_INLET_VALVE,   pctToReg(valveFilt));
  mb.Ireg(IR_PUMP_SPEED_FB, pctToReg(pumpSpeed));
  mb.Ireg(IR_PUMP_RUN_MIN,  (uint16_t)(pumpRunMs / 60000UL));
  mb.Ireg(IR_PUMP_STARTS,   pumpStarts);
  mb.Ireg(IR_HEARTBEAT,     ++heartbeat);
  mb.Ireg(IR_STATE,         (uint16_t)state);
  mb.Ireg(IR_WIFI_RSSI,     (uint16_t)(int16_t)(wifiOk ? WiFi.RSSI() : 0));
  mb.Ireg(IR_SCAN_TIME_MS,  lastScanTime);

  lastScanTime = (uint16_t)((micros() - scanStart) / 1000UL);

  // Serial "programming terminal" view, once per second
  static uint32_t lastPrint = 0;
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    const char* st[] = {"STOPPED", "RUNNING", "FAULT", "ESTOP"};
    Serial.printf("Lvl %5.1f%% | Valve %5.1f%% | Pump %-7s %5.1f%% | %s | HH:%d LL:%d FLT:%d UNACK:%d\n",
                  tankLevel, valveFilt, st[state], pumpSpeed, modeAuto ? "AUTO" : "MAN ",
                  almHH.active, almLL.active, almFault.active, anyUnacked);
  }
}
