#ifndef _AMIGO_ILI9486_H
#define _AMIGO_ILI9486_H

// Adafruit_GFX driver for the Maix Amigo's 3.5" 480x320 ILI9486 panel.
// The panel sits on the K210's 8-bit parallel LCD bus (SPI0 in octal mode).

#include <Arduino.h>
#include <Adafruit_GFX.h>

#define AMIGO_LCD_CS_PIN    36
#define AMIGO_LCD_RST_PIN   37
#define AMIGO_LCD_DCX_PIN   38
#define AMIGO_LCD_WR_PIN    39

// Peripheral resources used by the driver, the same as MaixPy's
#define AMIGO_LCD_SS          3   // SPI0 chip select line
#define AMIGO_LCD_RST_GPIONUM 30  // GPIOHS used for RST
#define AMIGO_LCD_DCX_GPIONUM 31  // GPIOHS used for D/C
#define AMIGO_LCD_DMA_CH      2

#define AMIGO_LCD_WIDTH     480
#define AMIGO_LCD_HEIGHT    320

#define COLOR_BLACK       0x0000
#define COLOR_NAVY        0x000F
#define COLOR_DARKGREEN   0x03E0
#define COLOR_DARKCYAN    0x03EF
#define COLOR_MAROON      0x7800
#define COLOR_PURPLE      0x780F
#define COLOR_OLIVE       0x7BE0
#define COLOR_LIGHTGREY   0xC618
#define COLOR_DARKGREY    0x7BEF
#define COLOR_BLUE        0x001F
#define COLOR_GREEN       0x07E0
#define COLOR_CYAN        0x07FF
#define COLOR_RED         0xF800
#define COLOR_MAGENTA     0xF81F
#define COLOR_YELLOW      0xFFE0
#define COLOR_WHITE       0xFFFF
#define COLOR_ORANGE      0xFD20
#define COLOR_GREENYELLOW 0xAFE5
#define COLOR_PINK        0xF81F

class Amigo_ILI9486 : public Adafruit_GFX {
public:
    Amigo_ILI9486(void);

    // Rotation 0 is landscape (480x320) with the camera on the left, 1 is
    // portrait with the camera at the top, 2 and 3 are those turned by 180.
    boolean begin(uint32_t freq = 15000000, uint16_t color = COLOR_BLACK);

    virtual void drawPixel(int16_t x, int16_t y, uint16_t color);
    virtual void writePixel(int16_t x, int16_t y, uint16_t color);
    virtual void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    virtual void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
    virtual void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);

    virtual void
    drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color),
    drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color),
    fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color),
    fillScreen(uint16_t color);

    virtual void setRotation(uint8_t r);
    virtual void invertDisplay(boolean invert);

    // Draws a w*h RGB565 image, no clipping: it must fit on screen.
    void drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t* img);

    // Raw controller access, like MaixPy's lcd.register()
    void writeCommand(uint8_t cmd, const uint8_t* data = nullptr, uint8_t len = 0);

protected:
    void initSequence(void);
    void setArea(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
};

#endif
