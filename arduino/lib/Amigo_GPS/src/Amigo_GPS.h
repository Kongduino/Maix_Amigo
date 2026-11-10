#ifndef _AMIGO_GPS_H
#define _AMIGO_GPS_H

// GPS receiver on the Amigo's Grove port, wired as for gps.py: UART2 at
// 9600 baud, NMEA sentences parsed by TinyGPSPlus. Amigo_GPS is a
// TinyGPSPlus, so location, date, time, satellites, hdop, altitude, speed
// and course are read the usual way; call update() often to feed it.
//
// The Grove pins are also the KEY1 (IO23) and KEY2 (IO20) buttons, which
// can't be used while the GPS is connected.
//
// UNTESTED: builds, but hasn't run with a GPS module yet.

#include <Arduino.h>
#include <TinyGPSPlus.h>

#define AMIGO_GPS_RX    23  // K210 RX, from the GPS TX
#define AMIGO_GPS_TX    20  // K210 TX, to the GPS RX
#define AMIGO_GPS_BAUD  9600

class Amigo_GPS : public TinyGPSPlus {
public:
    Amigo_GPS(UARTClass& port = Serial2);

    void begin(uint32_t baud = AMIGO_GPS_BAUD, uint8_t rx = AMIGO_GPS_RX, uint8_t tx = AMIGO_GPS_TX);

    // Parses what the GPS has sent since the last call. True if a sentence
    // was completed.
    bool update(void);

    // True if nothing has been received for ms milliseconds (or ever): no
    // GPS connected, wrong wiring or wrong baud rate
    bool silent(uint32_t ms = 3000) const;

private:
    UARTClass& _port;
    uint32_t _lastByteMs;
};

// v with the given number of decimals, without printf("%f"), which this
// newlib build may not support
const char* gpsFormat(double v, uint8_t decimals, char* buf, size_t len);

#endif
