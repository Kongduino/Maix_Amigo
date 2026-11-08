#ifndef _AMIGO_FT6X36_H
#define _AMIGO_FT6X36_H

// Driver for the Maix Amigo's FT6X36 capacitive touch controller.
// The chip shares the I2C bus (SCL 24, SDA 27) with the AXP173 and MSA301.
// Coordinates follow the same rotations as Amigo_ILI9486, so setRotation()
// on both keeps touch and display aligned.

#include <Arduino.h>
#include <Wire.h>

#define FT6X36_ADDR           0x38

#define FT6X36_REG_DEV_MODE   0x00
#define FT6X36_REG_GEST_ID    0x01
#define FT6X36_REG_TD_STATUS  0x02
#define FT6X36_REG_P1_XH      0x03
#define FT6X36_REG_TH_GROUP   0x80
#define FT6X36_REG_CHIP_ID    0xA3
#define FT6X36_REG_FIRM_VER   0xA6
#define FT6X36_REG_VENDOR_ID  0xA8

#define FT6X36_MAX_POINTS     2

// Raw panel size, in the panel's own (portrait) orientation
#define FT6X36_PANEL_WIDTH    320
#define FT6X36_PANEL_HEIGHT   480

// Per-point event flag reported by the chip
#define FT6X36_EVENT_DOWN     0
#define FT6X36_EVENT_UP       1
#define FT6X36_EVENT_CONTACT  2

struct TouchPoint {
    int16_t x;
    int16_t y;
    uint8_t id;
    uint8_t event;
};

class Amigo_FT6X36 {
public:
    Amigo_FT6X36(TwoWire& wire = Wire);

    // Returns false if the chip doesn't answer. Pass initWire = false if the
    // bus was already started, e.g. by the AXP173 library.
    // Lower threshold = more sensitive. 12 is what MaixPy uses.
    bool begin(uint8_t threshold = 12, bool initWire = true);

    // 0-3, same numbering as Amigo_ILI9486::setRotation
    void setRotation(uint8_t r);
    uint8_t getRotation(void) { return _rotation; }

    // Number of points currently touched (0-2)
    uint8_t touched(void);
    // Fills up to max points, returns how many were read
    uint8_t getPoints(TouchPoint* points, uint8_t max = FT6X36_MAX_POINTS);
    // First touch point only; returns false when nothing is touched
    bool getPoint(int16_t& x, int16_t& y);

    uint8_t chipID(void) { return readReg(FT6X36_REG_CHIP_ID); }
    uint8_t firmwareVersion(void) { return readReg(FT6X36_REG_FIRM_VER); }
    uint8_t vendorID(void) { return readReg(FT6X36_REG_VENDOR_ID); }

private:
    TwoWire& _wire;
    uint8_t  _rotation;

    bool    readRegs(uint8_t reg, uint8_t* buf, uint8_t len);
    uint8_t readReg(uint8_t reg);
    bool    writeReg(uint8_t reg, uint8_t value);
    void    mapPoint(uint16_t col, uint16_t row, int16_t& x, int16_t& y);
};

#endif
