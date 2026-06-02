import os
import glob
import re

src_dir = r"c:\Users\loyer\Nextcloud\Data\13-Projet Perso\L!M Vario\src\FreeVarioGauge"

# Restore original bak files first to read them properly
bak_files = glob.glob(os.path.join(src_dir, "*.ino.bak"))
for bf in bak_files:
    orig = bf.replace(".ino.bak", ".ino")
    if os.path.exists(bf):
        if os.path.exists(orig):
            os.remove(orig)
        os.rename(bf, orig)
print("Restored bak files to .ino")

# We want the main file first
main_file = os.path.join(src_dir, "FreeVarioGauge.ino")
if not os.path.exists(main_file):
    main_file = os.path.join(src_dir, "a_FreeVarioGauge.ino")

other_files = sorted(glob.glob(os.path.join(src_dir, "*.ino")))
if main_file in other_files:
    other_files.remove(main_file)

concatenated_content = []

# Read main file first
with open(main_file, "r", encoding="utf-8", errors="ignore") as f:
    concatenated_content.append(f"// === {os.path.basename(main_file)} ===\n")
    concatenated_content.append(f.read())
    concatenated_content.append("\n\n")

# Read other files
for fpath in other_files:
    with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
        # Comment out duplicate definition of double sf in ArcRefresh.ino
        if os.path.basename(fpath) == "ArcRefresh.ino":
            content = re.sub(r"double\s+sf\s*;", r"// double sf; // duplicate", content)
        concatenated_content.append(f"// === {os.path.basename(fpath)} ===\n")
        concatenated_content.append(content)
        concatenated_content.append("\n\n")

# Combine them all
full_code = "".join(concatenated_content)

# Fix ESP32Encoder puType case issue
full_code = full_code.replace("puType::down", "puType::DOWN")
full_code = full_code.replace("puType::up", "puType::UP")
full_code = full_code.replace("puType::none", "puType::NONE")

# Create all necessary forward declarations (prototypes)
prototypes = """
// --- MANUALLY ADDED PROTOTYPES FOR C++ COMPATIBILITY ---
#include "FS.h"
#include <TFT_eSPI.h>

// Forward declarations
void SPIFFSstart();
void DrawArc(float inangle, float liftValue, double speedToFly, float trueAirSpeed);
float deg2rad(float *angle);
uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);
void UpdateMode();
void Menu();
void setDrawMenuLevel(int selectedMenuNumber, int level);
void ValueRefresh(void *pvParameters);
void showBootScreen(String version);
void drawLogo();
void updateScreen();
void SerialScan(void *pvParameters);
void drawOuterRing();
void DrawMenu(int selectedMenuNumber, int level);
void changeValues(int Mode);
void changeMCvalue(bool mcUp);
void changeSpeedOption();
void changeHighOption();
void changeValueOptionRight();
void changeValueOptionLeft();
void changeLevelTwoMenu(bool changeLevelTwoValue);
void DrawText(TFT_eSprite fontOfName, uint32_t color, String infoType, String spriteName, String value, int spriteNameWidth, int spriteValueHight, int spriteValueWidth, int x, int y);
float filter(float filteredSTF, uint16_t filterfactor);
int calculateChecksum(String mce);
void fillArc(int x, int y, int start_angle, int seg_count, int rx, int ry, int w, unsigned int color);
void drawBmp(fs::File &bmpFS, int16_t x, int16_t y);
void ArcRefresh();
void printWatermark(void *pvParameters);
void drawOuterLineInSprite();
void drawRectangleInSprite(int x, int y, int w, int h, uint32_t color);
void backgroundArc(int startAngle, int endAngle, uint32_t color);
void DrawScreen();
// --------------------------------------------------------
"""

# Insert the prototypes right after "#include <ESP32Encoder.h>" or another standard include
target_include = '#include <ESP32Encoder.h>'
if target_include in full_code:
    full_code = full_code.replace(target_include, target_include + "\n" + prototypes)
else:
    # fallback
    full_code = prototypes + "\n" + full_code

# Write to a single .ino file so PlatformIO compiles it
output_ino = os.path.join(src_dir, "FreeVarioGauge.ino")

# Remove any old FreeVarioGauge.cpp to avoid duplicate definitions
cpp_file = os.path.join(src_dir, "FreeVarioGauge.cpp")
if os.path.exists(cpp_file):
    os.remove(cpp_file)
cpp_file_a = os.path.join(src_dir, "a_FreeVarioGauge.ino")
if os.path.exists(cpp_file_a) and cpp_file_a != output_ino:
    os.remove(cpp_file_a)

with open(output_ino, "w", encoding="utf-8") as f:
    f.write(full_code)

print(f"Successfully concatenated into unified {output_ino} with forward declarations")

# Rename other files to .bak so PlatformIO doesn't compile them separately
for fpath in other_files:
    if fpath != output_ino:
        os.rename(fpath, fpath + ".bak")

print("Renamed all other .ino files to .ino.bak")
