import time
import math
import sys
import os

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("\n[ERREUR] Le module 'pyserial' est requis pour ce script.")
    print("Veuillez l'installer en exécutant : pip install pyserial\n")
    sys.exit(1)

def calculate_checksum(sentence):
    """Calcule le checksum standard NMEA (XOR de tous les caractères entre $ et *)"""
    content = sentence
    if content.startswith('$') or content.startswith('!'):
        content = content[1:]
    if '*' in content:
        content = content.split('*')[0]
    
    xor = 0
    for char in content:
        xor ^= ord(char)
    return f"{xor:02X}"

def parse_igc_lat_lon(lat_str, lon_str):
    """Convertit les coordonnées IGC (DDMMMMM[N/S]) en degrés décimaux"""
    # Latitude: DDMMMMM N/S -> DD + MM.MMM / 60
    lat_deg = float(lat_str[0:2])
    lat_min = float(lat_str[2:7]) / 1000.0
    lat = lat_deg + (lat_min / 60.0)
    if lat_str[7] == 'S':
        lat = -lat
        
    # Longitude: DDDMMMMM E/W -> DDD + MM.MMM / 60
    lon_deg = float(lon_str[0:3])
    lon_min = float(lon_str[3:8]) / 1000.0
    lon = lon_deg + (lon_min / 60.0)
    if lon_str[8] == 'W':
        lon = -lon
        
    return lat, lon

def calculate_distance_and_bearing(lat1, lon1, lat2, lon2):
    """Calcule la distance en mètres et le cap en degrés entre deux points GPS"""
    # Formule de Haversine pour la distance
    R = 6371000.0 # rayon de la Terre en mètres
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    
    a = math.sin(dphi/2.0)**2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda/2.0)**2
    c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))
    distance = R * c
    
    # Formule pour le gisement (bearing)
    y = math.sin(dlambda) * math.cos(phi2)
    x = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(dlambda)
    bearing = math.degrees(math.atan2(y, x))
    heading = int((bearing + 360) % 360)
    
    return distance, heading

def read_igc_file(filepath):
    """Lit un fichier IGC et extrait la liste des points de vol valides"""
    flight_points = []
    print(f"Lecture du fichier IGC : {filepath}...")
    
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                if line.startswith('B'):
                    # Format standard d'une ligne B d'IGC:
                    # B 123456 4600000N 00600000E A 00123 00456
                    # 0 1-6    7-14     15-23    24 25-29 30-34
                    # Temps(UTC), Lat, Lon, Validité, Alt-Baro, Alt-GPS
                    if len(line) >= 35:
                        time_str = line[1:7]
                        lat_str = line[7:15]
                        lon_str = line[15:24]
                        valid = line[24]
                        alt_baro = float(line[25:30])
                        alt_gps = float(line[30:35])
                        
                        if valid == 'A':
                            lat, lon = parse_igc_lat_lon(lat_str, lon_str)
                            flight_points.append({
                                'time': time_str,
                                'lat': lat,
                                'lon': lon,
                                'alt_baro': alt_baro,
                                'alt_gps': alt_gps
                            })
    except Exception as e:
        print(f"Erreur lors de la lecture du fichier IGC : {e}")
        return None
        
    print(f"Chargement réussi : {len(flight_points)} points de vol extraits.")
    return flight_points

def print_dashboard(protocol, mode, phase, elapsed, alt, vario, tas, heading, sent_sentence, time_utc="00:00:00"):
    os.system('cls' if os.name == 'nt' else 'clear')
    print("=" * 75)
    print("                 REPLAY DE VOL PLANEUR POUR FREEVARIO")
    print("=" * 75)
    print(f" Source : {mode:<25} | Protocole : {protocol}")
    print(f" Temps simulé : {elapsed:.1f} s            | Heure UTC : {time_utc}")
    print(f" Statut : {phase}")
    print("-" * 75)
    print(f" Altitude (Baro) : {alt:6.1f} m     |  Vario : {vario:+5.2f} m/s")
    print(f" Vitesse (GS/TAS): {tas:6.1f} km/h  |  Cap (Heading) : {heading:3d}°")
    print("-" * 75)
    print(" Dernière trame brute envoyée :")
    print(f"   {sent_sentence}")
    print("=" * 75)
    print(" Appuyez sur Ctrl+C pour arrêter la simulation.")

def main():
    print("Recherche des ports série disponibles...")
    ports = list(serial.tools.list_ports.comports())
    
    if not ports:
        print("\nAucun port série trouvé ! Veuillez brancher votre ESP32.")
        input("\nAppuyez sur Entrée pour quitter...")
        sys.exit(1)
        
    print("\nPorts disponibles :")
    for i, p in enumerate(ports):
        print(f" [{i}] {p.device} - {p.description}")
        
    try:
        choice = int(input(f"\nChoisissez le port (0-{len(ports)-1}) : "))
        port_name = ports[choice].device
    except (ValueError, IndexError):
        print("Choix invalide.")
        sys.exit(1)
        
    print("\nModes de simulation :")
    print(" [1] Rejeu de vol réel à partir d'un fichier IGC (.igc)")
    print(" [2] Simulateur mathématique dynamique autonome")
    
    sim_mode_choice = input("\nChoisissez le mode (1 ou 2) : ")
    
    flight_points = None
    mode_name = ""
    
    if sim_mode_choice == '1':
        igc_path = input("\nEntrez le chemin ou le nom du fichier .igc : ").strip()
        # Enlever les guillemets si l'utilisateur a glissé-déposé le fichier
        igc_path = igc_path.replace('"', '').replace("'", "")
        
        if not os.path.exists(igc_path):
            print(f"Fichier non trouvé : {igc_path}")
            sys.exit(1)
            
        flight_points = read_igc_file(igc_path)
        if not flight_points:
            print("Impossible de lire les points du vol IGC.")
            sys.exit(1)
        mode_name = f"Replay IGC ({os.path.basename(igc_path)})"
    else:
        mode_name = "Simulation Mathématique"

    print("\nProtocoles de sortie :")
    print(" [1] OpenVario / XCSoar (115200 baud, trames $PFV)")
    print(" [2] Larus (38400 baud, trames $PLAR + $GPRMC)")
    print(" [3] LK8000 / variometre paragliding (115200 baud, trames $LK8EX1 + $GPRMC)")

    proto_choice = input("\nChoisissez le protocole (1, 2 ou 3) : ")
    if proto_choice == '2':
        protocol = "Larus"
        baud = 38400
    elif proto_choice == '3':
        protocol = "LK8000"
        baud = 115200
    else:
        protocol = "OpenVario/XCSoar"
        baud = 115200

    print(f"\nConnexion à {port_name} à {baud} baud...")
    try:
        ser = serial.Serial(port_name, baud, timeout=1)
        print("Connecté ! Attente du démarrage de l'ESP32", end="", flush=True)
        for _ in range(7):
            time.sleep(1)
            print(".", end="", flush=True)
        print(" OK")
    except Exception as e:
        print(f"Erreur de connexion : {e}")
        sys.exit(1)

    input("\nPrêt ! Appuyez sur Entrée pour démarrer la simulation...")

    start_time = time.time()
    last_send_time = 0
    interval = 0.2     # Fréquence d'envoi : 5 Hz (toutes les 200 ms)
    
    # Variables de vol
    alt = 450.0
    vario = 0.0
    vaa = 0.0
    tas = 100.0
    heading = 90
    time_utc = "12:00:00"
    
    vario_history = []
    igc_index = 0
    
    try:
        while True:
            current_time = time.time()
            elapsed = current_time - start_time
            
            # --- MODE REPLAY IGC ---
            if flight_points is not None:
                # On progresse dans l'index IGC à la vitesse réelle (1 point par seconde)
                igc_index = int(elapsed)
                
                if igc_index >= len(flight_points) - 1:
                    print("\nFin du fichier IGC atteinte !")
                    break
                
                # Récupération des points courants pour interpolation
                pt1 = flight_points[igc_index]
                pt2 = flight_points[igc_index + 1]
                
                # Heure UTC
                h, m, s = pt1['time'][0:2], pt1['time'][2:4], pt1['time'][4:6]
                time_utc = f"{h}:{m}:{s}"
                
                # Interpolation pour fluidité à 5Hz (intervalle de 0.2s)
                sub_step = (elapsed - igc_index) # valeur entre 0.0 et 1.0
                
                # Altitude barométrique
                alt = pt1['alt_baro'] + (pt2['alt_baro'] - pt1['alt_baro']) * sub_step
                
                # Le vario brut est la dérivée de l'altitude-pression baro par seconde
                # (Différence d'altitude baro entre pt1 et pt2)
                vario = pt2['alt_baro'] - pt1['alt_baro']
                
                # Calcul de la vitesse sol (GS) et du cap (Heading) géographique entre les deux coordonnées GPS
                dist, head = calculate_distance_and_bearing(pt1['lat'], pt1['lon'], pt2['lat'], pt2['lon'])
                
                # Distance en 1s -> km/h (* 3.6)
                tas = dist * 3.6
                if tas < 5.0:  # bruit GPS au sol
                    tas = 0.0
                    
                heading = head
                phase = f"Vol en cours (Rejeu IGC - point {igc_index}/{len(flight_points)})"
            
            # --- MODE SIMULATION MATHÉMATIQUE AUTONOME ---
            else:
                if elapsed < 20:
                    phase = "Cheminement (Transition)"
                    target_vario = -1.2 + 0.2 * math.sin(elapsed * 0.5)
                    target_tas = 120.0 + 5.0 * math.sin(elapsed * 0.3)
                    heading = int((90 + 5 * math.sin(elapsed * 0.1)) % 360)
                elif elapsed < 25:
                    phase = "Entrée dans la pompe !"
                    t = (elapsed - 20) / 5.0
                    target_vario = -1.2 + t * 3.7
                    target_tas = 120.0 - t * 35.0
                    heading = int((90 + (elapsed - 20) * 10) % 360)
                elif elapsed < 65:
                    phase = "Spirale en Thermique (+2.5 m/s)"
                    target_vario = 2.5 + 0.8 * math.sin(elapsed * 2.0) + 0.4 * math.cos(elapsed * 4.5)
                    target_tas = 85.0 + 3.0 * math.sin(elapsed * 0.8)
                    heading = int((elapsed * 12) % 360)
                else:
                    phase = "Sortie de pompe & Transition"
                    t = min(1.0, (elapsed - 65) / 5.0)
                    target_vario = 2.5 - t * 3.9
                    target_tas = 85.0 + t * 45.0
                    heading = int((heading + (1 - t) * 5) % 360)
                
                vario = target_vario
                alt += vario * interval
                tas = target_tas
                
                # Heure fictive
                now_sec = int(elapsed) % 60
                now_min = int(elapsed / 60) % 60
                time_utc = f"14:{now_min:02d}:{now_sec:02d}"

            # Vario moyen filtré (VAA) sur 20 secondes (100 ticks)
            vario_history.append(vario)
            if len(vario_history) > 100:
                vario_history.pop(0)
            vaa = sum(vario_history) / len(vario_history)

            # Speed-to-fly (STF) : vitesse optimale selon MacCready simplifié
            # En spirale : vitesse min-sink ~80 km/h ; en transition : ~110 km/h (MC=1)
            if vario >= 0.5:
                stf = 80.0
            else:
                stf = 90.0 + 20.0  # MC=1 -> ~110 km/h en transition

            # Envoi des trames au tick
            if current_time - last_send_time >= interval:
                last_send_time = current_time
                sent_sentence = ""

                if protocol == "OpenVario/XCSoar":
                    tick = int(elapsed * 5) % 6

                    sentences = [
                        f"$PFV,VAR,{vario:.2f}*",
                        f"$PFV,VAA,{vaa:.2f}*",
                        f"$PFV,TAS,{tas:.1f}*",
                        f"$PFV,HIG,{alt:.0f}*",
                        f"$PFV,GRS,{tas:.1f}*",
                        f"$PFV,STF,{stf:.1f}*"
                    ]

                    # Envoi vario principal
                    var_sentence = f"$PFV,VAR,{vario:.2f}*\r\n"
                    ser.write(var_sentence.encode('ascii'))
                    
                    # Extra info
                    extra_sentence = sentences[tick] + "\r\n"
                    ser.write(extra_sentence.encode('ascii'))
                    
                    # Vent fictif (pour allumer la rose des vents)
                    if tick == 0:
                        ser.write(f"$PFV,AWS,3.5*\r\n".encode('ascii'))  # 3.5 m/s
                        ser.write(f"$PFV,AWD,220*\r\n".encode('ascii'))  # du 220°
                        
                    sent_sentence = var_sentence.strip() + " | " + extra_sentence.strip()
                    
                elif protocol == "LK8000":
                    # Protocole LK8000 / variometre paragliding
                    # $LK8EX1,pressure_pa,altitude_m,vario_cms,temp_c,batt_mv*CS
                    pressure_pa = int(101325 * ((1 - alt / 44330.0) ** 5.2561))
                    vario_cms = int(vario * 100)
                    lk8ex1 = f"$LK8EX1,{pressure_pa},{int(alt)},{vario_cms},99999,99999*"
                    lk8ex1 += calculate_checksum(lk8ex1) + "\r\n"
                    ser.write(lk8ex1.encode('ascii'))

                    # $GPRMC pour vitesse sol et cap
                    h_val = time_utc.replace(":", "")[0:2]
                    m_val = time_utc.replace(":", "")[2:4]
                    s_val = time_utc.replace(":", "")[4:6]
                    gprmc = f"$GPRMC,{h_val}{m_val}{s_val}.00,A,4600.00,N,00600.00,E,{tas/1.852:.1f},{heading:.1f},180526,,,A*"
                    gprmc += calculate_checksum(gprmc) + "\r\n"
                    ser.write(gprmc.encode('ascii'))

                    sent_sentence = lk8ex1.strip()

                else:
                    # Protocole Larus
                    plarv = f"$PLARV,{vario:.2f},{vaa:.2f},{alt:.1f},{tas:.1f}*"
                    plarv += calculate_checksum(plarv) + "\r\n"
                    ser.write(plarv.encode('ascii'))

                    plara = f"$PLARA,0,0,{heading}*"
                    plara += calculate_checksum(plara) + "\r\n"
                    ser.write(plara.encode('ascii'))

                    # Vent fictif
                    plarw = f"$PLARW,220,3.5,I*"
                    plarw += calculate_checksum(plarw) + "\r\n"
                    ser.write(plarw.encode('ascii'))

                    # Horloge de l'écran synchronisée avec le fichier IGC !
                    h_val, m_val, s_val = time_utc.replace(":", "")[0:2], time_utc.replace(":", "")[2:4], time_utc.replace(":", "")[4:6]
                    gprmc = f"$GPRMC,{h_val}{m_val}{s_val}.00,A,4600.00,N,00600.00,E,{tas/1.852:.1f},0.0,180526,,,A*"
                    gprmc += calculate_checksum(gprmc) + "\r\n"
                    ser.write(gprmc.encode('ascii'))

                    sent_sentence = plarv.strip()
                
                # Dashboard console
                print_dashboard(protocol, mode_name, phase, elapsed, alt, vario, tas, heading, sent_sentence, time_utc)
                
            time.sleep(0.02)
            
    except KeyboardInterrupt:
        print("\n\nSimulation interrompue par l'utilisateur.")
    finally:
        ser.close()
        print("Port série fermé.")

if __name__ == "__main__":
    main()
