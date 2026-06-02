// ============================================
// TEST MINIMAL ILI9341 - Diagnostic
// ============================================
// Ce sketch peint l'écran en ROUGE, puis VERT, puis BLEU
// en boucle, à vitesse SPI TRÈS basse (1MHz).
// Si l'écran reste blanc → problème de pins ou de backlight.
// Si l'écran montre des couleurs → augmenter SPI_FREQUENCY.

#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== TFT TEST STARTING ===");
  Serial.printf("TFT_CS   = %d\n", TFT_CS);
  Serial.printf("TFT_DC   = %d\n", TFT_DC);
  Serial.printf("TFT_RST  = %d\n", TFT_RST);
  Serial.printf("TFT_MOSI = %d\n", TFT_MOSI);
  Serial.printf("TFT_SCLK = %d\n", TFT_SCLK);
  Serial.printf("TFT_MISO = %d\n", TFT_MISO);

  tft.init();
  tft.setRotation(0);

  Serial.println("TFT init done - filling RED");
  tft.fillScreen(TFT_RED);
  delay(1500);

  Serial.println("Filling GREEN");
  tft.fillScreen(TFT_GREEN);
  delay(1500);

  Serial.println("Filling BLUE");
  tft.fillScreen(TFT_BLUE);
  delay(1500);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("ILI9341 OK!");
  tft.setCursor(10, 40);
  tft.print("CS=5 DC=22");
  tft.setCursor(10, 70);
  tft.print("RST=21");

  Serial.println("=== TEST DONE ===");
}

void loop() {
  // Clignote pour confirmer que le programme tourne
  tft.fillRect(10, 100, 100, 30, TFT_RED);
  delay(500);
  tft.fillRect(10, 100, 100, 30, TFT_BLACK);
  delay(500);
  Serial.print(".");
}
