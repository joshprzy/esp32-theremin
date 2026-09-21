# Quick ESP32 Theremin

Two bare wires. One hand for pitch, one for volume. No extra ICs.

Works best on a **classic ESP32** (the dual-core WROOM / DevKit with DAC pins 25 and 26). On an S3 it still runs, but audio comes out as PWM instead of a true analog DAC.

## Wiring

```
GPIO4   ---- pitch antenna   (10-20 cm stiff wire or brass rod)
GPIO13  ---- volume antenna  (same)
GPIO25  ---- audio out       (classic ESP32 DAC)
GND     ---- audio ground
3V3 / 5V ---- power as usual for your board
```

On **ESP32-S3 / C3 / C6** there is no DAC. Audio is PWM on **GPIO17**.

Do not hang a raw 8 Ω speaker on the pin. The DAC is line-level and weak.

Use one of:

- LM386 module + speaker
- PAM8403 / any small amp
- Powered PC speakers
- Headphones through a 1 kΩ resistor and a 10 µF capacitor in series from GPIO25

Keep the two antennas a hand-span apart, and keep USB-cable mess away from the pitch wire.

## Flash (Arduino IDE)

1. Boards manager: install **esp32 by Espressif Systems**.
2. Board: **ESP32 Dev Module** (classic) or your S3 board.
3. Open `esp32-theremin.ino`.
4. Upload.
5. Serial Monitor at **115200**.
6. Hands off both wires for about 1.5 seconds while it prints `Calibrating`.
7. Move a hand toward GPIO4. You should see Hz climb in the monitor and hear a tone.

Type `c` in the serial monitor and press Enter to recalibrate if the room or your stance changed.

## Flash (PlatformIO)

```
pio run -t upload
pio device monitor -b 115200
```

## Play

- Closer to the pitch wire → higher note (about A2 to A5).
- Closer to the volume wire → louder.
- Hands away → silence (there is a noise gate).
- If it drones at idle, recalibrate farther from the desk, or increase the gate in code (`pSmooth < 0.04f`).

Classic ESP32 `touchRead()` falls as you approach. S2/S3 readings rise. The sketch detects the chip and maps both.

## If it is silent

- Confirm you are on a classic ESP32 if you wired GPIO25. S3 has no DAC there.
- Watch serial: `rawP` should move when you grab the pitch wire.
- If raw values barely change, the wire is too short, or you calibrated with a hand already near it.
- Add a 10–20 cm antenna. A breadboard jumper alone is a short-range pad, not an antenna.
