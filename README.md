<div align="center">

# L!M Vario

**Open-source inertial variometer & flight instrument for gliders**

A dual-ESP32 soaring instrument pairing a Waveshare ESP32-S3 2.1″ round IPS display (480×480) with a dedicated sensor/calculator unit — inertial AHRS + Kalman variometer, total-energy compensation, thermal assistant, SD flight logging and a WiFi companion app.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3-informational)
![Framework](https://img.shields.io/badge/framework-PlatformIO%20%7C%20Arduino-orange)
![UI](https://img.shields.io/badge/UI-LVGL%208.4%20%7C%20EEZ%20Studio-brightgreen)
![Version](https://img.shields.io/badge/firmware-v0.9.0-success)
![Status](https://img.shields.io/badge/status-active%20development-yellow)

<sub>Inspired by [Larus](https://github.com/larus-breeze) · [LXNAV](https://gliding.lxnav.com/) · [XCSoar](https://github.com/XCSoar) · [FreeVario](https://freevario.de)</sub>

</div>

---

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [System Architecture](#system-architecture)
- [Companion App](#companion-app)
- [Hardware](#hardware)
- [Building & Flashing](#building--flashing)
- [Repository Structure](#repository-structure)
- [Development Tools](#development-tools)
- [Roadmap](#roadmap)
- [License & Acknowledgments](#license--acknowledgments)

---

## Overview

L!M Vario is a self-contained soaring variometer built from two cooperating microcontrollers. A **calculator unit** samples the pressure and airspeed sensors, reads the encoders, generates the acoustic tone and logs the flight; a **display unit** runs the inertial sensor fusion and the LVGL user interface. The two exchange CRC-protected binary frames over UART, keeping the 480×480 UI fluid while sensor and radio work happen on the other core.

The project is fully open (GPL-3.0) and designed to be reproducible from off-the-shelf modules.

## Key Features

**Inertial variometer (50 Hz)**
- Mahony AHRS + 4-state Kalman filter fusing the onboard QMI8658 IMU with BMP388 barometric altitude (`{altitude, vario, acceleration, accel bias}`).
- Online accelerometer-bias estimation — no manual orientation calibration.
- Near-instant needle response with long-term barometric stability and zero drift.

**Total-energy & polar computations**
- Real-time TE compensation ($V_{comp} = V_{fuse} + \tfrac{V}{g}\tfrac{dV}{dt}$) from airspeed or GPS ground speed.
- MacCready speed-to-fly, netto and glide-ratio derived from a configurable glider polar (XCSoar database import).

**Modern UI (LVGL 8.4 / EEZ Studio)**
- Analog needle, MacCready arrow and integrated-climb arrow on a round gauge.
- **Configurable info-boxes** across the gauge (instant/average vario, MacCready, baro/GPS altitude, IAS, ground speed, wind, glide ratio, climb gain, flight time, alerts, mode…).
- **Status pod** with live WiFi, GPS and battery indicators.
- **Dynamic center**: thermal assistant (lift-distribution ring) in a turn, wind vector in cruise.
- **Dual flight profiles** (Climb / Cruise) with independent, NVS-persistent layouts.

**Acoustic variometer**
- Responsive vario tone cadence with configurable pitch, waveform, spread, sink alarm and volume.

**Automatic flight logging**
- Takeoff/landing detection writes 10 Hz CSV telemetry to SD (`VOL_<date>_<time>.csv`), including a 30 s pre-takeoff buffer.

**WiFi companion app**
- Built-in access point + captive portal serving a phone/tablet web app to configure the instrument, manage glider profiles and download logs — see below.

## System Architecture

A dual-microcontroller design guarantees a responsive UI without sensor latency or radio stalls:

```
┌─── Calculator Unit (ESP32) ──────────────┐         ┌─── Display Unit (ESP32-S3 2.1") ──────────┐
│ • BMP388 static pressure (altitude/vario)│         │ • 480×480 round IPS touchscreen (ST7701)  │
│ • MS4525DO differential pressure (IAS/TE) │  UART   │ • QMI8658 IMU (accel + gyro)              │
│ • 2× EC11 encoders + I2S audio            │ ──────► │ • Mahony AHRS + Kalman sensor fusion      │
│ • GPS (10 Hz) + FLARM traffic             │ 115200  │ • onboard microSD flight logger           │
│ • GPS-reception WiFi bridge               │ ◄────── │ • LVGL UI + WiFi companion app + profiles │
└───────────────────────────────────────────┘         └───────────────────────────────────────────┘
```

Barometric sampling, audio, GPS and the encoders live on the **calculator**; the **display unit** carries the IMU, the SD card slot (onboard the Waveshare board), the inertial fusion, the UI and the WiFi companion app.

The two units exchange binary packets (`lim_link` protocol, `Shared/lim_link.h`) protected by CRC validation.

## Companion App

Enabling **App connect** turns the calculator into a WiFi access point (`LIM-Vario`) hosting a self-contained web app (no internet required). A captive portal opens it automatically; it can also be added to the phone's home screen.

- Configure every setting, edit glider polars and manage the 5 profile slots.
- Download and delete SD flight logs.
- A **native Android APK** wrapper (`AndroidApp/`) is also provided: it joins the vario's WiFi programmatically (`WifiNetworkSpecifier` + `bindProcessToNetwork`) and shows a "not connected" fallback screen.

## Hardware

### Bill of Materials

| Component | Description | Role |
| :--- | :--- | :--- |
| **Waveshare ESP32-S3-Touch-LCD-2.1** | 2.1″ round 480×480 IPS display | Display unit, UI, AHRS fusion, QMI8658 IMU |
| **ESP32 (DevKit / WROOM-32D)** | Classic ESP32 | Calculator: sensors, encoders, audio, GPS |
| **BMP388** | High-precision barometric sensor | Static pressure → altitude & raw vario |
| **MS4525DO** *(optional)* | Differential pressure sensor | Dynamic pressure → IAS & TE compensation |
| **2× EC11** | Rotary encoders with push-button | Navigation / MacCready · Volume / mode |
| **MAX98357A + speaker** | I2S class-D amplifier | Acoustic vario output |
| **GPS module (u-blox M10, 10 Hz)** | GNSS receiver | Position for wind & track |
| **MicroSD** | Onboard slot on the display board | Autonomous CSV flight logging |

> A complete integrated-unit schematic (power, I2C, audio, encoders, dual-GPS) and a bench power-up test procedure are maintained alongside the project.

### Calculator pinout (ESP32)

```text
I2C sensors      SDA GPIO18 · SCL GPIO19            (BMP388 0x77, MS4525 0x28)
Encoder 1        A 32 · B 33 · SW 4                 (menu / MacCready)
Encoder 2        A 26 · B 27 · SW 14                (volume / mode)
Audio (I2S)      BCLK 22 · LRCLK 23 · DIN 25        (MAX98357A)
GPS (UART1)      RX 34 · TX 13                       (u-blox, config + NMEA)
FLARM            SoftSerial RX 35                    (via MAX3232, traffic)
Battery          ADC GPIO39/36                       (voltage divider)
UART to display  TX 17 · RX 16                       (Serial2 @ 115200)
```

### Display pinout (ESP32-S3 UART header)

```text
UART from calculator   RX GPIO44 · TX GPIO43 · GND
```

## Building & Flashing

The project builds with **[PlatformIO](https://platformio.org/)** (VS Code extension recommended).

```bash
# Display firmware (ESP32-S3)
pio run -d Firmware -t upload

# Calculator firmware (ESP32)
pio run -d Calculateur -t upload
```

> **UI development:** the display UI is authored in **EEZ Studio** (`L!M Vario UI/`). After *Build Files*, `Firmware/sync_ui.py` (run automatically as a PlatformIO pre-build step) copies the generated C into `Firmware/src/ui/`.

## Repository Structure

```text
LIM-Vario/
├── Firmware/            # Display unit firmware (ESP32-S3)
│   ├── src/main.cpp       # App lifecycle, state machine, menus, UI wiring
│   ├── src/VarioFusion.*  # Mahony AHRS + 4-state Kalman filter
│   ├── src/FlightLog.*    # SD logger + WiFi companion server + captive portal
│   ├── src/ui/            # LVGL UI generated by EEZ Studio
│   └── sync_ui.py         # EEZ → firmware UI sync (pre-build)
├── Calculateur/         # Calculator unit firmware (ESP32)
│   ├── src/main.cpp       # Sensor sampling, encoders, UART link
│   ├── src/GpsLink.*      # GPS / Condor telemetry ingestion
│   └── src/VarioSound.*   # Acoustic vario synthesizer
├── Shared/lim_link.h    # Binary UART frame + CRC (shared header)
├── L!M Vario UI/        # EEZ Studio project (.eez-project)
├── AndroidApp/          # Native Android companion (WebView wrapper)
├── Simulator/           # PC simulator (real LVGL UI + HTTP server)
├── Hardware_Schema/     # Netlist & schematics
└── KiCAD/               # PCB project
```

## Development Tools

- **PC simulator** (`Simulator/`) — builds the real EEZ `screens.c` against LVGL on Windows (GDI), with an embedded HTTP server to test the companion app without hardware.
- **Web mockup** (`infobox_simulator.html`) — standalone interactive info-box editor for quick UI iteration in a browser.

## Roadmap

- MS4525 airspeed wiring + XCSoar-style TE compensation.
- GPS-time → RTC synchronization.
- FLARM traffic display (radar + collision alerts).
- Full-sun high-contrast display mode.
- Real-flight validation of thermal assistant and wind estimation.

## License & Acknowledgments

Distributed under the **GPL-3.0** license — see [`LICENSE`](LICENSE). Contributions, issues and pull requests from glider pilots and firmware developers are welcome.

Thanks to the open-source gliding community and to the authors of **Larus**, **XCSoar** and **FreeVario** for pioneering open flight instrumentation.
