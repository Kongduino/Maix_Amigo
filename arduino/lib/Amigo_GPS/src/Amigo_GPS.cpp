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
