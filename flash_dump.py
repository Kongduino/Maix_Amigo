# Dump the whole 16 MiB SPI flash to the SD card, so the board can be
# restored later by flashing /sd/flash_dump.bin at address 0x0 with kflash.
from Maix import utils
import uos

CHUNK = 4096
SIZE = 16 * 1024 * 1024
OUT = "/sd/flash_dump.bin"

if not hasattr(utils, "flash_read"):
    raise Exception("Maix.utils.flash_read not available in this firmware")
if "sd" not in uos.listdir("/"):
    raise Exception("No SD card mounted")

with open(OUT, "wb") as f:
    for addr in range(0, SIZE, CHUNK):
        f.write(utils.flash_read(addr, CHUNK))
        if addr % 0x100000 == 0:
            print("0x%06x" % addr)

print("done:", uos.stat(OUT)[6], "bytes written to", OUT)
