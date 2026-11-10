# Meshtastic on the Maix Amigo: gap analysis

What a Meshtastic firmware port to the Sipeed Maix Amigo (Kendryte K210) needs,
compared with what this repo's Arduino environment already provides.

Based on meshtastic/firmware at commit d9c9916 (2026-11-10) and
meshtastic/device-ui at commit 199c0fe (2026-11-03). Effort estimates
are rough working days for someone familiar with both code bases.

## Summary

The port looks feasible, and most of the risk is front-loaded into getting the
firmware to compile on the Kendryte toolchain. Nothing found so far is a hard
blocker for a working node. The main limitations are on the client side: there
is no Bluetooth, so phones connect only over USB serial (Android) or
not at all (iOS), plus the web client and CLI over USB.

Meshtastic's STM32WL port is the closest template: no screen, no Bluetooth,
no Wi-Fi, no FreeRTOS, and its own LittleFS on internal flash.

| Area | Status | Work | Effort |
|---|---|---|---|
| Toolchain, C++17 | Works | GCC 8.2 compiles C++17 (`optional`, `string_view`, structured bindings tested) | - |
| Platform layer | Missing | New `ARCH_K210`: `architecture.h` and about 10 hook functions | 1-2 d |
| First compile | Unknown | Arduino API gaps in the Maixduino core, found by compiling; expect a shim | 2-5 d |
| Threading | Works | Without FreeRTOS, `OSThread` is cooperative and locks are no-ops (as STM32WL) | - |
| Settings storage | Missing | SPI3 NOR flash driver (no Maixduino driver; port MaixPy's `w25qxx.c`) + reuse STM32WL's LittleFS wrapper | 2-3 d |
| Radio | Partly | `RF95Interface` (SX127x) exists but needs a DIO0 interrupt the SP-MOD module doesn't have; add software IRQ polling (exists for SX126x only) | 1-2 d |
| Crypto | Works | Software AES/Curve25519 (meshtastic/Crypto) is portable | - |
| Random numbers | Partly | No hardware RNG on the K210; key generation relies on SX1276 wideband RSSI noise via RadioLib, which the RNG code already mixes in | 0.5 d |
| Node ID / MAC | Missing | No documented K210 unique ID; derive from the SPI flash's unique ID | 0.5 d |
| Client API | Works | `SerialConsole` / StreamAPI over USB serial: CLI, web client (Web Serial), Android over USB OTG | - |
| Bluetooth | Not possible | No BLE hardware: no iOS app, no Android over BLE | - |
| Wi-Fi | Large | ESP8285 is driven by AT commands; Meshtastic's Wi-Fi/TCP API expects native Wi-Fi | 5+ d, later |
| Display | Missing | `TFTDisplay` backend adapter over `Amigo_ILI9486` (needs `pushImage`, rotation, on/off) | 2-3 d |
| MUI (LVGL device UI) | Later | Screen size fits; needs its own thread and a K210 LVGL display/touch driver | 5-10 d, later |
| Touch | Small | `TouchScreenImpl1` takes a `getTouch(x, y)` function; `Amigo_FT6X36::getPoint` fits | 0.5 d |
| GPS | Small | K210 branch in `GPS.cpp` for the UART type and `begin(baud, rx, tx)` | 0.5 d |
| Battery | Small | AXP173 isn't supported (AXP192/AXP2101 only, ESP32 only); custom `HasBatteryLevel` reading the AXP173 | 1 d |
| Sleep / power saving | Skip | Exclude the power FSM at first; K210 has no practical deep sleep with timed wake | - |
| SD card | Optional | Shares SPI1 with the radio; needs pin re-routing under `SPILock` | later |

## Details

### Build system

- Meshtastic builds with PlatformIO and `framework = arduino`, so a variant can
  use the same `platform = kendryte210` plus the upstream Maixduino package as
  this repo. The `kendryte210` PlatformIO platform is old and unmaintained,
  which is a long-term risk.
- `arduino_base` sets `-std=gnu++17` and `-Os`. The Kendryte GCC 8.2.0
  compiles a C++17 test (`std::optional`, `std::string_view`, `if constexpr`,
  structured bindings, `std::atomic`) for `rv64imafc`/`lp64f`.
- The K210 is 64-bit (LP64): pointers and `long` are 64 bits. Meshtastic's
  native Linux/macOS builds are 64-bit too, so the shared code should cope.
- Meshtastic's build scripts (`bin/platformio-custom.py`) special-case ESP32
  and nRF52 only and fall back for other platforms.
- The K210 boot ROM copies the firmware into its 6 MB SRAM and runs it from
  there. A ~2-3 MB image leaves plenty of heap: memory class LARGE
  (`src/memory/MemClass.h`), like hosts and ESP32+PSRAM.

### Platform layer

A platform lives in `src/platform/<arch>/` and is selected by `ARCH_*` defines
(about 220 files use `ARCH_*`). STM32WL's is about 340 lines plus LittleFS.
Hooks to provide (see `src/target_specific.h` and `main-stm32wl.cpp`):
`getMacAddr`, `getDeviceId`, `getBluetoothAddr`, `setBluetoothEnable`,
`updateBatteryLevel`, `cpuDeepSleep`, `enterDfuMode`, `playStartMelody`, a
setup hook, and `preFSBegin`. Several are empty stubs on a Bluetooth-less
board. `HW_VENDOR` can be `meshtastic_HardwareModel_PRIVATE_HW` until an
official hardware model is added through the protobufs repo.

`millis()` returns `uint64_t` in the Maixduino core. On LP64 that is the same
width as `unsigned long`, so it should only cause warnings.

### Storage

`src/FSCommon.h` maps `FSCom` to a LittleFS with an Adafruit-style `File` API
per platform. STM32WL brings its own (`STM32_LittleFS*`, `littlefs/`), with
flash access in three hooks (`_internal_flash_read/prog/erase`) behind a page
cache. For the K210 these hooks would call an SPI3 NOR flash driver:

- The Maixduino core has no flash driver. MaixPy has one
  (`components/drivers/flash`, w25qxx).
- Since the firmware runs from RAM, writing the flash at run time is safe (no
  execute-in-place to disturb).
- Partition choice: MaixPy keeps its SPIFFS at 0xD00000-0x1000000. Placing
  LittleFS there overwrites MaixPy's files, so to keep the MaixPy backup
  restorable, use a different region (for example 0x400000-0xD00000).

### Radio

- `RF95Interface` (RadioLib SX1276) is supported, as the legacy RFM95 radio.
  The STM32WL base excludes SX127x; the K210 variant must not.
- `RF95Interface` arms the DIO0 interrupt (`enableInterrupt(isrRxLevel0)`).
  The SP-MOD LoRa module has no DIO0 line.
- `RadioLibInterface` already has a software IRQ-poll mode
  (`LORA_DIO1_SOFTWARE_POLL`: virtual `setRadioIsr`/`clearRadioIsr`,
  `handleSoftwareLoraIrqPoll`, a 1 ms poll tick, `deliverPendingIrqFromPoll`),
  implemented only in `SX126xInterface`. An RF95 version reads the SX127x IRQ
  flags register (as this repo's LoRa tester does) and is about 50 lines. It is
  a candidate for upstreaming.
- The radio's SPI1 is shared with the TF slot. With the SD card unused, no
  sharing is needed.

### Randomness and identity

- `HardwareRNG::fill` has no K210 source and falls back to
  `std::random_device`, then XORs in radio entropy (`RadioLibInterface::randomBytes`,
  RadioLib's SX127x wideband RSSI). Key generation must make sure the radio is
  up first; worth a check that `std::random_device` isn't constant on this
  newlib.
- `getMacAddr` must return a stable 6-byte address: derive it from the SPI
  NOR flash's unique ID (command 0x4B on most chips).

### Clients

- No Bluetooth hardware: the iOS app and Bluetooth on Android are not
  possible.
- `SerialConsole` runs StreamAPI on `Serial` (the CH552 USB serial, UARTHS):
  Python CLI, web client over Web Serial (Chrome), Android over USB OTG.
- Wi-Fi: the Amigo's ESP8285 speaks AT commands (the core has WiFiEsp).
  Meshtastic's `mesh/wifi` and `mesh/api` expect a native Wi-Fi stack, so a TCP
  API on port 4403 over ESP-AT is new work. Later, if at all.

### Display, touch, input

- Meshtastic's UI (`graphics/Screen`) renders an OLED-style buffer;
  `TFTDisplay` converts and pushes changed lines through a backend
  (LovyanGFX, TFT_eSPI or Arduino_GFX). A K210 backend needs `begin`,
  `setRotation`, `fillScreen`, `pushImage`, `startWrite`/`endWrite` and display
  on/off. `Amigo_ILI9486`'s framebuffer and row flushing fit this.
- The LVGL device UI (MUI) is a separate, later step: see below.
- Touch: `TouchScreenImpl1(width, height, getTouch)` takes a plain function,
  so `Amigo_FT6X36::getPoint` can drive Meshtastic's touch input.
- Buttons: KEY1 (IO23) and KEY2 (IO20) are also the Grove/GPS pins; the BACK
  key (IO31) is the LoRa MISO line in this setup.

### MUI (device-ui)

MUI is Meshtastic's LVGL touch UI (meshtastic/device-ui), the alternative to the
classic screen.

- **Screen size fits.** MUI's main view targets 320x240 and up:
  `ViewFactory` creates `TFTView_320x240` for any size that isn't one of the
  small special views (128x64, 160x80, 240x240, 480x222), and it lays itself
  out to the real resolution. The closest existing board is the Makerfabs
  Nomad Terminal, a 3.5" 320x480 panel (`TFT_WIDTH 320`, `TFT_HEIGHT 480`)
  built with `VIEW_320x240`, the same resolution as the Amigo.
- **Memory fits.** On ESP32, MUI needs PSRAM (the original CYD board no
  longer works for lack of memory); the K210's 6 MB of SRAM covers it.
- **It needs a thread.** `tftSetup.cpp` runs MUI's endless loop
  (`tft_task_handler`) in a FreeRTOS task on ESP32 or a `std::thread` on
  Linux; a bare K210 build has neither. Options: call MUI's handler from an
  `OSThread` (changes to how the firmware starts MUI), turn on the FreeRTOS
  that ships in the Kendryte SDK, or run it on the K210's second core (as
  MaixPy does), which needs real locks first, since Meshtastic's are no-ops
  without FreeRTOS.
- **It needs a display driver.** device-ui's hardware drivers use LovyanGFX,
  which doesn't support the K210 (the others are a Linux framebuffer and X11).
  A K210 `DisplayDriver` hands LVGL's flushes to `Amigo_ILI9486`'s framebuffer
  and reads touch from `Amigo_FT6X36`. LVGL itself is portable C.
- Estimate: 5-10 days, mostly the threading question, on top of the classic
  screen. Do it after the node works with the classic UI.

### GPS, battery

- `GPS.cpp` picks the serial type and `begin()` per architecture; a K210
  branch uses `UARTClass` and `begin(baud, rx, tx)`. Meshtastic uses its own
  TinyGPSPlus fork; this repo's `Amigo_GPS` is not needed there.
- PMU support (`HAS_PMU`, XPowersLib) is ESP32-only and covers AXP192/AXP2101.
  The AXP173 at 0x34 is the same address, but without `HAS_PMU` the K210 build
  doesn't touch it. Battery level needs a small `HasBatteryLevel` subclass
  reading the AXP173's battery voltage registers.

## Suggested order

1. **Skeleton build**: fork, add `src/platform/k210` with stubbed hooks and an
   Amigo variant, exclude screen, GPS, Wi-Fi, Bluetooth, MQTT, input and the
   power FSM, and get a link. This is where the unknowns are.
2. **Storage**: flash driver + LittleFS, so settings and keys persist.
3. **Radio**: RF95 software polling; interoperability test against another
   Meshtastic node on the same band.
4. **Serial client**: CLI and web client over USB.
5. **Display and touch**, then **GPS**, **battery**, node ID and RNG checks.
6. Optional later: MUI (the LVGL touch UI), SD card, Wi-Fi over ESP8285.

## Decisions

- The SP-MOD LoRa module is a 433 MHz part. Interop testing needs another
  Meshtastic node on a 433 MHz region (for example EU_433 or CN).
- The port stays a fork of meshtastic/firmware for now, with a private
  hardware model (`PRIVATE_HW`); upstreaming can come later.

## Open questions

- Does the K210 have a usable unique ID (OTP)? Not found in the SDK; the flash
  unique ID is the fallback.
