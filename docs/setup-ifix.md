# Setup: iFIX SCADA

iFIX menus vary by version. Use the **Getting Started with iFIX** guide for your version (link in [resources.md](resources.md)) alongside these steps, and note any differences here.

## 1. Install the trial

1. Request the free trial on GE Vernova's iFIX trial page. A single-use download link arrives by email.
2. The installer (~7.5 GB) includes iFIX, **Industrial Gateway Server (IGS)**, Proficy Historian and Operations Hub. Install iFIX, IGS and Historian.
3. The trial runs for **2 hours at a time**; restart iFIX to get another session. Save your work often.
4. If you need to match the iFIX version an existing site runs, the trial page also links the previous version (iFIX 2023).

## 2. Configure IGS (the Modbus driver)

1. Open the IGS configuration.
2. Add a **Channel**: driver = Modbus TCP/IP Ethernet. Name it `Channel1`.
3. Add a **Device** under it: name `ESP32`, IP = the ESP32's address, port 502, unit/station ID 1.
4. Check the device's addressing options: zero-based vs one-based addressing must match [io-map.md](io-map.md).
5. Add **Tags** from [io-map.md](io-map.md) (`P_101_Run` = `000001`, `LT_100_Pct` = `40000x`, etc.).
6. Use IGS's Quick Client to confirm every tag reads Good quality and live values. Toggle `HMI_ValveOpen` from the Quick Client and watch the LED.

## 3. Build the iFIX process database

1. Open **Database Manager**.
2. Create the blocks listed in the iFIX section of [io-map.md](io-map.md), each pointing at its IGS tag.
3. Set scan times (1 s is fine), EGU ranges (0–100 %), and alarm priorities on the DA blocks.
4. Put the blocks on scan and confirm values update.

## 4. Build the screens (WorkSpace)

**Tank Overview** (`TankOverview.grf`):
- Tank shape with a **fill animation** tied to `LT100`, plus a numeric readout.
- Pump symbol: color animation on `P101_RUN` (green running / gray stopped / red on `P101_FLT`).
- Valve symbol: color animation on `XV101_OPEN`.
- Buttons: Auto/Manual toggle (`MODE_AUTO`), Start/Stop (`P101_START` / `P101_STOP`, momentary), Valve Open/Close (`XV101_CMD`), Alarm ACK (`ALM_ACK`).
- Setpoint data links for `SP_PON`, `SP_POFF`, `SP_HI`, `SP_LO` with operator entry enabled.
- Text: pump starts and run time.
- Comm status: a **COMM LOSS** banner shown when `PLC_HB` hasn't changed for 5 s (see the VBA ideas below).

**Alarms**: add an Alarm Summary object filtered to this area.

**Trend**: chart of `LT100`, `P101_RUN`, `XV101_OPEN`. Then configure Proficy Historian to collect those tags for longer history.

## 5. Scripting (VBA) practice ideas

iFIX scripting is VBA. Small exercises:
- Confirmation dialog before a manual pump start.
- Disable the Start button while in Auto.
- Log every operator command to a text file with a timestamp.
- Comm watchdog: every second compare `PLC_HB` with its last value; if unchanged for 5 s, show the COMM LOSS banner.

## 6. Done when

- [ ] Every control in the control narrative works from the iFIX screen.
- [ ] Alarms appear in the summary and acknowledge from iFIX.
- [ ] The level trend shows several Auto pump cycles.
- [ ] Screenshots saved to `docs/images/`.
