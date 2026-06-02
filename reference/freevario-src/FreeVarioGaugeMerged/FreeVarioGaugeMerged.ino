
// ===== FreeVarioGauge.ino =====
//    OpenVarioGauge is a programm to generate the vario display using NMEA Output
//    of OpenVario.
//    Copyright (C) 2019  Dirk Jung Blaubart@gmx.de
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.

//*************************************************
//****  Screen and SPIFFS Headers and Defines  ****
//*************************************************
#include <Streaming.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
Preferences prefs;

#include "LogoOV.h"
#include "FS.h"
#include "SPIFFS.h"
#include <ESP32Encoder.h>

ESP32Encoder Vario_Enc;

#define RXD2 16                // PIN 27
#define TXD2 17                // PIN 28
#define VE_PB 27               // PIN 12
#define DEG2RAD 0.0174532925
#define STF_MODE 13            // PIN 16
#define STF_AUTO 33            // PIN 9
#define OuterRadius 160
#define InnerRadius 130
#define xCenter 160
#define yCenter 160
#define BLUE 0x04df

static TFT_eSPI tft = TFT_eSPI();
TFT_eSprite nameOfField = TFT_eSprite(&tft);
TFT_eSprite Name = TFT_eSprite(&tft);
TFT_eSprite smallFont = TFT_eSprite(&tft);
TFT_eSprite needleGreen = TFT_eSprite(&tft);
TFT_eSprite needleBlue = TFT_eSprite(&tft);
TFT_eSprite drawOuterLine = TFT_eSprite(&tft);
TFT_eSprite drawColoredArc = TFT_eSprite(&tft);
TFT_eSprite drawRectangle = TFT_eSprite(&tft);
TFT_eSprite number = TFT_eSprite(&tft);
TFT_eSprite background = TFT_eSprite(&tft);
WebServer server(80);

// ============================================================
// TCP Server NMEA : XCSoar se connecte à l'ESP32
// Téléphone : connecter le WiFi sur "FV_Displayboard" (mdp: 12345678)
// XCSoar config : Device -> driver OpenVario, TCP Client, host 192.168.2.1, port 4353
// ESP32 répond avec $POV,P pour débloquer l'envoi de données XCSoar
// ============================================================
WiFiServer nmeaServer(4353);
WiFiClient nmeaClient;

Print &cout = Serial;
TaskHandle_t SerialScanTask, TaskValueRefresh;

SemaphoreHandle_t xTFTSemaphore;

long NOT_SET = -1;
long pushButtonPressTime = NOT_SET;

const String SOFTWARE_VERSION = "  V2.4.4 - 2025";

const char *host = "FreeVario_Displayboard";
const char *ssid = "FV_Displayboard";
const char *password = "12345678";


String displayMode = "Waiting ...";
String soundMode = "Waiting ...";
String displayIP = "";
String soundIP = "";
String nameSetting = "QNH";
String nameSpeed = "NA";
String nameHight = "NA";
String stf_mode = "Vario";
String valueQnhAsString = "1013";
String valueBugAsString = "0";
String valueMuteAsString = "NA";
String valueWindAsString = "NA";
String valueSTFAsString = "NA";
String valueAttenAsString = "2";
String valueGrsAsString = "0";
String valueTasAsString = "0";
String valueVaaAsString = "+0.0";
String valueVanAsString = "+0.0";
String valueHigAsString = "0";
String valueFLAsString = "0";
String valueHagAsString = "0";
String valueAwsAsString = "0";
String valueCwsAsString = "0";
String valueMacAsString = "0.0";
String voltage = "0.0";
String UTC = "00:00";
String UTCHour = "00";
String UTCMinute = "00";
String valueVoltageAsString = "0.0";

extern uint16_t logoOV[];

float var = 0;
float stf = 0.0;
float valueTasAsFloat = 0;
float valueMacAsFloat = 0.0;
float valueAwsAsFloat = -1000;
float valueCwsAsFloat = -1000;
float valueAwdAsFloat = -1000;
float valueCwdAsFloat = -1000;
float valueHeaAsFloat = 0;
float encoderPosition = (float) - 999;
float menuActiveSince = 0;                  // Will be updated in menu run

double valueQnhAsFloat = 1013;
double valueBugAsFloat = 0;

bool loadMenuFont = true;
bool loadMenuArc = true;
bool updatemode = false;
bool showBootscreen = true;
bool mci = false;
bool pushButtonIsLongpress = false;
bool pushButtonIsShortpress = false;
bool pushButtonPressed = false;
bool encoderLeft = false;
bool encoderRight = false;
bool encoderWasMoved = false;
bool menuWasTriggered = false;
bool subMenuTriggered = false;
bool subMenuLevelTwoTriggered = false;
bool requestMenuPaint = false;
bool WasSend = false;
bool AutoWasSend = false;
bool SourceIsXCSoar = false;
bool SourceIsLarus = false;
bool SourceIsWifi = false;  // true quand XCSoar est connecte via WiFi TCP
bool isDemoMode = false;    // true quand timeout boot - pas de source detectee

int MENU_SPEED_TYP = 1;
int MENU_HIGHT_TYP = 2;
int MENU_VALUE_TYP = 3;
int MENU_VALUE_QNH = 1;
int MENU_VALUE_BUG = 2;
int MENU_VALUE_MUTE = 3;
int k = 0;
int stf_mode_state;
int startAngle, segmentDraw, segmentCount;
int Wificount = 0;
int valueMuteAsInt;
int valueAttenAsInt;
int valueWindAsInt;
int valueSTFAsInt;
int changeMode;
int oldChangeMode;
int offset = 0;
int selectedMenu = MENU_SPEED_TYP;
int requestDrawMenu = 1;
int requestDrawMenuLevel = 0;
int TimeDifference = 0;

unsigned long lastTimeBoot = 0;
unsigned long lastTimeReady = 0;
unsigned long mcSend  = 0;
unsigned long stayAlive = 0;

// ************************************
// ****  Initialize SPIFFS memory  ****
// ************************************
void SPIFFSstart() {
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield(); // Stay here twiddling thumbs waiting
  }
  Serial.println("\r\nInitialisation done.");
}


// ===== Forward Declarations (needed by BMPReader) =====
uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);

// ===== ArcRefresh.ino =====

//*************************
//****  Calculate arc  ****
//*************************
// x,y == coords of centre of arc
// start_angle = 0 - 359
// seg_count = number of 3 degree segments to draw (120 => 360 degree arc)
// rx = x axis radius
// yx = y axis radius
// w  = width (thickness) of arc in pixels
// color = 16 bit color value
// Note if rx and ry are the same an arc of a circle is drawn

double sf;                                      //Speedfaktor zur Berechnung des STF-Tons
float B, B_alt;
float MiddleRadius = ((OuterRadius - InnerRadius) / 2) + InnerRadius; //Middle of Sliding Circle radius

//***********************
//****  Refresh Arc *****
//***********************
void ArcRefresh() {
  while (showBootscreen) {
    vTaskDelay(1000);
  }
  if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
  {
    float angle = (var * 22) + 180;
    DrawArc(angle, var, stf, valueTasAsFloat);
    xSemaphoreGive(xTFTSemaphore);
  }
  vTaskDelay(10);
}

void fillArc(int x, int y, int start_angle, int seg_count, int rx, int ry, int w, unsigned int color)
{

  byte seg = 3; // Segments are 3 degrees wide = 120 segments for 360 degrees
  byte inc = 3; // Draw segments every 3 degrees, increase to 6 for segmented ring

  //***********************************************************
  // Calculate first pair of coordinates for segment start ****
  //***********************************************************
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);

  uint16_t x0 = sx * (rx - w) + x;
  uint16_t x1 = sx * rx + x;
  uint16_t y0;
  uint16_t y1;

  if (B > 0) {
    y0 = sy * (ry - w) + y;
    y1 = sy * ry + y;
  }
  else {
    y0 = 320 - (sy * (ry - w) + y);
    y1 = 320 - (sy * ry + y);
  }

  //***********************************************
  //****  Draw color blocks every inc degrees  ****
  //***********************************************
  for (int i = start_angle; i < (start_angle + seg * seg_count); i += inc) {

    //*********************************************************
    //****  Calculate pair of coordinates for segment end  ****
    //*********************************************************
    uint16_t y2;
    uint16_t y3;
    float sx2 = cos((i + seg - 90) * DEG2RAD);
    float sy2 = sin((i + seg - 90) * DEG2RAD);
    uint16_t x2 = sx2 * (rx - w) + x;
    uint16_t x3 = sx2 * rx + x;
    if (B > 0) {
      y2 = sy2 * (ry - w) + y;
      y3 = sy2 * ry + y;
    }
    else {
      y2 = -sy2 * (ry - w) + y;
      y3 = -sy2 * ry + y;
    }

    //********************
    //****  Draw Arc  ****
    //********************
    if ((B >= 0)) {
      if (x0 < 150) {
        drawColoredArc.createSprite(x2 - x1 + 20, y0 - y3 + 10);
        drawColoredArc.setTextColor(TFT_WHITE, TFT_DARKGREY);
        drawColoredArc.fillSprite(TFT_BLACK);
        drawColoredArc.fillTriangle(x0 - x1, y0 - y3, 0, y1 - y3, x2 - x1, y2 - y3, color);
        drawColoredArc.fillTriangle(0, y1 - y3, x2 - x1, y2 - y3, x3 - x1, 0, color);
        drawColoredArc.pushToSprite(&background, x1, y3, TFT_BLACK);
        drawColoredArc.deleteSprite();
      }
      if (x0 > 150) {
        drawColoredArc.createSprite(x3 - x0 + 20, y2 - y1 + 10);
        drawColoredArc.setTextColor(TFT_WHITE, TFT_DARKGREY);
        drawColoredArc.fillSprite(TFT_BLACK);
        drawColoredArc.fillTriangle(0, y0 - y1, x1 - x0, 0, x2 - x0, y2 - y1, color);
        drawColoredArc.fillTriangle(x1 - x0, 0, x2 - x0, y2 - y1, x3 - x0, y3 - y1, color);
        drawColoredArc.pushToSprite(&background, x0, y1, TFT_BLACK);
        drawColoredArc.deleteSprite();
      }
    }
    else if (B < 0) {
      if (x0 < 150) {
        drawColoredArc.createSprite(x2 - x1 + 20, y3 - y0 + 10);
        drawColoredArc.setTextColor(TFT_WHITE, TFT_DARKGREY);
        drawColoredArc.fillSprite(TFT_BLACK);
        drawColoredArc.fillTriangle(x0 - x1, 0, 0, y1 - y0, x2 - x1, y2 - y0, color);
        drawColoredArc.fillTriangle(0, y1 - y0, x2 - x1, y2 - y0, x3 - x1, y3 - y0, color);
        drawColoredArc.pushToSprite(&background, x1, y0, TFT_BLACK);
        drawColoredArc.deleteSprite();
      }
      if (x0 > 150) {
        drawColoredArc.createSprite(x3 - x0 + 20, y1 - y2 + 20);
        drawColoredArc.setTextColor(TFT_WHITE, TFT_DARKGREY);
        drawColoredArc.fillSprite(TFT_BLACK);
        drawColoredArc.fillTriangle(0, y0 - y2, x1 - x0, y1 - y2, x2 - x0, 0, color);
        drawColoredArc.fillTriangle(x1 - x0, y1 - y2, x2 - x0, 0, x3 - x0, y3 - y2, color);
        drawColoredArc.pushToSprite(&background, x0, y2, TFT_BLACK);
        drawColoredArc.deleteSprite();
      }
    }
    //**************************************************************
    //****  Copy segment end to sgement start for next segment  ****
    //**************************************************************
    x0 = x2;
    y0 = y2;
    x1 = x3;
    y1 = y3;
  }
}

void DrawArc(float inangle, float liftValue, double speedToFly, float trueAirSpeed) {
  if (SourceIsXCSoar == true) {
    stf_mode_state = digitalRead(STF_MODE);
  }
  else   if (SourceIsLarus == true) {
    stf_mode_state = 0;
  }

  unsigned int color;
  String st;

  //**********************************
  //****  Vario Mode Colored Arc  ****
  //**********************************
  if (stf_mode_state == 0) {
    if (liftValue <= -6) B = -6;
    if ((liftValue > -6) && (liftValue < 6)) B = liftValue;
    if (liftValue >= 6) B = 6;
  }

  //********************************
  //****  STF Mode colored Arc  ****
  //********************************
  if (stf_mode_state == 1) {
    sf = (trueAirSpeed - speedToFly) / 10;
    if (sf <= -6) B = -6;
    if ((sf > -6) && (sf < 6)) B = sf;
    if (sf >= 6) B = 6;
  }

  //**********************************
  //****  Calculate Arc Segments  ****
  //**********************************
  segmentCount = abs(B) * 7.4;
  deg2rad(&inangle);

  if (B >= 0) {
    startAngle = 270;
    segmentDraw = segmentCount;
    color = TFT_GREEN;
  }
  else if (B < 0) {
    startAngle = 270;
    segmentDraw = segmentCount;
    color = TFT_RED;
  }

  //***************************************
  //****  Start Funktion to Draw Arc  ****
  //***************************************
  fillArc(160, 160, startAngle, segmentDraw, 160, 160, 30, color);
  B_alt = B;

  //**************************************
  //****  Draw divisions and numbers  ****
  //**************************************
  for (int i = 70; i <= 300; i += 22) {
    float divangle = i;
    deg2rad(&divangle);
    int x0 = OuterRadius * cos(divangle) + xCenter;
    int y0 = OuterRadius * sin(divangle) + yCenter;
    int x1 = (MiddleRadius + 10) * cos(divangle) + xCenter;
    int y1 = (MiddleRadius + 10) * sin(divangle) + yCenter;
    drawOuterLine.createSprite(30, 30);
    drawOuterLine.setTextColor(TFT_WHITE, TFT_DARKGREY);
    drawOuterLine.fillSprite(TFT_BLACK);
    drawOuterLine.drawLine(0 + 10, 0 + 10, x1 - x0 + 10, y1 - y0 + 10, TFT_WHITE);
    drawOuterLine.pushToSprite(&background, x0 - 10, y0 - 10, TFT_BLACK);
    drawOuterLine.deleteSprite();

  }
  if (loadMenuArc) {
    number.loadFont("micross20");
    loadMenuArc = false;
  }

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("0", 15, 2);
  number.pushToSprite(&background, 7, 150, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("1", 15, 2);
  number.pushToSprite(&background, 17, 95, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("2", 15, 2);
  number.pushToSprite(&background, 48, 50, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 18);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("3", 15, 2);
  number.pushToSprite(&background, 89, 20, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("4", 15, 2);
  number.pushToSprite(&background, 143, 7, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("5", 15, 2);
  number.pushToSprite(&background, 201, 16, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("1", 15, 2);
  number.pushToSprite(&background, 17, 200, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("2", 15, 2);
  number.pushToSprite(&background, 48, 246, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("3", 15, 2);
  number.pushToSprite(&background, 89, 278, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("4", 15, 2);
  number.pushToSprite(&background, 143, 292, TFT_BLACK);
  number.deleteSprite();

  number.createSprite(20, 25);
  number.setTextColor(TFT_WHITE, TFT_DARKGREY);
  number.fillSprite(TFT_BLACK);
  number.setTextSize(2);
  number.setTextDatum(TR_DATUM);
  number.drawString("5", 15, 2);
  number.pushToSprite(&background, 201, 285, TFT_BLACK);
  number.deleteSprite();
}

// ===== BMPReader.ino =====
// Heavily inspired by Bodmer's BMP image rendering function
void drawBmp(fs::File &bmpFS, int16_t x, int16_t y) {

  if ((x >= tft.width()) || (y >= tft.height())) {
#ifdef DEBUG
    Serial.println("Image paint position out of screen boundaries.");
#endif
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t  r, g, b;

  if (read16(bmpFS) == 0x4D42)
  {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0))
    {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16-bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
          *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
    }
#ifdef DEBUG
    else Serial.println("BMP format not recognized.");
#endif
  }
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

// ===== Calculations.ino =====
//**************************
//****  Filter for STF  ****
//**************************
float filter(float filteredSTF, uint16_t filterfactor) {
  static uint16_t count = 0;
  // so that at the beginning the value is close to the measured value
  if (count < filterfactor) {
    filterfactor = count++;
  }
  stf = ((stf * filterfactor) + filteredSTF) / (filterfactor + 1);
  return stf;
}

//********************************
//****  calculate Checksumme  ****
//********************************
int calculateChecksum(String mce) {
  int i, XOR, c;
  for (XOR = 0, i = 0; i < mce.length(); i++) {
    c = (unsigned char)mce.charAt(i);
    if (c == '*') break;
    if ((c != '$') && (c != '!')) XOR ^= c;
  }
  return XOR;
}

//*********************************
//****  Deg to Rad conversion  ****
//*********************************
float deg2rad(float *angle) {
  //  float tempangle=*angle;
  *angle = *angle / 180 * 3.141516;
  return *angle;
}

//****************************************
//****  To Calculate xTask StackSize  ****
//****************************************
void printWatermark(void *pvParameters){
    while(1){
        delay(2000);
        Serial.print("TASK: ");
        Serial.print(pcTaskGetName(SerialScanTask)); // Get task name with handler
        Serial.print(", High Watermark: ");
        Serial.print(uxTaskGetStackHighWaterMark(SerialScanTask));
        Serial.println();
        Serial.print("TASK: ");
        Serial.print(pcTaskGetName(TaskValueRefresh)); // Get task name with handler
        Serial.print(", High Watermark: ");
        Serial.print(uxTaskGetStackHighWaterMark(TaskValueRefresh));
        Serial.println();
    }
}

// ===== DrawText.ino =====
//************************************
//****  Draw ValueBoxes and Data  ****
//************************************
void DrawText(TFT_eSprite fontOfName, uint32_t color, String infoType, String spriteName, String value, int spriteNameWidth, int spriteValueHight, int spriteValueWidth, int x, int y) {
  if (loadMenuFont) {
    Name.loadFont("micross15");
    loadMenuFont = false;
  }
  Name.createSprite(spriteNameWidth, 25);
  Name.setTextColor(color, color);
  Name.fillSprite(TFT_BLACK);
  Name.setTextDatum(TR_DATUM);
  Name.drawString(spriteName, spriteNameWidth, 2);
  Name.pushToSprite(&background, x, y, TFT_BLACK);
  Name.deleteSprite();

  if (infoType == "small") {
    if (k == 1) {
      smallFont.loadFont("micross30");
    }
    smallFont.createSprite(spriteValueWidth, spriteValueHight);
    smallFont.setTextColor(color, TFT_BLACK);
    smallFont.fillSprite(TFT_BLACK);
    smallFont.setTextDatum(TR_DATUM);
    smallFont.drawString(value, spriteValueWidth, 2);
    smallFont.pushToSprite(&background, x + spriteNameWidth, y, TFT_BLACK);
    smallFont.deleteSprite();
  }

  else if (infoType == "large") {
    fontOfName.loadFont("micross50");
    fontOfName.createSprite(spriteValueWidth, spriteValueHight);
    fontOfName.setTextColor(color, TFT_BLACK);
    fontOfName.fillSprite(TFT_BLACK);
    fontOfName.setTextDatum(TR_DATUM);
    fontOfName.drawString(value, spriteValueWidth, 2);
    fontOfName.pushToSprite(&background, x + spriteNameWidth, y, TFT_BLACK);
    fontOfName.deleteSprite();

    //fontOfName.createSprite(spriteunitWidth + 5, 25);
  }
  k++;
  if (k == 7) {
    k = 0;
  }
}

// ===== ChangeValues.ino =====
void changeMCvalue(bool mcUp) {
  String mce;
  if (mci == true) {
    mce = ("$PFV,M,S," + String((float)valueMacAsFloat) + "*");
    int checksum = calculateChecksum(mce);
    Serial2.printf("%s%X\n", mce.c_str(), checksum); //set MCE to MCI
    mci = false;
  }
  // Mise à jour locale pour feedback visuel immédiat (0.0 – 5.0, pas de 0.1)
  if (mcUp && valueMacAsFloat < 5.0) {
    valueMacAsFloat = round((valueMacAsFloat + 0.1) * 10.0) / 10.0;
  } else if (!mcUp && valueMacAsFloat > 0.0) {
    valueMacAsFloat = round((valueMacAsFloat - 0.1) * 10.0) / 10.0;
  }
  char mcBuf[8];
  dtostrf(valueMacAsFloat, 3, 1, mcBuf);
  valueMacAsString = String(mcBuf);

  if (mcUp && (millis() - mcSend) > 100) {
    Serial2.println("$PFV,M,U*58");  //McCready Up
    mcSend = millis();
  }
  else if (!mcUp && (millis() - mcSend) > 100) {
    Serial2.println("$PFV,M,D*49");  //McCready Down
    mcSend = millis();
  }
  nameSetting = "MC";

  // Sync MC → XCSoar via TCP WiFi si connecte
  if (nmeaClient && nmeaClient.connected()) {
    char lxwp2[40];
    snprintf(lxwp2, sizeof(lxwp2), "$LXWP2,%.1f,100,100", valueMacAsFloat);
    String lxwp2Str(lxwp2);
    int cs2 = calculateChecksum(lxwp2Str);
    nmeaClient.printf("%s*%02X\r\n", lxwp2, cs2);
  }
}

void changeSpeedOption () {
  if (nameSpeed == "TAS") {
    nameSpeed = "GS";
  }
  else {
    nameSpeed = "TAS";
  }
  prefs.begin("settings", false);
  prefs.putString("nameSpeed", nameSpeed);
  prefs.end();
}

void changeHighOption () {
  if (nameHight == "AGL" && !SourceIsLarus) {
    nameHight = "MSL";
  }
  else if (nameHight == "MSL" && !SourceIsLarus) {
    nameHight = "AGL";
  }
  else if (nameHight == "MSL" && SourceIsLarus) {
    nameHight = "FL";
  }
  else if (nameHight == "FL" && SourceIsLarus) {
    nameHight = "MSL";
  }

  prefs.begin("settings", false);
  prefs.putString("nameHight", nameHight);
  prefs.end();
}

void changeValueOptionRight () {
  if ( nameSetting == "QNH" && !SourceIsLarus) {
    nameSetting = "Bug";
  }
  else if ( nameSetting == "Bug" && !SourceIsLarus) {
    nameSetting = "ATTEN";
  }
  else if ( nameSetting == "ATTEN" && !SourceIsLarus) {
    nameSetting = "Mute";
  }
  else if ( nameSetting == "Mute") {
    nameSetting = "Wind";
  }
  else if ( nameSetting == "Wind") {
    nameSetting = "STF";
  }
  else if ( nameSetting == "STF" && !SourceIsLarus) {
    nameSetting = "QNH";
  }
  else if ( nameSetting == "STF" && SourceIsLarus) {
    nameSetting = "TimeDifference";
  }
  else if ( nameSetting == "TimeDifference" && SourceIsLarus) {
    nameSetting = "Mute";
  }
}

void changeValueOptionLeft () {
  if ( nameSetting == "QNH" && !SourceIsLarus) {
    nameSetting = "STF";
  }
  else if ( nameSetting == "STF") {
    nameSetting = "Wind";
  }
  else if ( nameSetting == "Wind") {
    nameSetting = "Mute";
  }
  else if ( nameSetting == "Mute" && !SourceIsLarus) {
    nameSetting = "ATTEN";
  }
  else if ( nameSetting == "Mute"  && SourceIsLarus) {
    nameSetting = "TimeDifference";
  }
  else if ( nameSetting == "TimeDifference"  && SourceIsLarus) {
    nameSetting = "STF";
  }
  else if ( nameSetting == "ATTEN"  && !SourceIsLarus) {
    nameSetting = "Bug";
  }
  else if ( nameSetting == "Bug"  && !SourceIsLarus) {
    nameSetting = "QNH";
  }
}
void changeLevelTwoMenu (bool changeLevelTwoValue) {
  if (nameSetting == "QNH") {
    if (changeLevelTwoValue && valueQnhAsFloat < 1300) {
      valueQnhAsFloat = valueQnhAsFloat + 1;
    }
    else if (!changeLevelTwoValue && valueQnhAsFloat > 850) {
      valueQnhAsFloat = valueQnhAsFloat - 1;
    }
    String qnhStr = ("$PFV,Q,S," + String(valueQnhAsFloat) + "*");
    int checksum = calculateChecksum(qnhStr);
    char buf[20];
    // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
    valueQnhAsString = dtostrf(valueQnhAsFloat, 4, 0, buf);
    Serial2.printf("%s%X\n", qnhStr.c_str(), checksum);                //send QNH to XCSoar
  }

  if (nameSetting == "Bug") {
    if (changeLevelTwoValue && valueBugAsFloat < 50) {
      valueBugAsFloat = valueBugAsFloat + 1;
    }
    else if (!changeLevelTwoValue && valueBugAsFloat > 0) {
      valueBugAsFloat = valueBugAsFloat - 1;
    }
    String bugStr = ("$PFV,B,S," + String(valueBugAsFloat) + "*");
    int checksum = calculateChecksum(bugStr);
    char buf[20];
    // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
    valueBugAsString = dtostrf(valueBugAsFloat, 2, 0, buf);
    Serial2.printf("%s%X\n", bugStr.c_str(), checksum);                //send bug to XCSoar
  }
  if (nameSetting == "ATTEN") {
    if (changeLevelTwoValue && valueAttenAsInt < 3) {
      valueAttenAsInt = valueAttenAsInt + 1;
    }
    else if (!changeLevelTwoValue && valueAttenAsInt > 0) {
      valueAttenAsInt = valueAttenAsInt - 1;
    }
    valueAttenAsString = String(valueAttenAsInt);
    prefs.begin("settings", false);
    prefs.putUInt("ATTEN", valueAttenAsInt);
    prefs.end();
    String attStr = ("$PFV,A,S," + valueAttenAsString + "*");
    int checksum = calculateChecksum(attStr);
    char buf[20];
    // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
    Serial2.printf("%s%X\n", attStr.c_str(), checksum);
  }
  
  if (nameSetting == "TimeDifference") {
    if (changeLevelTwoValue && TimeDifference < 12) {
      TimeDifference = TimeDifference + 1;
    }
    else if (!changeLevelTwoValue && TimeDifference > -12) {
      TimeDifference = TimeDifference - 1;
    }
    prefs.begin("settings", false);
    prefs.putUInt("TimeDifference", TimeDifference);
    prefs.end();
  }
}
void changeLevelTwoMenuTurn (bool changeLevelTwoValue) {
  if (nameSetting == "Mute") {
    if (changeLevelTwoValue && valueMuteAsInt == 1) {
      valueMuteAsInt = 0;
      valueMuteAsString = "OFF";
    }
    else if (changeLevelTwoValue && valueMuteAsInt == 0) {
      valueMuteAsInt = 1;
      valueMuteAsString = "ON";
    }
    prefs.begin("settings", false);
    prefs.putUInt("Mute", valueMuteAsInt);
    prefs.end();
    String muteStr = ("$PFV,S,S," + String(valueMuteAsInt) + "*");
    int checksum = calculateChecksum(muteStr);
    char buf[20];
    // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
    Serial2.printf("%s%X\n", muteStr.c_str(), checksum);
  }
  if (nameSetting == "Wind") {
    if (changeLevelTwoValue && valueWindAsInt == 1) {
      valueWindAsInt = 0;
      valueWindAsString = "OFF";
    }
    else if (changeLevelTwoValue && valueWindAsInt == 0) {
      valueWindAsInt = 1;
      valueWindAsString = "ON";
    }
    prefs.begin("settings", false);
    prefs.putUInt("Wind", valueWindAsInt);
    prefs.end();
  }
  if (nameSetting == "STF") {
    if (changeLevelTwoValue && valueSTFAsInt == 0) {
      valueSTFAsInt = 1;
      valueSTFAsString = "OV";
    }
    else if (changeLevelTwoValue && valueSTFAsInt == 1) {
      valueSTFAsInt = 0;
      valueSTFAsString = "Flaps";
    }
    prefs.begin("settings", false);
    prefs.putUInt("STF", valueSTFAsInt);
    prefs.end();
    String STFStr = ("$PFV,D,S," + String(valueSTFAsInt) + "*");
    int checksum = calculateChecksum(STFStr);
    char buf[20];
    // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
    Serial2.printf("%s%X\n", STFStr.c_str(), checksum);
  }
}

// ===== EncoderReader.ino =====
void EncoderReader() {

  //*****************************************
  //****  Read Encoder & Buttons / Dips  ****
  //*****************************************
  const long LONGPRESS_TIME      = 500;
  const long SHORTPRESS_TIME_MIN = 20;
  const int  DEBOUNCE_DELAY      = 20;

  // Cooldown de 100ms entre deux déclenchements : absorbe tous les counts parasites
  // d'un même cran, quel que soit le nombre de counts par detent de l'encodeur
  const unsigned long ENCODER_COOLDOWN = 100;
  static unsigned long lastEncoderTrigger = 0;
  bool cooldownExpired = (millis() - lastEncoderTrigger) >= ENCODER_COOLDOWN;

  long rawCount = Vario_Enc.getCount();

  if (rawCount != (long)encoderPosition) {
    if (!pushButtonPressed && cooldownExpired) {
      if (rawCount > (long)encoderPosition) {
        encoderRight = true;
        encoderLeft  = false;
      } else {
        encoderLeft  = true;
        encoderRight = false;
      }
      encoderWasMoved    = true;
      lastEncoderTrigger = millis();
    } else {
      encoderWasMoved = false;
      encoderRight    = false;
      encoderLeft     = false;
    }
    encoderPosition = rawCount;
  } else {
    encoderWasMoved = false;
    encoderRight    = false;
    encoderLeft     = false;
  }

  // Button: detect press and release edges
  bool buttonCurrentlyPressed = (digitalRead(VE_PB) == LOW);

  if (!pushButtonPressed && buttonCurrentlyPressed) {
    // Falling edge – debounce then start timing
    vTaskDelay(DEBOUNCE_DELAY);
    if (digitalRead(VE_PB) == LOW) {
      pushButtonPressed   = true;
      pushButtonPressTime = millis();
      pushButtonIsShortpress = true;
    }
  }
  else if (pushButtonPressed && !buttonCurrentlyPressed) {
    // Rising edge
    vTaskDelay(DEBOUNCE_DELAY);
    pushButtonPressed   = false;
    pushButtonPressTime = NOT_SET;
    pushButtonIsLongpress = false;
  }

  // Detect longpress while button is held
  if (pushButtonPressed && pushButtonPressTime != NOT_SET &&
      (millis() - pushButtonPressTime) >= LONGPRESS_TIME) {
    pushButtonIsLongpress  = true;
    pushButtonIsShortpress = false;
  }
}

// ===== Menu.ino =====
void DrawMenu(int selectedMenuNumber, int level) {
  setDrawMenuLevel(selectedMenuNumber, level);
  requestMenuPaint = true;
}

void setDrawMenuLevel(int selectedMenuNumber, int level) {
  requestDrawMenu = selectedMenuNumber;
  requestDrawMenuLevel = level;
}

void settingStartValueType () {
  if (!SourceIsLarus) {
    nameSetting = "QNH";
  }
  else {
    nameSetting = "Mute";
  }
}

void settingStandardValueType () {
  if (!SourceIsLarus) {
    nameSetting = "MC";
  }
  else {
    nameSetting = "Time";
  }
}

void Menu () {
  const long TIME_SINCE_BOOT = 5000;
  long timeSystemRuns = millis() - lastTimeBoot;

  //******************************
  //****  Calculate  Actions  ****
  //******************************

  if (menuWasTriggered && encoderRight) {
    selectedMenu ++;
  }
  else if  (menuWasTriggered && encoderLeft) {
    selectedMenu --;
  }

  if (selectedMenu > MENU_VALUE_TYP && !SourceIsLarus) {
    selectedMenu = MENU_SPEED_TYP;
  }
  else if (selectedMenu < MENU_SPEED_TYP && !SourceIsLarus) {
    selectedMenu = MENU_VALUE_TYP;
  }
  else if (selectedMenu > MENU_VALUE_TYP && SourceIsLarus) {
    selectedMenu = MENU_HIGHT_TYP;
  }
  else if (selectedMenu < MENU_HIGHT_TYP && SourceIsLarus) {
    selectedMenu = MENU_VALUE_TYP;
  }

  if (pushButtonIsLongpress && !menuWasTriggered && !subMenuTriggered && !subMenuLevelTwoTriggered && !encoderWasMoved && timeSystemRuns > TIME_SINCE_BOOT) {
    menuWasTriggered = true;
    if (SourceIsLarus) {
      selectedMenu = MENU_HIGHT_TYP;
    }
    else {
      selectedMenu = MENU_SPEED_TYP;
    }
    DrawMenu(selectedMenu, 1);
    menuActiveSince = millis(); // set time to now
    pushButtonIsLongpress = false;
  }

  else if (!pushButtonPressed && !menuWasTriggered && !subMenuTriggered && !subMenuLevelTwoTriggered && encoderWasMoved && !SourceIsLarus) {
    changeMCvalue(encoderRight);
  }
  else if (!pushButtonPressed && menuWasTriggered && !subMenuTriggered && encoderWasMoved) {
    DrawMenu(selectedMenu, 1);
    menuActiveSince = millis(); // set time to now
  }
  else if (menuWasTriggered && !subMenuTriggered && pushButtonIsShortpress) {
    pushButtonIsShortpress = false;
    if (selectedMenu == MENU_SPEED_TYP) {
      menuWasTriggered = false;
      subMenuTriggered = true;
      DrawMenu(selectedMenu, 2);
    }
    else if (selectedMenu == MENU_HIGHT_TYP) {
      menuWasTriggered = false;
      subMenuTriggered = true;
      DrawMenu(selectedMenu, 2);
    }
    else if (selectedMenu == MENU_VALUE_TYP) {
      menuWasTriggered = false;
      subMenuTriggered = true;
      settingStartValueType();
      DrawMenu(selectedMenu, 2);
    }
    menuActiveSince = millis(); // set time to now
  }
  else if (!menuWasTriggered && subMenuTriggered && !pushButtonIsShortpress && !pushButtonIsLongpress && !pushButtonPressed &&  encoderWasMoved ) {
    //Serial.println("Welcher Wert soll eingestellt werden?");
    if (selectedMenu == MENU_SPEED_TYP) {
      changeSpeedOption();
    }
    else if (selectedMenu == MENU_HIGHT_TYP) {
      changeHighOption();
    }
    else if (selectedMenu == MENU_VALUE_TYP && encoderRight ) {
      changeValueOptionRight();
    }
    else if (selectedMenu == MENU_VALUE_TYP && encoderLeft ) {
      changeValueOptionLeft();
    }
    menuActiveSince = millis(); // set time to now
  }

  else if (!menuWasTriggered && subMenuTriggered && pushButtonIsShortpress) {
    pushButtonIsShortpress = false;
    //Serial.println("Einzustellnder Wert wird ausgewählt");
    if (selectedMenu == MENU_SPEED_TYP || selectedMenu  == MENU_HIGHT_TYP) {
      subMenuTriggered = false;
      selectedMenu = MENU_SPEED_TYP;
      setDrawMenuLevel(selectedMenu, 0);
    }
    else if (selectedMenu == MENU_VALUE_TYP) {
      menuActiveSince = millis(); // set time to now
      subMenuTriggered = false;
      subMenuLevelTwoTriggered = true;
      setDrawMenuLevel(selectedMenu, 3);
    }
  }

  else if (!menuWasTriggered && !subMenuTriggered && !pushButtonIsShortpress && !pushButtonIsLongpress  && !pushButtonPressed && subMenuLevelTwoTriggered && encoderWasMoved) {
    //Serial.println("Wert wird eingestellt");
    changeLevelTwoMenu(encoderRight);
    changeLevelTwoMenuTurn(encoderWasMoved);
    menuActiveSince = millis(); // set time to now
  }

  else if (!menuWasTriggered && !subMenuTriggered && pushButtonIsShortpress && subMenuLevelTwoTriggered && !encoderWasMoved) {
    pushButtonIsShortpress = false;
    //Serial.println("eingestellter Wert wird gespeichert");
    subMenuLevelTwoTriggered = false;
    settingStandardValueType();
    selectedMenu = MENU_SPEED_TYP;
    setDrawMenuLevel(selectedMenu, 0);
  }

  // check run time in menu and exit if time > 10000
  if ((millis() - menuActiveSince) > 10000 && subMenuLevelTwoTriggered) {
    subMenuLevelTwoTriggered = false;
    settingStandardValueType();
    if (SourceIsLarus) {
      selectedMenu = MENU_HIGHT_TYP;
    }
    else {
      selectedMenu = MENU_SPEED_TYP;
    }
    setDrawMenuLevel(selectedMenu, 0);
  }
  else if ((millis() - menuActiveSince) > 10000 && subMenuTriggered) {
    subMenuTriggered = false;
    settingStandardValueType();
    if (SourceIsLarus) {
      selectedMenu = MENU_HIGHT_TYP;
    }
    else {
      selectedMenu = MENU_SPEED_TYP;
    }
    setDrawMenuLevel(selectedMenu, 0);
  }
  else if ((millis() - menuActiveSince) > 10000 && menuWasTriggered) {
    menuWasTriggered = false;
    settingStandardValueType();
    if (SourceIsLarus) {
      selectedMenu = MENU_HIGHT_TYP;
    }
    else {
      selectedMenu = MENU_SPEED_TYP;
    }
    setDrawMenuLevel(selectedMenu, 0);
  }
}

// ===== SerialScan.ino =====
void SerialScan (void *p) {
  Serial.println("Serial Scan Task Created");
  float valueVaaAsFloat = 0;
  float valueVanAsFloat = 0;
  float valueGrsAsFloat = 0;
  float valueHagAsFloat = 0;
  float valueHigAsFloat = 0;
  float valueFLAsFloat = 0;
  float valueVoltageAsFloat = 0;
  float tem = 0;
  float hea = 0;
  double stfValue = 0;
  char serialString;
  String mod;
  bool serial2Error = false;
  unsigned long lastTimeSerial2 = 0;
  int pos, pos1, pos2;
  int oldstf_mode_state = digitalRead(STF_MODE);

  while (showBootscreen) {
    vTaskDelay(1000);
  }
  while (1) {
    String dataString;

    // ============================================================
    // TCP Server : attendre que XCSoar se connecte (OpenVario TCP Client)
    // XCSoar config : driver OpenVario, TCP Client, host 192.168.2.1, port 4353
    // ============================================================
    if (!nmeaClient || !nmeaClient.connected()) {
      SourceIsWifi = false;
      WiFiClient newClient = nmeaServer.available();
      if (newClient) {
        nmeaClient = newClient;
        SourceIsWifi = true;
        Serial.println("[WiFi] XCSoar connecte au TCP Server ESP32 !");
      }
    }

    // Envoi periodique $POV,P (pression simulee) pour maintenir XCSoar actif
    // et declencher l'envoi de $POV,V (vario), $POV,A (TAS), $POV,E (alt) en retour
    if (SourceIsWifi && nmeaClient.connected()) {
      static unsigned long lastPovP = 0;
      if (millis() - lastPovP > 2000) {
        lastPovP = millis();
        String povP = "$POV,P,101325*";
        int cs = calculateChecksum(povP);
        povP += String(cs, HEX);
        povP.toUpperCase();
        nmeaClient.println(povP);
        Serial.println("[WiFi->XCSoar] " + povP);
      }
    }

    // Priorite : WiFi TCP (XCSoar) > Serial2 (vrai vario) > Serial (USB)
    if (SourceIsWifi && nmeaClient.connected() && nmeaClient.available()) {
      serialString = nmeaClient.read();
      if (serialString == '$') {
        long timeSystemReady = millis() - lastTimeReady;
        while (serialString != 10) {
          if (serialString >= 32 && serialString <= 126) {
            dataString += serialString;
          }
          if (!nmeaClient.available()) vTaskDelay(2);
          serialString = nmeaClient.read();
          if (dataString.length() > 300) {
            dataString = "ERROR";
            Serial.println("Break WiFi Read!");
            break;
          }
        }
        if (timeSystemReady < 2000) {
          nmeaClient.println("$PFV,M,S,0.5*59");
          nmeaClient.println("$PFV,Q,S,1013*6D");
        }
        Serial.println("[WiFi] " + dataString);
      }
    } else if (!SourceIsWifi) {
      // Fallback Serial2 ou Serial USB
      Stream* src = Serial2.available() ? (Stream*)&Serial2
                  : (Serial.available() ? (Stream*)&Serial : nullptr);
      if (src) {
        serialString = src->read();
        if (serialString == '$') {
          long timeSystemReady = millis() - lastTimeReady;
          while (serialString != 10) {
            if (serialString >= 32 && serialString <= 126) {
              dataString += serialString;
            }
            serialString = src->read();
            if (dataString.length() > 300) {
              dataString = "ERROR";
              Serial.println("Break serial Read!");
              break;
            }
          }
          if (timeSystemReady < 2000) {
            Serial2.println("$PFV,M,S,0.5*59");
            Serial2.println("$PFV,Q,S,1013*6D");
          }
          //Serial.println(dataString);
        }
      }
    }
    // ─── Sortie du mode DEMO dès que des données NMEA arrivent ───────────────
    if (isDemoMode && dataString.startsWith("$") && dataString.length() > 4) {
      isDemoMode = false;
      Serial.println("[Data] Source NMEA detectee - sortie du mode DEMO");
    }

    //****************************
    //****  XCSoar is source  ****
    //****************************
    if ((dataString.startsWith("$PFV,VAR")) || (dataString.startsWith("$PFV,VAN"))) {
      if (!SourceIsXCSoar) {
        SourceIsXCSoar = true;
        SourceIsLarus = false;
      }
    }

    //*************************************************************
    //****  OpenVario $POV protocol (XCSoar TCP Server output)  ****
    //  $POV,V,{vario_m_s}    vertical speed
    //  $POV,E,{alt_baro_m}   barometric altitude
    //  $POV,A,{airspeed_kmh} true airspeed
    //  $POV,S,{vario_m_s}    netto vario
    //  $POV,T,{temp_c}       temperature
    //  $POV,L,{volts}        battery voltage
    //*************************************************************
    if (dataString.startsWith("$POV,V") || dataString.startsWith("$POV,E") ||
        dataString.startsWith("$POV,A") || dataString.startsWith("$POV,S")) {
      if (!SourceIsXCSoar) {
        SourceIsXCSoar = true;
        SourceIsLarus = false;
        Serial.println("[POV] OpenVario source detected");
      }
    }
    if (dataString.startsWith("$POV") && SourceIsXCSoar == true) {
      // Parse: $POV,<type>,<value>*CS
      int pos0 = dataString.indexOf(',');          // after $POV
      int pos1 = dataString.indexOf(',', pos0+1);  // after type letter
      int pos2 = dataString.indexOf('*', pos1+1);  // before checksum
      if (pos0 > 0 && pos1 > 0) {
        String povType = dataString.substring(pos0+1, pos1);
        String povVal  = (pos2 > 0) ? dataString.substring(pos1+1, pos2)
                                     : dataString.substring(pos1+1);
        float povFloat = povVal.toFloat();
        char buf[20];
        if (povType == "V") {                      // vario integre
          var = povFloat;
        } else if (povType == "S") {               // netto vario
          valueVanAsFloat = povFloat;
          valueVanAsString = (povFloat >= 0 ? "+" : "") + String(povFloat, 1);
        } else if (povType == "E") {               // altitude baro
          valueHigAsFloat = povFloat;
          valueHigAsString = dtostrf(povFloat, 4, 0, buf);
        } else if (povType == "A") {               // airspeed km/h
          valueTasAsFloat = povFloat;
          valueTasAsString = dtostrf(povFloat, 3, 0, buf);
        } else if (povType == "L") {               // tension batterie
          voltage = String(povFloat, 1);
        }
        lastTimeSerial2 = millis();
      }
    }

    if (dataString.startsWith("$PFV") && SourceIsXCSoar == true) {
      if (serial2Error == true) {
        serial2Error = false;
        Serial.println("Error detected");

        // synchronize QNH after reboot of OV
        String qnhStr = ("$PFV,Q,S," + String(valueQnhAsFloat) + "*");
        int checksumQnh = calculateChecksum(qnhStr);
        String checksumQnhAsString = String (checksumQnh, HEX);
        String syncQnh = (qnhStr + checksumQnhAsString);
        Serial2.println(syncQnh);

        // synchronize Bug after reboot of OV
        String bugStr = ("$PFV,B,S," + String(valueBugAsFloat) + "*");
        int checksumBug = calculateChecksum(bugStr);
        String checksumBugAsString = String (checksumBug, HEX);
        String syncBug = (bugStr + checksumBugAsString);
        Serial2.println(syncBug);

        // synchronize MC after reboot of OV
        String mcStr = ("$PFV,M,S," + valueMacAsString + "*");
        int checksumMc = calculateChecksum(mcStr);
        String checksumMcAsString = String (checksumMc, HEX);
        String synMc = (mcStr + checksumMcAsString);
        Serial2.println(synMc);
      }

      lastTimeSerial2 = millis();
      //Serial2.println(DataString);
      int pos = dataString.indexOf(',');
      dataString.remove(0, pos + 1);
      int pos1 = dataString.indexOf(',');                   //findet den Ort des ersten ,
      String variable = dataString.substring(0, pos1);      //erfasst den ersten Datensatz
      int pos2 = dataString.indexOf('*', pos1 + 1 );        //findet den Ort des *
      String wert = dataString.substring(pos1 + 1, pos2);   //erfasst den zweiten Datensatz
      float wertAsFloat = wert.toFloat();                   // der Wert als float

      //**********************************
      //****  analyse vertical speed  ****
      //**********************************
      if (variable == "VAR") {
        var = wertAsFloat;
      }

      //******************************************
      //****  analyse average vertical speed  ****
      //******************************************
      else if (variable == "VAA") {
        valueVaaAsFloat = wertAsFloat;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        if (valueVaaAsFloat >= 0) {
          valueVaaAsString = dtostrf(abs(valueVaaAsFloat), 3, 1, buf);
          valueVaaAsString = "+" + valueVaaAsString;
        }
        else {
          valueVaaAsString = dtostrf(abs(valueVaaAsFloat), 3, 1, buf);
          valueVaaAsString = "-" + valueVaaAsString;
        }
      }

      //****************************************
      //****  analyse netto vertical speed  ****
      //****************************************
      else if (variable == "VAN") {
        valueVanAsFloat = wertAsFloat;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        if (valueVanAsFloat >= 0) {
          valueVanAsString = dtostrf(abs(valueVanAsFloat), 3, 1, buf);
          valueVanAsString = "+" + valueVanAsString;
        }
        else {
          valueVanAsString = dtostrf(abs(valueVanAsFloat), 3, 1, buf);
          valueVanAsString = "-" + valueVanAsString;
        }
      }

      //*******************************************
      //****  analyse internal McCready value  ****
      //*******************************************
      else if (variable == "MCI") {
        valueMacAsFloat = wertAsFloat;
        mci = true;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueMacAsString = dtostrf(valueMacAsFloat, 3, 1, buf);
      }

      //*******************************************
      //****  analyse external McCready value  ****
      //*******************************************
      else if ((variable == "MCE") && (mci == false)) {
        valueMacAsFloat = wertAsFloat;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueMacAsString = dtostrf(valueMacAsFloat, 3, 1, buf);
      }

      //***************************************
      //****  analyse current XCSoar mode  ****
      //***************************************
      else if (variable == "MOD") {
        mod = wert;
      }

      //********************************
      //****  analyse speed to fly  ****
      //********************************
      else if (variable == "STF") {
        stfValue = wert.toFloat();
        if (valueTasAsFloat > 10) {
          int FF = (valueAttenAsInt * 10) + 1;
          stf = filter(stfValue, FF);
        }
        else {
          stf = valueTasAsFloat;
        }
      }

      //*********************************
      //****  analyse true airspeed  ****
      //*********************************
      else if (variable == "TAS") {
        valueTasAsFloat = wertAsFloat;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueTasAsString = dtostrf(valueTasAsFloat, 3, 0, buf);
      }

      //*******************************
      //****  analyse groundspeed  ****
      //*******************************
      else if (variable == "GRS") {
        valueGrsAsFloat = wertAsFloat;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueGrsAsString = dtostrf(valueGrsAsFloat, 3, 0, buf);
      }

      //*****************************
      //****  analyse hight MSL  ****
      //*****************************
      else if (variable == "HIG") {
        valueHigAsFloat = wertAsFloat;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueHigAsString = dtostrf(valueHigAsFloat, 4, 0, buf);
      }

      //********************************************
      //****  analyse hight about ground level  ****
      //********************************************
      else if (variable == "HAG") {
        valueHagAsFloat = wertAsFloat;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueHagAsString = dtostrf(valueHagAsFloat, 4, 0, buf);
      }

      //*****************************************
      //****  analyse average wind strength  ****
      //*****************************************
      else if (variable == "AWS") {
        valueAwsAsFloat = wertAsFloat * 3.6;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueAwsAsString = dtostrf(valueAwsAsFloat, 3, 0, buf);
      }

      //*****************************************
      //****  analyse current wind strength  ****
      //*****************************************
      else if (variable == "CWS") {
        valueCwsAsFloat = wertAsFloat * 3.6;
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueCwsAsString = dtostrf(valueCwsAsFloat, 3, 0, buf);
      }

      //******************************************
      //****  analyse average wind direction  ****
      //******************************************
      else if (variable == "AWD") {
        valueAwdAsFloat = wertAsFloat;
      }

      //******************************************
      //****  analyse current wind direction  ****
      //******************************************
      else if (variable == "CWD") {
        valueCwdAsFloat = wertAsFloat;
      }

      //******************************
      //****  analyse temperatur  ****
      //******************************
      else if (variable == "TEM") {
        tem = wertAsFloat;
      }

      //***********************
      //****  analyse QNH  ****
      //***********************
      else if (variable == "QNH") {
        valueQnhAsFloat = wertAsFloat;
        valueQnhAsString = String(valueQnhAsFloat, 0);
      }

      //***********************
      //****  analyse bug  ****
      //***********************
      else if (variable == "BUG") {
        valueBugAsFloat = wertAsFloat;
        valueBugAsString = String(valueBugAsFloat, 0);
      }

      //************************
      //****  analyse Mute  ****
      //************************
      else if (variable == "MUT") {
        valueMuteAsInt = wert.toInt();
      }

      //*******************************
      //****  analyse Attenuation  ****
      //*******************************
      else if (variable == "ATT") {
        valueAttenAsInt = wert.toInt();
      }
    }

    //***************************
    //****  Larus is source  ****
    //***************************

    //****************************
    //****  analyse headingd  ****
    //****************************
    if (dataString.startsWith("$PLARA")) {

      if (!SourceIsLarus) {
        nameSpeed = "TAS";
        nameSetting = "Time";
        SourceIsLarus = true;
        SourceIsXCSoar = false;
      }

      if (serial2Error == true) {
        serial2Error = false;
        Serial.println("Error detected");
      }

      int pos0 = dataString.indexOf('*');
      String dataToCheck = dataString.substring(0, pos0);
      dataString.remove(0, pos0 + 1);
      String CheckSum = dataString;
      CheckSum.toLowerCase();
      CheckSum.trim();
      int checksum = calculateChecksum(dataToCheck);
      String checksumString = String(checksum, HEX);
      if (CheckSum == checksumString) {
        lastTimeSerial2 = millis();
        //Serial2.println(dataString);
        dataToCheck.remove(0, 7);
        int pos1 = dataToCheck.indexOf(',');                   //findet den Ort des ersten ,
        int pos2 = dataToCheck.indexOf(',', pos1 + 1);         //findet den Ort des zweiten ,
        String HEA = dataToCheck.substring(pos2 + 1, pos0);    //erfasst das aktuelle Heading
        hea = HEA.toFloat();                                   //wandelt das aktuelle Heading in float
      }
    }

    //***************************************************************************
    //****  analyse current and average climb rate, hight and true airspeed  ****
    //***************************************************************************
    if (dataString.startsWith("$PLARV")) {

      if (!SourceIsLarus) {
        nameSpeed = "TAS";
        nameSetting = "Time";
        SourceIsLarus = true;
        SourceIsXCSoar = false;
      }

      if (serial2Error == true) {
        serial2Error = false;
        Serial.println("Error detected");
      }

      int pos0 = dataString.indexOf('*');
      String dataToCheck = dataString.substring(0, pos0);
      dataString.remove(0, pos0 + 1);
      String CheckSum = dataString;
      CheckSum.toLowerCase();
      CheckSum.trim();
      int checksum = calculateChecksum(dataToCheck);
      String checksumString = String(checksum, HEX);
      if (CheckSum == checksumString) {

        lastTimeSerial2 = millis();
        //Serial2.println(dataString);
        dataToCheck.remove(0, 7);
        int pos1 = dataToCheck.indexOf(',');                   //findet den Ort des ersten ,
        String VAR = dataToCheck.substring(0, pos1);           //erfasst das aktuelle Steigen
        var = VAR.toFloat();                                   //wandelt das aktuelle Steigen in float

        int pos2 = dataToCheck.indexOf(',', pos1 + 1);         //findet den Ort des zweiten ,
        String VAA = dataToCheck.substring(pos1 + 1, pos2);    //erfasst das gemittelte Steigen

        valueVaaAsFloat = VAA.toFloat();                       //wandelt das gemittelte Steigen in float
        char buf[20];                                          //wandelt das gemittelte Steigen in String inkl. Vorzeichen um
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        if (valueVaaAsFloat >= 0) {
          valueVaaAsString = dtostrf(abs(valueVaaAsFloat), 3, 1, buf);
          valueVaaAsString = "+" + valueVaaAsString;
        }
        else {
          valueVaaAsString = dtostrf(abs(valueVaaAsFloat), 3, 1, buf);
          valueVaaAsString = "-" + valueVaaAsString;
        }

        int pos3 = dataToCheck.indexOf(',', pos2 + 1);         //findet den Ort des dritten ,
        String HIG = dataToCheck.substring(pos2 + 1, pos3);    //erfasst die barometrischen Höhe
        valueHigAsFloat = HIG.toFloat();                       //wandelt die barometrischen Höhe in float
        valueFLAsFloat = valueHigAsFloat / 30;                 //errechnet die Flugfläche
        char buf1[20];
        char buf2[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueHigAsString = dtostrf(valueHigAsFloat, 4, 0, buf1);
        valueFLAsString = dtostrf(valueFLAsFloat, 4, 0, buf2);

        int pos4 = dataToCheck.indexOf(',', pos3 + 1);         //findet den Ort des vierten ,
        String TAS = dataToCheck.substring(pos3 + 1, pos4);    //erfasst die TAS
        valueTasAsFloat = TAS.toFloat();                       //wandelt die TAS in float
        char buf3[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueTasAsString = dtostrf(valueTasAsFloat, 3, 0, buf3);
      }
    }

    //**************************************************
    //****  analyse instantaneous and average wind  ****
    //**************************************************
    if (dataString.startsWith("$PLARW")) {

      if (!SourceIsLarus) {
        nameSpeed = "TAS";
        nameSetting = "Time";
        SourceIsLarus = true;
        SourceIsXCSoar = false;
      }

      if (serial2Error == true) {
        serial2Error = false;
        Serial.println("Error detected");
      }

      int pos0 = dataString.indexOf('*');
      String dataToCheck = dataString.substring(0, pos0);
      dataString.remove(0, pos0 + 1);
      String CheckSum = dataString;
      CheckSum.toLowerCase();
      CheckSum.trim();
      int checksum = calculateChecksum(dataToCheck);
      String checksumString = String(checksum, HEX);
      if (CheckSum == checksumString) {
        lastTimeSerial2 = millis();
        //Serial2.println(dataString);
        dataToCheck.remove(0, 7);
        int pos1 = dataToCheck.indexOf(',');                   //findet den Ort des ersten ,
        String WD = dataToCheck.substring(0, pos1);            //erfasst die Windrichtung
        float valueWdAsFloat = WD.toFloat();                   //wandelt die Windrichtung in float

        int pos2 = dataToCheck.indexOf(',', pos1 + 1);         //findet den Ort des zweiten ,
        String WS = dataToCheck.substring(pos1 + 1, pos2);     //erfasst die Windstärke
        float valueWsAsFloat = WS.toFloat();                   //wandelt die Windstärke in float

        int pos3 = dataToCheck.indexOf(',', pos2 + 1);         //findet den Ort des dritten ,
        String WTYP = dataToCheck.substring(pos2 + 1, pos3);   //erfasst die Windart

        if (WTYP == "I") {                                     //legt Windart fest
          valueCwdAsFloat = valueWdAsFloat - hea + 180;
          valueCwsAsFloat = valueWsAsFloat;
          char buf[20];
          // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
          valueCwsAsString = dtostrf(valueCwsAsFloat, 3, 0, buf);
        }
        else if (WTYP == "A") {
          valueAwdAsFloat = valueWdAsFloat - hea + 180;
          valueAwsAsFloat = valueWsAsFloat;
          char buf[20];
          // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
          valueAwsAsString = dtostrf(valueAwsAsFloat, 3, 0, buf);
        }
      }
    }

    //***********************************
    //****  analyse battery voltage  ****
    //***********************************
    if (dataString.startsWith("$PLARB")) {

      if (!SourceIsLarus) {
        nameSpeed = "TAS";
        nameSetting = "Time";
        SourceIsLarus = true;
        SourceIsXCSoar = false;
      }

      if (serial2Error == true) {
        serial2Error = false;
        Serial.println("Error detected");
      }

      int pos0 = dataString.indexOf('*');
      String dataToCheck = dataString.substring(0, pos0);
      dataString.remove(0, pos0 + 1);
      String CheckSum = dataString;
      CheckSum.toLowerCase();
      CheckSum.trim();
      int checksum = calculateChecksum(dataToCheck);
      String checksumString = String(checksum, HEX);
      if (CheckSum == checksumString) {
        lastTimeSerial2 = millis();
        //Serial2.println(dataString);
        dataToCheck.remove(0, 7);
        int pos1 = dataToCheck.indexOf(',');                   //findet den Ort des ersten ,
        voltage = dataToCheck.substring(0, pos1);              //erfasst die Spannung
        float valueVoltageAsFloat = voltage.toFloat();         //wandelt die Spannung in float
        char buf[20];
        // dtostrf(floatvar, stringlength, digits_after_decimal, charbuf);
        valueVoltageAsString = dtostrf(valueVoltageAsFloat, 3, 1, buf);
      }
    }

    //***********************
    //****  analyse UTC  ****
    //***********************
    if (dataString.startsWith("$GPRMC")) {

      if (!SourceIsLarus) {
        nameSpeed = "TAS";
        nameSetting = "Time";
        SourceIsLarus = true;
        SourceIsXCSoar = false;
      }

      if (serial2Error == true) {
        serial2Error = false;
        Serial.println("Error detected");
      }

      int pos0 = dataString.indexOf('*');
      String dataToCheck = dataString.substring(0, pos0);
      dataString.remove(0, pos0 + 1);
      String CheckSum = dataString;
      CheckSum.toLowerCase();
      CheckSum.trim();
      int checksum = calculateChecksum(dataToCheck);
      String checksumString = String(checksum, HEX);
      if (CheckSum == checksumString) {
        lastTimeSerial2 = millis();
        //Serial2.println(dataString);
        dataToCheck.remove(0, 7);
        int pos1 = dataToCheck.indexOf(',');                   //findet den Ort des ersten ,
        UTCHour = dataToCheck.substring(0, 2);                 //erfasst die Stunde
        UTCMinute = dataToCheck.substring(2, 4);               //erfasst die Minuten
      }
    }

    else if ((millis() - lastTimeSerial2) > 10000) {
      serial2Error = true;
    }

    //*******************************
    //****  XCSoar WiFi (NMEA)   ****
    //*******************************
    // Détection source XCSoar/LK8000 via WiFi
    if (!SourceIsXCSoar && (dataString.startsWith("$LXWP0") || dataString.startsWith("$LXWP2") ||
        dataString.startsWith("$PLXVC") || dataString.startsWith("$PGRMZ") ||
        dataString.startsWith("$GPRMC") || dataString.startsWith("$GPGGA") ||
        dataString.startsWith("$LK8EX1"))) {
      SourceIsXCSoar = true;
      SourceIsLarus  = false;
    }

    // $PLXVC : réponse XCSoar (driver LXNav) → MC, STF, vent
    // $PLXVC,MC,STF_mode,STF,wind_dir,wind_spd,flags*CS
    if (dataString.startsWith("$PLXVC")) {
      int c[7]; int cc = 0;
      for (int i = 0; i < (int)dataString.length() && cc < 7; i++) {
        if (dataString[i] == ',' || dataString[i] == '*') c[cc++] = i;
      }
      if (cc >= 5) {
        // MacCready (champ 1)
        float mc = dataString.substring(c[0] + 1, c[1]).toFloat();
        if (mc >= 0.0f && mc <= 5.0f) {
          valueMacAsFloat = mc;
          char buf[8]; dtostrf(valueMacAsFloat, 3, 1, buf);
          valueMacAsString = String(buf);
        }
        // STF km/h (champ 3)
        float stfKmh = dataString.substring(c[2] + 1, c[3]).toFloat();
        if (stfKmh > 0) stf = stfKmh;
        // Vent direction (champ 4) et vitesse (champ 5, m/s → km/h)
        float wDir = dataString.substring(c[3] + 1, c[4]).toFloat();
        float wSpd = dataString.substring(c[4] + 1, c[5]).toFloat() * 3.6f;
        if (wSpd >= 0.0f) {
          valueAwdAsFloat = wDir;
          valueAwsAsFloat = wSpd;
          char buf[8]; dtostrf(valueAwsAsFloat, 3, 0, buf);
          valueAwsAsString = String(buf);
        }
      }
    }

    // $LXWP0 : vario, vitesse air, altitude, netto vario
    // $LXWP0,logger,IAS,BaroAlt,Vario,Hdg,WS,WD,u,u,CmpHdg,GS,Netto*cs
    if (dataString.startsWith("$LXWP0")) {
      String s = dataString;
      int p = s.indexOf(',') + 1; // skip $LXWP0
      p = s.indexOf(',', p) + 1;  // skip logger → champ 2 (IAS)

      // IAS km/h
      int e = s.indexOf(',', p);
      float ias = s.substring(p, e).toFloat();
      if (ias > 0) {
        valueTasAsFloat = ias;
        char buf[20];
        valueTasAsString = dtostrf(valueTasAsFloat, 3, 0, buf);
      }
      p = e + 1;

      // Altitude baro m
      e = s.indexOf(',', p);
      float alt = s.substring(p, e).toFloat();
      if (alt > 0) {
        valueHigAsFloat = alt;
        char buf[20];
        valueHigAsString = dtostrf(valueHigAsFloat, 4, 0, buf);
      }
      p = e + 1;

      // Vario m/s
      e = s.indexOf(',', p);
      String varStr = s.substring(p, e);
      if (varStr.length() > 0) var = varStr.toFloat();
      p = e + 1;

      // Sauter champs 5 à 11 (7 virgules)
      for (int i = 0; i < 7; i++) {
        int next = s.indexOf(',', p);
        if (next < 0) { p = -1; break; }
        p = next + 1;
      }

      // Netto vario m/s (champ 12)
      if (p > 0) {
        e = s.indexOf('*', p);
        if (e < 0) e = s.length();
        valueVaaAsFloat = s.substring(p, e).toFloat();
        char buf[20];
        if (valueVaaAsFloat >= 0)
          valueVaaAsString = "+" + String(dtostrf(abs(valueVaaAsFloat), 3, 1, buf));
        else
          valueVaaAsString = "-" + String(dtostrf(abs(valueVaaAsFloat), 3, 1, buf));
      }
    }

    // $LXWP2 : MacCready
    // $LXWP2,MC,bugs,ballast,...
    else if (dataString.startsWith("$LXWP2")) {
      int c1 = dataString.indexOf(',');
      int c2 = dataString.indexOf(',', c1 + 1);
      if (c1 > 0 && c2 > 0) {
        float mc = dataString.substring(c1 + 1, c2).toFloat();
        if (mc >= 0.0 && mc <= 5.0) {
          valueMacAsFloat = mc;
          char mcBuf[8];
          dtostrf(valueMacAsFloat, 3, 1, mcBuf);
          valueMacAsString = String(mcBuf);
        }
      }
    }

    // $GPRMC : vitesse sol et cap
    // $GPRMC,time,A,lat,N,lon,E,speed_kts,heading,...
    else if (dataString.startsWith("$GPRMC")) {
      // Trouver les 10 premières virgules/étoiles
      int c[10]; int cc = 0;
      for (int i = 0; i < (int)dataString.length() && cc < 10; i++) {
        if (dataString[i] == ',' || dataString[i] == '*') c[cc++] = i;
      }
      if (cc >= 9 && dataString.substring(c[1] + 1, c[2]) == "A") {
        // Speed knots → km/h (champ 7)
        float speedKts = dataString.substring(c[6] + 1, c[7]).toFloat();
        valueGrsAsFloat = speedKts * 1.852f;
        char buf[20];
        valueGrsAsString = dtostrf(valueGrsAsFloat, 3, 0, buf);
        // Cap (champ 8)
        hea = dataString.substring(c[7] + 1, c[8]).toFloat();
      }
    }

    // $GPGGA : altitude GPS + dérivation du vario
    // $GPGGA,time,lat,N,lon,E,fix,sats,hdop,alt,M,...
    else if (dataString.startsWith("$GPGGA")) {
      int c[12]; int cc = 0;
      for (int i = 0; i < (int)dataString.length() && cc < 12; i++) {
        if (dataString[i] == ',' || dataString[i] == '*') c[cc++] = i;
      }
      // champ 6 = fix quality (0 = invalide)
      if (cc >= 10 && dataString.substring(c[5] + 1, c[6]) != "0") {
        float gpAlt = dataString.substring(c[8] + 1, c[9]).toFloat();
        // Dérive vario depuis l'altitude GPS
        static float prevGpAlt  = -9999.0f;
        static unsigned long prevGpAltMs = 0;
        unsigned long nowMs = millis();
        if (prevGpAlt > -9000.0f && nowMs > prevGpAltMs) {
          float dt = (nowMs - prevGpAltMs) / 1000.0f;
          if (dt > 0.05f) {
            float rawVario = (gpAlt - prevGpAlt) / dt;
            // Filtre passe-bas léger (GPS vario bruité)
            var = var * 0.6f + rawVario * 0.4f;
          }
        }
        prevGpAlt   = gpAlt;
        prevGpAltMs = nowMs;
        // Altitude affichée
        valueHigAsFloat = gpAlt;
        char buf[20];
        valueHigAsString = dtostrf(valueHigAsFloat, 4, 0, buf);
      }
    }

    // $PGRMZ : altitude barométrique envoyée par XCSoar (pieds → mètres)
    // $PGRMZ,ALT,f,2*CS  (f=feet, 2=baro-derived)
    else if (dataString.startsWith("$PGRMZ")) {
      int c1 = dataString.indexOf(',');
      int c2 = dataString.indexOf(',', c1 + 1);
      if (c1 > 0 && c2 > 0) {
        float altFt = dataString.substring(c1 + 1, c2).toFloat();
        float altM  = altFt * 0.3048f;
        if (altM > -500.0f && altM < 10000.0f) {
          // Vario dérivé de la baro (plus précis que GPS)
          static float prevBaroAlt = -9999.0f;
          static unsigned long prevBaroMs = 0;
          unsigned long nowMs = millis();
          if (prevBaroAlt > -9000.0f && nowMs > prevBaroMs) {
            float dt = (nowMs - prevBaroMs) / 1000.0f;
            if (dt > 0.05f) {
              float rawV = (altM - prevBaroAlt) / dt;
              var = var * 0.5f + rawV * 0.5f;
            }
          }
          prevBaroAlt = altM;
          prevBaroMs  = nowMs;
          valueHigAsFloat = altM;
          char buf[20];
          valueHigAsString = dtostrf(valueHigAsFloat, 4, 0, buf);
        }
      }
    }

    // $LK8EX1 : LK8000 app / variomètres paragliding → baro alt + vario
    // $LK8EX1,pressure_pa,altitude_m,vario_cms,temp_c,batt_mv*CS
    // altitude_m = 99999 si non dispo ; vario_cms = 99999 si non dispo
    else if (dataString.startsWith("$LK8EX1")) {
      int c[6]; int cc = 0;
      for (int i = 0; i < (int)dataString.length() && cc < 6; i++) {
        if (dataString[i] == ',' || dataString[i] == '*') c[cc++] = i;
      }
      if (cc >= 4) {
        // Altitude barométrique en mètres (champ 2)
        int altM = dataString.substring(c[1] + 1, c[2]).toInt();
        if (altM != 99999 && altM > -1000 && altM < 20000) {
          valueHigAsFloat = (float)altM;
          char buf[10];
          valueHigAsString = dtostrf(valueHigAsFloat, 4, 0, buf);
          nameHight = "MSL";
        }
        // Vario en cm/s → m/s (champ 3) - valeur déjà filtrée par LK8000
        int varioCms = dataString.substring(c[2] + 1, c[3]).toInt();
        if (varioCms != 99999) {
          float rawVar = varioCms / 100.0f;
          var = var * 0.3f + rawVar * 0.7f;  // léger lissage pour l'affichage
        }
      }
    }

    // $IIMWV : vent calculé par XCSoar
    // $IIMWV,angle,R/T,speed,K/N/M,A*CS
    // R=relatif au cap, T=vent vrai ; K=km/h, N=noeuds, M=m/s
    else if (dataString.startsWith("$IIMWV") || dataString.startsWith("$WIMWV")) {
      int c[6]; int cc = 0;
      for (int i = 0; i < (int)dataString.length() && cc < 6; i++) {
        if (dataString[i] == ',' || dataString[i] == '*') c[cc++] = i;
      }
      if (cc >= 5 && dataString.substring(c[4] + 1, c[5]) == "A") {
        float wAngle = dataString.substring(c[0] + 1, c[1]).toFloat();
        String ref   = dataString.substring(c[1] + 1, c[2]);  // R ou T
        float wSpd   = dataString.substring(c[2] + 1, c[3]).toFloat();
        String unit  = dataString.substring(c[3] + 1, c[4]);  // K/N/M
        // Conversion en km/h
        if (unit == "N") wSpd *= 1.852f;
        else if (unit == "M") wSpd *= 3.6f;
        valueAwsAsFloat = wSpd;
        char buf[20];
        valueAwsAsString = dtostrf(valueAwsAsFloat, 3, 0, buf);
        // Direction : si relatif (R), on ajoute le cap
        if (ref == "R") valueAwdAsFloat = fmod(wAngle + hea + 360.0f, 360.0f);
        else            valueAwdAsFloat = wAngle;
      }
    }

    //*****************************
    //****  Check Flight Mode  ****
    //*****************************
    if (SourceIsXCSoar == true) {
      if (nameHight == "FL") {
        nameHight = "MSL";
      }
      stf_mode_state = digitalRead(STF_MODE);
      if (oldstf_mode_state != stf_mode_state || !WasSend) {
        if (digitalRead(STF_MODE) == LOW && digitalRead(STF_AUTO) == LOW) {
          Serial2.println("$POV,C,VAR*4F");  //Vario-Mode
          Serial2.println("$PFV,F,C*45");    //Vario-Mode
          WasSend = true;
          AutoWasSend = false;
          oldstf_mode_state = digitalRead(STF_MODE);
        }
        else if (digitalRead(STF_MODE) == HIGH && digitalRead(STF_AUTO) == LOW) {
          Serial2.println("$POV,C,STF*4B");  //STF-Mode
          Serial2.println("$PFV,F,S*55");    //STF-Mode
          WasSend = true;
          AutoWasSend = false;
          oldstf_mode_state = digitalRead(STF_MODE);
        }
      }
      if (digitalRead(STF_AUTO) == HIGH && !AutoWasSend) {
        Serial2.println("$PFV,F,A*47");    //Auto-Mode
        AutoWasSend = true;
        WasSend = false;
      }
      if (digitalRead(STF_MODE) == HIGH) {
        stf_mode = "STF";
      }
      else if (digitalRead(STF_MODE) == LOW) {
        stf_mode = "Vario";
      }
    }
    else if (SourceIsLarus == true) {
      if (nameHight == "AGL") {
        nameHight = "MSL";
      }
      stf_mode = "Vario";
    }

    dataString = "";
    vTaskDelay(20);
  }
}

// ===== ShowBootScreen.ino =====
void showBootScreen(String versionString) {
  String waitingMessage = "Waiting for Data ...";
  String dataString;
  long showVersionTime = millis();
  long ChangeBaud = millis();
  int serial2IsReady = 0;
  int baudDetect = 0;
  unsigned long loopTime = 5000;
  tft.loadFont("micross20");
  TFT_eSprite bootSprite = TFT_eSprite(&tft);
  bootSprite.loadFont("micross20_boot");
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.fillScreen(TFT_WHITE);
  drawLogo();
  bootSprite.println(versionString);
  bootSprite.pushSprite(40, 245);
  bootSprite.deleteSprite();
  lastTimeBoot = millis();
  changeMode = digitalRead(STF_MODE);
  oldChangeMode = changeMode;
  while (millis() - showVersionTime <= loopTime) {
    changeMode = digitalRead(STF_MODE);
    if (oldChangeMode != changeMode) {
      updatemode = true;
      loopTime = millis() + 500;
      bootSprite.loadFont("micross20_boot");
      bootSprite.createSprite(195, 25);
      bootSprite.fillSprite(TFT_WHITE);
      bootSprite.setCursor(0, 2);
      bootSprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.fillScreen(TFT_WHITE);
      tft.setWindow(40, 55, 40 + 193, 55 + 155);
      tft.pushColors(logoOV, 194 * 156);
      bootSprite.println("starting Update Mode");
      bootSprite.pushSprite(40, 245);
      bootSprite.deleteSprite();
    }
    oldChangeMode = changeMode;
  }
  if (updatemode == false) {
    // AP WiFi deja demarre dans setup() - on affiche juste l'info
    bootSprite.createSprite(195, 25);
    bootSprite.fillSprite(TFT_WHITE);
    bootSprite.setCursor(0, 2);
    bootSprite.println("WiFi: FV_Displayboard");
    bootSprite.pushSprite(40, 245);
    bootSprite.deleteSprite();
    delay(1000);

    bootSprite.createSprite(195, 25);
    bootSprite.fillSprite(TFT_WHITE);
    bootSprite.setCursor(0, 2);
    bootSprite.println(waitingMessage);
    bootSprite.pushSprite(40, 245);
    bootSprite.deleteSprite();

    //********************************************************
    //****  Waiting until XCSoar delivers correct values  ****
    //****  Accepte Serial2 OU WiFi TCP (port 8880)       ****
    //****  OR timeout after 15s for standalone demo mode  ****
    //********************************************************
    unsigned long waitStart = millis();
    do {
      // --- Source Serial2 ---
      if (Serial2.available()) {
        char serialString = Serial2.read();
        if (serialString == '$') {
          while (serialString != 10) {
            dataString += serialString;
            serialString = Serial2.read();
          }
        } else {
          if ((!SourceIsXCSoar && !SourceIsLarus) && (baudDetect == 0) && (millis() - ChangeBaud <= 5000)) {
            Serial2.end();
            Serial.println("Looking for XCSoar");
            Serial.println("baud rate is set to 115200");
            Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
            delay(500);
            baudDetect = 1;
          } else if ((!SourceIsXCSoar && !SourceIsLarus) && (baudDetect == 1) && (millis() - ChangeBaud > 5000) && (millis() - ChangeBaud <= 10000)) {
            Serial2.end();
            Serial.println("Looking for Larus");
            Serial.println("baud rate is set to 38400");
            Serial2.begin(38400, SERIAL_8N1, RXD2, TXD2);
            delay(500);
            baudDetect = 0;
          } else if ((!SourceIsXCSoar && !SourceIsLarus) && (millis() - ChangeBaud > 10000)) {
            ChangeBaud = millis();
          }
        }
        // Sortir des qu'une phrase NMEA valide est recue (n'importe quel driver)
        if (dataString.startsWith("$") && dataString.length() > 4) {
          serial2IsReady = 1;
        }
        dataString = "";
      }

      // --- Source Serial USB (simulation depuis PC via port COM) ---
      if (Serial.available()) {
        char sc = Serial.read();
        if (sc == '$') {
          dataString = "$";
          while (sc != '\n') {
            if (Serial.available()) {
              sc = Serial.read();
              if (sc >= 32 && sc <= 126) dataString += sc;
            } else { delay(1); }
            if (dataString.length() > 300) break;
          }
          if (dataString.startsWith("$") && dataString.length() > 4) {
            serial2IsReady = 1;
          }
          dataString = "";
        }
      }

      // --- Source WiFi TCP (XCSoar se connecte à l'ESP32 TCP Server) ---
      if (!nmeaClient || !nmeaClient.connected()) {
        WiFiClient newClient = nmeaServer.available();
        if (newClient) {
          nmeaClient = newClient;
          SourceIsWifi = true;
          Serial.println("Boot: XCSoar connecte au TCP Server ESP32 !");
        }
      }
      if (SourceIsWifi && nmeaClient.connected() && nmeaClient.available()) {
        char wc = nmeaClient.read();
        if (wc == '$') {
          dataString = "$";
          while (wc != 10 && nmeaClient.connected()) {
            if (nmeaClient.available()) {
              wc = nmeaClient.read();
              if (wc >= 32 && wc <= 126) dataString += wc;
            } else { delay(1); }
          }
          Serial.println("Boot WiFi: " + dataString);
          // Sortir des qu'une phrase NMEA valide est recue (n'importe quel driver)
          if (dataString.startsWith("$") && dataString.length() > 4) {
            serial2IsReady = 1;
          }
          dataString = "";
        }
      }

      // Timeout de 90 secondes : mode demo sans XCSoar
      if (millis() - waitStart > 90000) {
        Serial.println("Timeout: no XCSoar detected - starting in DEMO mode");
        isDemoMode = true;
        SourceIsXCSoar = false;
        SourceIsLarus = false;
        var = 0.0;
        stf = 0.0;
        valueTasAsFloat = 0;
        valueVaaAsString = "+0.0";
        valueVanAsString = "+0.0";
        valueHigAsString = "0";
        valueGrsAsString = "0";
        valueTasAsString = "0";
        serial2IsReady = 1;  // sortir de la boucle
      }
    } while (serial2IsReady == 0);  //0 = waiting, 1 = start
    bootSprite.unloadFont();
    tft.fillScreen(TFT_BLACK);
    lastTimeReady = millis();
    showBootscreen = false;
  }
}

void drawLogo() {

  fs::File bmpFS;

  if (SPIFFS.exists("/FreeVario_194x156.bmp")) {
    bmpFS = SPIFFS.open("/FreeVario_194x156.bmp", "r");
    drawBmp(bmpFS, 40, 55);
    bmpFS.close();
  } else {
#ifdef DEBUG
    Serial.println("Logo not found. Using deprecated LogoOV2. Update of SPIFFS required.");
#endif
    bool swap = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.pushImage(40, 55, 194, 156, logoOV);
    tft.setSwapBytes(swap);
  }

}

// ===== UpdateMode.ino =====
void UpdateMode() {
  if (updatemode == true) {

    pushButtonPressTime = millis();
    if (Wificount == 0) {
      updateScreen();
      Wificount = 1;
      WiFi.softAP(ssid, password);
      delay(100);
      IPAddress Ip(192, 168, 2, 1);  //setto IP Access Point same as gateway
      IPAddress NMask(255, 255, 255, 0);
      WiFi.softAPConfig(Ip, Ip, NMask);
      IPAddress myIP = WiFi.softAPIP();
      server.begin();
      displayIP = myIP.toString();
      if (displayIP != "" && displayIP != "0.0.0.0") {
        displayMode = "Ready to connect";
      } else {
        displayMode = "WiFi Error";
      }
      delay(10);
      Serial.println();
      Serial.println("Startup");

      while ((soundIP == "") || (soundIP == "0.0.0.0")) {
        char Data;
        String DataString;
        if (Serial2.available()) {
          Data = Serial2.read();
          if (Data == '$') {
            while (Data != 10) {
              DataString += Data;
              Data = Serial2.read();
            }
            //Serial.println(DataString);
          }
          if (DataString.startsWith("$PFV")) {
            //Serial2.println(DataString);
            int pos = DataString.indexOf(',');
            DataString.remove(0, pos + 1);
            int pos1 = DataString.indexOf(',');                  //finds the place of the first,
            String variable = DataString.substring(0, pos1);     //captures the first record
            int pos2 = DataString.indexOf('*', pos1 + 1);        //finds the place of *
            String wert = DataString.substring(pos1 + 1, pos2);  //captures the second record

            if (variable == "Ready") {
              soundIP = wert;
              soundMode = "Ready to connect";
            }

            if (variable == "Error") {
              soundIP = wert;
              soundMode = "WiFi Error";
            }
          }
          DataString = "";
          vTaskDelay(50);
        }
      }

      //use mdns for host name resolution
      if (!MDNS.begin(host)) {  //http://esp32.local
        Serial.println("Error setting up MDNS responder!");
        while (1) {
          delay(1000);
        }
      }
      Serial.println("mDNS responder started");
      //return index page which is stored in serverIndex //
      server.on("/", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", "/display-login.html");
      });
      server.on("/serverIndex", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", "/display-update.html");
      });
      server.on("/style.css", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/css", "/style.css");
      });
      server.on("/script.js", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/js", "/script.js");
      });
      //handling uploading firmware file //
      server.on(
        "/update", HTTP_POST, []() {
          server.sendHeader("Connection", "close");
          server.send((Update.hasError()) ? 422 : 200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
          delay(1000);
          ESP.restart();
        },
        []() {
          HTTPUpload& upload = server.upload();
          if (upload.status == UPLOAD_FILE_START) {
            String filename = upload.filename;
            if (filename.endsWith(".bin")) {
              Serial.printf("Update: %s\n", filename.c_str());
              if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {  //start with max available size
                Update.printError(Serial);
              }
            } else if (filename.indexOf("spiffs") != -1) {
              Serial.printf("Update SPIFFS: %s\n", filename.c_str());
              size_t spiffsSize = SPIFFS.totalBytes();    // Get the size of the SPIFFS partition
              if (!Update.begin(spiffsSize, U_SPIFFS)) {  // Start update of SPIFFS partition
                Update.printError(Serial);
              }
            } else {
              Serial.println("Error: filename must end with '.bin' or contain 'spiffs'");
            }
          } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              Update.printError(Serial);
            }
          } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {  //true to set the size to the current progress
              Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
            } else {
              Update.printError(Serial);
            }
          }
        });
      updateScreen();
    }
    server.handleClient();
    delay(1);
  }
}

// ===== UpdateScreen.ino =====
void updateScreen() {
  String modeMessage = "Update Mode";
  tft.loadFont("micross20");
  TFT_eSprite bootSprite = TFT_eSprite(&tft);
  bootSprite.loadFont("micross20_boot");
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.fillScreen(TFT_WHITE);
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.println(modeMessage);
  bootSprite.pushSprite(85, 35);
  bootSprite.deleteSprite();
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.println("FV_Displayboard:");
  bootSprite.pushSprite(35, 85);
  bootSprite.deleteSprite();
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.println(displayMode);
  bootSprite.pushSprite(35, 105);
  bootSprite.deleteSprite();
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.println("IP: " + displayIP);
  bootSprite.pushSprite(35, 125);
  bootSprite.deleteSprite();
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.println("FV_Soundboard:");
  bootSprite.pushSprite(35, 175);
  bootSprite.deleteSprite();
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.println(soundMode);
  bootSprite.pushSprite(35, 195);
  bootSprite.deleteSprite();
  bootSprite.createSprite(195, 25);
  bootSprite.fillSprite(TFT_WHITE);
  bootSprite.setCursor(0, 2);
  bootSprite.println("IP: " + soundIP);
  bootSprite.pushSprite(35, 215);
  bootSprite.deleteSprite();
}

// ===== ValueRefresh.ino =====
void ValueRefresh(void *parameter) {
  String valueSetting, valueSpeed, valueHight;
  String unitSpeed, unitHight, unitSetting;
  while (showBootscreen) {
    vTaskDelay(1000);
  }
  background.setColorDepth(8);
  background.createSprite(240, 320);
  background.fillSprite(TFT_BLACK);
  while (true) {
    if (nameHight == "MSL") {
      valueHight = valueHigAsString;
    }
    else if (nameHight == "AGL") {
      valueHight = valueHagAsString;
    }
    else if (nameHight == "FL") {
      valueHight = valueFLAsString;
    }
    if (nameSpeed == "GS") {
      valueSpeed = valueGrsAsString;
    }
    else if (nameSpeed == "TAS") {
      valueSpeed = valueTasAsString;
    }

    //void DrawText(TFT_eSprite fontOfName, TFT_eSprite fontOfInfo, String spriteName, String value,
    //String unit, int spriteNameWidth, int spriteValueHight, int spriteValueWidth, int spriteunitWidth, int x, int y)
    background.setPivot(154, 160);
    background.fillSprite(TFT_BLACK);
    ArcRefresh();

    //*****************************************
    //****  Calculate and Refresh Needles  ****
    //*****************************************
    if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {

      if (valueAwdAsFloat < 0) {
        valueAwdAsFloat + 360;
      }

      if (valueCwdAsFloat < 0) {
        valueCwdAsFloat + 360;
      }

      needleBlue.createSprite(20, 130);
      needleBlue.drawWedgeLine(11, 0, 11, 130, 1, 10, BLUE);
      needleBlue.pushRotated(&background, valueAwdAsFloat, TFT_BLACK);

      if (valueCwdAsFloat != -1000) {
        needleGreen.createSprite(20, 130);
        needleGreen.drawWedgeLine(11, 0, 11, 130, 1, 10, TFT_GREEN);
        needleGreen.pushRotated(&background, valueCwdAsFloat, TFT_BLACK);
      }
    }

    //****************************
    //****  Draw Menu Frames  ****
    //****************************
    if (requestMenuPaint) {
      if (requestDrawMenuLevel == 2) {
        if (requestDrawMenu == 1) {
          if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
            offset = 0;
          }
          else {
            offset = -18;
          }
          drawRectangle.createSprite(170, 41);
          drawRectangle.fillSprite(TFT_BLACK);
          drawRectangle.drawRect(2, 2, 166, 37, TFT_RED);
          drawRectangle.drawRect(1, 1, 168, 39, TFT_RED);
          drawRectangle.drawRect(0, 0, 170, 41, TFT_RED);
          drawRectangle.pushToSprite(&background, 55, 126 + offset, TFT_BLACK);
          drawRectangle.deleteSprite();
        }

        if (requestDrawMenu == 2) {
          if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
            offset = 0;
          }
          else {
            offset = -12;
          }
          drawRectangle.createSprite(151, 41);
          drawRectangle.fillSprite(TFT_BLACK);
          drawRectangle.drawRect(2, 2, 148, 37, TFT_RED);
          drawRectangle.drawRect(1, 1, 150, 39, TFT_RED);
          drawRectangle.drawRect(0, 0, 152, 41, TFT_RED);
          drawRectangle.pushToSprite(&background, 73, 164 + offset, TFT_BLACK);
          drawRectangle.deleteSprite();
        }

        if (requestDrawMenu == 3) {
          if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
            offset = 0;
          }
          else {
            offset = -6;
          }
          drawRectangle.createSprite(151, 41);
          drawRectangle.fillSprite(TFT_BLACK);
          drawRectangle.drawRect(2, 2, 147, 37, TFT_RED);
          drawRectangle.drawRect(1, 1, 149, 39, TFT_RED);
          drawRectangle.drawRect(0, 0, 151, 41, TFT_RED);
          drawRectangle.pushToSprite(&background, 74, 201 + offset, TFT_BLACK);
          drawRectangle.deleteSprite();
        }
      }
      else if (requestDrawMenuLevel == 3) {
        drawRectangle.createSprite(150, 3);
        drawRectangle.fillSprite(TFT_BLACK);
        drawRectangle.drawLine(0, 2, 150, 2, TFT_RED);;
        drawRectangle.drawLine(0, 1, 150, 1, TFT_RED);
        drawRectangle.drawLine(0, 0, 150, 0, TFT_RED);
        drawRectangle.pushToSprite(&background, 74, 241 + offset, TFT_BLACK);
        drawRectangle.deleteSprite();
      }
    }

    //**************************
    //****  Refresh Values  ****
    //**************************

    if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
      offset = 0;
    }
    else {
      offset = -24;
    }
    if (stf_mode_state == 0) {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE ) {
        DrawText(nameOfField, TFT_WHITE, "large", "Avg.", valueVaaAsString, 34, 40, 99, 88, 84 + offset); //+1
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    if (stf_mode_state == 1) {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        DrawText(nameOfField, TFT_WHITE, "large", "Net.", valueVanAsString, 34, 40, 99, 88, 84 + offset);
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
      if (valueCwsAsFloat >= 99) {
        valueCwsAsString = "99";
      }
      if (valueAwsAsFloat >= 99) {
        valueAwsAsString = "99";
      }
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        DrawText(nameOfField, TFT_WHITE, "small", "Wind", "", 36, 25, 0, 105, 45);
        xSemaphoreGive(xTFTSemaphore);
      }
      if (valueCwdAsFloat != -1000) {
        if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
        {
          DrawText(nameOfField, TFT_GREEN, "small", "", valueCwsAsString, 0, 25, 40, 140, 40);
          //avsWasUpdated = true;
          //casWasUpdated = false;
          xSemaphoreGive(xTFTSemaphore);
        }
      }
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        DrawText(nameOfField, BLUE, "small", "", valueAwsAsString, 0, 25, 40, 176, 40);
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
      offset = 0;
    }
    else {
      offset = -18;
    }

    if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
    {
      if ((requestDrawMenuLevel == 1 ) && (requestDrawMenu == 1) || (requestDrawMenuLevel == 2 ) && (requestDrawMenu == 1)) {
        DrawText(nameOfField, TFT_RED, "small", nameSpeed, valueSpeed + " km/h", 28, 25, 130, 63, 136 + offset); //+18
      }
      else {
        DrawText(nameOfField, TFT_WHITE, "small", nameSpeed, valueSpeed + " km/h", 28, 25, 130, 63, 136 + offset);
      }
      xSemaphoreGive(xTFTSemaphore);
    }


    if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
      offset = 0;
    }
    else {
      offset = -12;
    }

    if (!SourceIsLarus || (SourceIsLarus && nameHight != "FL")) {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {

        if ((requestDrawMenuLevel == 1 ) && (requestDrawMenu == 2) || (requestDrawMenuLevel == 2 ) && (requestDrawMenu == 2)) {
          DrawText(nameOfField, TFT_RED, "small", nameHight, valueHight + " m", 31, 25, 108, 82, 173 + offset); //+12
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", nameHight, valueHight + " m", 31, 25, 108, 82, 173 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }
    else if (SourceIsLarus && nameHight == "FL") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {

        if ((requestDrawMenuLevel == 1 ) && (requestDrawMenu == 2) || (requestDrawMenuLevel == 2 ) && (requestDrawMenu == 2)) {
          DrawText(nameOfField, TFT_RED, "small", nameHight, valueHight + "", 31, 25, 108, 82, 173 + offset); //+12
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", nameHight, valueHight + "", 31, 25, 108, 82, 173 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    if ((valueWindAsInt == 1) && (valueAwdAsFloat != -1000)) {
      offset = 0;
    }
    else {
      offset = -6;
    }
    if (nameSetting == "MC") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = valueMacAsString;
        if ((requestDrawMenuLevel == 1 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "MC", valueSetting + " m/s", 24, 25, 114, 83, 210 + offset); //+6
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "MC", valueSetting + " m/s", 24, 25, 114, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "Time") {
      int TimeHour = UTCHour.toInt() + TimeDifference;
      String Time = String(TimeHour) + ":" + UTCMinute;
      String TimeType = "UTC";
      if (TimeDifference != 0) {
        TimeType = "Local";
      }

      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        if ((requestDrawMenuLevel == 1 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", TimeType, Time, 35, 25, 103, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", TimeType, Time, 35, 25, 103, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "QNH") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = valueQnhAsString;
        if ((requestDrawMenuLevel == 2 ) && (requestDrawMenu == 3) || (requestDrawMenuLevel == 3 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "QNH", valueSetting, 50, 25, 88, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "QNH", valueSetting, 50, 25, 88, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "Bug") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = valueBugAsString;
        if ((requestDrawMenuLevel == 2 ) && (requestDrawMenu == 3) || (requestDrawMenuLevel == 3 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "Bug", valueSetting + " %", 39, 25, 99, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "Bug", valueSetting + " %", 39, 25, 99, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "ATTEN") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = valueAttenAsString;
        if ((requestDrawMenuLevel == 2 ) && (requestDrawMenu == 3) || (requestDrawMenuLevel == 3 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "ATT", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "ATT", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "Mute") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = valueMuteAsString;
        if ((requestDrawMenuLevel == 2 ) && (requestDrawMenu == 3) || (requestDrawMenuLevel == 3 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "Mute", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "Mute", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "Wind") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = valueWindAsString;
        if ((requestDrawMenuLevel == 2 ) && (requestDrawMenu == 3) || (requestDrawMenuLevel == 3 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "Wind", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "Wind", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "STF") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = valueSTFAsString;
        if ((requestDrawMenuLevel == 2 ) && (requestDrawMenu == 3) || (requestDrawMenuLevel == 3 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "STF", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "STF", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    else if (nameSetting == "TimeDifference") {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        valueSetting = String(TimeDifference);
        if (TimeDifference >= 0) {
          valueSetting = "+" + String(TimeDifference);
        }
        if ((requestDrawMenuLevel == 2 ) && (requestDrawMenu == 3) || (requestDrawMenuLevel == 3 ) && (requestDrawMenu == 3)) {
          DrawText(nameOfField, TFT_RED, "small", "Diff.", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        else {
          DrawText(nameOfField, TFT_WHITE, "small", "Diff.", valueSetting, 39, 25, 99, 83, 210 + offset);
        }
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    if (!SourceIsLarus) {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        DrawText(nameOfField, TFT_WHITE, "small", "Mode", stf_mode, 38, 25, 75, 105, 248);
        xSemaphoreGive(xTFTSemaphore);
      }
    }
    else {
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        DrawText(nameOfField, TFT_WHITE, "small", "Bat.", valueVoltageAsString + " V", 25, 45, 90, 105, 248);
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    if (isDemoMode) {
      // Mode DEMO : point rouge clignotant en haut a gauche
      if ( xSemaphoreTake( xTFTSemaphore, ( TickType_t ) 5 ) == pdTRUE )
      {
        static uint8_t blinkCount = 0;
        blinkCount++;
        uint32_t blinkColor = (blinkCount < 33) ? TFT_RED : TFT_BLACK;
        if (blinkCount >= 66) blinkCount = 0;
        background.fillCircle(10, 10, 5, blinkColor);
        xSemaphoreGive(xTFTSemaphore);
      }
    }

    vTaskDelay(15);
    background.pushSprite(0, 0);
  }
  if (requestMenuPaint) {
    requestMenuPaint = false;
  }
}

// ===== Loop.ino =====
void loop() {
  if (showBootscreen) {
    UpdateMode();
  }
  else {
    EncoderReader();
    Menu ();
    if (millis() - stayAlive >= 9000) {
      String muteStr = ("$PFV,S,S," + String(valueMuteAsInt) + "*");
      int checksum = calculateChecksum(muteStr);
      Serial2.printf("%s%X\n", muteStr.c_str(), checksum);
      stayAlive = millis();
    }
  }
}

// ===== Setup.ino =====
void setup() {
  server.on("/SoftwareVersion", []() {
    server.send(200, "application/json",  "\"" + SOFTWARE_VERSION + "\"");
  });

  //***********************************************
  //****  Enable the weak pull UP resistors    ****
  //***********************************************
  ESP32Encoder::useInternalWeakPullResistors = puType::UP;

  if ( xTFTSemaphore == NULL )
  { xTFTSemaphore = xSemaphoreCreateMutex();
    if ( ( xTFTSemaphore ) != NULL )
      xSemaphoreGive( ( xTFTSemaphore ) );
  }

  tft.init();
  tft.setRotation(0);
  //************************************
  //****  set starting count value  ****
  //************************************
  Vario_Enc.attachSingleEdge(26, 32);   // GPIO 26=CLK, 32=DT (SingleEdge to fix double-stepping per detent)

  Vario_Enc.setCount(16380);
  pinMode(VE_PB, INPUT_PULLUP);
  pinMode(STF_MODE, INPUT_PULLDOWN);  // no switch = LOW = Vario mode by default
  pinMode(STF_AUTO, INPUT_PULLUP);
  Serial.begin(115200, SERIAL_8N1);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  // Demarrage WiFi AP (serveur de mise a jour)
  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, 2, 1);
  IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, apMask);
  WiFi.softAP(ssid, password);
  delay(100);
  displayIP = WiFi.softAPIP().toString();
  Serial.println("WiFi AP: " + String(ssid) + "  IP: " + displayIP);
  nmeaServer.begin();
  Serial.println("TCP NMEA Server port 4353 - XCSoar: OpenVario TCP Client -> 192.168.2.1:4353");
  SPIFFSstart();
  server.serveStatic("/B612-Bold.ttf", SPIFFS, "/B612-Bold.ttf");
  server.serveStatic("/B612-Regular.ttf", SPIFFS, "/B612-Regular.ttf");
  server.serveStatic("/style.css", SPIFFS, "/style.css");
  server.serveStatic("/script.js", SPIFFS, "/script.js");
  server.serveStatic("/serverIndex", SPIFFS, "/display-update.html");
  server.serveStatic("/", SPIFFS, "/display-login.html");

  xTaskCreate(SerialScan, "Serial Scan", 3000, NULL, 2, &SerialScanTask);
  xTaskCreate(ValueRefresh, "Value Refresh", 4500, NULL, 24, &TaskValueRefresh);
  //xTaskCreate(printWatermark, "Print Watermark", 2048, NULL, 1, NULL);     //Task zum Messen des benötigten Stacks der anderen Tasks

  prefs.begin("settings", false);
  valueMuteAsInt = prefs.getUInt("Mute", 1);
  valueAttenAsInt = prefs.getUInt("ATTEN", 2);
  valueWindAsInt = prefs.getUInt("Wind", 1);
  valueSTFAsInt = prefs.getUInt("STF", 1);
  nameSpeed = prefs.getString("nameSpeed", "GS");
  nameHight = prefs.getString("nameHight", "MSL");
  TimeDifference = prefs.getUInt("TimeDifference", 0);
  prefs.end();

  valueAttenAsString = String(valueAttenAsInt);

  if (valueMuteAsInt == 0) {
    valueMuteAsString = "OFF";
  }
  else if (valueMuteAsInt == 1) {
    valueMuteAsString = "ON";
  }

  if (valueWindAsInt == 0) {
    valueWindAsString = "OFF";
  }
  else if (valueWindAsInt == 1) {
    valueWindAsString = "ON";
  }

  if (valueSTFAsInt == 1) {
    valueSTFAsString = "OV";
  }
  else if (valueSTFAsInt == 0) {
    valueSTFAsString = "Flaps";
  }
  showBootScreen(SOFTWARE_VERSION);
}
