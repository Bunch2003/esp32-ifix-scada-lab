# I/O and Tag Map

One table for every signal: where it is wired, its PLC address, its Modbus address, and its iFIX database tag. Keep this file in sync with `plc/tank_pump.st` and the iFIX database.

> **Verify before trusting.** OpenPLC's ESP32 GPIO assignments and its holding-register offset for `%MW` depend on the editor version. Fill in the GPIO column from the OpenPLC Editor's ESP32 board pin map, and confirm the register offset with `tools/scan_registers.py`.

## Physical I/O

| Tag | Description | PLC address | ESP32 GPIO | Breadboard device |
| --- | --- | --- | --- | --- |
| HS-101A | Local START | `%IX0.0` | _from editor_ | Pushbutton |
| HS-101B | Local STOP | `%IX0.1` | _from editor_ | Pushbutton |
| LSHH-100 | High-high float switch | `%IX0.2` | _from editor_ | Pushbutton |
| LT-100 | Level transmitter (raw) | `%IW0` | _from editor (ADC1 pin)_ | 10 kΩ potentiometer |
| P-101 | Pump run | `%QX0.0` | _from editor_ | LED + 220 Ω, or relay module |
| XV-101 | Inlet valve open | `%QX0.1` | _from editor_ | LED + 220 Ω |
| XA-100 | Alarm horn | `%QX0.2` | _from editor_ | LED + 220 Ω, or active buzzer |

## Modbus map (what iFIX / IGS reads and writes)

OpenPLC maps `%QX` to coils and `%IX` to discrete inputs starting at address 0, eight bits per byte (`%QX1.0` = coil 8). Addresses below are **0-based**; IGS's Modbus driver uses 1-based, type-prefixed addresses (coil 0 = `000001`, holding register 0 = `400001`).

### Coils (read/write)

| Coil | IGS address | PLC address | Tag | Direction |
| --- | --- | --- | --- | --- |
| 0 | 000001 | `%QX0.0` | P_101_Run | PLC → SCADA |
| 1 | 000002 | `%QX0.1` | XV_101_Open | PLC → SCADA |
| 2 | 000003 | `%QX0.2` | XA_100_Horn | PLC → SCADA |
| 8 | 000009 | `%QX1.0` | HMI_Start (momentary) | SCADA → PLC |
| 9 | 000010 | `%QX1.1` | HMI_Stop (momentary) | SCADA → PLC |
| 10 | 000011 | `%QX1.2` | HMI_AutoMode | SCADA → PLC |
| 11 | 000012 | `%QX1.3` | HMI_ValveOpen | SCADA → PLC |
| 12 | 000013 | `%QX1.4` | HMI_AlarmAck (momentary) | SCADA → PLC |
| 13 | 000014 | `%QX1.5` | LAH_100 high alarm | PLC → SCADA |
| 14 | 000015 | `%QX1.6` | LAL_100 low alarm | PLC → SCADA |
| 15 | 000016 | `%QX1.7` | P_101_Fault | PLC → SCADA |

### Discrete inputs (read only)

| Input | IGS address | PLC address | Tag |
| --- | --- | --- | --- |
| 0 | 100001 | `%IX0.0` | HS_101A_Start |
| 1 | 100002 | `%IX0.1` | HS_101B_Stop |
| 2 | 100003 | `%IX0.2` | LSHH_100 |

### Holding registers (read/write)

`%MW0` starts at register **N**, where N depends on the OpenPLC target. Find N with `python tools/scan_registers.py <esp32-ip>` (look for the register holding 70, 30, 90, 10 in a row: those are the default setpoints at N+1…N+4). The simulator firmware uses N = 0.

| Register | PLC address | Tag | Units | Direction |
| --- | --- | --- | --- | --- |
| N+0 | `%MW0` | LT_100_Pct | % | PLC → SCADA |
| N+1 | `%MW1` | SP_PumpOn | % | SCADA → PLC |
| N+2 | `%MW2` | SP_PumpOff | % | SCADA → PLC |
| N+3 | `%MW3` | SP_HighAlarm | % | SCADA → PLC |
| N+4 | `%MW4` | SP_LowAlarm | % | SCADA → PLC |
| N+5 | `%MW5` | P_101_Starts | count | PLC → SCADA |
| N+6 | `%MW6` | P_101_RunSec | s | PLC → SCADA |
| N+7 | `%MW7` | PLC_Heartbeat | count (wraps at 65535) | PLC → SCADA |

### Input registers (read only)

| Register | IGS address | PLC address | Tag |
| --- | --- | --- | --- |
| 0 | 300001 | `%IW0` | LT_100_Raw |

## iFIX database tags

Suggested iFIX block names (iFIX tag names are limited in length and character set; keep them short):

| iFIX tag | Block type | I/O address (IGS) | Notes |
| --- | --- | --- | --- |
| LT100 | AI | `Channel1.ESP32.LT_100_Pct` | EGU 0–100 %, alarm limits optional (PLC already alarms) |
| P101_RUN | DI | `Channel1.ESP32.P_101_Run` | |
| XV101_OPEN | DI | `Channel1.ESP32.XV_101_Open` | |
| P101_START | DO | `Channel1.ESP32.HMI_Start` | Momentary pushbutton |
| P101_STOP | DO | `Channel1.ESP32.HMI_Stop` | Momentary pushbutton |
| MODE_AUTO | DO | `Channel1.ESP32.HMI_AutoMode` | Toggle |
| XV101_CMD | DO | `Channel1.ESP32.HMI_ValveOpen` | Toggle |
| ALM_ACK | DO | `Channel1.ESP32.HMI_AlarmAck` | Momentary |
| LAH100 | DA | `Channel1.ESP32.LAH_100` | Digital alarm, High priority |
| LAL100 | DA | `Channel1.ESP32.LAL_100` | Digital alarm, High priority |
| P101_FLT | DA | `Channel1.ESP32.P_101_Fault` | Digital alarm, Critical |
| SP_PON | AO | `Channel1.ESP32.SP_PumpOn` | 0–100 % |
| SP_POFF | AO | `Channel1.ESP32.SP_PumpOff` | 0–100 % |
| SP_HI | AO | `Channel1.ESP32.SP_HighAlarm` | 0–100 % |
| SP_LO | AO | `Channel1.ESP32.SP_LowAlarm` | 0–100 % |
| P101_STARTS | AI | `Channel1.ESP32.P_101_Starts` | |
| P101_RUNSEC | AI | `Channel1.ESP32.P_101_RunSec` | |
| PLC_HB | AI | `Channel1.ESP32.PLC_Heartbeat` | Comm health: alarm if unchanged for 5 s |

Block types: AI/AO = analog input/output, DI/DO = digital input/output, DA = digital alarm. Confirm the exact block type names and I/O address syntax in the iFIX Database Manager for your version.
