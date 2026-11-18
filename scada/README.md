# SCADA configuration backups

Working iFIX 2024 and Industrial Gateway Server (IGS) 7.6 configuration for this lab, saved so the SCADA side can be rebuilt on any PC. These are the files the programs saved themselves; they are binary, so edit them in iFIX and IGS, not in a text editor.

| File | What it is | Default location on the PC |
| --- | --- | --- |
| `ifix/TankOverview.grf` | Tank overview screen: level, fill, pump light, operator buttons, alarm summary | `<iFIX install>\PIC\` |
| `ifix/DATABASE.PDB` | iFIX process database: all blocks in [docs/io-map.md](../docs/io-map.md) | `<iFIX install>\PDB\` |
| `igs/esp32-ifix-lab.opf` | IGS project: `Channel1` (Modbus TCP/IP Ethernet) → device `ESP32` with every tag, plus the IGS sample channels | `C:\ProgramData\GE Digital\Industrial Gateway OPC Server\V7\default.opf` |

In this lab, `<iFIX install>` was `D:\Program Files\Proficy\iFIX`.

## Restoring on a fresh install

Do these with iFIX shut down.

1. **IGS:** open the IGS Configuration (right-click the tray icon → Configuration) → **File → Open** → `igs/esp32-ifix-lab.opf`. Change the **ESP32** device's **Device ID** to your board's IP address, then **File → Save**.
2. **iFIX database:** copy `DATABASE.PDB` into `<iFIX install>\PDB\`. If your SCU uses a database name other than `DATABASE`, rename the file to match (SCU → Configure → SCADA → Database Definition).
3. **iFIX screen:** copy `TankOverview.grf` into `<iFIX install>\PIC\`.
4. Check that the SCU lists the **IGS** driver (SCU → Configure → SCADA → I/O drivers), then start iFIX.

The node name in all links is `FIX` (for example `Fix32.FIX.LT100.F_CV`). If your node has a different name, update the links or use the default node name.

Never commit anything else from the IGS ProgramData folder. It holds the server's certificates and private keys (`CertificateStore`, `UA`, `Config API Service\RESTServer\cert`).

## Updating these backups

After changing the screen, database or IGS project, copy the files from the locations above back into this folder and commit them. Before changing the database a lot, also do **File → Export** in the Database Manager. It saves a CSV that you can read and diff.
