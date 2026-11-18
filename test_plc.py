"""
Bench test for the ESP32 Mini PLC over Modbus TCP — run this BEFORE iFIX.
If this works, any later problem is in the iFIX/IGS config, not the ESP32.

    pip install pymodbus>=3.6
    python test_plc.py 192.168.1.50            # live monitor
    python test_plc.py 192.168.1.50 --manual   # switch to MANUAL and run the pump 5 s
"""
import sys
import time

from pymodbus.client import ModbusTcpClient

STATES = ["STOPPED", "RUNNING", "FAULT", "ESTOP"]
UNIT = 1  # the ESP32 library answers any unit ID; 1 is conventional


def call(fn, *args, **kw):
    """pymodbus renamed 'slave' -> 'device_id' in 3.10; support both."""
    try:
        return fn(*args, device_id=UNIT, **kw)
    except TypeError:
        return fn(*args, slave=UNIT, **kw)


def snapshot(c):
    ir = call(c.read_input_registers, 0, count=9)
    di = call(c.read_discrete_inputs, 0, count=10)
    hr = call(c.read_holding_registers, 0, count=5)
    if ir.isError() or di.isError() or hr.isError():
        raise RuntimeError(f"Modbus error: {ir} {di} {hr}")
    r, b, h = ir.registers, di.bits, hr.registers
    rssi = r[7] - 65536 if r[7] > 32767 else r[7]
    return (
        f"Lvl {r[0]/10:5.1f}% | Valve {r[1]/10:5.1f}% | Pump {STATES[r[6]]:7s} {r[2]/10:5.1f}% "
        f"| {'AUTO' if b[1] else 'MAN '} | HH:{b[3]:d} LL:{b[4]:d} FLT:{b[5]:d} UNACK:{b[6]:d} "
        f"| HB {r[5]:5d} | starts {r[4]} | RSSI {rssi} dBm "
        f"| SP start/stop {h[0]/10}/{h[1]/10}%"
    )


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ip = sys.argv[1]
    c = ModbusTcpClient(ip, port=502, timeout=2)
    if not c.connect():
        sys.exit(f"Could not connect to {ip}:502 — check WiFi, IP, firewall.")
    print(f"Connected to {ip}:502")

    try:
        if "--manual" in sys.argv:
            print("-> MANUAL mode, START pump")
            call(c.write_coil, 2, False)   # C_MODE_AUTO = 0
            call(c.write_coil, 0, True)    # C_PUMP_START (momentary)
            for _ in range(5):
                print(snapshot(c)); time.sleep(1)
            print("-> STOP pump, back to AUTO")
            call(c.write_coil, 1, True)    # C_PUMP_STOP
            call(c.write_coil, 2, True)

        last_hb = None
        while True:
            line = snapshot(c)
            hb = int(line.split("HB")[1].split("|")[0])
            stale = " <-- HEARTBEAT NOT CHANGING" if hb == last_hb else ""
            print(line + stale)
            last_hb = hb
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        c.close()


if __name__ == "__main__":
    main()
