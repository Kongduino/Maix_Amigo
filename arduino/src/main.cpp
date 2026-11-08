#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  Serial.println("Hello from the Maix Amigo");
}

void loop() {
  static const uint8_t leds[] = {LED_RED, LED_GREEN, LED_BLUE};
  static uint8_t i = 0;
  for (uint8_t j = 0; j < 3; j++) digitalWrite(leds[j], j == i ? LOW : HIGH);
  Serial.printf("LED %d\n", i);
  i = (i + 1) % 3;
  delay(500);
}
