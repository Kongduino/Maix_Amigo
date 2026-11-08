#include <Arduino.h>
#include <Amigo_ILI9486.h>

Amigo_ILI9486 lcd;

static const uint16_t bars[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
                                COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE, COLOR_BLACK};

void drawTestScreen(uint8_t rotation) {
  lcd.setRotation(rotation);
  int16_t w = lcd.width(), h = lcd.height();
  int16_t barW = w / 8;
  for (uint8_t i = 0; i < 8; i++) lcd.fillRect(i * barW, 0, barW, h / 2, bars[i]);
  lcd.fillRect(0, h / 2, w, h - h / 2, COLOR_NAVY);
  lcd.drawRect(0, 0, w, h, COLOR_WHITE);
  lcd.setTextColor(COLOR_WHITE);
  lcd.setTextSize(3);
  lcd.setCursor(10, h / 2 + 10);
  lcd.printf("Maix Amigo");
  lcd.setTextSize(2);
  lcd.setCursor(10, h / 2 + 50);
  lcd.printf("rotation %d: %dx%d", rotation, w, h);
  lcd.fillCircle(w - 40, h - 40, 25, COLOR_ORANGE);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_GREEN, OUTPUT);
  lcd.begin();
  Serial.println("Display initialised");
}

void loop() {
  static uint8_t rotation = 0;
  drawTestScreen(rotation);
  Serial.printf("rotation %d\n", rotation);
  digitalWrite(LED_GREEN, rotation & 1 ? LOW : HIGH);
  rotation = (rotation + 1) & 3;
  delay(3000);
}
