#include "Amigo_SD.h"
#include "fpioa.h"

bool amigoSdBegin(void)
{
    // The SD library starts the global SPI object with SPI.begin() and no
    // pins, which would route SPI1 to the Maix Go's pins: give SPI the TF
    // slot's pins first.
    SPI = SPIClass(SPI1, AMIGO_SD_SCK, AMIGO_SD_MISO, AMIGO_SD_MOSI, -1);
    return SD.begin(AMIGO_SD_CS);
}

void amigoSdSelect(void)
{
    fpioa_set_function(AMIGO_SD_SCK, FUNC_SPI1_SCLK);
    fpioa_set_function(AMIGO_SD_MOSI, FUNC_SPI1_D0);
    fpioa_set_function(AMIGO_SD_MISO, FUNC_SPI1_D1);
}
