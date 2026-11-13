# MicroPython for the Maix Amigo (design note)

Status: brainstorm, nothing built. Written 2026-11-13; the Meshtastic port
comes first.

The goal: a lean, current MicroPython on the Amigo, in place of MaixPy-v1,
which is large and years behind upstream.

## Where things stand

- **MaixPy-v1** is MicroPython of about 2019 (the v1.11 era) with the OpenMV
  image stack and Kendryte's KPU (AI accelerator) support added. That is
  where its size and age come from.
- **Upstream MicroPython has no K210 port.** Its RISC-V support covers the
  RP2350 and ESP32-C3/C6 (32-bit) and a QEMU test port, which may cover
  64-bit RISC-V in recent releases (to check). There is no native-code
  emitter for 64-bit RISC-V, so `@micropython.native` and `viper` wouldn't be
  available; plain Python is unaffected.
- **LoBo's MicroPython_K210** (FreeRTOS-based) was the other port, and is
  older still.

## Approach

A new `ports/k210` on upstream MicroPython, on the bare Kendryte standalone
SDK (no FreeRTOS), the way the Meshtastic port runs: one interpreter on
core 0, core 1 free for drivers (the LCD, as in the Meshtastic port).

What to leave out to stay lean: the OpenMV image library and the KPU, unless
wanted later as optional modules.

## Phases

| Phase | Contents | Effort (rough) |
|---|---|---|
| 1. Core | REPL over the USB serial port (raw REPL too, so `mpremote` works), `machine.Pin` / `I2C` / `SPI` / `UART` / `Timer`, `time`, LittleFS on the SPI flash as the VFS | 1-2 weeks |
| 2. Amigo hardware | the LCD as a `framebuf` (480x320 RGB565, 300 KB) with DMA on core 1, touch, then the onboard chips | ~1 week |
| 3. Extras | SD card (micropython-lib's `sdcard.py` over `machine.SPI` should work as is), camera, audio (ES8374 codec, I2S) | as wanted |
| Later, maybe | LVGL bindings (`lv_micropython`); the KPU, only if MaixPy's AI side is wanted | - |

The MSA301 accelerometer and the AXP173 power chip can be plain Python
modules over `machine.I2C`.

## What carries over from the Meshtastic port

The Meshtastic port (fork `meshtastic-k210`, branch `k210-amigo`) already
solved the parts that took debugging. Most of it is Arduino-style C++ and
would be rewritten in C against the SDK, but the findings carry over:

- **LCD**: the ILI9481-type panel on SPI0 in octal mode, its init sequence,
  DMA through the uncached alias (0x8xxxxxxx -> 0x4xxxxxxx), conversion and
  flushing on core 1 woken by an IPI (`AmigoLCD.cpp`).
- **Touch**: the FT6X36 needs the whole 13-byte frame read, and can start a
  touch with the previous touch's last point (`maix_amigo/variant.cpp`).
- **Flash and file system**: the SPI flash driver and the LittleFS block
  device (`k210_flash`, `LittleFS-k210.cpp`).
- **Camera**: the rear OV7740 answers only through I2C2 on IO40/41, not the
  DVP's own SCCB (which reaches the front GC0328 at the same address); the
  PWDN bit selects the camera; frames read through uncached memory
  (`AmigoCamera.cpp`).
- **Serial**: Maixduino's 64-byte receive buffer dropped bytes; MicroPython's
  UART code would be its own, but needs a generous receive buffer.
- **Toolchain gaps**: no libatomic for 1-byte atomics (`atomic-k210.c`).
- **I2C pins**: the main bus is IO27 (SDA) / IO24 (SCL); Grove port 3 is
  IO20 / IO23 (also the NEXT and ENTER keys); Grove 1 and 2 share the SX1276
  SP-MOD's pins while it's fitted.

## Toolchain

The Kendryte GCC 8.2 that Maixduino uses is old. A current RISC-V GCC
(13 or 14, `-march=rv64imafc_zicsr_zifencei -mabi=lp64f`) should build both
the Kendryte SDK and current MicroPython. Worth confirming first: it decides
how current the port can be.

## Flash layout

MicroPython would be one of the launcher's apps (see `app-launcher.md`): its
image in a slot, and its own file-system range, for example the 1 MB at
0xC00000, or a new region. It shouldn't reuse MaixPy's SPIFFS at 0xD00000, so
a MaixPy backup stays restorable. Without the launcher, it would sit at
address 0 like any K210 firmware.

## First step, when this resumes

Build upstream MicroPython's minimal port for the K210 with the newer
toolchain and get a REPL on the USB serial port. That settles the toolchain
and the port's skeleton in a day or two.

## Open questions

1. Which MicroPython release to start from (the latest at the time), and
   whether to aim for upstreaming the port or keep it as a fork.
2. Keep MaixPy's module names (`lcd`, `sensor`) for existing scripts, or use
   upstream conventions (`framebuf`, a `camera` module)?
3. Is the KPU wanted at all? It's MaixPy's main reason to exist, and the
   heaviest part to port (nncase models, the KPU driver).
