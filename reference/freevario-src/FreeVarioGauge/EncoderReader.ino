void EncoderReader() {

  //*****************************************
  //****  Read Encoder & Buttons / Dips  ****
  //*****************************************
  const long LONGPRESS_TIME     = 500;
  const long SHORTPRESS_TIME_MIN = 20;
  const int  DEBOUNCE_DELAY     = 20;

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
