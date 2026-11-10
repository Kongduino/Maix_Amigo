#include "Amigo_GPS.h"

Amigo_GPS::Amigo_GPS(UARTClass& port)
: _port(port), _lastByteMs(0)
{
}

void Amigo_GPS::begin(uint32_t baud, uint8_t rx, uint8_t tx)
{
    _port.begin(baud, rx, tx);
}

bool Amigo_GPS::update(void)
{
    bool sentence = false;
    while (_port.available()) {
        if (encode(_port.read())) sentence = true;
        _lastByteMs = millis();
    }
    return sentence;
}

bool Amigo_GPS::silent(uint32_t ms) const
{
    return charsProcessed() == 0 || millis() - _lastByteMs > ms;
}

const char* gpsFormat(double v, uint8_t decimals, char* buf, size_t len)
{
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
