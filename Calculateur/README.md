# L!M Vario — Calculator Unit Firmware

This directory contains the firmware for the **Calculator Unit** (Sensor & Telemetry Processor) of the L!M Vario open-source gliding variometer, designed to run on a standard **ESP32 DevKit V4** microcontroller.

---

## 🌟 Overview

To keep the round display interface running smoothly at 60 FPS without sensor polling interrupts or wireless stack blocking, the Calculator Unit acts as the hardware hub of the instrument:
- **Barometric & Airspeed Acquisition:** Polls the BMP388 high-precision static pressure sensor at 50 Hz and optional MS4525DO differential pitot tube sensor.
- **User Interface Inputs:** Reads and debounces dual EC11 rotary encoders (rotations and button clicks) with reversible direction support (`ENC1_REVERSE`, `ENC2_REVERSE`).
- **High-Speed Telemetry Link:** Formats sensor and encoder data into binary frames (`lim_link`) verified by CRC16 checksums and transmits them at 50 Hz over UART (115200 baud) to the display unit.
- **Acoustic Variometer Audio (`VarioSound`):** Generates responsive, Larus-style climb and sink beep cadences using hardware PWM on an external speaker or buzzer.
- **Autonomous Flight Logger (`FlightLog`):** Automatically detects takeoff and landing, continuously logging 10 Hz flight telemetry (altitude, vario, vertical acceleration) to a MicroSD card in standard IGC/CSV formats (`VOL_<date>_<time>.csv`).
- **WiFi Access Point & Web Server:** A long-press on Encoder 2 activates an onboard WiFi hotspot (`LIM-Vario`, `http://192.168.4.1`), allowing pilots to download or delete flight logs directly from a phone or laptop after landing.

---

## 📌 Hardware Pin Mapping

```text
I2C Bus (BMP388 / Pitot)  →  SDA: GPIO18   |  SCL: GPIO19   |  VCC: 3.3V
Encoder 1 (Menu/Nav)      →  Pin A: GPIO32 |  Pin B: GPIO33 |  Switch: GPIO25
Encoder 2 (Volume/AP)     →  Pin A: GPIO26 |  Pin B: GPIO27 |  Switch: GPIO14
Audio Speaker / Buzzer    →  PWM Output: GPIO23
UART Link to Display Unit →  TX: GPIO17    |  RX: GPIO16    |  Baud: 115200
SD Card Module (SPI Bus)  →  MOSI: GPIO23* |  MISO: GPIO19* |  SCK: GPIO18* |  CS: GPIO5 (*or secondary SPI)
```

---

## 🚀 Building & Uploading

Ensure you have [PlatformIO Core](https://docs.platformio.org/) installed.

### Build Firmware
```bash
pio run
```

### Upload to ESP32 DevKit
Connect your ESP32 DevKit board via USB and execute:
```bash
pio run --target upload
```

### Monitor Serial Console
```bash
pio device monitor --baud 115200
```

---

## 📜 License

Licensed under the **GPL-3.0 License**. See root repository for details.
