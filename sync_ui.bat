@echo off
REM ============================================================
REM  Synchronise l'UI generee par EEZ Studio vers le firmware
REM  A lancer apres chaque "Build Files" dans EEZ Studio
REM  - vars.c exclu (maintenu a la main pour les variables natives)
REM  - corrige automatiquement les includes lvgl/lvgl.h -> lvgl.h
REM ============================================================

set "SRC=C:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario\L!M Vario UI\src\ui"
set "DST=C:\Dev\LM-Vario\src\ui"

echo Sync EEZ  -^>  firmware ...
robocopy "%SRC%" "%DST%" /E /XF vars.c /NFL /NDL /NJH /NJS

echo Correction des includes LVGL ...
powershell -NoProfile -Command "Get-ChildItem -Path '%DST%' -Recurse -Include *.c,*.h | ForEach-Object { $p=$_.FullName; (Get-Content $p -Raw).Replace('lvgl/lvgl.h','lvgl.h') | Set-Content $p -NoNewline }"

echo.
echo Termine. Previens Claude pour verifier la compatibilite avant de flasher.
pause
