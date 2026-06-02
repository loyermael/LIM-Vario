# L!M Vario

Variomètre / calculateur de vol à voile open source pour planeur.

Affichage sur écran rond **Waveshare ESP32-S3-Touch-LCD-2.1** (480×480 IPS, ST7701S),
interface graphique réalisée avec **LVGL** + **EEZ Studio**.

---

## 🎯 Objectif

Un instrument de vol à voile affichant :
- Variomètre (aiguille analogique fluide, vario instantané + intégré)
- Assistant de centrage thermique
- Flèches de vent (mode transition)
- Réglages MacCready, volume, QNH via encodeurs
- Connexion XCSoar (WiFi / NMEA)

## 🛠️ Matériel

| Élément | Référence |
|---|---|
| MCU + écran | Waveshare ESP32-S3-Touch-LCD-2.1 (480×480 rond) |
| Contrôleur écran | ST7701S (RGB) |
| Tactile | CST820 |
| IMU | QMI8658 (accéléro + gyro) |

## 📁 Structure

```
platformio.ini → projet PlatformIO ESP32-S3 + LVGL
include/        → lv_conf.h (config LVGL)
src/            → firmware : drivers Waveshare + ui/ (interface EEZ)
tools/          → outils PC (simulation de vol, forward GPS, sim HTML)
reference/      → code de référence à porter (ancien firmware FreeVario, scripts XCSoar)
docs/           → notes et documentation
```

## 🚀 Compilation

Projet **PlatformIO** (Arduino-ESP32 3.x via pioarduino).

```
pio run            # compiler
pio run -t upload  # flasher
```

## 📜 Licence

Sous licence **GPL-3.0** (voir [LICENSE](LICENSE)).

### Crédits / code dérivé

Ce projet réutilise et s'inspire de logiciels open source :
- **[FreeVarioGauge](https://github.com/freevariode/FreeVarioGauge)** (GPL-2.0) — base variomètre, logique XCSoar/STF
- **[Larus](https://github.com/larus-breeze)** (GPL-3.0) — algorithmes vario/vent (à porter)
- **[XCSoar](https://github.com/XCSoar)** (GPL-2.0) — concepts, base de polaires de planeurs
- **Waveshare** — drivers écran/tactile/IMU pour l'ESP32-S3-Touch-LCD-2.1
