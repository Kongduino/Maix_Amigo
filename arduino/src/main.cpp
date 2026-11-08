#include <Arduino.h>
#include <Amigo_ILI9486.h>
#include <Amigo_FT6X36.h>

// Finger painting. Press the BOOT key (KEY1) to rotate and clear the screen,
// which also checks that touch stays aligned with the display.

Amigo_ILI9486 lcd;
Amigo_FT6X36 touch;

static const uint16_t pointColors[FT6X36_MAX_POINTS] = {COLOR_YELLOW, COLOR_CYAN};
static uint8_t rotation = 0;

void resetScreen() {
  lcd.setRotation(rotation);
  touch.setRotation(rotation);
  lcd.fillScreen(COLOR_BLACK);
  lcd.drawRect(0, 0, lcd.width(), lcd.height(), COLOR_DARKGREY);
  lcd.setTextColor(COLOR_WHITE, COLOR_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(6, 6);
  lcd.printf("rotation %d  %dx%d", rotation, lcd.width(), lcd.height());
}

void setup() {
  Serial.begin(115200);
  pinMode(KEY1, INPUT_PULLUP);
  lcd.begin();
  if (touch.begin()) {
    Serial.printf("FT6X36 found: chip 0x%02X, firmware 0x%02X, vendor 0x%02X\n",
                  touch.chipID(), touch.firmwareVersion(), touch.vendorID());
  } else {
    Serial.println("FT6X36 not found");
  }
  resetScreen();
}

void loop() {
  static int lastKey = HIGH;
  int key = digitalRead(KEY1);
  if (key == LOW && lastKey == HIGH) {
    rotation = (rotation + 1) & 3;
    resetScreen();
  }
  lastKey = key;

  TouchPoint points[FT6X36_MAX_POINTS];
  uint8_t n = touch.getPoints(points);
  for (uint8_t i = 0; i < n; i++) {
    lcd.fillCircle(points[i].x, points[i].y, 4, pointColors[i]);
    lcd.setCursor(6, 30 + 20 * i);
    lcd.printf("P%d %3d,%3d ", i + 1, points[i].x, points[i].y);
    Serial.printf("P%d id %d event %d: %d,%d\n", i + 1, points[i].id, points[i].event,
                  points[i].x, points[i].y);
  }
  delay(10);
}
