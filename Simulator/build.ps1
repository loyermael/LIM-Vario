# Compile le simulateur LVGL natif du L!M Vario (Win32/GDI, sans SDL, via MinGW deja
# present dans MSYS2). Reutilise en LECTURE les sources EEZ/LVGL du firmware -- rien
# n'est copie, donc un `git pull` / sync EEZ ulterieur est pris en compte au prochain build.
# Sortie (sim.exe, logs, fichiers de reponse) dans C:\PioBuild\LM-Vario-Sim (hors repo,
# gitignore, comme tous les autres build PlatformIO du projet).

# cc1.exe (le vrai compilateur, sous-processus de gcc) a besoin des DLL runtime du
# toolchain (libmpfr-6.dll etc.) presentes dans mingw64\bin -- absent du PATH quand on
# invoque gcc.exe par chemin absolu hors d'un shell MSYS2, d'ou l'erreur silencieuse.
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$Gcc = "C:\msys64\mingw64\bin\gcc.exe"

$FirmwareDir = Resolve-Path "$PSScriptRoot\..\Firmware"
$UiDir       = "$FirmwareDir\src\ui"
$IncludeDir  = "$FirmwareDir\include"
$LvglDir     = "$FirmwareDir\.pio\libdeps\lm-vario\lvgl"

$OutDir = "C:\PioBuild\LM-Vario-Sim"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutExe = "$OutDir\sim.exe"

if (-not (Test-Path $Gcc))      { Write-Error "gcc MinGW introuvable : $Gcc"; exit 1 }
if (-not (Test-Path $LvglDir))  { Write-Error "LVGL introuvable (as-tu compile le firmware au moins une fois ?) : $LvglDir"; exit 1 }

$LvglSources = Get-ChildItem "$LvglDir\src" -Recurse -Filter *.c | Select-Object -ExpandProperty FullName
$UiImageSources = Get-ChildItem "$UiDir\ui_image_*.c" | Select-Object -ExpandProperty FullName
$UiSources = @(
    "$UiDir\screens.c", "$UiDir\styles.c", "$UiDir\images.c", "$UiDir\vars.c"
) + $UiImageSources
$SimSources = @("$PSScriptRoot\eez_flow_stubs.c", "$PSScriptRoot\sim_menu.c", "$PSScriptRoot\sim_main.c")

$AllSources = $LvglSources + $UiSources + $SimSources
Write-Output "Compilation de $($AllSources.Count) fichiers .c..."

# Fichier de reponse GCC (@file) : evite la limite de longueur de ligne de commande
# Windows (~8191 caracteres), largement depassee avec 200+ chemins contenant des espaces.
# IMPORTANT : GCC traite `\` comme caractere d'echappement dans un @file (parseur type
# shell de libiberty) -> corrompt les chemins Windows (ex: "C:\Users" -> "C:Users").
# On utilise donc des slashes, acceptes nativement par MinGW/Windows.
function ToFwdSlash($p) { $p -replace '\\', '/' }
$RspFile = "$OutDir\build_args.rsp"
$Lines = @(
    "-O1", "-g",
    "-DLV_CONF_INCLUDE_SIMPLE=1", "-D_WIN32_WINNT=0x0601",
    "-I`"$(ToFwdSlash $IncludeDir)`"", "-I`"$(ToFwdSlash $UiDir)`"", "-I`"$(ToFwdSlash $LvglDir)`"",
    "-I`"$(ToFwdSlash $FirmwareDir)/src`""
) + ($AllSources | ForEach-Object { "`"$(ToFwdSlash $_)`"" }) + @(
    "-o", "`"$(ToFwdSlash $OutExe)`"",
    "-lgdi32", "-luser32", "-lkernel32", "-lm", "-mwindows"
)
Set-Content -Path $RspFile -Value $Lines -Encoding ASCII

$OutLog = "$OutDir\build_out.txt"
$ErrLog = "$OutDir\build_err.txt"
& $Gcc "@$RspFile" 1>$OutLog 2>$ErrLog
Get-Content $OutLog -ErrorAction SilentlyContinue | Select-Object -Last 30
Get-Content $ErrLog -ErrorAction SilentlyContinue | Select-Object -Last 60

if (Test-Path $OutExe) {
    Write-Output "`nOK -> $OutExe"
} else {
    Write-Output "`nECHEC de compilation."
    exit 1
}
