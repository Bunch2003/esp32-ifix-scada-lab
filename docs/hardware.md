# Hardware: Parts and Wiring

## Parts list

| Qty | Part | Role |
| --- | --- | --- |
| 1 | ESP32 dev board (ESP32-WROOM / DevKit V1) | PLC |
| 1 | Full-size breadboard + jumper wires | Field wiring |
| 3 | Momentary pushbuttons | START, STOP, high-high float switch |
| 3 | 10 kΩ resistors | Pull-downs for the buttons |
| 1 | 10 kΩ potentiometer | Level transmitter |
| 3 | LEDs (green pump, blue valve, red alarm) | Pump, valve, horn |
| 3 | 220–330 Ω resistors | LED current limit |
| 1 | Active buzzer (optional) | Audible horn |
| 1 | 3.3 V-compatible relay module (optional) | Switch a real load later |
| 1 | USB cable | Power + programming |

**Optional upgrade:** a 5 V mini water pump, two cups, and a float switch make the "tank" real. Drive the pump through a logic-level MOSFET or relay with its own 5 V supply and a flyback diode, never directly from a GPIO.

## ESP32 pin rules

- Logic is **3.3 V**. Never put 5 V on a GPIO.
- **GPIO 34, 35, 36, 39** are input-only and have **no internal pull-ups or pull-downs**, so buttons on them need the external 10 kΩ pull-down below.
- Use **ADC1 pins (GPIO 32–39)** for the potentiometer. ADC2 pins do not work while WiFi is on, and the PLC talks Modbus over WiFi.
- Avoid **GPIO 6–11** (connected to flash) and the strapping pins **0, 2, 12, 15** for I/O.
- Use the GPIO numbers from the OpenPLC Editor's ESP32 pin map for `%IX`, `%QX` and `%IW` (record them in [io-map.md](io-map.md)).

## Wiring

### Pushbuttons (START, STOP, LSHH)

```
3.3V ──[ button ]──┬── GPIO (input)
                   │
                 [10kΩ]
                   │
                  GND
```
Pressed = 3.3 V = TRUE. Released = pulled to GND = FALSE.

> Real plants wire STOP buttons and safety switches **normally closed**, so a broken wire stops the machine. Once the basic version works, try rewiring STOP as NC and inverting it in the logic. It's a common interview question.

### Potentiometer (LT-100)

```
3.3V ── pot end
        pot wiper ── GPIO (ADC1 input)
GND  ── pot other end
```

### LEDs (P-101, XV-101, XA-100)

```
GPIO (output) ──[220Ω]──►|── GND
                        LED
```

## Simulator firmware wiring

`firmware/esp32_modbus_sim` uses its own fixed pins (listed at the top of the sketch). It runs fine with nothing wired; the tank level is simulated in code.
