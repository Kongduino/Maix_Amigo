#include "Amigo_ILI9486.h"
#include "st7789.h"
#include "fpioa.h"
#include "sysctl.h"

// MADCTL values per rotation: MY 0x80, MX 0x40, MV 0x20, BGR 0x08.
// 0x28 is what MaixPy uses on the Amigo ("dir": 40 in config.json).
static const uint8_t madctl[4] = {0x28, 0x88, 0xE8, 0x48};

Amigo_ILI9486::Amigo_ILI9486(void)
: Adafruit_GFX(AMIGO_LCD_WIDTH, AMIGO_LCD_HEIGHT)
{
}

boolean Amigo_ILI9486::begin(uint32_t freq, uint16_t color)
{
    fpioa_set_function(AMIGO_LCD_CS_PIN, (fpioa_function_t)(FUNC_SPI0_SS0 + AMIGO_LCD_SS));
    fpioa_set_function(AMIGO_LCD_WR_PIN, FUNC_SPI0_SCLK);
    sysctl_set_spi0_dvp_data(1);
    tft_hard_init(0, AMIGO_LCD_SS, AMIGO_LCD_RST_GPIONUM, AMIGO_LCD_DCX_GPIONUM, freq,
                  AMIGO_LCD_RST_PIN, AMIGO_LCD_DCX_PIN, AMIGO_LCD_DMA_CH);
    initSequence();
    setRotation(0);
    fillScreen(color);
    writeCommand(DISPALY_ON);
    return true;
}

void Amigo_ILI9486::writeCommand(uint8_t cmd, const uint8_t* data, uint8_t len)
{
    tft_write_command(cmd);
    if (len) tft_write_byte((uint8_t*)data, len);
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

    writeCommand(SOFTWARE_RESET);
    delay(50);
    writeCommand(0xF1, f1, sizeof(f1));
    writeCommand(0xF2, f2, sizeof(f2));
    writeCommand(0xF8, f8, sizeof(f8));
    writeCommand(0xF9, f9, sizeof(f9));
    writeCommand(INVERSION_CTL, &zero, 1);
    writeCommand(POWER_CTL2, &pwr2, 1);
    writeCommand(VCOM_CTL1, vcom, sizeof(vcom));
    writeCommand(POSITIVE_GAMMA_CORRECT, pgamma, sizeof(pgamma));
    writeCommand(NEGATIVE_GAMMA_CORRECT, ngamma, sizeof(ngamma));
    writeCommand(PIXEL_FORMAT_SET, &pixfmt, 1);
    writeCommand(SLEEP_OFF);
    delay(120);
    writeCommand(NORMAL_DISPALY_ON);
    delay(10);
}

void Amigo_ILI9486::setArea(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4];

    data[0] = x1 >> 8; data[1] = x1; data[2] = x2 >> 8; data[3] = x2;
    writeCommand(HORIZONTAL_ADDRESS_SET, data, 4);
    data[0] = y1 >> 8; data[1] = y1; data[2] = y2 >> 8; data[3] = y2;
    writeCommand(VERTICAL_ADDRESS_SET, data, 4);
    tft_write_command(MEMORY_WRITE);
}

void Amigo_ILI9486::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= _width || y >= _height) return;
    setArea(x, y, x, y);
    tft_write_half(&color, 1);
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

    // tft_fill_data sends 32-bit frames, i.e. two pixels at a time. For an odd
    // pixel count the extra pixel wraps to the window start, which already
    // has the same color.
    uint32_t data = ((uint32_t)color << 16) | color;
    setArea(x, y, x + w - 1, y + h - 1);
    tft_fill_data(&data, ((uint32_t)w * h + 1) / 2);
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
    writeCommand(MEMORY_ACCESS_CTL, &madctl[rotation], 1);
}

void Amigo_ILI9486::invertDisplay(boolean invert)
{
    writeCommand(invert ? INVERSION_DISPALY_ON : INVERSION_DISPALY_OFF);
}

void Amigo_ILI9486::drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t* img)
{
    setArea(x, y, x + w - 1, y + h - 1);
    tft_write_half(img, (uint32_t)w * h);
}
