#!/usr/bin/env python3
# ============================================================
#  Script PRE-BUILD PlatformIO, partage entre Calculateur et Firmware.
#  Genere automatiquement la chaine de version affichee dans le menu
#  About de l'ecran (et imprimee au boot du calculateur) :
#     <base depuis VERSION>+<hash git court>[-dirty]
#  ex: 0.10.0+a1b2c3d          (build propre, HEAD = ce commit)
#      0.10.0+a1b2c3d-dirty    (modifs non committees au moment du build)
#
#  -> plus besoin d'editer une chaine "0.9.0" a la main a chaque fix :
#     le numero MAJOR.MINOR.PATCH dans VERSION reste un choix humain
#     (bump volontaire), mais le suffixe identifie sans ambiguite le
#     commit exact qui tourne sur la carte, a chaque build.
# ============================================================
Import("env")
import subprocess
import os

REPO_ROOT = r"C:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario"
VERSION_FILE = os.path.join(REPO_ROOT, "Shared", "VERSION")


def base_version():
    try:
        with open(VERSION_FILE, "r", encoding="utf-8") as f:
            return f.read().strip()
    except Exception:
        return "0.0.0"


def git_suffix():
    try:
        h = subprocess.check_output(
            ["git", "-C", REPO_ROOT, "rev-parse", "--short=7", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
        dirty = subprocess.run(
            ["git", "-C", REPO_ROOT, "status", "--porcelain"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        ).stdout.strip()
        return h + ("-dirty" if dirty else "")
    except Exception:
        return "unknown"


full_version = "%s+%s" % (base_version(), git_suffix())

env.Append(CPPDEFINES=[("LIM_FW_VERSION", env.StringifyMacro(full_version))])
print("[version] LIM_FW_VERSION = %s" % full_version)
