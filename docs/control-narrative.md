# Control Narrative – Tank T-100 / Pump P-101

A control narrative is the plain-language spec an integrator writes before programming: what the process is, how each mode works, and what every alarm and interlock does. Write the logic to match this document, and update this document whenever the logic changes.

## 1. Process description

Tank T-100 is filled through inlet valve XV-101 and emptied by pump P-101. Level is measured by level transmitter LT-100 (potentiometer, 0–100%). A high-high float switch LSHH-100 provides a hardwired backup to the transmitter.

## 2. Equipment

| Tag | Device | Breadboard stand-in |
| --- | --- | --- |
| LT-100 | Level transmitter, 0–100% | 10 kΩ potentiometer |
| LSHH-100 | High-high level float switch | Pushbutton (pressed = level high-high) |
| HS-101A | Local START pushbutton | Pushbutton |
| HS-101B | Local STOP pushbutton | Pushbutton |
| P-101 | Pump | LED (or relay module) |
| XV-101 | Inlet valve | LED |
| XA-100 | Alarm horn / beacon | LED or active buzzer |

## 3. Modes

The operator selects **Auto** or **Manual** from SCADA.

### 3.1 Auto
- P-101 starts when level ≥ **Pump On** setpoint (default 70%).
- P-101 stops when level ≤ **Pump Off** setpoint (default 30%).
- Local and SCADA START are ignored in Auto. STOP is always honored, but in Auto it only holds the pump off while pressed; to keep the pump off, switch to Manual.

### 3.2 Manual
- P-101 starts on a local START press or SCADA START command.
- P-101 runs until STOP (local or SCADA), or an interlock trips.

### 3.3 Inlet valve XV-101
- Opened and closed by the operator from SCADA in either mode.
- Forced closed while the High alarm or LSHH-100 is active.

## 4. Interlocks (always active, both modes)

| Interlock | Condition | Action |
| --- | --- | --- |
| Dry-run protection | Level ≤ Low alarm setpoint | Pump stops and cannot start |
| Overfill protection | Level ≥ High alarm setpoint, or LSHH-100 active | Inlet valve forced closed |
| Pump fault | Pump has run longer than the max run time (default 60 s) | Pump stops; fault latches until acknowledged |
| Stop priority | Any STOP input | Overrides every start request |

## 5. Alarms

| Alarm | Trigger | Clears when |
| --- | --- | --- |
| LAH-100 High level | Level ≥ High alarm SP (default 90%) or LSHH-100 | Condition clears |
| LAL-100 Low level | Level ≤ Low alarm SP (default 10%) | Condition clears |
| P-101 Fault | Max run time exceeded | Condition reset by operator acknowledge |

- Any new alarm sounds horn XA-100.
- Operator **Acknowledge** from SCADA silences the horn and resets the pump fault.

## 6. SCADA (iFIX) requirements

- Tank overview: animated level fill, level %, pump and valve status colors, mode indicator.
- Operator controls: Auto/Manual, pump START/STOP (Manual), valve OPEN/CLOSE, alarm ACK.
- Setpoint entry: Pump On, Pump Off, High alarm, Low alarm (0–100%).
- Alarm summary for all alarms above.
- Trend: level, pump running, valve open (Proficy Historian).
- Counters: pump starts and total run time (seconds).
- Communication health: the PLC increments `PLC_Heartbeat` every scan. SCADA shows **COMM LOSS** and raises an alarm if it stops changing for 5 s, so operators never act on frozen values.

## 7. Setpoint defaults

| Setpoint | Default |
| --- | --- |
| Pump On | 70% |
| Pump Off | 30% |
| High alarm | 90% |
| Low alarm | 10% |
| Pump max run time | 60 s (constant in the PLC program) |
