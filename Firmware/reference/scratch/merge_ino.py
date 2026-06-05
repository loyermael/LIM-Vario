"""
Fusionne tous les fichiers .ino du projet FreeVarioGauge en un seul,
en respectant l'ordre correct : FreeVarioGauge.ino EN PREMIER,
puis les autres dans l'ordre logique de dépendance.
"""
import os
import glob

src_dir = r"c:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario\src\FreeVarioGauge"

# Ordre explicite : fichier principal d'abord, puis les utilitaires,
# puis les modules métier, et setup/loop en dernier
ORDERED_FILES = [
    "FreeVarioGauge.ino",     # globals, includes, read16/read32, filter, deg2rad
    "ArcRefresh.ino",
    "BMPReader.ino",
    "Calculations.ino",
    "DrawText.ino",
    "ChangeValues.ino",
    "EncoderReader.ino",
    "Menu.ino",
    "SerialScan.ino",
    "ShowBootScreen.ino",
    "UpdateMode.ino",
    "UpdateScreen.ino",
    "ValueRefresh.ino",
    "Loop.ino",
    "Setup.ino",
]

# Correction de la casse de puType pour la version installée de ESP32Encoder
REPLACEMENTS = [
    ("puType::down", "puType::DOWN"),
    ("puType::up",   "puType::UP"),
    ("puType::none", "puType::NONE"),
]

parts = []
for fname in ORDERED_FILES:
    fpath = os.path.join(src_dir, fname)
    if not os.path.exists(fpath):
        print(f"MISSING: {fname}")
        continue
    with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    parts.append(f"\n// ===== {fname} =====\n")
    parts.append(content)
    print(f"OK: {fname}")

merged = "".join(parts)

for old, new in REPLACEMENTS:
    merged = merged.replace(old, new)

# Écrire le fichier unique dans un nouveau dossier pour ne pas polluer le git
out_dir = r"c:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario\src\FreeVarioGaugeMerged"
os.makedirs(out_dir, exist_ok=True)

# Copier les fichiers statiques nécessaires
import shutil
for f in ["LogoOV.h"]:
    src = os.path.join(src_dir, f)
    dst = os.path.join(out_dir, f)
    if os.path.exists(src):
        shutil.copy2(src, dst)
        print(f"Copied: {f}")

# Copier le dossier data
data_src = os.path.join(src_dir, "data")
data_dst = os.path.join(out_dir, "data")
if os.path.exists(data_src) and not os.path.exists(data_dst):
    shutil.copytree(data_src, data_dst)
    print("Copied: data/")

out_file = os.path.join(out_dir, "FreeVarioGaugeMerged.ino")
with open(out_file, "w", encoding="utf-8") as f:
    f.write(merged)

print(f"\nFichier fusionné créé : {out_file}")
print(f"Taille : {len(merged)} caractères")
