#ifndef _AMIGO_SD_H
#define _AMIGO_SD_H

// SD card support for the Maix Amigo's TF slot, through the core's SD library
// (sdfatlib: FAT16/FAT32, 8.3 file names). Mount with amigoSdBegin(), then
// use the global SD object as usual.
//
// The slot is on SPI1, which the SP-MOD LoRa module also uses, on other pins.
// The K210 routes each SPI1 signal to one pin at a time, so after using
// another SPI1 device, call amigoSdSelect() before SD calls, and that
// device's SPIClass::begin() before using it again.

#include <SD.h>

#define AMIGO_SD_SCK   11
#define AMIGO_SD_MISO   6
#define AMIGO_SD_MOSI  10
#define AMIGO_SD_CS    26

// Mounts the card on the global SD object. False if there is no card or no
// FAT16/FAT32 volume on it.
bool amigoSdBegin(void);

// Routes SPI1 back to the TF slot
void amigoSdSelect(void);

#endif
