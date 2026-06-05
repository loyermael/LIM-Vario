#!/usr/bin/env python3
# ============================================================
#  Script PRE-BUILD PlatformIO
#  Synchronise automatiquement l'UI generee par EEZ Studio
#  vers le firmware, AVANT chaque compilation.
#  -> tu fais juste "Build + Upload" dans VS, le reste est auto.
#
#  - copie src/ui depuis le projet EEZ
#  - exclut vars.c (maintenu a la main pour les variables natives)
#  - exclut le runtime EEZ (eez-flow.*) : il ne change PAS quand on edite
#    l'UI, et EEZ regenere parfois un header/source incoherents qui
#    cassent le build. On garde donc la paire qui compile, figee.
#  - corrige les includes lvgl/lvgl.h -> lvgl.h
# ============================================================
import os
import shutil
import glob

SRC = r"C:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario\L!M Vario UI\src\ui"
DST = r"C:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario\Firmware\src\ui"

# Fichiers JAMAIS ecrases par le sync (maintenus a la main / figes)
EXCLUDE = {"vars.c", "eez-flow.cpp", "eez-flow.h"}

def main():
    if not os.path.isdir(SRC):
        print("[sync_ui] ATTENTION: dossier EEZ introuvable, sync ignoree :")
        print("          " + SRC)
        print("          -> fais 'Build Files' dans EEZ Studio d'abord")
        return

    # Copie tous les fichiers UI sauf vars.c
    copied = 0
    for name in os.listdir(SRC):
        s = os.path.join(SRC, name)
        if not os.path.isfile(s):
            continue
        if name in EXCLUDE:
            continue
        shutil.copy2(s, os.path.join(DST, name))
        copied += 1

    # Corrige les includes LVGL
    for f in glob.glob(os.path.join(DST, "*.c")) + glob.glob(os.path.join(DST, "*.h")):
        try:
            with open(f, "r", encoding="utf-8", errors="ignore") as fh:
                c = fh.read()
            if "lvgl/lvgl.h" in c:
                with open(f, "w", encoding="utf-8") as fh:
                    fh.write(c.replace("lvgl/lvgl.h", "lvgl.h"))
        except Exception as e:
            print("[sync_ui] souci sur", f, ":", e)

    print("[sync_ui] UI EEZ synchronisee (%d fichiers) + includes corriges" % copied)

main()
