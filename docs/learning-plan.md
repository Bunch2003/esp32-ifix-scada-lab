# Learning Plan

Target: a working PLC + SCADA stack, end to end, in about 8 weeks at 5–8 hours a week.

| Week | Focus | Deliverable |
| --- | --- | --- |
| 1 | Controls fundamentals: PLC scan cycle, discrete vs analog I/O, tags, Modbus function codes and address types, HMI vs SCADA vs historian | Notes in `docs/notes.md`; simulator firmware running (M1) |
| 2 | Install iFIX trial; IGS channel/device/tags; iFIX Database Manager basics | Live simulator values in iFIX (M2) |
| 3 | OpenPLC Editor; IEC 61131-3 Structured Text; wire the breadboard | Real PLC logic passing every test in setup-openplc.md (M3) |
| 4 | iFIX WorkSpace: graphics, animations, data links, buttons | Tank overview screen (M4) |
| 5 | Alarming, alarm summary, Historian, trends | Alarm + trend working (M5) |
| 6 | Ladder Diagram: contacts, coils, latches, timers, counters | Same program rebuilt in ladder (M6) |
| 7 | iFIX VBA scripting; high-performance HMI principles (gray backgrounds, color only for abnormal) | Scripted features + cleaned-up screen |
| 8 | Portfolio: screenshots, demo video, resume bullet | M7 done |

## Questions worth asking a SCADA integrator

- Which iFIX version do your clients run?
- Which PLC brands come up most (Allen-Bradley, Siemens, Modicon, other)?
- What industries: water/wastewater, manufacturing, other?
- What does a typical job look like: new screens, migrations, troubleshooting, panel work?
- What should a new controls technician learn first?

## Next topics after this project

- Allen-Bradley ladder (Studio 5000 / RSLogix concepts), since many municipal systems run Rockwell PLCs
- Other protocols: EtherNet/IP, DNP3 (common in utilities), OPC UA
- ISA-18.2 alarm management and ISA-101 HMI design basics
- Industrial networking and OT cybersecurity basics
