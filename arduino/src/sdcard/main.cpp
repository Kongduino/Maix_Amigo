#include <Arduino.h>
#include <Amigo_ILI9486.h>
#include <Amigo_SD.h>

// SD card demo: mounts the TF card, lists the root directory, appends a line
// to AMIGO.TXT and reads the file back. Output on the screen and Serial.

Amigo_ILI9486 lcd;

#define LOG(...) do { Serial.printf(__VA_ARGS__); lcd.printf(__VA_ARGS__); } while (0)

// The full listing goes to Serial. The screen doesn't scroll, so it only
// gets what fits while leaving room for the file contents below.
#define LCD_RESERVED_LINES 12

static void listRoot() {
  File root = SD.open("/");
  if (!root) {
    LOG("Can't open the root directory\n");
    return;
  }
  uint16_t count = 0, hidden = 0;
  char line[32];
  while (File entry = root.openNextFile()) {
    if (entry.isDirectory())
      snprintf(line, sizeof(line), " %-12s   <DIR>\n", entry.name());
    else
      snprintf(line, sizeof(line), " %-12s %8u\n", entry.name(), (unsigned)entry.size());
    entry.close();
    Serial.print(line);
    if (lcd.getCursorY() + 8 * LCD_RESERVED_LINES < lcd.height())
      lcd.print(line);
    else
      hidden++;
    count++;
  }
  root.close();
  if (!count) LOG(" (empty)\n");
  if (hidden) lcd.printf(" ... %u more, see Serial\n", (unsigned)hidden);
}

static void appendLine() {
  File f = SD.open("AMIGO.TXT", FILE_WRITE);
  if (!f) {
    LOG("Can't open AMIGO.TXT for writing\n");
    return;
  }
  f.printf("Hello from the Amigo, %u ms after boot\n", (unsigned)millis());
  f.close();
}

static void dumpFile() {
  File f = SD.open("AMIGO.TXT");
  if (!f) {
    LOG("Can't open AMIGO.TXT for reading\n");
    return;
  }
  LOG("AMIGO.TXT, %u bytes:\n", (unsigned)f.size());
  // The last few lines only, to fit on the screen
  uint32_t size = f.size();
  if (size > 400) f.seek(size - 400);
  while (f.available()) {
    char c = f.read();
    Serial.write(c);
    lcd.write(c);
  }
  f.close();
}

void setup() {
  Serial.begin(115200);
  lcd.begin();
  lcd.setRotation(1);
  lcd.fillScreen(COLOR_BLACK);
  lcd.setTextColor(COLOR_WHITE, COLOR_BLACK);
  lcd.setTextSize(1);
  lcd.setCursor(0, 0);

  LOG("SD card demo\n\n");
  if (!amigoSdBegin()) {
    LOG("No card, or no FAT16/FAT32 volume on it\n");
    return;
  }
  LOG("Card mounted. Root directory:\n");
  listRoot();
  LOG("\n");
  appendLine();
  dumpFile();
}

void loop() {
}
