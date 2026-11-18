"""Read the tank PLC over Modbus TCP and optionally send a command.

Usage:
    python modbus_test.py 192.168.1.50                 # read everything once
    python modbus_test.py 192.168.1.50 --watch         # refresh every second
    python modbus_test.py 192.168.1.50 --auto on       # set Auto mode
    python modbus_test.py 192.168.1.50 --valve on      # open inlet valve
    python modbus_test.py 192.168.1.50 --start         # manual pump start
    python modbus_test.py 192.168.1.50 --hr-base 1024  # OpenPLC %MW offset

Requires: pip install -r requirements.txt
"""

import argparse
import sys
import time

from pymodbus.client import ModbusTcpClient

COILS = {
    0: "P_101_Run", 1: "XV_101_Open", 2: "XA_100_Horn",
    8: "HMI_Start", 9: "HMI_Stop", 10: "HMI_AutoMode",
    11: "HMI_ValveOpen", 12: "HMI_AlarmAck",
    13: "LAH_100", 14: "LAL_100", 15: "P_101_Fault",
}
HOLDING = ["LT_100_Pct", "SP_PumpOn", "SP_PumpOff", "SP_HighAlarm",
           "SP_LowAlarm", "P_101_Starts", "P_101_RunSec", "PLC_Heartbeat"]


def read_all(client, hr_base):
    coils = client.read_coils(0, count=16)
    regs = client.read_holding_registers(hr_base, count=len(HOLDING))
    if coils.isError() or regs.isError():
        print(f"Read error: coils={coils} regs={regs}")
        return
    print("-" * 40)
    for addr, name in COILS.items():
        print(f"coil {addr:>2}  {name:<14} {'ON' if coils.bits[addr] else 'off'}")
    for i, name in enumerate(HOLDING):
        print(f"HR {hr_base + i:>4}  {name:<14} {regs.registers[i]}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("host")
    p.add_argument("--port", type=int, default=502)
    p.add_argument("--hr-base", type=int, default=0,
                   help="holding register number of %%MW0 (see scan_registers.py)")
    p.add_argument("--watch", action="store_true")
    p.add_argument("--auto", choices=["on", "off"])
    p.add_argument("--valve", choices=["on", "off"])
    p.add_argument("--start", action="store_true")
    p.add_argument("--stop", action="store_true")
    p.add_argument("--ack", action="store_true")
    args = p.parse_args()

    client = ModbusTcpClient(args.host, port=args.port)
    if not client.connect():
        sys.exit(f"Could not connect to {args.host}:{args.port}")

    try:
        if args.auto:
            client.write_coil(10, args.auto == "on")
        if args.valve:
            client.write_coil(11, args.valve == "on")
        if args.start:
            client.write_coil(8, True)
        if args.stop:
            client.write_coil(9, True)
        if args.ack:
            client.write_coil(12, True)

        read_all(client, args.hr_base)
        while args.watch:
            time.sleep(1)
            read_all(client, args.hr_base)
    except KeyboardInterrupt:
        pass
    finally:
        client.close()


if __name__ == "__main__":
    main()
