# L!M Vario

**Open source glider variometer** built on a Waveshare ESP32-S3 2.1" round display (480×480).

Inspired by [Larus](https://github.com/larus-breeze), [LX Navigation](https://gliding.lxnav.com/) and [FreeVario](https://freevario.de).

> ⚠️ Work in progress — V0.5 (basic vario, no compensation yet)

---

## Hardware

| Component | Role |
|-----------|------|
| [Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1) | Round 480×480 display + touch |
| ESP32 DevKit V4 | Sensor & encoder calculator |
| BMP388 (CJMCU-388) | Absolute pressure → altitude + vario |
| MS4525DO *(optional)* | Differential pressure → airspeed → TE compensation |
| 2× EC11 rotary encoder | MC setting / menu + volume / mode |

### Wiring (Calculator — ESP32 DevKit V4)

```
BMP388 / MS4525DO  →  SDA: GPIO21  SCL: GPIO22  VCC: 3V3  GND: GND
Encoder 1 (MC/menu)→  A: GPIO32   B: GPIO33   SW: GPIO25
Encoder 2 (vol/mode)→ A: GPIO26   B: GPIO27   SW: GPIO14
UART → Display     →  TX: GPIO17  RX: GPIO16
```

### Wiring (Display — ESP32-S3, UART header)

```
RX: GPIO44  TX: GPIO43  GND: GND
```

---

## Architecture

```
┌─ Calculator (ESP32 DevKit) ──────────┐       ┌─ Display (ESP32-S3) ────────┐
│  BMP388 → altitude, vario            │       │  LVGL UI (EEZ Studio)       │
│  MS4525DO → airspeed (optional)      │─UART─►│  Needle + MC arrow          │
│  Encoder 1 + 2 → events              │       │  Quick menu                 │
│  Sends lim_link binary frame @50 Hz  │       │  Buzzer (GPIO0)             │
└──────────────────────────────────────┘       └─────────────────────────────┘
```

The two ESP32s communicate via **UART at 115200 baud** using a fixed-size binary frame (`Shared/lim_link.h`) with CRC16 validation.

---

## Features (V0.5)

- [x] Real-time vario needle (BMP388, 50 Hz)
- [x] Thermal integration arrow
- [x] MC (MacCready) green arrow — encoder 1
- [x] Quick menu: QNH / Water ballast / Bugs / Pilot weight / Profile / Sink sound
- [x] Altitude display (QNH-corrected)
- [x] Vario + integrated vario digital readout
- [x] Auto-close menu after inactivity
- [x] Long press → Setup menu (placeholder)
- [ ] Buzzer / audio vario
- [ ] IMU fusion (faster vario response)
- [ ] GPS compensation (V0.7)
- [ ] MS4525DO TE compensation (V0.8)

---

## Project Structure

```
LIM-Vario/
├── Firmware/           # Display firmware (ESP32-S3)
│   ├── src/main.cpp    # Main logic: menu, liaison, needles
│   ├── src/ui/         # EEZ Studio generated UI code
│   ├── include/        # LVGL config
│   └── platformio.ini
├── Calculateur/        # Calculator firmware (ESP32 DevKit)
│   └── src/main.cpp    # BMP388 + encoders + UART send
├── Shared/
│   └── lim_link.h      # Shared binary protocol
└── L!M Vario UI/       # EEZ Studio project (LVGL UI designer)
```

---

## Build & Flash

Requirements: [PlatformIO](https://platformio.org/) + VS Code

```bash
# Display (ESP32-S3)
cd Firmware
pio run --target upload

# Calculator (ESP32 DevKit)
cd Calculateur
pio run --target upload
```

> The `sync_ui.py` script automatically syncs EEZ Studio generated code before each build.

---

## Roadmap

| Version | Feature |
|---------|---------|
| **V0.5** | Basic vario (BMP388), 2 ESP32, quick menu ✅ |
| **V0.6** | IMU + baro fusion (faster response) |
| **V0.7** | GPS compensation (phone BLE/WiFi) |
| **V0.8** | MS4525DO TE compensation (pitot) |
| **V0.9** | Dedicated GPS module (nav, wind, thermal) |

---

## License

[GPL-3.0](Firmware/LICENSE) — open source, contributions welcome.
