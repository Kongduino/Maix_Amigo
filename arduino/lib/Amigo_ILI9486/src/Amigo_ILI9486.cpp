#include "Amigo_ILI9486.h"
#include "fpioa.h"
#include "sysctl.h"
#include "gpiohs.h"
#include "dmac.h"
// Explicit path: on a case-insensitive filesystem "spi.h" finds Arduino's SPI.h
#include "kendryte-standalone-sdk/lib/drivers/include/spi.h"

// The bus layer below follows MaixPy-v1's st7789.c (octal mode). The core's
// tft_* helpers drive RST/DC through regular GPIO and leave the display blank
// on the Amigo.

#define LCD_SPI  SPI_DEVICE_0
#define LCD_SS   ((spi_chip_select_t)AMIGO_LCD_SS)
#define LCD_DMA  ((dmac_channel_number_t)AMIGO_LCD_DMA_CH)

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
#define CMD_INVERSION_CTL    0xB4
#define CMD_POWER_CTL2       0xC1
#define CMD_VCOM_CTL1        0xC5
#define CMD_POS_GAMMA        0xE0
#define CMD_NEG_GAMMA        0xE1

// MADCTL values per rotation: MY 0x80, MX 0x40, MV 0x20, BGR 0x08.
// 0x28 is what MaixPy uses on the Amigo ("dir": 40 in config.json). The
// portrait values were checked on the panel: 0x88/0x48 come out mirrored.
static const uint8_t madctl[4] = {0x28, 0xC8, 0xE8, 0x08};

static void busCommand(uint8_t cmd)
{
    gpiohs_set_pin(AMIGO_LCD_DCX_GPIONUM, GPIO_PV_LOW);
    spi_init(LCD_SPI, SPI_WORK_MODE_0, SPI_FF_OCTAL, 8, 0);
    spi_init_non_standard(LCD_SPI, 8, 0, 0, SPI_AITM_AS_FRAME_FORMAT);
    spi_send_data_normal_dma(LCD_DMA, LCD_SPI, LCD_SS, &cmd, 1, SPI_TRANS_CHAR);
}

static void busBytes(const uint8_t* data, uint32_t len)
{
    gpiohs_set_pin(AMIGO_LCD_DCX_GPIONUM, GPIO_PV_HIGH);
    spi_init(LCD_SPI, SPI_WORK_MODE_0, SPI_FF_OCTAL, 8, 0);
    spi_init_non_standard(LCD_SPI, 0, 8, 0, SPI_AITM_AS_FRAME_FORMAT);
    spi_send_data_normal_dma(LCD_DMA, LCD_SPI, LCD_SS, data, len, SPI_TRANS_CHAR);
}

static void busHalfwords(const uint16_t* data, uint32_t len)
{
    gpiohs_set_pin(AMIGO_LCD_DCX_GPIONUM, GPIO_PV_HIGH);
    spi_init(LCD_SPI, SPI_WORK_MODE_0, SPI_FF_OCTAL, 16, 0);
    spi_init_non_standard(LCD_SPI, 0, 16, 0, SPI_AITM_AS_FRAME_FORMAT);
    spi_send_data_normal_dma(LCD_DMA, LCD_SPI, LCD_SS, data, len, SPI_TRANS_SHORT);
}

// Sends the same 32-bit word len times
static void busFill(const uint32_t* word, uint32_t len)
{
    gpiohs_set_pin(AMIGO_LCD_DCX_GPIONUM, GPIO_PV_HIGH);
    spi_init(LCD_SPI, SPI_WORK_MODE_0, SPI_FF_OCTAL, 32, 0);
    spi_init_non_standard(LCD_SPI, 0, 32, 0, SPI_AITM_AS_FRAME_FORMAT);
    spi_fill_data_dma(LCD_DMA, LCD_SPI, LCD_SS, word, len);
}

Amigo_ILI9486::Amigo_ILI9486(void)
: Adafruit_GFX(AMIGO_LCD_WIDTH, AMIGO_LCD_HEIGHT)
{
}

boolean Amigo_ILI9486::begin(uint32_t freq, uint16_t color)
{
    fpioa_set_function(AMIGO_LCD_RST_PIN, (fpioa_function_t)(FUNC_GPIOHS0 + AMIGO_LCD_RST_GPIONUM));
    fpioa_set_function(AMIGO_LCD_DCX_PIN, (fpioa_function_t)(FUNC_GPIOHS0 + AMIGO_LCD_DCX_GPIONUM));
    fpioa_set_function(AMIGO_LCD_CS_PIN, (fpioa_function_t)(FUNC_SPI0_SS0 + AMIGO_LCD_SS));
    fpioa_set_function(AMIGO_LCD_WR_PIN, FUNC_SPI0_SCLK);
    sysctl_set_spi0_dvp_data(1);
    // The Maixduino core never calls dmac_init(), and every transfer below
    // goes through DMA.
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

    initSequence();
    setRotation(0);
    invertDisplay(false);
    fillScreen(color);
    writeCommand(CMD_DISPLAY_ON);
    return true;
}

void Amigo_ILI9486::writeCommand(uint8_t cmd, const uint8_t* data, uint8_t len)
{
    busCommand(cmd);
    if (len) busBytes(data, len);
}

// Same sequence as MaixPy-v1's lcd_init_sequence_for_ili9486 + mcu_lcd_init
void Amigo_ILI9486::initSequence(void)
{
    static const uint8_t f1[] = {0x36, 0x04, 0x00, 0x3C, 0x0F, 0x8F};
    static const uint8_t f2[] = {0x18, 0xA3, 0x12, 0x02, 0xB2, 0x12, 0xFF, 0x10, 0x00};
    static const uint8_t f8[] = {0x21, 0x04};
    static const uint8_t f9[] = {0x00, 0x08};
    static const uint8_t vcom[] = {0x00, 0x18};
    static const uint8_t pgamma[] = {0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
                                     0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00};
    static const uint8_t ngamma[] = {0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
                                     0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00};
    static const uint8_t zero = 0x00, pwr2 = 0x41, pixfmt = 0x55;

    writeCommand(CMD_SOFTWARE_RESET);
    delay(50);
    writeCommand(0xF1, f1, sizeof(f1));
    writeCommand(0xF2, f2, sizeof(f2));
    writeCommand(0xF8, f8, sizeof(f8));
    writeCommand(0xF9, f9, sizeof(f9));
    writeCommand(CMD_INVERSION_CTL, &zero, 1);
    writeCommand(CMD_POWER_CTL2, &pwr2, 1);
    writeCommand(CMD_VCOM_CTL1, vcom, sizeof(vcom));
    writeCommand(CMD_POS_GAMMA, pgamma, sizeof(pgamma));
    writeCommand(CMD_NEG_GAMMA, ngamma, sizeof(ngamma));
    writeCommand(CMD_PIXEL_FORMAT, &pixfmt, 1);
    writeCommand(CMD_SLEEP_OFF);
    delay(120);
    writeCommand(CMD_PIXEL_FORMAT, &pixfmt, 1);
    delay(10);
    writeCommand(CMD_NORMAL_DISP_ON);
    delay(10);
}

void Amigo_ILI9486::setArea(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4];

    data[0] = x1 >> 8; data[1] = x1; data[2] = x2 >> 8; data[3] = x2;
    writeCommand(CMD_COLUMN_ADDR_SET, data, 4);
    data[0] = y1 >> 8; data[1] = y1; data[2] = y2 >> 8; data[3] = y2;
    writeCommand(CMD_PAGE_ADDR_SET, data, 4);
    busCommand(CMD_MEMORY_WRITE);
}

void Amigo_ILI9486::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    setArea(x, y, x, y);
    busHalfwords(&color, 1);
}

void Amigo_ILI9486::writePixel(int16_t x, int16_t y, uint16_t color)
{
    drawPixel(x, y, color);
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

    // busFill sends 32-bit frames, i.e. two pixels at a time. For an odd
    // pixel count the extra pixel wraps to the window start, which already
    // has the same color.
    uint32_t data = ((uint32_t)color << 16) | color;
    setArea(x, y, x + w - 1, y + h - 1);
    busFill(&data, ((uint32_t)w * h + 1) / 2);
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
    writeFillRect(x, y, 1, h, color);
}

void Amigo_ILI9486::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    writeFillRect(x, y, w, 1, color);
}

void Amigo_ILI9486::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    writeFillRect(x, y, w, h, color);
}

void Amigo_ILI9486::fillScreen(uint16_t color)
{
    writeFillRect(0, 0, _width, _height, color);
}

void Amigo_ILI9486::setRotation(uint8_t r)
{
    rotation = r & 3;
    _width  = (rotation & 1) ? HEIGHT : WIDTH;
    _height = (rotation & 1) ? WIDTH : HEIGHT;
    writeCommand(CMD_MEMORY_ACCESS, &madctl[rotation], 1);
}

// The Amigo's panel shows inverted colors unless the controller's inversion
// is on, so "not inverted" means INVERSION_ON here.
void Amigo_ILI9486::invertDisplay(boolean invert)
{
    writeCommand(invert ? CMD_INVERSION_OFF : CMD_INVERSION_ON);
}

void Amigo_ILI9486::drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t* img)
{
    setArea(x, y, x + w - 1, y + h - 1);
    busHalfwords(img, (uint32_t)w * h);
}
