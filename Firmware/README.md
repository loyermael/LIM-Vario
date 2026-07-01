# L!M Vario — Display Unit Firmware

This directory contains the firmware for the **Display Unit** of the L!M Vario open-source gliding variometer, running on a **Waveshare ESP32-S3-Touch-LCD-2.1** round display (480×480 IPS, ST7701S RGB controller).

---

## 🌟 Overview

The Display Unit handles high-speed graphical rendering at 60 FPS and runs the real-time sensor fusion algorithms:
- **Inertial AHRS & Kalman Filter:** Fuses the onboard QMI8658 6-axis IMU (accelerometer + gyroscope) with pressure telemetry received from the calculator unit to calculate instantaneous needle response.
- **Total Energy (TE) Compensation:** Applies kinetic energy changes ($V/g \cdot dV/dt$) to compensate for stick movements.
- **Interactive LVGL 8.4 UI:** Built with EEZ Studio, featuring 6 customizable InfoBox zones, central glider Thermal Helper, 3D cylindrical zooming menus, and dual Climb/Cruise flight profiles.

---

## 📁 Directory Structure

```text
Firmware/
├── platformio.ini       # PlatformIO build configuration (ESP32-S3 Arduino framework)
├── include/
│   └── lv_conf.h        # LVGL library configuration & font definitions
├── src/
│   ├── main.cpp         # Main application loop, UI state machines, InfoBox configuration
│   ├── VarioFusion.*    # Mahony AHRS & 4-State Kalman filter implementation
│   ├── Display_ST7701.* # RGB panel display driver & CST820 touch driver
│   └── ui/              # EEZ Studio generated UI C code
├── tools/               # PC utilities & Python scripts
└── sync_ui.py           # Script to synchronize generated EEZ Studio UI files
```

---

## 🚀 Building & Uploading

Ensure you have [PlatformIO Core](https://docs.platformio.org/page/core.html) or the VS Code IDE extension installed.

### Build Firmware
```bash
pio run
```

### Upload to Hardware
Connect the Waveshare ESP32-S3 board via USB-C (native USB/JTAG port) and run:
```bash
pio run --target upload
```

### Monitor Serial Console
```bash
pio device monitor --baud 115200
```

---

## 🔄 Updating UI from EEZ Studio

Whenever you edit the visual design in `../L!M Vario UI/L!M Vario.eez-project`:
1. Save and export code in EEZ Studio.
2. Run the sync utility to update header paths and copy generated files into `src/ui/`:
```bash
python sync_ui.py
```

---

## 📜 License & Acknowledgments

Licensed under the **GPL-3.0 License**.
Builds upon open-source contributions from the gliding community including **FreeVarioGauge**, **Larus**, and **XCSoar**.
