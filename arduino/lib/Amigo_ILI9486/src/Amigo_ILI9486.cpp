#include "Amigo_ILI9486.h"
#include "fpioa.h"
#include "sysctl.h"
#include "gpiohs.h"
#include "dmac.h"
// Explicit path: on a case-insensitive filesystem "spi.h" finds Arduino's SPI.h
#include "kendryte-standalone-sdk/lib/drivers/include/spi.h"

// The bus layer below is configured like MaixPy-v1's st7789.c (octal mode).
// The core's tft_* helpers drive RST/DC through regular GPIO and leave the
// display blank on the Amigo.
//
// The SDK's spi_send_data_normal_dma() moves data in bursts of 4 words, and a
// transfer shorter than that (a command byte, a single pixel) goes out with
// stray words after it: wrong pixels and bogus commands. Commands and their
// parameters go through the FIFO directly instead (busFifo).
//
// Drawing goes to a framebuffer, and the changed rows are sent whole, in one
// DMA transfer per drawing call (MaixPy likewise sends whole frames).

#define LCD_SPI  SPI_DEVICE_0
#define LCD_SS   AMIGO_LCD_SS
#define LCD_DMA  ((dmac_channel_number_t)AMIGO_LCD_DMA_CH)
#define LCD_FIFO_DEPTH 32

#define CMD_SOFTWARE_RESET   0x01
#define CMD_SLEEP_OFF        0x11
#define CMD_NORMAL_DISP_ON   0x13
#define CMD_INVERSION_OFF    0x20
#define CMD_INVERSION_ON     0x21
#define CMD_DISPLAY_ON       0x29
#define CMD_COLUMN_ADDR_SET  0x2A
#define CMD_PAGE_ADDR_SET    0x2B
#define CMD_MEMORY_WRITE     0x2C
#define CMD_MEMORY_ACCESS    0x36
#define CMD_PIXEL_FORMAT     0x3A
#define CMD_POWER_SETTING    0xD0
#define CMD_VCOM_CONTROL     0xD1

// The panel is driven as MaixPy drives it on the Amigo (lcd.rotation(1) +
// lcd.mirror(1), the setting its LoRa tester uses): portrait, MADCTL 0x48
// (MX + BGR). Rotations are applied in software (see toPanel).
#define PANEL_MADCTL 0x48
#define PANEL_WIDTH  AMIGO_LCD_HEIGHT  // 320
#define PANEL_HEIGHT AMIGO_LCD_WIDTH   // 480

static void busConfig(bool data, uint8_t bits)
{
    gpiohs_set_pin(AMIGO_LCD_DCX_GPIONUM, data ? GPIO_PV_HIGH : GPIO_PV_LOW);
    spi_init(LCD_SPI, SPI_WORK_MODE_0, SPI_FF_OCTAL, bits, 0);
    if (data)
        spi_init_non_standard(LCD_SPI, 0, bits, 0, SPI_AITM_AS_FRAME_FORMAT);
    else
        spi_init_non_standard(LCD_SPI, 8, 0, 0, SPI_AITM_AS_FRAME_FORMAT);
}

// One DMA transfer of len 32-bit FIFO words. It only works in bursts of 4
// words, so len must be a multiple of 4.
static void busDma(const uint32_t* words, uint32_t len)
{
    volatile spi_t* s = spi[LCD_SPI];
    s->ctrlr0 = (s->ctrlr0 & ~(3U << 8)) | (SPI_TMOD_TRANS << 8);  // transmit only
    s->dmacr = 0x2;  // DMA transmit
    s->ssienr = 1;
    sysctl_dma_select((sysctl_dma_channel_t)LCD_DMA, SYSCTL_DMA_SELECT_SSI0_TX_REQ);
    dmac_set_single_mode(LCD_DMA, words, (void*)&s->dr[0], DMAC_ADDR_INCREMENT, DMAC_ADDR_NOCHANGE,
                         DMAC_MSIZE_4, DMAC_TRANS_WIDTH_32, len);
    s->ser = 1U << LCD_SS;
    dmac_wait_done(LCD_DMA);
    while ((s->sr & 0x05) != 0x04) {}  // TX FIFO empty, not busy
    s->ser = 0;
    s->ssienr = 0;
}

// Sends up to LCD_FIFO_DEPTH words from the CPU. The FIFO is filled before
// the transfer starts, so it cannot run dry halfway. (Feeding a longer
// transfer from the CPU does not keep up with the bus.)
static void busFifo(const uint32_t* words, uint32_t len)
{
    volatile spi_t* s = spi[LCD_SPI];
    s->ctrlr0 = (s->ctrlr0 & ~(3U << 8)) | (SPI_TMOD_TRANS << 8);  // transmit only
    s->dmacr = 0;
    s->ssienr = 1;
    for (uint32_t i = 0; i < len; i++) s->dr[0] = words[i];
    s->ser = 1U << LCD_SS;
    while ((s->sr & 0x05) != 0x04) {}  // TX FIFO empty, not busy
    s->ser = 0;
    s->ssienr = 0;
}

static void busCommand(uint8_t cmd)
{
    uint32_t word = cmd;
    busConfig(false, 8);
    busFifo(&word, 1);
}

// Command parameters, up to LCD_FIFO_DEPTH bytes
static void busBytes(const uint8_t* data, uint32_t len)
{
    uint32_t words[LCD_FIFO_DEPTH];
    for (uint32_t i = 0; i < len; i++) words[i] = data[i];
    busConfig(true, 8);
    busFifo(words, len);
}

// Framebuffer in panel layout: PANEL_HEIGHT rows of PANEL_WIDTH pixels, two
// pixels per 32-bit word, the left one in the high half (sent first), as
// MaixPy's mcu_lcd_draw_picture() packs them. The CPU and the DMA both use
// its uncached alias (0x8xxxxxxx -> 0x4xxxxxxx), so the DMA, which reads RAM
// directly, never sees data still sitting in the CPU's cache.
#define FB_WORDS_PER_ROW (PANEL_WIDTH / 2)
static uint32_t fbMem[PANEL_HEIGHT * FB_WORDS_PER_ROW];
static uint32_t* const fb = (uint32_t*)((uintptr_t)fbMem - 0x40000000);

static inline void fbSet(uint16_t px, uint16_t py, uint16_t color)
{
    uint32_t* w = &fb[py * FB_WORDS_PER_ROW + px / 2];
    if (px & 1) *w = (*w & 0xFFFF0000) | color;
    else        *w = (*w & 0x0000FFFF) | ((uint32_t)color << 16);
}

// Fills panel pixels px1..px2 of row py
static void fbRow(uint16_t py, uint16_t px1, uint16_t px2, uint16_t color)
{
    if (px1 & 1) fbSet(px1++, py, color);
    if (px1 > px2) return;
    if (!(px2 & 1)) fbSet(px2--, py, color);
    if (px1 > px2) return;
    uint32_t word = ((uint32_t)color << 16) | color;
    uint32_t* w = &fb[py * FB_WORDS_PER_ROW + px1 / 2];
    for (uint16_t n = (px2 - px1 + 1) / 2; n; n--) *w++ = word;
}

Amigo_ILI9486::Amigo_ILI9486(void)
: Adafruit_GFX(AMIGO_LCD_WIDTH, AMIGO_LCD_HEIGHT),
  _dirtyMin(PANEL_HEIGHT), _dirtyMax(0), _writeDepth(0)
{
}

boolean Amigo_ILI9486::begin(uint32_t freq, uint16_t color)
{
    fpioa_set_function(AMIGO_LCD_RST_PIN, (fpioa_function_t)(FUNC_GPIOHS0 + AMIGO_LCD_RST_GPIONUM));
    fpioa_set_function(AMIGO_LCD_DCX_PIN, (fpioa_function_t)(FUNC_GPIOHS0 + AMIGO_LCD_DCX_GPIONUM));
    fpioa_set_function(AMIGO_LCD_CS_PIN, (fpioa_function_t)(FUNC_SPI0_SS0 + AMIGO_LCD_SS));
    fpioa_set_function(AMIGO_LCD_WR_PIN, FUNC_SPI0_SCLK);
    sysctl_set_spi0_dvp_data(1);
    // The Maixduino core never calls dmac_init()
    dmac_init();

    gpiohs_set_drive_mode(AMIGO_LCD_DCX_GPIONUM, GPIO_DM_OUTPUT);
    gpiohs_set_pin(AMIGO_LCD_DCX_GPIONUM, GPIO_PV_HIGH);
    gpiohs_set_drive_mode(AMIGO_LCD_RST_GPIONUM, GPIO_DM_OUTPUT);
    gpiohs_set_pin(AMIGO_LCD_RST_GPIONUM, GPIO_PV_LOW);
    spi_init(LCD_SPI, SPI_WORK_MODE_0, SPI_FF_OCTAL, 8, 0);
    spi_set_clk_rate(LCD_SPI, freq);
    delay(50);
    gpiohs_set_pin(AMIGO_LCD_RST_GPIONUM, GPIO_PV_HIGH);
    delay(50);

    static const uint8_t madctl = PANEL_MADCTL;
    initSequence();
    writeCommand(CMD_MEMORY_ACCESS, &madctl, 1);
    setRotation(0);
    invertDisplay(false);
    writeCommand(CMD_NORMAL_DISP_ON);
    fillScreen(color);
    writeCommand(CMD_DISPLAY_ON);
    return true;
}

uint32_t Amigo_ILI9486::setFrequency(uint32_t freq)
{
    return spi_set_clk_rate(LCD_SPI, freq);
}

void Amigo_ILI9486::writeCommand(uint8_t cmd, const uint8_t* data, uint8_t len)
{
    busCommand(cmd);
    if (len) busBytes(data, len);
}

// Same sequence as MaixPy-v1 on the Amigo IPS (its "amigo_ips" config:
// lcd_type ILI9481, invert 1): mcu_lcd_init with the ILI9481 pre-init (D0
// power setting, D1 VCOM). The ILI9486 register set (F1/F2/F8/F9, B4, C1,
// C5, gamma) used for the other Amigo panel leaves thin lines broken on this
// one: on an ILI9481, C1 and C5 are display timing registers.
void Amigo_ILI9486::initSequence(void)
{
    static const uint8_t power[] = {0x07, 0x42, 0x1B};
    static const uint8_t vcom[] = {0x00, 0x05, 0x0C};
    static const uint8_t pixfmt = 0x55;

    writeCommand(CMD_SOFTWARE_RESET);
    delay(50);
    writeCommand(CMD_POWER_SETTING, power, sizeof(power));
    writeCommand(CMD_VCOM_CONTROL, vcom, sizeof(vcom));
    writeCommand(CMD_SLEEP_OFF);
    delay(120);
    writeCommand(CMD_PIXEL_FORMAT, &pixfmt, 1);
    delay(10);
}

// Panel coordinates (px, py) of a point (x, y) in the current rotation. The
// panel is portrait with the camera at the top, i.e. rotation 1:
//   0: (319 - y, x)           2: (y, 479 - x)
//   1: (x, y)                 3: (319 - x, 479 - y)
void Amigo_ILI9486::toPanel(int16_t x, int16_t y, uint16_t& px, uint16_t& py)
{
    switch (rotation) {
        case 0: px = PANEL_WIDTH - 1 - y; py = x; break;
        case 1: px = x; py = y; break;
        case 2: px = y; py = PANEL_HEIGHT - 1 - x; break;
        default: px = PANEL_WIDTH - 1 - x; py = PANEL_HEIGHT - 1 - y; break;
    }
}

void Amigo_ILI9486::markDirty(uint16_t py1, uint16_t py2)
{
    if (py1 < _dirtyMin) _dirtyMin = py1;
    if (py2 > _dirtyMax) _dirtyMax = py2;
}

// Sends the changed rows, whole: a full-width window, 160 words per row
void Amigo_ILI9486::flush(void)
{
    if (_dirtyMin > _dirtyMax) return;
    uint16_t py1 = _dirtyMin, py2 = _dirtyMax;
    _dirtyMin = PANEL_HEIGHT;
    _dirtyMax = 0;
    uint8_t data[4] = {0, 0, (PANEL_WIDTH - 1) >> 8, (PANEL_WIDTH - 1) & 0xFF};
    writeCommand(CMD_COLUMN_ADDR_SET, data, 4);
    data[0] = py1 >> 8; data[1] = py1; data[2] = py2 >> 8; data[3] = py2;
    writeCommand(CMD_PAGE_ADDR_SET, data, 4);
    busCommand(CMD_MEMORY_WRITE);
    busConfig(true, 32);
    busDma(&fb[py1 * FB_WORDS_PER_ROW], (uint32_t)(py2 - py1 + 1) * FB_WORDS_PER_ROW);
}

// Drawing between startWrite() and endWrite() is sent once, at the end
void Amigo_ILI9486::startWrite(void)
{
    _writeDepth++;
}

void Amigo_ILI9486::endWrite(void)
{
    if (_writeDepth && --_writeDepth == 0) flush();
}

void Amigo_ILI9486::writePixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    uint16_t px, py;
    toPanel(x, y, px, py);
    fbSet(px, py, color);
    markDirty(py, py);
}

void Amigo_ILI9486::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    startWrite();
    writePixel(x, y, color);
    endWrite();
}

void Amigo_ILI9486::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;

    uint16_t ax, ay, bx, by;
    toPanel(x, y, ax, ay);
    toPanel(x + w - 1, y + h - 1, bx, by);
    uint16_t px1 = min(ax, bx), px2 = max(ax, bx), py1 = min(ay, by), py2 = max(ay, by);
    for (uint16_t py = py1; py <= py2; py++) fbRow(py, px1, px2, color);
    markDirty(py1, py2);
}

void Amigo_ILI9486::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    writeFillRect(x, y, 1, h, color);
}

void Amigo_ILI9486::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    writeFillRect(x, y, w, 1, color);
}

void Amigo_ILI9486::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    fillRect(x, y, 1, h, color);
}

void Amigo_ILI9486::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    fillRect(x, y, w, 1, color);
}

void Amigo_ILI9486::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    startWrite();
    writeFillRect(x, y, w, h, color);
    endWrite();
}

void Amigo_ILI9486::fillScreen(uint16_t color)
{
    fillRect(0, 0, _width, _height, color);
}

void Amigo_ILI9486::setRotation(uint8_t r)
{
    rotation = r & 3;
    _width  = (rotation & 1) ? HEIGHT : WIDTH;
    _height = (rotation & 1) ? WIDTH : HEIGHT;
}

// The Amigo's panel shows inverted colors unless the controller's inversion
// is on, so "not inverted" means INVERSION_ON here.
void Amigo_ILI9486::invertDisplay(boolean invert)
{
    writeCommand(invert ? CMD_INVERSION_OFF : CMD_INVERSION_ON);
}

void Amigo_ILI9486::drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t* img)
{
    startWrite();
    for (uint16_t j = 0; j < h; j++)
        for (uint16_t i = 0; i < w; i++) writePixel(x + i, y + j, *img++);
    endWrite();
}

// Adafruit_GFX's classic 5x7 font (a static array, so this is our own copy)
#include "glcdfont.c"

#define MAX_FAST_TEXT_SIZE 8

// A whole string is sent once, at the end
size_t Amigo_ILI9486::write(const uint8_t* buffer, size_t size)
{
    startWrite();
    size_t n = 0;
    while (size--) n += write(*buffer++);
    endWrite();
    return n;
}

size_t Amigo_ILI9486::write(uint8_t c)
{
    // Adafruit_GFX draws each dot of a character as its own rectangle. For
    // opaque text in the classic font, draw the character cell pixel by
    // pixel instead.
    uint8_t s = textsize;
    if (gfxFont || textbgcolor == textcolor || c == '\n' || c == '\r' || s > MAX_FAST_TEXT_SIZE)
        return Adafruit_GFX::write(c);
    if (wrap && cursor_x + s * 6 > _width) {
        cursor_x = 0;
        cursor_y += s * 8;
    }
    int16_t w = 6 * s, h = 8 * s;
    if (cursor_x < 0 || cursor_y < 0 || cursor_x + w > _width || cursor_y + h > _height)
        return Adafruit_GFX::write(c);

    uint8_t glyph = c;
    if (!_cp437 && glyph >= 176) glyph++;  // same quirk as Adafruit_GFX, 255 wraps to 0
    startWrite();
    for (int16_t py = 0; py < h; py++) {
        uint8_t bit = 1 << (py / s);
        for (int16_t px = 0; px < w; px++) {
            uint8_t col = px / s;
            bool on = col < 5 && (font[glyph * 5 + col] & bit);
            writePixel(cursor_x + px, cursor_y + py, on ? textcolor : textbgcolor);
        }
    }
    endWrite();
    cursor_x += w;
    return 1;
}
