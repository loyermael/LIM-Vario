# L!M Vario - Version History & Changelog

## Versioning Scheme (Semantic Versioning)
We follow **MAJOR.MINOR.PATCH** format:
- **MAJOR** (`v1.x.x`): Major hardware change or full architecture rewrite.
- **MINOR** (`v0.8.x` -> `v0.9.x`): New major feature additions (e.g. InfoBox Customization System, Full English Internationalization, New EEZ UI screens).
- **PATCH** (`v0.8.0` -> `v0.8.1`): Bug fixes, comment translations, code cleanup, minor optimizations.

---

## [v0.9.0] - 2026-07-19 (Current Working Version)
### Companion App, Status Bar & New Metrics
- **Companion App fully working**: fixed the root cause that prevented any phone from connecting (internal RAM exhaustion — the pre-takeoff log buffer was moved from internal `.bss` to PSRAM, freeing enough DMA-capable RAM for the SoftAP to accept a client). Captive-portal DNS offering added.
- **Native Android APK**: thin WebView wrapper (`AndroidApp/`) that joins the vario's WiFi via `WifiNetworkSpecifier` + `bindProcessToNetwork` and shows a "not connected" fallback screen.
- **Screen status bar**: WiFi (on/off vs App connect) and battery (full/med/low from measured voltage, with hysteresis) icons wired to real state; reworked GPS icons and gauge background.
- **New info-box metrics**: `Alerts` (LINK / SD / BAT / GPS — most severe fault, else OK) and `Mode` (Climb / Cruise). Info-box zone 5 (status pod) activated in the editor.
- **RGB panel glitch fix**: `esp_lcd_rgb_panel_restart()` + full LVGL redraw after WiFi on/off, to recover the PSRAM framebuffer from the cache-disable transient.
- **Simulator kept in sync** with the firmware metric list.

## [v0.8.1] - 2026-07-01
### Internationalization & Code Base Cleanup
- **Firmware (`lm-vario`)**:
  - Fully translated all French comments to English in `ThermalDraw.cpp`, `LVGL_Driver.cpp`, `Gyro_QMI8658.cpp`.
  - Synchronized EEZ Studio UI codebase (`eez-flow.cpp`, `screens.c`).
- **Calculator (`calculateur`)**:
  - Fully translated all French comments and docstrings to English in `MS4525DO.h/cpp`, `VarioSound.h/cpp`, `GpsLink.h/cpp`, and `main.cpp`.
  - Standardized serial debug outputs and telemetry log formatting to English.
- **Documentation**:
  - Translated project `README.md` and repository setup guides to English.

---

## [v0.8.0] - 2026-06-30
### InfoBox System & Advanced UI Integration
- **6-Zone Customizable InfoBox System**: Implemented interactive zone selection and layout formatting (`ib_frame_0` to `ib_frame_5`).
- **Dynamic Formatting**: Added multi-line text wrapping for right-hand rectangular frames (`ib_frame_1`, `ib_frame_4`) and symmetrical formatting for left-hand frames.
- **Flight Profiles**: Introduced dedicated InfoBox configuration profiles for **Climb / Thermal (Ascendance)** and **Cruise / Glide (Transition)** modes.
- **Settings Audit**: Verified and completed system settings menus (Menu 10 InfoBox configuration).

---

## [v0.7.0] - Earlier Milestone
### Total Energy & Acoustic Vario Engine
- **Airspeed Auto-Detection**: Added MS4525DO differential pressure sensor auto-detection via I2C (`0x28`).
- **Total Energy Compensation**: Implemented real-time TE compensation ($dV/dt$) using airspeed or GPS ground speed.
- **Analog Acoustic Synthesizer**: Replaced PWM buzzer output with clean analog sine wave synthesis via ESP32 DAC1 (`GPIO25`).
- **Square Root Loudness Mapping**: Applied non-linear acoustic volume scaling for natural human ear perception.

---

## [v0.5.0] - Initial Prototype Base
### Dual-Processor Architecture Setup
- **Screen MCU (`lm-vario`)**: ESP32-S3 driving round touch LCD (480x480) via LVGL 8.4 and EEZ Studio flow engine.
- **Calculator MCU (`calculateur`)**: Classic ESP32 DevKit handling BMP388 barometric sampling, rotary encoders, and WiFi AP NMEA bridge (`LIM-Vario`).
- **Inter-Processor UART Link**: Full-duplex custom binary protocol (`lim_packet_t`, `lim_cmd_t`) running at 115200 baud.
