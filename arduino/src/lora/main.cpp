// LoRa tester for the SP-LoRa (SX1276) module, ported from LoRa_Tester.py
// (MaixPy + sx127x.py) to RadioLib. Same radio defaults and the same touch
// menu: ping, bandwidth, spreading factor, frequency, TX power.
// Incoming packets are shown with their RSSI and SNR. With an SD card in the
// TF slot, every event is also appended to LORALOG.CSV.
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Amigo_ILI9486.h>
#include <Amigo_FT6X36.h>
#include <Amigo_SD.h>

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

#define LOG_FILE "LORALOG.CSV"

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
enum Button { PING, BW6, BW7, SF10, SF12, F433, F868, TX10, TX17, NUM_BUTTONS };
static const char* labels[NUM_BUTTONS] = {"ping", "BW6", "BW7", "SF10", "SF12", "433", "868", "Tx10", "Tx17"};

#define BTN_W      90
#define BTN_H      70
#define BTN_X0     10
#define BTN_Y0     50
#define BTN_GAP    10
#define PACKET_Y   (BTN_Y0 + 3 * (BTN_H + BTN_GAP))
#define MESSAGE_Y  445
#define TEXT_CHARS 25   // characters per line at text size 2

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

static void buttonRect(uint8_t b, int16_t& x, int16_t& y) {
  x = BTN_X0 + (b % 3) * (BTN_W + BTN_GAP);
  y = BTN_Y0 + (b / 3) * (BTN_H + BTN_GAP);
}

static int8_t buttonAt(int16_t px, int16_t py) {
  for (uint8_t b = 0; b < NUM_BUTTONS; b++) {
    int16_t x, y;
    buttonRect(b, x, y);
    if (px >= x && px < x + BTN_W && py >= y && py < y + BTN_H) return b;
  }
  return -1;
}

static void drawButton(uint8_t b, bool pressed) {
  int16_t x, y;
  buttonRect(b, x, y);
  lcd.fillRect(x, y, BTN_W, BTN_H, pressed ? PRESSED_COLOR : BG_COLOR);
  for (uint8_t i = 0; i < 3; i++) lcd.drawRect(x + i, y + i, BTN_W - 2 * i, BTN_H - 2 * i, COLOR_BLACK);
  if (isChecked(b))
    for (uint8_t i = 3; i < 6; i++) lcd.drawRect(x + i, y + i, BTN_W - 2 * i, BTN_H - 2 * i, CHECK_COLOR);
  int16_t len = strlen(labels[b]);
  lcd.setTextSize(3);
  lcd.setTextColor(pressed ? PRESSED_TEXT : COLOR_WHITE, pressed ? PRESSED_COLOR : BG_COLOR);
  lcd.setCursor(x + (BTN_W - len * 18 + 3) / 2, y + (BTN_H - 21) / 2);
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

static void showMessage(const char* text) {
  Serial.println(text);
  drawCentered(MESSAGE_Y, lcd.height() - MESSAGE_Y, text, COLOR_BLACK);
}

static void showPacket(const char* text, float rssi, float snr) {
  lcd.fillRect(0, PACKET_Y, lcd.width(), MESSAGE_Y - PACKET_Y, BG_COLOR);
  lcd.setTextSize(2);
  lcd.setTextColor(PACKET_TEXT, BG_COLOR);
  int16_t y = PACKET_Y + 4;
  size_t len = strlen(text);
  for (size_t i = 0; i < len && y < MESSAGE_Y - 40; i += TEXT_CHARS, y += 22) {
    char line[TEXT_CHARS + 1];
    strncpy(line, text + i, TEXT_CHARS);
    line[TEXT_CHARS] = 0;
    lcd.setCursor(6, y);
    lcd.print(line);
  }
  char line[48], r[16], s[16];
  snprintf(line, sizeof(line), "RSSI: %s SNR: %s", fmt1(rssi, r, sizeof(r)), fmt1(snr, s, sizeof(s)));
  lcd.setCursor(6, y);
  lcd.print(line);
}

static void drawScreen() {
  lcd.fillScreen(BG_COLOR);
  drawSettings();
  for (uint8_t b = 0; b < NUM_BUTTONS; b++) drawButton(b, false);
}

// ---- SD log ----
// One CSV line per event:
//   ms,event,freq_mhz,bw_khz,sf,tx_dbm,rssi,snr,payload
// rssi and snr only for received packets. Times are since boot (no clock).
// The file is opened and closed for each line, so a power cut loses at most
// the line being written.

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
  File f = SD.open(LOG_FILE, FILE_WRITE);
  if (f) {
    if (f.size() == 0) f.print("ms,event,freq_mhz,bw_khz,sf,tx_dbm,rssi,snr,payload\n");
    char bw[16], r[16] = "", n[16] = "";
    if (rssi) fmt1(*rssi, r, sizeof(r));
    if (snr) fmt1(*snr, n, sizeof(n));
    f.printf("%u,%s,%u.%03u,%s,%u,%d,%s,%s,", (unsigned)millis(), event, (unsigned)(freqKHz / 1000),
             (unsigned)(freqKHz % 1000), fmt1(bwHz / 1000.0f, bw, sizeof(bw)), sf, txPower, r, n);
    writeCsvField(f, payload);
    f.write('\n');
    f.close();
  } else {
    sdReady = false;
  }
  loraSPI.begin();
  if (!sdReady) showMessage("SD log failed, stopped");
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
  }
  for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    if (isChecked(i) != wasChecked[i]) drawButton(i, false);
  applySettings();
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
  sdReady = amigoSdBegin();
  Serial.println(sdReady ? "SD card mounted, logging to " LOG_FILE : "No SD card, not logging");
  radioReady = initRadio();
  if (radioReady) showMessage(sdReady ? "Radio ready, logging to SD" : "Radio ready, no SD log");
  logEvent("boot");
}

void loop() {
  pollTouch();
  pollRadio();
  delay(10);
}
