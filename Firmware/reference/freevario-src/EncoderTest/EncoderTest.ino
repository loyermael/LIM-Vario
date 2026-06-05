#include <Arduino.h>
#include <ESP32Encoder.h>

ESP32Encoder encoder;

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ENCODER TEST START ---");

  // Activer les résistances de tirage vers le haut internes
  ESP32Encoder::useInternalWeakPullResistors = puType::UP;
  
  // Broches 26 et 32
  encoder.attachHalfQuad(26, 32);
  encoder.setCount(0);
  
  // Bouton sur broche 27
  pinMode(27, INPUT_PULLUP);
  
  Serial.println("Turn the encoder or press the button!");
}

long lastPosition = -999;

long lastHeartbeat = 0;

void loop() {
  if (millis() - lastHeartbeat >= 1000) {
    Serial.print(".");
    lastHeartbeat = millis();
  }

  long newPosition = encoder.getCount();
  if (newPosition != lastPosition) {
    Serial.println();
    Serial.print("Encoder Position: ");
    Serial.println(newPosition);
    lastPosition = newPosition;
  }
  
  if (digitalRead(27) == LOW) {
    Serial.println();
    Serial.println("BUTTON PRESSED!");
    delay(200); // debounce
  }
  
  delay(10);
}
