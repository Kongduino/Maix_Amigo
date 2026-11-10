#include <Arduino.h>
#include <Amigo_ILI9486.h>
#include <Amigo_GPS.h>

// GPS status screen, like gps.py: position, satellites, HDOP, altitude,
// speed, course, UTC date and time, refreshed every second. Fixes are also
// printed to Serial.
// UNTESTED: builds, but hasn't run with a GPS module yet.

Amigo_ILI9486 lcd;
Amigo_GPS gps;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static const uint16_t BG_COLOR = rgb(2, 105, 198);  // as gps.py
static const uint16_t DIM_COLOR = rgb(150, 190, 230);

#define LINE_H     26
#define LINE_CHARS 26  // characters per line at text size 2

// Fixed-point decimals, without printf("%f"), which this newlib build may
// not support
static const char* fmtFixed(double v, uint8_t decimals, char* buf, size_t len) {
  uint32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  int64_t t = llround(v * scale);
  uint64_t a = t < 0 ? -t : t;
  if (decimals)
    snprintf(buf, len, "%s%u.%0*u", t < 0 ? "-" : "", (unsigned)(a / scale), decimals, (unsigned)(a % scale));
  else
    snprintf(buf, len, "%s%u", t < 0 ? "-" : "", (unsigned)a);
  return buf;
}

// One line of the status screen, padded so it overwrites the previous one
static void drawLine(uint8_t row, uint16_t color, const char* fmt, ...) {
  char text[64];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  lcd.setTextColor(color, BG_COLOR);
  lcd.setCursor(10, 50 + row * LINE_H);
  lcd.printf("%-*.*s", LINE_CHARS, LINE_CHARS, text);
}

static void drawStatus() {
  char a[24], b[24];
  lcd.startWrite();  // the whole screen in one transfer
  lcd.setTextSize(2);
  uint8_t row = 0;

  if (gps.silent()) {
    drawLine(row++, rgb(255, 230, 120), "No data from the GPS");
    drawLine(row++, DIM_COLOR, "Grove: RX IO%u, TX IO%u", AMIGO_GPS_RX, AMIGO_GPS_TX);
  } else {
    bool fix = gps.location.isValid() && gps.location.age() < 3000;
    drawLine(row++, fix ? COLOR_WHITE : rgb(255, 230, 120), fix ? "Fix" : "Searching...");
    drawLine(row++, DIM_COLOR, "%u chars, %u bad", (unsigned)gps.charsProcessed(),
             (unsigned)gps.failedChecksum());
  }
  row++;

  bool loc = gps.location.isValid();
  drawLine(row++, loc ? COLOR_WHITE : DIM_COLOR, "Lat: %s", loc ? fmtFixed(gps.location.lat(), 6, a, sizeof(a)) : "?");
  drawLine(row++, loc ? COLOR_WHITE : DIM_COLOR, "Lng: %s", loc ? fmtFixed(gps.location.lng(), 6, a, sizeof(a)) : "?");
  drawLine(row++, COLOR_WHITE, "Alt: %s m",
           gps.altitude.isValid() ? fmtFixed(gps.altitude.meters(), 1, a, sizeof(a)) : "?");
  drawLine(row++, COLOR_WHITE, "Satellites: %s",
           gps.satellites.isValid() ? fmtFixed(gps.satellites.value(), 0, a, sizeof(a)) : "?");
  drawLine(row++, COLOR_WHITE, "HDOP: %s", gps.hdop.isValid() ? fmtFixed(gps.hdop.hdop(), 1, a, sizeof(a)) : "?");
  drawLine(row++, COLOR_WHITE, "Speed: %s km/h",
           gps.speed.isValid() ? fmtFixed(gps.speed.kmph(), 1, a, sizeof(a)) : "?");
  drawLine(row++, COLOR_WHITE, "Course: %s",
           gps.course.isValid() ? fmtFixed(gps.course.deg(), 0, a, sizeof(a)) : "?");
  row++;

  if (gps.date.isValid() && gps.date.year() > 2000)
    snprintf(a, sizeof(a), "%04u-%02u-%02u", gps.date.year(), gps.date.month(), gps.date.day());
  else
    snprintf(a, sizeof(a), "?");
  if (gps.time.isValid())
    snprintf(b, sizeof(b), "%02u:%02u:%02u", gps.time.hour(), gps.time.minute(), gps.time.second());
  else
    snprintf(b, sizeof(b), "?");
  drawLine(row++, COLOR_WHITE, "Date UTC: %s", a);
  drawLine(row++, COLOR_WHITE, "Time UTC: %s", b);
  lcd.endWrite();
}

void setup() {
  Serial.begin(115200);
  lcd.begin();
  lcd.setRotation(1);
  lcd.fillScreen(BG_COLOR);
  lcd.setTextSize(3);
  lcd.setTextColor(COLOR_WHITE, BG_COLOR);
  lcd.setCursor((lcd.width() - 3 * 18) / 2, 10);
  lcd.print("GPS");
  gps.begin();
  drawStatus();
}

void loop() {
  static uint32_t lastDraw = 0;
  if (gps.update() && gps.location.isUpdated()) {
    char a[24], b[24];
    Serial.printf("Fix: %s, %s, %u satellites\n", fmtFixed(gps.location.lat(), 6, a, sizeof(a)),
                  fmtFixed(gps.location.lng(), 6, b, sizeof(b)), (unsigned)gps.satellites.value());
  }
  if (millis() - lastDraw >= 1000) {
    lastDraw = millis();
    drawStatus();
  }
  delay(10);
}
