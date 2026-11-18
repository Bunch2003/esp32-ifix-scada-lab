"""Find where OpenPLC put %MW0 in the holding-register space.

The PLC program loads default setpoints 70, 30, 90, 10 into %MW1..%MW4 on
first scan. This script reads common candidate ranges and reports where that
pattern appears, so you know the --hr-base value for modbus_test.py and the
holding-register addresses to use in IGS.

Usage:
    python scan_registers.py 192.168.1.50
"""

import sys

from pymodbus.client import ModbusTcpClient

CANDIDATE_STARTS = [0, 32, 100, 1024]
BLOCK = 64
PATTERN = [70, 30, 90, 10]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    client = ModbusTcpClient(sys.argv[1], port=502)
    if not client.connect():
        sys.exit("Could not connect")

    found = False
    for start in CANDIDATE_STARTS:
        rr = client.read_holding_registers(start, count=BLOCK)
        if rr.isError():
            print(f"{start:>5}-{start + BLOCK - 1}: not readable")
            continue
        regs = rr.registers
        nonzero = {start + i: v for i, v in enumerate(regs) if v}
        print(f"{start:>5}-{start + BLOCK - 1}: non-zero {nonzero or 'none'}")
        for i in range(len(regs) - len(PATTERN)):
            if regs[i:i + len(PATTERN)] == PATTERN:
                base = start + i - 1
                print(f"\n>>> Setpoints found. %MW0 = holding register {base}")
                print(f">>> Use --hr-base {base}; IGS address {400001 + base}")
                found = True
    client.close()
    if not found:
        print("\nPattern not found. Is the PLC program running?")


if __name__ == "__main__":
    main()
