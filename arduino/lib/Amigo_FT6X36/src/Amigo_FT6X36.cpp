#include "Amigo_FT6X36.h"

Amigo_FT6X36::Amigo_FT6X36(TwoWire& wire)
: _wire(wire), _rotation(0)
{
}

bool Amigo_FT6X36::begin(uint8_t threshold, bool initWire)
{
    if (initWire) _wire.begin((uint8_t)SDA, (uint8_t)SCL, 400000);
    uint8_t vendor;
    if (!readRegs(FT6X36_REG_VENDOR_ID, &vendor, 1)) return false;
    writeReg(FT6X36_REG_DEV_MODE, 0);  // normal operating mode
    writeReg(FT6X36_REG_TH_GROUP, threshold);
    return true;
}

void Amigo_FT6X36::setRotation(uint8_t r)
{
    _rotation = r & 3;
}

bool Amigo_FT6X36::readRegs(uint8_t reg, uint8_t* buf, uint8_t len)
{
    _wire.beginTransmission((uint8_t)FT6X36_ADDR);
    _wire.write(reg);
    if (_wire.endTransmission(false) != 0) return false;
    if (_wire.requestFrom((uint8_t)FT6X36_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = _wire.read();
    return true;
}

uint8_t Amigo_FT6X36::readReg(uint8_t reg)
{
    uint8_t value = 0;
    readRegs(reg, &value, 1);
    return value;
}

bool Amigo_FT6X36::writeReg(uint8_t reg, uint8_t value)
{
    _wire.beginTransmission((uint8_t)FT6X36_ADDR);
    _wire.write(reg);
    _wire.write(value);
    return _wire.endTransmission() == 0;
}

uint8_t Amigo_FT6X36::touched(void)
{
    uint8_t n = readReg(FT6X36_REG_TD_STATUS) & 0x0F;
    return n > FT6X36_MAX_POINTS ? 0 : n;
}

// The touch panel reports in the LCD's physical (row, col) space, except that
// its column axis runs the other way (MaixPy's touch.py has invert_y for it).
// After flipping that, this is the inverse of the ILI9486 MADCTL mapping used
// for each rotation.
void Amigo_FT6X36::mapPoint(uint16_t col, uint16_t row, int16_t& x, int16_t& y)
{
    if (col >= FT6X36_PANEL_WIDTH) col = FT6X36_PANEL_WIDTH - 1;
    if (row >= FT6X36_PANEL_HEIGHT) row = FT6X36_PANEL_HEIGHT - 1;
    col = FT6X36_PANEL_WIDTH - 1 - col;
    switch (_rotation) {
        case 0:  // MV: landscape
            x = row;
            y = col;
            break;
        case 1:  // MY: portrait
            x = col;
            y = FT6X36_PANEL_HEIGHT - 1 - row;
            break;
        case 2:  // MV | MX | MY: landscape, flipped
            x = FT6X36_PANEL_HEIGHT - 1 - row;
            y = FT6X36_PANEL_WIDTH - 1 - col;
            break;
        default: // MX: portrait, flipped
            x = FT6X36_PANEL_WIDTH - 1 - col;
            y = row;
            break;
    }
}

uint8_t Amigo_FT6X36::getPoints(TouchPoint* points, uint8_t max)
{
    // TD_STATUS followed by two 6-byte point records
    uint8_t buf[1 + 6 * FT6X36_MAX_POINTS];
    if (!readRegs(FT6X36_REG_TD_STATUS, buf, sizeof(buf))) return 0;
    uint8_t n = buf[0] & 0x0F;
    if (n > FT6X36_MAX_POINTS) return 0;
    if (n > max) n = max;
    for (uint8_t i = 0; i < n; i++) {
        const uint8_t* p = &buf[1 + 6 * i];
        uint16_t col = ((p[0] & 0x0F) << 8) | p[1];
        uint16_t row = ((p[2] & 0x0F) << 8) | p[3];
        mapPoint(col, row, points[i].x, points[i].y);
        points[i].event = p[0] >> 6;
        points[i].id = p[2] >> 4;
    }
    return n;
}

bool Amigo_FT6X36::getPoint(int16_t& x, int16_t& y)
{
    TouchPoint p;
    if (getPoints(&p, 1) == 0) return false;
    x = p.x;
    y = p.y;
    return true;
}
