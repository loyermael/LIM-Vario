#!/usr/bin/env python3
"""
gps_forward.py - Baro + GPS → ESP32 forwarder
Lit le barometre du telephone (LPS25H via termux-sensor)
+ position GPS (termux-location)
Envoie tout via TCP CLIENT vers l'ESP32 (192.168.2.1:4353)

Install :
  pkg update && pkg install python termux-api
  termux-setup-storage
  Autoriser localisation a Termux:API dans parametres Android

Usage :
  python gps_forward.py
"""

import socket
import subprocess
import json
import time
import threading
import math

# ── Config ────────────────────────────────────────────────────
ESP32_IP        = "192.168.2.1"
ESP32_PORT      = 4353
BARO_SENSOR     = "LPS25H Barometer"   # nom exact du capteur
QNH             = 1013.25              # pression reference (hPa) - ajuster si besoin
VARIO_ALPHA     = 0.3                  # lissage vario (0=tres lisse, 1=brut)
VARIO_AVG_SEC   = 10                   # fenetre moyenne vario (secondes)

# ── Donnees partagees entre threads ───────────────────────────
gps = {
    'lat': None, 'lon': None,
    'alt': 0.0,
    'speed': 0.0,    # km/h
    'bearing': 0.0,
    'fix': False
}

baro = {
    'pressure':  1013.25,  # hPa
    'altitude':  0.0,      # m
    'vario':     0.0,      # m/s instantane lisse
    'avg_vario': 0.0,      # m/s moyenne sur VARIO_AVG_SEC
    'available': False
}

# ── Formule barometrique ──────────────────────────────────────

def pressure_to_altitude(hpa, qnh=QNH):
    """Altitude barometrique depuis la pression (formule standard ICAO)"""
    return 44330.0 * (1.0 - (hpa / qnh) ** 0.1903)

# ── Utilitaires NMEA ──────────────────────────────────────────

def checksum(s):
    s = s.lstrip('$').split('*')[0]
    x = 0
    for c in s:
        x ^= ord(c)
    return f"{x:02X}"

def nmea(sentence):
    return f"${sentence}*{checksum(sentence)}\r\n"

def to_nmea_coord(deg, is_lat):
    d = int(abs(deg))
    m = (abs(deg) - d) * 60.0
    if is_lat:
        return f"{d:02d}{m:07.4f}", "N" if deg >= 0 else "S"
    return f"{d:03d}{m:07.4f}", "E" if deg >= 0 else "W"

def make_gprmc():
    t = time.gmtime()
    ts = f"{t.tm_hour:02d}{t.tm_min:02d}{t.tm_sec:02d}.00"
    ds = f"{t.tm_mday:02d}{t.tm_mon:02d}{t.tm_year % 100:02d}"
    la, ld  = to_nmea_coord(gps['lat'], True)
    lo, lod = to_nmea_coord(gps['lon'], False)
    spd_kn  = (gps['speed'] / 3.6) / 1.852   # km/h → noeuds
    hdg     = gps['bearing']
    s = f"GPRMC,{ts},A,{la},{ld},{lo},{lod},{spd_kn:.1f},{hdg:.1f},{ds},,"
    return nmea(s)

def make_gpgga():
    t = time.gmtime()
    ts = f"{t.tm_hour:02d}{t.tm_min:02d}{t.tm_sec:02d}.00"
    la, ld  = to_nmea_coord(gps['lat'], True)
    lo, lod = to_nmea_coord(gps['lon'], False)
    # Altitude : baro si dispo, sinon GPS
    alt = baro['altitude'] if baro['available'] else gps['alt']
    s = f"GPGGA,{ts},{la},{ld},{lo},{lod},1,08,1.0,{alt:.1f},M,0.0,M,,"
    return nmea(s)

# ── Thread GPS ────────────────────────────────────────────────

def gps_thread():
    print("[GPS] Thread demarre")
    while True:
        try:
            r = subprocess.run(
                ['termux-location', '-p', 'gps', '-r', 'last'],
                capture_output=True, text=True, timeout=8
            )
            if r.stdout.strip():
                d = json.loads(r.stdout)
                gps['lat']     = d.get('latitude')
                gps['lon']     = d.get('longitude')
                gps['alt']     = d.get('altitude', 0.0)
                gps['speed']   = d.get('speed', 0.0) * 3.6  # m/s → km/h
                gps['bearing'] = d.get('bearing', 0.0)
                gps['fix']     = gps['lat'] is not None
        except Exception as e:
            print(f"[GPS] Erreur: {e}")
        time.sleep(1)

# ── Thread Barometre ──────────────────────────────────────────

def baro_thread():
    print(f"[BARO] Thread demarre - {BARO_SENSOR}")
    prev_alt   = None
    prev_time  = None
    vario_inst = 0.0
    alt_history = []   # pour moyenne vario

    while True:
        try:
            r = subprocess.run(
                ['termux-sensor', '-s', BARO_SENSOR, '-n', '1'],
                capture_output=True, text=True, timeout=5
            )
            raw = r.stdout.strip()
            if raw:
                d = json.loads(raw)

                # Format : {"LPS25H Barometer": {"values": [hPa], ...}}
                # ou      : {"values": [hPa], ...}  selon version termux
                if BARO_SENSOR in d:
                    values = d[BARO_SENSOR].get('values', [])
                elif 'values' in d:
                    values = d['values']
                else:
                    # Cherche n'importe quelle cle avec 'values'
                    values = next(
                        (v['values'] for v in d.values() if isinstance(v, dict) and 'values' in v),
                        []
                    )

                if values:
                    pressure = float(values[0])
                    alt      = pressure_to_altitude(pressure)

                    baro['pressure']  = pressure
                    baro['altitude']  = alt
                    baro['available'] = True

                    now = time.time()
                    if prev_alt is not None and prev_time is not None:
                        dt = now - prev_time
                        if 0 < dt < 2.0:
                            raw_vario  = (alt - prev_alt) / dt
                            vario_inst = VARIO_ALPHA * raw_vario + (1 - VARIO_ALPHA) * vario_inst
                            baro['vario'] = vario_inst

                            # Moyenne glissante
                            alt_history.append((now, alt))
                            cutoff = now - VARIO_AVG_SEC
                            alt_history = [(t, a) for t, a in alt_history if t >= cutoff]
                            if len(alt_history) >= 2:
                                dt_avg = alt_history[-1][0] - alt_history[0][0]
                                da_avg = alt_history[-1][1] - alt_history[0][1]
                                if dt_avg > 0:
                                    baro['avg_vario'] = da_avg / dt_avg

                    prev_alt  = alt
                    prev_time = now

        except Exception as e:
            print(f"[BARO] Erreur: {e}")

        time.sleep(0.2)   # lecture 5 Hz pour un vario reactif

# ── Demarrage des threads ──────────────────────────────────────

threading.Thread(target=gps_thread,  daemon=True).start()
threading.Thread(target=baro_thread, daemon=True).start()

# ── Boucle principale TCP CLIENT → ESP32 ──────────────────────

print("=" * 45)
print(" Baro + GPS → ESP32 Forwarder")
print("=" * 45)
print(f" 1. Connecte WiFi : FV_Displayboard / 12345678")
print(f" 2. Connexion ESP32 : {ESP32_IP}:{ESP32_PORT}")
print(f" 3. Capteur baro  : {BARO_SENSOR}")
print(f" 4. QNH reference : {QNH} hPa")
print("=" * 45)

while True:
    try:
        print(f"\n[TCP] Connexion a l'ESP32 {ESP32_IP}:{ESP32_PORT}...")
        conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        conn.settimeout(5)
        conn.connect((ESP32_IP, ESP32_PORT))
        conn.settimeout(None)
        print("[TCP] Connecte a l'ESP32 !")

        while True:
            b = baro

            # Vario instantane → $POV,V
            conn.sendall(nmea(f"POV,V,{b['vario']:.2f}").encode())

            # Vario moyen → $POV,S
            conn.sendall(nmea(f"POV,S,{b['avg_vario']:.2f}").encode())

            # Altitude baro → $POV,E
            conn.sendall(nmea(f"POV,E,{b['altitude']:.1f}").encode())

            # Vent → 0 (pas de donnee vent sans XCSoar)
            conn.sendall(nmea(f"PLXVC,MC,0,0,0,0.00,0").encode())

            # Position GPS → $GPRMC + $GPGGA
            if gps['fix']:
                conn.sendall(make_gprmc().encode())
                conn.sendall(make_gpgga().encode())

            src = "BARO" if b['available'] else "GPS"
            print(
                f"[OK] [{src}] "
                f"Vario:{b['vario']:+.1f} "
                f"Vmoy:{b['avg_vario']:+.1f} "
                f"AltBaro:{b['altitude']:.0f}m "
                f"Pression:{b['pressure']:.1f}hPa "
                f"AltGPS:{gps['alt']:.0f}m "
                f"GS:{gps['speed']:.0f}km/h"
            )
            time.sleep(1)

    except (BrokenPipeError, ConnectionResetError, OSError) as e:
        print(f"[TCP] Deconnecte ({e}) - reconnexion dans 3s...")
        try: conn.close()
        except: pass
        time.sleep(3)
