# ESP32 Mini PLC — Tank & Pump (Modbus TCP) for iFIX practice

An ESP32 that behaves like a small PLC/RTU: fixed 100 ms scan cycle, its own control logic, and a Modbus TCP server that SCADA (iFIX via IGS) polls and commands.

**The process:** a tank fills through an inlet valve (the potentiometer) and a pump drains it (an LED whose brightness is the pump speed). In **AUTO** the pump starts at 80% and stops at 20%. In **MANUAL** the operator runs it from SCADA or the local buttons. It also has HH/LL level alarms, a pump fault you can inject for training, a latching E-stop, a heartbeat and run statistics.

## Parts
- ESP32 DevKit (WROOM-32 / "ESP32 Dev Module")
- 10 kΩ potentiometer
- 3 × momentary pushbuttons (START, STOP, E-STOP)
- 2 × LEDs + 2 × 220–330 Ω resistors (PUMP, ALARM)
- Breadboard + jumpers

## Wiring

| Function | ESP32 pin | Wiring |
|---|---|---|
| Inlet valve (pot) | GPIO34 | Wiper → GPIO34, outer legs → 3V3 and GND. **Must be an ADC1 pin (32–39); ADC2 doesn't work while WiFi is on.** |
| START button | GPIO25 | Button between pin and GND (internal pull-up) |
| STOP button | GPIO26 | Button between pin and GND |
| E-STOP button | GPIO27 | Button between pin and GND |
| PUMP LED | GPIO16 | Pin → resistor → LED anode, cathode → GND. Brightness = speed |
| ALARM LED | GPIO17 | Same. **Flashing** = unacknowledged, **solid** = acknowledged but still active |
| Status LED | GPIO2 | Onboard LED. Slow blink = OK, fast blink = no WiFi |

## Bring-up
1. Arduino IDE → Boards Manager: install **esp32 by Espressif**. Library Manager: install **modbus-esp8266** (by Alexander Emelianov, v4.1+).
2. **Credentials:** in the `esp32_plc/` folder, copy `secrets.example.h` to `secrets.h` and fill in your WiFi name, password and a free static IP on your LAN (gateway and subnet to match). `secrets.h` is in `.gitignore`, so your credentials never get committed. The sketch refuses to compile until that file exists.
3. Board: **ESP32 Dev Module** → Upload → open Serial Monitor at 115200. You'll see one status line per second.
4. **Test before iFIX:** `pip install pymodbus` and then run `python test_plc.py 192.168.1.50`. Turn the pot and watch the level change. `--manual` runs the pump for 5 s from the PC.
5. Only then set up IGS and iFIX (see below).

## Register map

Offsets on the wire are 0-based. The "Address" column is the classic 1-based form, which is also what IGS uses in its default setting (zero-based addressing ON, so `400001` = offset 0).

### Coils (0x) — commands from SCADA (read/write)
| Address | Offset | Name | Type |
|---|---|---|---|
| 000001 | 0 | PUMP_START | Momentary (PLC clears it) |
| 000002 | 1 | PUMP_STOP | Momentary |
| 000003 | 2 | MODE_AUTO (1=AUTO, 0=MANUAL) | Maintained |
| 000004 | 3 | ALARM_ACK | Momentary |
| 000005 | 4 | ESTOP_RESET | Momentary (only works once the E-stop button is released) |
| 000006 | 5 | SIM_PUMP_FAULT | Maintained (training: trips the pump if it's running) |

### Discrete inputs (1x) — status (read-only)
| Address | Offset | Name |
|---|---|---|
| 100001 | 0 | PUMP_RUNNING |
| 100002 | 1 | MODE_AUTO feedback |
| 100003 | 2 | ESTOP_ACTIVE |
| 100004 | 3 | ALM_LEVEL_HH |
| 100005 | 4 | ALM_LEVEL_LL |
| 100006 | 5 | ALM_PUMP_FAULT |
| 100007 | 6 | ALM_UNACKED (any) |
| 100008–100010 | 7–9 | Raw START / STOP / E-STOP button states |

### Input registers (3x) — process values (read-only)
| Address | Offset | Name | Scaling |
|---|---|---|---|
| 300001 | 0 | TANK_LEVEL | 0–1000 = 0.0–100.0 % |
| 300002 | 1 | INLET_VALVE | 0–1000 = 0.0–100.0 % |
| 300003 | 2 | PUMP_SPEED_FB | 0–1000 = 0.0–100.0 % |
| 300004 | 3 | PUMP_RUN_MIN | minutes |
| 300005 | 4 | PUMP_STARTS | count |
| 300006 | 5 | HEARTBEAT | +1 per scan (~10/s) |
| 300007 | 6 | STATE | 0=STOPPED 1=RUNNING 2=FAULT 3=ESTOP |
| 300008 | 7 | WIFI_RSSI | **signed** dBm (read as Short) |
| 300009 | 8 | SCAN_TIME_MS | ms |

### Holding registers (4x) — setpoints (read/write)
| Address | Offset | Name | Default |
|---|---|---|---|
| 400001 | 0 | SP_PUMP_START (AUTO) | 800 (80.0 %) |
| 400002 | 1 | SP_PUMP_STOP (AUTO) | 200 (20.0 %). The PLC forces it to stay below START |
| 400003 | 2 | SP_ALM_HH | 950 |
| 400004 | 3 | SP_ALM_LL | 50 (also the pump's dry-run trip) |
| 400005 | 4 | PUMP_SPEED_SP | 1000 (VFD-style speed reference) |

Setpoints reset to their defaults on power-up. A real PLC keeps them in retentive memory, and adding that with `Preferences` is a good extension.

## Behaviour notes
- **The PLC runs on its own.** Pull the network cable or stop iFIX and the tank keeps cycling in AUTO. That's the right split: SCADA supervises, the PLC controls.
- **Momentary command coils:** SCADA writes 1, the PLC acts on it and writes it back to 0. This avoids stuck "start" bits, a classic SCADA bug.
- **Alarm states:** a new alarm is active and unacked (LED flashing). After ALARM_ACK it is active and acked (solid). When the condition clears it goes to clear (off). Alarms have a 2% deadband so they don't chatter.
- **Fault reset:** turn SIM_PUMP_FAULT off, then send ALARM_ACK.
- **E-stop** latches on press and needs ESTOP_RESET after the button is released. (A real E-stop is hardwired and never goes through software or a network.)
- **Detecting comm loss:** if HEARTBEAT stops changing in iFIX, the data is stale. Build an alarm on that.

## iFIX / IGS quick notes
- In IGS: Channel = **Modbus TCP/IP Ethernet**. Device IP = `PLC_IP`, port 502, unit ID 1.
- Use 6-digit addresses (`400001`, not `40001`) to avoid the common addressing mix-up.
- Good first iFIX database blocks:
  - **AI** on 300001 (EGU 0–100 by scaling 0–1000), with HI/HIHI alarm limits in iFIX too
  - **DI** on 100001
  - **DO** on 000001 and 000002 for the start/stop buttons
  - **AO** on 400005 for a speed slider
- Back up the IGS project file separately, because it isn't included in iFIX project backups.

## Files
- `esp32_plc/esp32_plc.ino` — the PLC program
- `test_plc.py` — PC-side Modbus bench test and monitor
