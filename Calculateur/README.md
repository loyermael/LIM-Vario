# L!M Vario — Calculator Unit Firmware

This directory contains the firmware for the **Calculator Unit** (Sensor & Telemetry Processor) of the L!M Vario open-source gliding variometer, designed to run on a standard **ESP32 DevKit V4** microcontroller.

---

## 🌟 Overview

To keep the round display interface running smoothly at 60 FPS without sensor-polling interrupts or radio-stack stalls, the Calculator Unit acts as the hardware acquisition hub of the instrument:
- **Barometric & Airspeed Acquisition:** Polls the BMP388 high-precision static pressure sensor at 50 Hz and the optional MS4525DO differential pitot sensor for indicated airspeed and total-energy compensation.
- **GPS Positioning:** Reads a u-blox M10 receiver over UART1, auto-configured to 10 Hz NMEA output for ground speed, track and wind estimation.
- **User Interface Inputs:** Reads and debounces dual EC11 rotary encoders (rotation and button press) with reversible direction support (`ENC1_REVERSE`, `ENC2_REVERSE`).
- **Acoustic Variometer Audio (`VarioSound`):** Generates responsive climb and sink beep cadences through an I2S class-D amplifier (MAX98357A) driving an external speaker.
- **High-Speed Telemetry Link:** Formats sensor, GPS and encoder data into binary frames (`lim_link`) protected by CRC16 checksums and transmits them at 50 Hz over UART (115200 baud) to the display unit, which handles fusion, logging and the companion app.

---

## 📌 Hardware Pin Mapping

```text
I2C Bus (BMP388 / MS4525)  →  SDA: GPIO18  |  SCL: GPIO19   |  VCC: 3.3V
Encoder 1 (Menu / MacCready) →  A: GPIO32   |  B: GPIO33     |  Switch: GPIO4
Encoder 2 (Volume / Mode)  →  A: GPIO26     |  B: GPIO27     |  Switch: GPIO14
Audio (I2S, MAX98357A)     →  BCLK: GPIO22  |  LRCLK: GPIO23 |  DIN: GPIO25
GPS (u-blox M10, UART1)    →  RX: GPIO34    |  TX: GPIO13     |  10 Hz NMEA
UART Link to Display Unit  →  TX: GPIO17    |  RX: GPIO16     |  Baud: 115200
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
