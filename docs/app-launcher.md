# App launcher for the Maix Amigo (design note)

Status: brainstorm, nothing built. Written 2026-11-13 so the idea isn't lost;
the Meshtastic port comes first.

The idea: keep several firmwares on the Amigo (Meshtastic, MaixPy, Maixduino
sketches, demos) and pick one from a launcher, installing new ones from the SD
card.

## How the K210 boots

The boot ROM always reads the image at flash address 0 (1 byte AES flag,
4 bytes length, the code, a SHA-256), copies it into SRAM at 0x80000000 and
runs it. There is no slot selection in the ROM. The ROM's download mode
(BOOT/ISP) is always there, so a bad image at address 0 can be reflashed.

The Amigo's original MaixPy firmware has no second-stage bootloader: address 0
holds the MaixPy image itself (checked in `flash_dump.bin`).

## Design

Three parts:

1. **Bootloader** at flash address 0, a few KB. The ROM loads it like any
   firmware. It moves itself out of the way (to the top of RAM, or the 2 MB AI
   SRAM at 0x80600000, unused before the KPU starts), reads the boot table,
   copies the chosen app from its flash slot to 0x80000000, checks it, and
   jumps to it. Holding a key at power-on boots the launcher, as a rescue.
2. **Launcher**, an ordinary firmware in its own slot. Touch UI: the apps on
   the SD card and the ones installed. *Install* copies an app from SD into a
   flash slot and records it in the boot table; *Run* sets it as the next app
   and resets the board.
3. **Apps**, unchanged: each is linked for 0x80000000 as today. Because the
   board resets between apps, each starts from the state the ROM gives it.

Why apps run from flash slots rather than straight from the SD card: the
bootloader stays tiny (no SD or FAT code to go wrong), boot stays fast, apps
run without a card, and the card serves as the app library.

Getting back to the launcher: the rescue key at power-on, or a "Launcher" menu
item in the apps we control (Meshtastic: write the boot table, reset).

## Flash layout (16 MiB)

The fixed points are other firmwares' data: Meshtastic's LittleFS and
MaixPy's SPIFFS can't move without reformatting them (Meshtastic's holds the
settings and the node's keys).

| Range | Size | Use | Notes |
|---|---|---|---|
| 0x000000-0x00FFFF | 64 KB | Bootloader | ROM image format; the only thing the ROM loads |
| 0x010000-0x011FFF | 2 x 4 KB | Boot table, two copies | written alternately, with a sequence number, so a power cut mid-write leaves the other copy |
| 0x012000-0x01FFFF | 56 KB | Spare | |
| 0x020000-0x0FFFFF | 896 KB | Launcher | LCD, touch, SD and FAT, a list UI |
| 0x100000-0x3FFFFF | 3 MB | Slot A: Meshtastic | 0.9 MB today, ~2.4 MB with MUI |
| 0x400000-0x5FFFFF | 2 MB | Meshtastic's LittleFS | fixed (settings, keys, font packs) |
| 0x600000-0x8FFFFF | 3 MB | Slot B: MaixPy | the original is 2.1 MB (0x000000-0x20FFFF in the dump) |
| 0x900000-0xBFFFFF | 3 MB | Slot C: another app | |
| 0xC00000-0xCFFFFF | 1 MB | App data / Slot D (small) | e.g. KPU models, or a small sketch |
| 0xD00000-0xFFFFFF | 3 MB | MaixPy's SPIFFS | fixed (config.json, boot.py, main.py) |

Today Meshtastic sits at 0x000000 (0.9 MB) with its LittleFS at 0x400000:
moving it to slot A frees address 0 for the bootloader and leaves the LittleFS
where it is, so its settings survive.

## Boot table

One 4 KB sector, two copies (the newer valid one wins):

- magic, format version, sequence number, CRC of the table
- the default app, and an optional "next boot only" app (run once, then back
  to the default)
- up to 8 entries: name, flash address, size, SHA-256 (the ROM's format, so an
  image written by kflash at a slot address is already a valid entry), flags,
  and the flash ranges the app writes (see open questions)

The K210's SHA-256 accelerator checks an image in a few ms per MB.

## SD card

App library, for example `/apps/<name>/app.bin` and `/apps/<name>/app.json`
(name, version, SHA-256, size, the flash ranges it uses, an icon). The card's
pins (MaixPy config: SCLK IO11, MOSI IO10, MISO IO6, CS IO26) don't clash with
anything the launcher uses. The radio isn't used in the launcher.

## Things to get right

- **Core 1 at hand-off.** After reset the second core waits in the ROM until
  the running image wakes it. The bootloader must leave it there, untouched,
  so each app's startup code wakes it as usual. The one detail to prove early.
- **Apps' own flash ranges.** Apps run from RAM, so where their code sits in
  flash doesn't matter to them, but their data does. MaixPy scripts load KPU
  models from fixed flash addresses (0x300000 is common), which in this
  layout is inside slot A. The manifest should declare each app's ranges, and
  the launcher refuse an install that overlaps another app's.
- **App size.** Up to about 6 MB (8 MB with the AI SRAM, if the app doesn't
  use the KPU), minus the bootloader's corner while it copies.
- **The rescue key.** A key the bootloader can read before anything is set
  up. ENTER (IO23) is also Grove port 3, where a BME680 currently sits
  (its I2C line idles high, so it reads as "not pressed").
- **Flashing during development.** kflash writes at an address; a `.kfpkg`
  puts an app at its slot address and updates nothing else. The boot table
  then needs an entry for it (a small host tool, or the launcher's Install).

## Effort (rough)

| Part | Days |
|---|---|
| Bootloader and boot table | 2-3 |
| Launcher: SD, list, install, run, touch UI | 3-5 |
| "Launcher" menu item in Meshtastic | 0.5 |

The launcher can reuse the Meshtastic port's LCD and touch drivers and this
repo's `Amigo_SD` library.

## First step, when this resumes

A minimal bootloader that boots today's Meshtastic image from slot A instead
of address 0, with the boot table hard-coded. If that works (core 1 included),
the layout and the hand-off are proven and the rest is ordinary app code.

## Open questions

1. Does any app read its own image back from flash at address 0 (version
   checks, OTA)? Meshtastic doesn't; MaixPy unknown.
2. Should the launcher be the default after a power cut, or the last app run?
3. Keep a copy of the launcher in two places, in case an install goes wrong?
