# L!M Vario — Netlist / câblage de l'unité intégrée

Architecture : 2× ESP32 (Calculateur + Écran), GPS via FLARM (RS232→MAX3232),
capteurs I2C (BMP388 statique + MS4525 différentiel), audio DAC→PAM8403,
alimentation bus 12 V → buck 5 V.

> Convention : `[U1]` = repère composant. Les noms en MAJ (ex. `+5V`, `SDA`) sont des nets.

---

## 1. Liste des composants (repères)

| Rep. | Composant | Référence |
|------|-----------|-----------|
| J1 | Entrée alimentation 12 V (bornier 2 pts) | — |
| F1 | Fusible 1 A | Littelfuse 0233001 (verre 5×20) |
| D1 | Diode Schottky anti-inversion | SS34 (3 A / 40 V) |
| TVS1 | Suppresseur de transitoires bidir. | SMBJ16CA |
| C1 | Réservoir entrée buck | 1000 µF / 25 V (EEU-FR1E102) |
| U1 | Convertisseur 12→5 V | TracoPower TSR 2-2450 |
| C2 | Filtrage sortie 5 V | 10 µF céram. |
| U2 | Calculateur | ESP32 DevKit V4 |
| U3 | Baromètre statique | BMP388 (I2C 0x77) |
| U4 | Pression différentielle | MS4525DO-DS5AI001DP (I2C 0x28) |
| R1, R2 | Pull-ups I2C | 4,7 kΩ |
| U5 | Adaptateur RS232↔TTL | module MAX3232 (MAX3232CSE) |
| J2 | Connecteur FLARM (RJ45) | selon modèle FLARM |
| C3 | Couplage audio | 1 µF céram. X7R |
| U6 | Ampli audio | module PAM8403 |
| C4 | Découplage alim PAM | 10 µF (ou 1000 µF) |
| LS1 | Haut-parleur | 8 Ω 1 W (→ idéal 4 Ω 2-3 W) |
| U7 | Écran | ESP32-S3-Touch-LCD-2.1 |

---

## 2. Nets principaux

| Net | Description |
|-----|-------------|
| `+12V` | Entrée bus planeur (après J1) |
| `+12P` | 12 V protégé (après F1 + D1) |
| `+5V`  | Sortie buck → U2, U7, U6 |
| `+3V3` | Régulé par chaque ESP32 → capteurs + côté logique U5 |
| `GND`  | Masse commune (tout) |
| `SDA` / `SCL` | Bus I2C (U2 ↔ U3, U4) |
| `TELEM` | Calculateur → Écran (données vario) |
| `CMD`   | Écran → Calculateur (commande sink, etc.) |
| `GPS_RX` | MAX3232 R1OUT → U2 (NMEA reçu) |
| `GPS_TX` | U2 → MAX3232 T1IN (optionnel) |
| `FLARM_TXD` / `FLARM_RXD` | Lignes RS232 côté FLARM |
| `AUDIO` | U2 GPIO25 → couplage → PAM |
| `SPK+` / `SPK-` | Sortie HP |

---

## 3. Connexions par composant

### Alimentation
```
J1.+12  → F1 → D1(anode→cathode) → +12P
J1.GND  → GND
+12P    → TVS1 → GND          (écrêtage)
+12P    → C1(+) ; C1(-) → GND (réservoir)
+12P    → U1.VIN
U1.GND  → GND
U1.VOUT → +5V
+5V     → C2 → GND
```

### Calculateur U2 (ESP32)
| Pin U2 | Net | Vers |
|--------|-----|------|
| 5V / VIN | `+5V` | U1.VOUT |
| GND | `GND` | masse |
| 3V3 (sortie) | `+3V3` | U3, U4, U5 (VCC logique) |
| GPIO18 | `SDA` | U3, U4 + R1 |
| GPIO19 | `SCL` | U3, U4 + R2 |
| GPIO17 | `TELEM` | U7.GPIO44 |
| GPIO16 | `CMD` | U7.GPIO43 |
| GPIO23 | `GPS_RX` | U5.R1OUT |
| GPIO22 | `GPS_TX` | U5.T1IN (optionnel) |
| GPIO25 | `AUDIO` | C3 → U6.IN |
| GPIO32 / 33 / 4 | — | Encodeur 1 (A / B / SW) |
| GPIO26 / 27 / 14 | — | Encodeur 2 (A / B / SW) |

> ⚠️ GPIO1 / GPIO3 : NE RIEN brancher (UART0 USB debug).

### Capteurs I2C
| Pin | Net | Note |
|-----|-----|------|
| U3(BMP388).VCC | `+3V3` | adresse 0x77 |
| U3.GND | `GND` | exposé à la **statique** (altitude) |
| U3.SDA / SCL | `SDA` / `SCL` | |
| U4(MS4525).VCC | `+3V3` | adresse 0x28 |
| U4.GND | `GND` | port HAUT = Pitot, port BAS = statique |
| U4.SDA / SCL | `SDA` / `SCL` | |
| R1 | `SDA` ↔ `+3V3` | pull-up 4,7 kΩ |
| R2 | `SCL` ↔ `+3V3` | pull-up 4,7 kΩ |

### GPS FLARM (RS232) — U5 MAX3232
| Pin U5 | Net | Vers |
|--------|-----|------|
| VCC | `+3V3` | U2.3V3 |
| GND | `GND` | masse commune avec FLARM |
| T1IN | `GPS_TX` | U2.GPIO22 |
| R1OUT | `GPS_RX` | U2.GPIO23 |
| T1OUT | `FLARM_RXD` | J2 (entrée data FLARM) |
| R1IN | `FLARM_TXD` | J2 (sortie data FLARM) |
| J2.GND | `GND` | — |
| J2.+12 (alim) | **NC** | non connecté (on alimente par le bus) |

### Audio
```
U2.GPIO25 → C3(1µF) → U6.IN(L)
U6.V+  → +5V ; U6.GND → GND
+5V    → C4 → GND          (découplage, + côté +5V)
U6.OUT+ → SPK+ → LS1
U6.OUT- → SPK- → LS1
```

### Écran U7 (ESP32-S3)
| Pin U7 | Net | Vers |
|--------|-----|------|
| 5V | `+5V` | U1.VOUT |
| GND | `GND` | masse |
| GPIO44 (RX1) | `TELEM` | U2.GPIO17 |
| GPIO43 (TX1) | `CMD` | U2.GPIO16 |

---

## 4. Liaison Calculateur ↔ Écran (3 fils)
```
U2.GPIO17 ─────────────► U7.GPIO44   (TELEM : vario → écran)
U2.GPIO16 ◄───────────── U7.GPIO43   (CMD : commandes → calc)
U2.GND    ───────────────U7.GND      (masse commune)
```

---

## 5. Rappels critiques
- Masse `GND` strictement commune : 12 V−, U2, U7, FLARM, capteurs, PAM.
- FLARM = RS232 sur les lignes data → MAX3232 obligatoire (vérifier le modèle).
- MS4525 = différentiel (dynamique) ; la statique/altitude vient du BMP388.
- Avant vol réel : `SIM_VARIO = 0` et `SOUND_TEST = 0` dans le firmware.
- Baud GPS FLARM : typiquement 19200 (à confirmer selon modèle).
