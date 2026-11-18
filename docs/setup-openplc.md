# Setup: OpenPLC on the ESP32

Menu names change between OpenPLC Editor versions. If a step doesn't match what you see, follow the intent and note the real menu path here.

## 1. Prove the board and network first (recommended)

Before OpenPLC, flash `firmware/esp32_modbus_sim` with the Arduino IDE (you already have it installed):

1. Install the **Espressif ESP32** board package (Boards Manager) and the **modbus-esp8266** library by Alexander Emelianov (Library Manager).
2. Copy `secrets.h.example` to `secrets.h` and enter your WiFi.
3. Board: *ESP32 Dev Module*. Upload. Open Serial Monitor at 115200 to see the IP address.
4. From this repo: `pip install -r tools/requirements.txt`, then `python tools/modbus_test.py <ip> --watch`.

If values update, WiFi + Modbus work, and any later problem is in OpenPLC or iFIX setup, not the board.

**Tip:** give the ESP32 a DHCP reservation in your router so its IP never changes. iFIX/IGS will point at that IP.

## 2. Install OpenPLC Editor

Download the editor from the OpenPLC Project site (see [resources.md](resources.md)) and install it on Windows.

## 3. Create the project

1. New project → name `TankPump`.
2. Add a Program POU named `TankPump`. Language: **Structured Text** for the first pass (ladder version is milestone M6).
3. In the variable table, add every variable from the `VAR` blocks in `plc/tank_pump.st`, with the same name, type and **Location** (`%IX0.0`, `%MW0`, …). Add the two constants as constants.
4. Paste the program body (everything between the `END_VAR` lines and `END_PROGRAM`) into the code area.
5. Confirm the resource/task: one cyclic task, 50 ms, running an instance of `TankPump`.
6. Build. Fix any compiler messages (log what you changed in the file header).

## 4. Check the ESP32 pin map

Open the editor's board/hardware settings for **ESP32** and record which GPIO each `%IX0.x`, `%QX0.x` and `%IW0` uses. Write them into [io-map.md](io-map.md) and wire the breadboard to match ([hardware.md](hardware.md)). Make sure the analog input lands on an ADC1 pin (GPIO 32–39).

## 5. Upload to the ESP32

1. Open the upload / transfer-to-board dialog.
2. Board: **ESP32**. Select the COM port.
3. Enable **Modbus TCP** and enter your WiFi SSID and password. Set a static IP here if offered.
4. Upload. Watch the serial monitor for the IP.

## 6. Verify

1. `python tools/scan_registers.py <ip>` → note the `%MW0` offset it reports. Update the `N` value in [io-map.md](io-map.md).
2. `python tools/modbus_test.py <ip> --hr-base <N> --watch`
3. Test against [control-narrative.md](control-narrative.md):
   - Turn the pot: `LT_100_Pct` follows 0–100.
   - Manual: press START → pump LED on; press STOP → off.
   - `--auto on`: pump starts at ≥70 %, stops at ≤30 %.
   - Pot below 10 %: low alarm, pump forced off, horn on. `--ack` silences horn.
   - Pot above 90 % or press LSHH: high alarm, valve forced closed.
   - Run pump >60 s: fault, pump stops; `--ack` resets.

When every line passes, milestone M3 is done.
