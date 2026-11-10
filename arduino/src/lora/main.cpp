// LoRa tester for the SP-LoRa (SX1276) module, ported from LoRa_Tester.py
// (MaixPy + sx127x.py) to RadioLib. Same radio defaults and the same touch
// menu: ping, bandwidth, spreading factor, frequency, TX power.
// Incoming packets are shown with their RSSI and SNR. With an SD card in the
// TF slot, every event is also appended to a CSV log, which the Log button
// shows, with the position from a GPS on the Grove port when it has a fix.
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Amigo_ILI9486.h>
#include <Amigo_FT6X36.h>
#include <Amigo_SD.h>
#include <Amigo_GPS.h>

// SP-LoRa plugged straight into the left SP-MOD socket. MISO lands on IO31,
// which is also the BACK key: pressing it during a transfer corrupts it.
// (LoRa_Tester.py used a jumper cable instead: SCK 19, MISO 9, RST 22.)
#define LORA_CS        12
#define LORA_RST       19
#define LORA_SCK        9
#define LORA_MOSI       7
#define LORA_MISO      31
#define LORA_SPI_FREQ  2000000

// Radio settings from sx127x.py, so both can talk to each other
#define LORA_CR        5      // coding rate 4/5
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE  8

#define LOG_HEADER "ms,event,freq_mhz,bw_khz,sf,tx_dbm,rssi,snr,lat,lng,payload"
#define GPS_MAX_AGE 10000  // ms: an older position isn't logged

// Portrait, like LoRa_Tester.py. Use 3 if the screen is upside down.
#define SCREEN_ROTATION 1

SPIClass loraSPI(SPI1, LORA_SCK, LORA_MISO, LORA_MOSI, -1, LORA_SPI_FREQ);
// The SP-MOD LoRa module has no DIO0 line: like sx127x.py, this program
// polls the IRQ flags register instead. RadioLib only needs the pin for its
// blocking transmit()/receive()/scanChannel(), which aren't used here.
SX1276 radio = new Module(LORA_CS, RADIOLIB_NC, LORA_RST, RADIOLIB_NC, loraSPI,
                          SPISettings(LORA_SPI_FREQ, SPI_MSBFIRST, SPI_MODE0));
Amigo_ILI9486 lcd;
Amigo_FT6X36 touch;
Amigo_GPS gps;

// Current settings, defaults as in LoRa_Tester.py
static uint32_t freqKHz = 433000;
static uint32_t bwHz = 125000;
static uint8_t  sf = 12;
static int8_t   txPower = 17;

static bool     radioReady = false;
static bool     sdReady = false;
static bool     transmitting = false;
static uint32_t txStart, txTimeoutMs;
static uint32_t pingCounter = 0;
static char     txPayload[24];

// ---- Screen layout (320x480 portrait) ----
enum Button {
  PING, BW6, BW7, SF10, SF12, F433, F868, TX10, TX17,  // main screen, 3x3 grid
  LOG,                                                 // main screen, below the packet
  VIEW_BACK, VIEW_OLDER, VIEW_NEWER,                   // log viewer
  NUM_BUTTONS
};
static const char* labels[NUM_BUTTONS] = {"ping", "BW6", "BW7", "SF10", "SF12", "433", "868", "Tx10", "Tx17",
                                          "Log", "Back", "Older", "Newer"};

#define BTN_W      90
#define BTN_H      70
#define BTN_X0     10
#define BTN_Y0     50
#define BTN_GAP    10
#define PACKET_Y   (BTN_Y0 + 3 * (BTN_H + BTN_GAP))
#define MESSAGE_Y  445
#define LOG_BTN_Y  (MESSAGE_Y - 45)
#define LOG_BTN_H  40
#define TEXT_CHARS 25   // characters per line at text size 2

// Log viewer: a list at text size 1, buttons along the bottom
#define VIEW_BTN_Y  430
#define VIEW_BTN_W  100
#define VIEW_BTN_H  45
#define VIEW_LIST_Y 30
#define VIEW_LINE_H 10
#define VIEW_LINES  ((VIEW_BTN_Y - 5 - VIEW_LIST_Y) / VIEW_LINE_H)
#define VIEW_CHARS  53  // characters per line at text size 1

static bool viewing = false;  // the log viewer is on screen

// Last message and packet, to redraw the main screen after the viewer
static char  lastMessage[48] = "";
static char  lastPacket[256] = "";
static float lastRssi, lastSnr;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static const uint16_t BG_COLOR = rgb(255, 64, 64);
static const uint16_t PRESSED_COLOR = rgb(0, 191, 191);
static const uint16_t CHECK_COLOR = rgb(0, 0, 255);
static const uint16_t PRESSED_TEXT = rgb(33, 33, 33);
static const uint16_t PACKET_TEXT = rgb(255, 222, 222);

// One decimal, without printf("%f"), which this newlib build may not support
static const char* fmt1(float v, char* buf, size_t len) {
  int t = (int)lroundf(v * 10);
  snprintf(buf, len, "%s%d.%d", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
  return buf;
}

static bool isChecked(uint8_t b) {
  switch (b) {
    case BW6:  return bwHz == 62500;
    case BW7:  return bwHz == 125000;
    case SF10: return sf == 10;
    case SF12: return sf == 12;
    case F433: return freqKHz == 433000;
    case F868: return freqKHz == 868000;
    case TX10: return txPower == 10;
    case TX17: return txPower == 17;
    default:   return false;
  }
}

static void buttonRect(uint8_t b, int16_t& x, int16_t& y, int16_t& w, int16_t& h) {
  if (b < LOG) {
    x = BTN_X0 + (b % 3) * (BTN_W + BTN_GAP);
    y = BTN_Y0 + (b / 3) * (BTN_H + BTN_GAP);
    w = BTN_W;
    h = BTN_H;
  } else if (b == LOG) {
    x = BTN_X0 + 2 * (BTN_W + BTN_GAP);
    y = LOG_BTN_Y;
    w = BTN_W;
    h = LOG_BTN_H;
  } else {
    x = 5 + (b - VIEW_BACK) * (VIEW_BTN_W + 5);
    y = VIEW_BTN_Y;
    w = VIEW_BTN_W;
    h = VIEW_BTN_H;
  }
}

static bool buttonVisible(uint8_t b) {
  return viewing ? b >= VIEW_BACK : b <= LOG;
}

static int8_t buttonAt(int16_t px, int16_t py) {
  for (uint8_t b = 0; b < NUM_BUTTONS; b++) {
    if (!buttonVisible(b)) continue;
    int16_t x, y, w, h;
    buttonRect(b, x, y, w, h);
    if (px >= x && px < x + w && py >= y && py < y + h) return b;
  }
  return -1;
}

static void drawButton(uint8_t b, bool pressed) {
  int16_t x, y, w, h;
  buttonRect(b, x, y, w, h);
  lcd.fillRect(x, y, w, h, pressed ? PRESSED_COLOR : BG_COLOR);
  for (uint8_t i = 0; i < 3; i++) lcd.drawRect(x + i, y + i, w - 2 * i, h - 2 * i, COLOR_BLACK);
  if (isChecked(b))
    for (uint8_t i = 3; i < 6; i++) lcd.drawRect(x + i, y + i, w - 2 * i, h - 2 * i, CHECK_COLOR);
  int16_t len = strlen(labels[b]);
  lcd.setTextSize(3);
  lcd.setTextColor(pressed ? PRESSED_TEXT : COLOR_WHITE, pressed ? PRESSED_COLOR : BG_COLOR);
  lcd.setCursor(x + (w - len * 18 + 3) / 2, y + (h - 21) / 2);
  lcd.print(labels[b]);
}

static void drawCentered(int16_t y, int16_t h, const char* text, uint16_t color) {
  lcd.fillRect(0, y, lcd.width(), h, BG_COLOR);
  lcd.setTextSize(2);
  lcd.setTextColor(color, BG_COLOR);
  lcd.setCursor((lcd.width() - (int16_t)strlen(text) * 12) / 2, y + (h - 14) / 2);
  lcd.print(text);
}

static void drawSettings() {
  char line[48], bw[16];
  snprintf(line, sizeof(line), "%u.%uMHz SF%u BW%s %ddBm", (unsigned)(freqKHz / 1000), (unsigned)(freqKHz % 1000) / 100,
           sf, fmt1(bwHz / 1000.0f, bw, sizeof(bw)), txPower);
  // drop a trailing ".0" from the bandwidth to keep the line short
  char* dot = strstr(line, ".0 ");
  if (dot && dot > strstr(line, "BW")) memmove(dot, dot + 2, strlen(dot + 2) + 1);
  drawCentered(0, BTN_Y0 - 5, line, COLOR_WHITE);
}

static void drawMessage() {
  drawCentered(MESSAGE_Y, lcd.height() - MESSAGE_Y, lastMessage, COLOR_BLACK);
}

// Messages and packets that arrive while the log viewer is open are drawn
// when it closes
static void showMessage(const char* text) {
  Serial.println(text);
  strncpy(lastMessage, text, sizeof(lastMessage) - 1);
  if (!viewing) drawMessage();
}

static void drawPacket() {
  lcd.fillRect(0, PACKET_Y, lcd.width(), LOG_BTN_Y - 4 - PACKET_Y, BG_COLOR);
  if (!lastPacket[0]) return;
  lcd.setTextSize(2);
  lcd.setTextColor(PACKET_TEXT, BG_COLOR);
  int16_t y = PACKET_Y + 4;
  size_t len = strlen(lastPacket);
  for (size_t i = 0; i < len && y < LOG_BTN_Y - 4 - 44; i += TEXT_CHARS, y += 22) {
    char line[TEXT_CHARS + 1];
    strncpy(line, lastPacket + i, TEXT_CHARS);
    line[TEXT_CHARS] = 0;
    lcd.setCursor(6, y);
    lcd.print(line);
  }
  char line[48], r[16], s[16];
  snprintf(line, sizeof(line), "RSSI: %s SNR: %s", fmt1(lastRssi, r, sizeof(r)), fmt1(lastSnr, s, sizeof(s)));
  lcd.setCursor(6, y);
  lcd.print(line);
}

static void showPacket(const char* text, float rssi, float snr) {
  strncpy(lastPacket, text, sizeof(lastPacket) - 1);
  lastRssi = rssi;
  lastSnr = snr;
  if (!viewing) drawPacket();
}

static void drawScreen() {
  lcd.fillScreen(BG_COLOR);
  drawSettings();
  for (uint8_t b = 0; b <= LOG; b++) drawButton(b, false);
  drawPacket();
  drawMessage();
}

// ---- SD log ----
// One CSV line per event, with the columns of LOG_HEADER. rssi and snr only
// for received packets, lat and lng only with a recent GPS fix. Times are
// since boot (no clock). The file is opened and closed for each line, so a
// power cut loses at most the line being written.

static char logFile[13] = "LORALOG.CSV";

static void readLine(File& f, char* buf, size_t len) {
  size_t i = 0;
  int c;
  while ((c = f.read()) >= 0 && c != '\n')
    if (i + 1 < len) buf[i++] = c;
  buf[i] = 0;
}

// A log written with other columns is left alone: entries go to the first of
// LORALOG.CSV, LORALOG1.CSV ... LORALOG9.CSV that is new or has these columns.
// SPI1 must be routed to the card.
static bool chooseLogFile() {
  for (uint8_t i = 0; i < 10; i++) {
    if (i) snprintf(logFile, sizeof(logFile), "LORALOG%u.CSV", i);
    File f = SD.open(logFile);
    if (!f) return true;
    char header[96];
    readLine(f, header, sizeof(header));
    bool empty = f.size() == 0;
    f.close();
    if (empty || !strcmp(header, LOG_HEADER)) return true;
  }
  return false;
}

static bool gpsFix() {
  return gps.location.isValid() && gps.location.age() < GPS_MAX_AGE;
}

static void writeCsvField(File& f, const char* text) {
  f.write('"');
  for (; *text; text++) {
    if (*text == '"') f.write('"');
    f.write(*text);
  }
  f.write('"');
}

static void logEvent(const char* event, const char* payload = "", const float* rssi = nullptr,
                     const float* snr = nullptr) {
  if (!sdReady) return;
  // The SD card and the radio share SPI1: route it to the card, then back
  amigoSdSelect();
  File f = SD.open(logFile, FILE_WRITE);
  if (f) {
    if (f.size() == 0) f.print(LOG_HEADER "\n");
    char bw[16], r[16] = "", n[16] = "", lat[16] = "", lng[16] = "";
    if (rssi) fmt1(*rssi, r, sizeof(r));
    if (snr) fmt1(*snr, n, sizeof(n));
    if (gpsFix()) {
      gpsFormat(gps.location.lat(), 6, lat, sizeof(lat));
      gpsFormat(gps.location.lng(), 6, lng, sizeof(lng));
    }
    f.printf("%u,%s,%u.%03u,%s,%u,%d,%s,%s,%s,%s,", (unsigned)millis(), event, (unsigned)(freqKHz / 1000),
             (unsigned)(freqKHz % 1000), fmt1(bwHz / 1000.0f, bw, sizeof(bw)), sf, txPower, r, n, lat, lng);
    writeCsvField(f, payload);
    f.write('\n');
    f.close();
  } else {
    sdReady = false;
  }
  loraSPI.begin();
  if (!sdReady) showMessage("SD log failed, stopped");
}

// ---- Log viewer ----
static uint32_t* logLines = nullptr;  // file offset of each entry
static uint32_t  logCount = 0, logCapacity = 0;
static uint32_t  viewTop = 0;         // first entry on screen

// Records where each entry starts. Every line but the header follows a
// newline.
static void indexLog() {
  logCount = 0;
  amigoSdSelect();
  File f = SD.open(logFile);
  if (f) {
    uint32_t size = f.size(), pos = 0;
    uint8_t buf[512];
    int n;
    while ((n = f.read(buf, (uint16_t)sizeof(buf))) > 0) {
      for (int i = 0; i < n; i++) {
        if (buf[i] != '\n' || pos + i + 1 >= size) continue;
        if (logCount == logCapacity) {
          uint32_t capacity = logCapacity ? 2 * logCapacity : 1024;
          uint32_t* grown = (uint32_t*)realloc(logLines, capacity * sizeof(uint32_t));
          if (!grown) break;
          logLines = grown;
          logCapacity = capacity;
        }
        logLines[logCount++] = pos + i + 1;
      }
      pos += n;
    }
    f.close();
  }
  loraSPI.begin();
}

// One log line (LOG_HEADER's columns) as one screen line, the position last
// when there is one. Returns the color for its event.
enum { F_MS, F_EVENT, F_FREQ, F_BW, F_SF, F_DBM, F_RSSI, F_SNR, F_LAT, F_LNG, F_PAYLOAD, NUM_FIELDS };

static uint16_t formatEntry(char* line, char* out, size_t len) {
  char* field[NUM_FIELDS];
  uint8_t n = 0;
  field[n++] = line;
  for (char* p = line; *p && n < NUM_FIELDS; p++)
    if (*p == ',') {
      *p = 0;
      field[n++] = p + 1;
    }
  if (n < NUM_FIELDS) {
    snprintf(out, len, "?");
    return COLOR_YELLOW;
  }
  // Unquote the payload ("" stands for ")
  char *src = field[F_PAYLOAD], *dst = field[F_PAYLOAD];
  if (*src == '"') {
    for (src++; *src; ) {
      if (*src == '"' && src[1] != '"') break;
      if (*src == '"') src++;
      *dst++ = *src++;
    }
    *dst = 0;
  }
  uint32_t ms = strtoul(field[F_MS], nullptr, 10);
  char t[16];
  snprintf(t, sizeof(t), "%u.%u", (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 100));
  const char* event = field[F_EVENT];
  uint16_t color = PACKET_TEXT;
  if (!strcmp(event, "rx")) {
    snprintf(out, len, "%7s %-10s %s/%s %s", t, event, field[F_RSSI], field[F_SNR], field[F_PAYLOAD]);
    color = COLOR_WHITE;
  } else if (!strcmp(event, "crc_error")) {
    snprintf(out, len, "%7s %-10s %s/%s", t, event, field[F_RSSI], field[F_SNR]);
    color = COLOR_YELLOW;
  } else if (!strcmp(event, "tx") || !strcmp(event, "tx_timeout")) {
    snprintf(out, len, "%7s %-10s %s", t, event, field[F_PAYLOAD]);
    if (strcmp(event, "tx")) color = COLOR_YELLOW;
  } else {
    snprintf(out, len, "%7s %-10s %sMHz BW%s SF%s %sdBm", t, event, field[F_FREQ], field[F_BW], field[F_SF],
             field[F_DBM]);
  }
  size_t used = strlen(out);
  if (*field[F_LAT] && used < len - 1)
    snprintf(out + used, len - used, " @%s,%s", field[F_LAT], field[F_LNG]);
  return color;
}

static void drawLogPage() {
  char header[48];
  if (logCount)
    snprintf(header, sizeof(header), "Log %u-%u of %u", (unsigned)viewTop + 1,
             (unsigned)min(viewTop + VIEW_LINES, logCount), (unsigned)logCount);
  else
    snprintf(header, sizeof(header), "Log is empty");
  drawCentered(0, VIEW_LIST_Y - 5, header, COLOR_WHITE);
  lcd.fillRect(0, VIEW_LIST_Y, lcd.width(), VIEW_LINES * VIEW_LINE_H, BG_COLOR);
  if (!logCount) return;

  amigoSdSelect();
  File f = SD.open(logFile);
  if (f) {
    lcd.startWrite();  // the whole page in one transfer
    lcd.setTextSize(1);
    for (uint32_t i = viewTop; i < logCount && i < viewTop + VIEW_LINES; i++) {
      char line[300], out[VIEW_CHARS + 1];
      f.seek(logLines[i]);
      readLine(f, line, sizeof(line));
      uint16_t color = formatEntry(line, out, sizeof(out));
      lcd.setTextColor(color, BG_COLOR);
      lcd.setCursor(2, VIEW_LIST_Y + (i - viewTop) * VIEW_LINE_H);
      lcd.print(out);
    }
    lcd.endWrite();
    f.close();
  }
  loraSPI.begin();
}

static void showLogEnd() {
  indexLog();
  viewTop = logCount > VIEW_LINES ? logCount - VIEW_LINES : 0;
  drawLogPage();
}

static void openViewer() {
  if (!sdReady) {
    showMessage("No SD card");
    return;
  }
  viewing = true;
  lcd.fillScreen(BG_COLOR);
  for (uint8_t b = VIEW_BACK; b < NUM_BUTTONS; b++) drawButton(b, false);
  showLogEnd();
}

static void closeViewer() {
  viewing = false;
  drawScreen();
}

static void pageOlder() {
  if (viewTop == 0) return;
  viewTop = viewTop > VIEW_LINES ? viewTop - VIEW_LINES : 0;
  drawLogPage();
}

// On the last page, picks up the entries logged since
static void pageNewer() {
  if (viewTop + VIEW_LINES >= logCount) {
    showLogEnd();
    return;
  }
  viewTop = min(viewTop + VIEW_LINES, logCount - VIEW_LINES);
  drawLogPage();
}

// ---- Radio ----
static bool checkRadio(int16_t state, const char* what) {
  if (state == RADIOLIB_ERR_NONE) return true;
  char msg[40];
  snprintf(msg, sizeof(msg), "%s failed: %d", what, state);
  showMessage(msg);
  return false;
}

static void startReceive() {
  checkRadio(radio.startReceive(), "startReceive");
}

// Reads the version register (0x42, 0x12 on an SX1276) without RadioLib, so
// a wiring problem shows up as a clear value on Serial.
static uint8_t probeVersion() {
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
  loraSPI.begin();
  loraSPI.beginTransaction(SPISettings(LORA_SPI_FREQ, SPI_MSBFIRST, SPI_MODE0));
  digitalWrite(LORA_CS, LOW);
  loraSPI.transfer(0x42);
  uint8_t version = loraSPI.transfer(0x00);
  digitalWrite(LORA_CS, HIGH);
  loraSPI.endTransaction();
  return version;
}

static bool initRadio() {
  Serial.printf("SX1276 version register: 0x%02X (expect 0x12)\n", probeVersion());

  int16_t state = radio.begin(freqKHz / 1000.0f, bwHz / 1000.0f, sf, LORA_CR, LORA_SYNC_WORD,
                              txPower, LORA_PREAMBLE, 0);
  if (!checkRadio(state, "radio.begin")) return false;
  // sx127x.py sends without CRC
  if (!checkRadio(radio.setCRC(false), "setCRC")) return false;
  startReceive();
  return true;
}

static void applySettings() {
  drawSettings();
  if (!radioReady) return;
  if (!checkRadio(radio.setFrequency(freqKHz / 1000.0f), "setFrequency")) return;
  if (!checkRadio(radio.setBandwidth(bwHz / 1000.0f), "setBandwidth")) return;
  if (!checkRadio(radio.setSpreadingFactor(sf), "setSpreadingFactor")) return;
  if (!checkRadio(radio.setOutputPower(txPower), "setOutputPower")) return;
  startReceive();
  showMessage("Settings applied");
  logEvent("settings");
}

static void sendPing() {
  if (!radioReady) return;
  snprintf(txPayload, sizeof(txPayload), "PING #%u", (unsigned)pingCounter++);
  size_t len = strlen(txPayload);
  if (!checkRadio(radio.startTransmit((uint8_t*)txPayload, len), "startTransmit")) {
    startReceive();
    return;
  }
  transmitting = true;
  txStart = millis();
  txTimeoutMs = 2 * radio.getTimeOnAir(len) / 1000 + 500;
  char msg[40];
  snprintf(msg, sizeof(msg), "Sending %s", txPayload);
  showMessage(msg);
}

static void pollRadio() {
  if (!radioReady) return;
  uint16_t flags = radio.getIRQFlags();

  if (transmitting) {
    char msg[40];
    if (flags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE) {
      radio.finishTransmit();
      snprintf(msg, sizeof(msg), "Sent %s", txPayload);
    } else if (millis() - txStart > txTimeoutMs) {
      radio.finishTransmit();
      snprintf(msg, sizeof(msg), "TX timeout: %s", txPayload);
    } else {
      return;
    }
    transmitting = false;
    showMessage(msg);
    startReceive();
    logEvent(flags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE ? "tx" : "tx_timeout", txPayload);
    return;
  }

  if (flags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE) {
    uint8_t buf[256];
    size_t len = radio.getPacketLength();
    int16_t state = radio.readData(buf, len);
    float rssi = radio.getRSSI();
    float snr = radio.getSNR();
    if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      showMessage("CRC error");
      logEvent("crc_error", "", &rssi, &snr);
      return;
    }
    if (!checkRadio(state, "readData")) return;
    for (size_t i = 0; i < len; i++)
      if (buf[i] < 0x20 || buf[i] > 0x7E) buf[i] = '.';
    buf[len] = 0;
    char r[16], s[16];
    Serial.printf("*** Received: %s RSSI %s SNR %s\n", (char*)buf, fmt1(rssi, r, sizeof(r)), fmt1(snr, s, sizeof(s)));
    showPacket((char*)buf, rssi, snr);
    showMessage("Incoming!");
    logEvent("rx", (char*)buf, &rssi, &snr);
  }
}

static void runButton(uint8_t b) {
  switch (b) {
    case LOG:        openViewer(); return;
    case VIEW_BACK:  closeViewer(); return;
    case VIEW_OLDER: pageOlder(); return;
    case VIEW_NEWER: pageNewer(); return;
  }
  if (transmitting) {
    showMessage("Busy transmitting");
    return;
  }
  bool wasChecked[NUM_BUTTONS];
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) wasChecked[i] = isChecked(i);
  switch (b) {
    case PING: sendPing(); return;
    case BW6:  bwHz = 62500; break;
    case BW7:  bwHz = 125000; break;
    case SF10: sf = 10; break;
    case SF12: sf = 12; break;
    case F433: freqKHz = 433000; break;
    case F868: freqKHz = 868000; break;
    case TX10: txPower = 10; break;
    case TX17: txPower = 17; break;
    default:   return;
  }
  for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    if (isChecked(i) != wasChecked[i]) drawButton(i, false);
  applySettings();
}

// Feeds the GPS and reports changes on the status line. "No data" only shows
// if a GPS was seen, then lost: without one, the tester just logs no position.
static void pollGps() {
  enum { NO_DATA, SEARCHING, FIX };
  static const char* const states[] = {"GPS: no data", "GPS: searching", "GPS: fix"};
  static uint8_t shown = NO_DATA;
  gps.update();
  uint8_t state = gps.silent() ? NO_DATA : gpsFix() ? FIX : SEARCHING;
  if (state == shown) return;
  shown = state;
  showMessage(states[state]);
}

// Highlights the button under the finger, and runs it on release
static void pollTouch() {
  static int8_t pressed = -1;
  static bool wasTouched = false;
  TouchPoint p;
  bool touched = touch.getPoints(&p, 1) > 0;
  if (touched) {
    int8_t b = buttonAt(p.x, p.y);
    if (b != pressed) {
      if (pressed >= 0) drawButton(pressed, false);
      if (b >= 0) drawButton(b, true);
      pressed = b;
    }
  } else if (wasTouched && pressed >= 0) {
    int8_t b = pressed;
    pressed = -1;
    drawButton(b, false);
    runButton(b);
  }
  wasTouched = touched;
}

void setup() {
  Serial.begin(115200);
  lcd.begin();
  lcd.setRotation(SCREEN_ROTATION);
  if (!touch.begin()) Serial.println("FT6X36 not found");
  touch.setRotation(SCREEN_ROTATION);
  drawScreen();
  // Before the radio: mounting routes SPI1 to the card, initRadio() back
  sdReady = amigoSdBegin() && chooseLogFile();
  if (sdReady)
    Serial.printf("SD card mounted, logging to %s\n", logFile);
  else
    Serial.println("No SD card (or no free log file name), not logging");
  gps.begin();
  radioReady = initRadio();
  if (radioReady) showMessage(sdReady ? "Radio ready, logging to SD" : "Radio ready, no SD log");
  logEvent("boot");
}

void loop() {
  pollTouch();
  pollRadio();
  pollGps();
  delay(10);
}
