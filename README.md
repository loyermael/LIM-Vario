# L!M Vario

**Open-source Glider Variometer & Navigation Instrument** built around a Waveshare ESP32-S3 2.1" round IPS display (480×480) and a dedicated sensor/calculator ESP32 unit.

Inspired by [Larus](https://github.com/larus-breeze), [LX Navigation](https://gliding.lxnav.com/), [FreeVario](https://freevario.de), and [XCSoar](https://github.com/XCSoar).

![L!M Vario Instrument](GraphX/logo.png)

> **Current Firmware Version:** **V0.8** (Inertial AHRS/Kalman Vario + Total Energy Compensation + 6-Zone InfoBox System + SD Card Flight Logger)

---

## 🌟 Key Features

- **⚡ High-Precision Inertial Variometer (50 Hz):**
  - **Mahony AHRS + 4-State Kalman Filter:** Fuses onboard QMI8658 IMU accelerometers/gyros with BMP388 barometric altitude (`{altitude, vario, acceleration, accel bias}`).
  - Online accelerometer bias estimation (no tedious manual orientation calibration needed).
  - Near-instantaneous needle response with zero drift and long-term barometric stability.
- **🚀 Total Energy (TE) & GPS Compensation:**
  - Real-time TE compensation ($V_{comp} = V_{fuse} + \frac{V}{g} \frac{dV}{dt}$) derived from airspeed or GPS groundspeed NMEA feeds.
- **🎨 Modern LVGL 8.4 User Interface (EEZ Studio):**
  - Fluid analog needle, MacCready green arrow, and thermal integration arrow.
  - **6-Zone Interactive InfoBox System + Central Hub:** Fully configurable digital readout boxes displaying Instantaneous Vario, Average Vario, MacCready, Altitude, Airspeed (IAS), Ground Speed, Flight Time, Battery, Glide Ratio, Wind, and Climb Gain.
  - **Dynamic Central Display:** Real-time **Thermal Helper** (glider symbol showing lift distribution) or Wind Direction vector.
  - **3D Cylindrical Roller Wheel Menu:** Smooth zooming perspective effect (`transform_zoom`) when scrolling lists via rotary encoders, preventing circular edge text clipping.
  - **Dual Flight Profiles:** Instant switching between **Climb Mode** and **Cruise Mode** with independent NVS layout persistence.
- **🔊 Acoustic Variometer:**
  - Responsive Larus-style vario tone cadence generated via dedicated hardware PWM buzzer (`VarioSound`) with configurable sink alarm and volume control.
- **📂 Automatic IGC/CSV Flight Logging & WiFi Retrieval:**
  - Automatic takeoff/landing detection logging high-frequency flight telemetry (10 Hz) directly to an SD card (`VOL_<date>_<time>.csv`).
  - Built-in WiFi access point (`http://192.168.4.1`) on the calculator unit for effortless log retrieval and management from any smartphone or tablet.

---

## 📐 System Architecture

L!M Vario uses a dual-microcontroller architecture to guarantee real-time UI rendering at 60 FPS without sensor latency or WiFi interrupts:

```
┌─── Calculator Unit (ESP32 DevKit V4) ────┐        ┌─── Display Unit (ESP32-S3 2.1") ─────────┐
│ • BMP388 Pressure Sensor (Altitude/Vario)│        │ • 480×480 Round IPS Touchscreen (ST7701) │
│ • MS4525DO Pitot Sensor (Airspeed / TE)  │        │ • QMI8658 IMU (6-Axis Accel + Gyro)      │
│ • 2× EC11 Rotary Encoders + Buzzer       │─UART──►│ • Mahony AHRS + Kalman Sensor Fusion     │
│ • GPS/GNSS Receiver or WiFi NMEA Bridge  │ 115200 │ • LVGL 8.4 UI / 6-Zone InfoBox System    │
│ • SD Card Logger + WiFi Log Download AP  │        │ • 3D Roller Menu + Dual Flight Profiles  │
└──────────────────────────────────────────┘        └──────────────────────────────────────────┘
```

The two microcontrollers exchange high-frequency binary packets (`lim_link` protocol, `Shared/lim_link.h`) at 50 Hz protected by CRC16 checksum validation.

---

## 🛠️ Hardware & Wiring Specification

### 1. Components Bill of Materials (BOM)

| Component | Description | Role |
| :--- | :--- | :--- |
| **Waveshare ESP32-S3-Touch-LCD-2.1** | 2.1" Round 480×480 IPS Display | Main display unit, UI rendering, AHRS fusion, QMI8658 IMU |
| **ESP32 DevKit V4** | Standard ESP32 NodeMCU | Sensor calculator, encoder processing, SD logging, WiFi bridge |
| **BMP388 (CJMCU-388)** | High-precision barometric sensor | Absolute static pressure $\rightarrow$ altitude and raw vertical speed |
| **MS4525DO** *(Optional)* | Differential pressure sensor | Dynamic pressure $\rightarrow$ Indicated Airspeed (IAS) & TE compensation |
| **2× EC11 Rotary Encoders** | Push-button digital encoders | Encoder 1: Navigation / MacCready / Menu<br>Encoder 2: Audio Volume / Mode Switch / Long-press WiFi AP |
| **MicroSD Card Module** | SPI interface memory card | Autonomous CSV/IGC flight telemetry logging |

### 2. Calculator Unit Pinout (ESP32 DevKit V4)

```text
I2C Bus (Sensors)       →  SDA: GPIO18   |  SCL: GPIO19   |  VCC: 3.3V  |  GND: GND
Rotary Encoder 1 (Menu) →  Pin A: GPIO32 |  Pin B: GPIO33 |  Switch: GPIO25
Rotary Encoder 2 (Vol)  →  Pin A: GPIO26 |  Pin B: GPIO27 |  Switch: GPIO14
Audio Buzzer            →  PWM: GPIO23   |  GND: GND
UART Link to Display    →  TX: GPIO17    |  RX: GPIO16
SD Card Module (SPI)    →  MOSI: GPIO23* |  MISO: GPIO19* |  SCK: GPIO18* |  CS: GPIO5 (*or dedicated SPI bus)
```

### 3. Display Unit Pinout (Waveshare ESP32-S3 UART Header)

```text
UART Link from Calculator →  RX: GPIO44    |  TX: GPIO43    |  GND: GND
```

---

## 📁 Repository Structure

```text
LIM-Vario/
├── Firmware/                 # Display Unit Firmware (Waveshare ESP32-S3)
│   ├── src/main.cpp          # Application lifecycle, state machine, menu handlers
│   ├── src/VarioFusion.*     # Inertial AHRS & 4-State Kalman filter algorithms
│   ├── src/ui/               # LVGL 8.4 UI C code generated via EEZ Studio
│   └── platformio.ini        # PlatformIO build configuration
├── Calculateur/              # Calculator Unit Firmware (ESP32 DevKit V4)
│   ├── src/main.cpp          # Sensor polling (BMP388), encoder reading, UART link
│   ├── src/FlightLog.*       # SD card auto-segmenting flight logger & WiFi web server
│   └── src/VarioSound.*      # PWM acoustic variometer tone generator
├── Shared/                   # Cross-firmware shared headers
│   └── lim_link.h            # Binary UART frame structure & CRC16 verification
├── L!M Vario UI/             # EEZ Studio visual project file (.eez-project)
├── infobox_simulator.html    # Standalone interactive web simulator for UI design verification
└── GraphX/                   # Branding assets, icons, and logos
```

---

## 🚀 Building & Flashing

This project uses **PlatformIO**. Install [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://platformio.org/).

### 1. Flash the Display Firmware (ESP32-S3)
```bash
cd Firmware
pio run --target upload
```

### 2. Flash the Calculator Firmware (ESP32 DevKit)
```bash
cd ../Calculateur
pio run --target upload
```

> **💡 UI Development Note:** When modifying layouts in `L!M Vario UI/L!M Vario.eez-project`, run `python sync_ui.py` inside `Firmware/` to automatically generate and integrate updated C files prior to building.

---

## 🌐 Interactive Web Simulators

To test and iterate on user interface designs without hardware, open the standalone HTML simulators directly in your web browser:
- `infobox_simulator.html` — High-fidelity interactive mockup featuring live encoder rotation, 6-zone InfoBox assignment modal, and instant Climb/Cruise mode toggle.

---

## 🤝 Contributing & License

Contributions, bug reports, and pull requests are warmly welcomed from international glider pilots and firmware developers!

Distributed under the **GPL-3.0 License**. See `Firmware/LICENSE` for details.

### Acknowledgments
Special thanks to the open-source gliding community and the authors of **FreeVarioGauge**, **Larus**, and **XCSoar** for pioneering open flight instrumentation.
