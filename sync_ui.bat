@echo off
REM ============================================================
REM  Synchronise l'UI generee par EEZ Studio vers le firmware
REM  A lancer apres chaque "Build Files" dans EEZ Studio
REM  (vars.c est exclu : maintenu a la main pour les variables natives)
REM ============================================================

set "SRC=C:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario\L!M Vario UI\L!M Vario\src\ui"
set "DST=C:\Dev\LM-Vario\src\ui"

echo Sync EEZ  -^>  firmware ...
robocopy "%SRC%" "%DST%" /E /XF vars.c /NFL /NDL /NJH /NJS

echo.
echo Termine. Tu peux maintenant compiler/flasher dans PlatformIO.
pause
