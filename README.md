# S3 WiFi Theremin

Your ESP32-S3 makes a WiFi network. A browser on the computer plays the tone through the PC speakers. No aux cable.

## Pins (S3-N16R8 / your board)

- GPIO4 — pitch antenna (jumper standing up is fine)
- GPIO13 — volume antenna
- GND — not required for the WiFi path

## Tonight

1. Arduino IDE → board **ESP32S3 Dev Module** (not ESP32 Dev Module).
2. Upload `esp32-theremin.ino`.
3. Serial Monitor 115200. Hands off while it says `Calibrating`.
4. On the computer: join WiFi **S3-Theremin**, password **theremin**.
5. Browser: http://192.168.4.1
6. Tap **Tap to start sound**. Unmute the PC.
7. Move a finger near the GPIO4 wire.

Type `c` in serial to recalibrate.

The computer will drop its normal WiFi while it is joined to the S3. That is expected. Switch back when you are done.
