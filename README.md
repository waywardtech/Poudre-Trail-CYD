# Poudre Trail - CYD

A narrative travel game for the **ESP32-2432S028R** ("Cheap Yellow Display", 2.8") and the
**ESP32-3248S035R** (3.5"). Set in the Cache la Poudre valley, Colorado, May 1864: the river is
rising, the army is coming, and every map is a promise to someone. *Who draws it when the water falls?*

## The story

You are a household travelling from **Laporte** down the Cache la Poudre to the high ground
that will become **Fort Collins**. Along the way you meet the people who are quietly deciding what
this valley is going to be, and a surveyor, Peter Ames, who is about to write it down.

| Character | Who they are | What they want from you |
|-----------|--------------|--------------------------|
| **Antoine Janis** | Trader at Laporte since 1858 | Plain dealing; that you ask before you take |
| **Chief Friday** | Arapaho leader | That you listen, and carry names accurately |
| **Little Willow** | Arapaho guide and trader at the canyon mouth | Respect for the crossings she knows |
| **Virginia Slade** | Keeper of the Virginia Dale home station | Useful hands; no mythmaking |
| **Mason** | Land claimant and contractor | Your signature, and your agreement |
| **Peter Ames** | Young army surveyor | Names for his blank map |

**Route:** Laporte, Canyon Mouth, Council Tree, Virginia Dale, Mason's Claim, Fort Collins.

**Timeline:** Day 1 is May 12, 1864. Time passes with distance, rest and some choices. Once the
calendar reaches May 19 the river floods (a timed event that fires once, wherever you are). The
epilogue records what history did next: the flood took Camp Collins from Laporte, the army raised
Fort Collins on higher ground that August, and in November soldiers killed more than a hundred
Cheyenne and Arapaho at Sand Creek.

**Historical liberties:** this is fiction built on real places and people. Dates and events are
compressed (the Slades left Virginia Dale before 1864; Jack Slade was hanged in March 1864), and
the stone point is the kind of Folsom-era artifact later found in northern Colorado. Little Willow,
Ames and Mason are invented. Chief Friday, Antoine Janis and Virginia Slade were real.

## Gameplay

- **Travel** costs days; each day costs food (9). Horses wear down, morale slips, and hunger hurts health.
- **Camp** lets you *rest* (health, horses, morale) or *hunt* (3 ammo, may find game). Both cost a day.
- **Trade** at five posts, spending trade goods. Several purchases per visit.
- **Events**: each stop has story events (some unlock only after earlier choices), and each leg has a
  55% chance of a random trail event (hail, a rising ford, a patrol that wants names, a fever...).
  Every event happens at most once per run.
- **Three reputations** (Janis, Friday, Mason) are tracked separately. Reach 5+ with both Mason and
  Friday and you earn the *moral contradiction* flag.
- **The map**: tap the title bar to open a stylised trail map. At Fort Collins, what you tell Ames
  (every name and where you learned it, only a few, nothing, or Mason's line) decides the ending:
  *A Map With Names*, *A Partial Map*, *The Blank Places*, or *Mason's Line*, with extra lines for the
  flood, the stone point, and Virginia's letter.
- **Result screens** show what changed (`Food +10, Trade goods -1`). Long text is paged: tap the text.
- You can lose: food, health, horses or morale reaching 0 ends the run.

## Features

- Runs with **no SD card**: the whole story is compiled in (`include/world_data.hpp`). The SD card adds art and saves, and may override the story (see below).
- **Animated art**: GIF scenes with small animated details (smoke, river glint, leaves, flags, fire) and PNG portraits, drawn from SD. The 3.5" panel draws them at 1.5x.
- Layout adapts to the panel size (320x240 or 480x320); text never clips because it wraps and pages.
- Split reputation, dated timeline, timed + random events, camp, trading, save/load (saves flags and which events have happened).

## Quick Start

### Windows

```powershell
# In an Administrator PowerShell, from the repo root:
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\setup_and_flash.ps1
```

A menu lets you run each step independently:

| Option | Action |
|--------|--------|
| 1 | Full setup — format SD + copy data + build + flash |
| 2 | Format SD only — wipe and partition the SD card |
| 3 | Copy data only — copy game files to an existing POUDRE volume |
| 4 | Build + flash — compile and upload firmware (no SD needed) |
| 5 | SD prep only — format SD + copy data (no flash) |

The script auto-detects your ESP32 COM port and SD card, creates a 16 GB FAT32 `POUDRE` partition and an exFAT `STORAGE` partition on the remainder, copies all game content, then builds and flashes via PlatformIO.

**Requirements:** Python 3, `pip install platformio` (the script falls back to `python -m platformio` if `pio` is not on PATH).

### Linux / macOS

```bash
chmod +x scripts/setup_and_flash.sh
sudo ./scripts/setup_and_flash.sh
```

Same five-option menu as the Windows script. Additional requirements: `dosfstools` (`sudo apt install dosfstools` / `brew install dosfstools`) and `exfatprogs` (`sudo apt install exfatprogs`) for the two-partition layout.

### Manual build (no SD card needed)

```bash
pip install platformio
pio run              # compile
pio run -t upload    # flash (auto-detects port)
pio device monitor   # serial output at 115200
```

The firmware runs a full fallback world with hard-coded content when no SD card is present.

## Hardware

Two supported boards (same shared SPI bus; pick the PlatformIO environment that matches yours):

| | ESP32-3248S035R (default) | ESP32-2432S028R |
|---|---|---|
| Display | 3.5" ST7796, 480x320 | 2.8" ILI9341, 320x240 |
| Backlight | GPIO 27 | GPIO 21 |
| Env | `esp32-3248S035R` | `esp32-2432S028R` |

Shared by both: touch XPT2046 (CS 33, IRQ 36), SD (CS 5), TFT CS 15, DC 2, MOSI 13, MISO 12, SCK 14, RST not used.
The sketch drives both backlight pins, so it lights either board.

## Build

```bash
pip install platformio

pio run                          # 3.5" board (default)
pio run -e esp32-2432S028R       # 2.8" board
pio run -t upload                # flash
pio device monitor               # serial output at 115200
```

`platformio.ini` carries the TFT_eSPI configuration; no `User_Setup.h` edits needed. Libraries
(TFT_eSPI, XPT2046_Touchscreen, AnimatedGIF, PNGdec) are fetched automatically.

### Host tests (g++)

```bash
make -C test run
```

Runs the engine tests (no Arduino toolchain needed): world loading, event queue and results,
calendar, timed flood, save/load, camp and trade, game over, a full run to the epilogue,
contradiction flag, finale gating, determinism.

## Diagnostics

Tap the **footer bar** (bottom strip) to open a live diagnostics screen, refreshed twice a second:
firmware build, board and chip, panel size and layout, SD card type/size and boot result, heap
(free / minimum / largest block), uptime, current art (file, bytes, decode ms, GIF frames played,
failure reason), touch (IRQ pin level, tap count, raw and mapped coordinates, which button was hit)
and the game state (mode, stop, day, RNG, events seen, flags). Taps elsewhere draw a crosshair,
which is the quickest way to check touch calibration. Tap the footer again to close.

Over serial (115200 baud): `d` dumps the same report, `i` toggles the screen, `m` toggles the map.
A one-line `[DIAG]` heartbeat prints every 10 s, and each tap prints a `[TOUCH]` line.

## Editing the story

The story lives in plain files; no C++ changes are needed to add events.

- `data/locations.csv`, `data/events.csv`, `data/trades.csv`, `data/events/<id>.txt`
- `events.csv` columns: `id, locationId, title, bodyFile, image, kind, requiresFlag`, then three choices of
  `label, food, ammo, med, trade, horse, morale, health, rep_janis, rep_friday, rep_mason, setFlag, result, days`
- `kind`: `story` (fires at its location), `trail` (random, between stops) or `timed:N` (fires on arrival once day >= N)
- `requiresFlag`: `a&!b&c|d` means a AND NOT b AND (c OR d). `setFlag` takes several flags separated by commas.
- After editing, run `python3 tools/embed_world.py` to rebuild `include/world_data.hpp` (the built-in copy
  of the story), then rebuild the firmware. Copying `data/` to the SD card overrides the built-in story
  without reflashing; an SD story in the old 13-column format is ignored automatically.

## Art

`data/art/` holds 96x72 images: **GIF** animations for scenes with no single subject (small details
move: smoke, river glint, leaves, flags, firelight) and **PNG** stills for portraits and objects.
The 3.5" board draws them at 1.5x. Regenerate everything with `python3 tools/make_art.py` (needs Pillow).
Art is read from the SD card; without one the game shows a placeholder and still plays.

## SD card layout

Copy the contents of `data/` to the SD card (the setup scripts do this for you):

```
/art/*.gif  /art/*.png
/data/locations.csv  /data/events.csv  /data/trades.csv
/data/events/*.txt
/saves/slot1.sav        <- created on first save
```

## Changelog

### Story, gameplay and graphics pass

- **Story**: 1864 timeline with a real calendar and a timed flood; new characters (Little Willow, Peter Ames) and the central question of who draws the map; 21 events, 12 trades, 6 stops
- **Gameplay**: random trail events, camp (rest / hunt), multi-purchase trading, result screens with deltas, game-over on food / health / horses / morale, endings driven by flags, saved flags and seen events
- **Map**: stylised trail map overlay (tap the title bar)
- **Art**: PNG portraits and animated GIF scenes replace the BMPs; 3.5" support with adaptive layout and 1.5x art
- **Text**: word-wrapped, paged body text (no clipping); long results fit
- **Single source of truth**: the CSVs are baked into the firmware by `tools/embed_world.py`; the hard-coded duplicate world is gone

### merge/reputation-e2e

- Split reputation (`repJanis`, `repFriday`, `repMason`), moral contradiction flag, `travelTo()` mode guard, Virginia Dale stop, expanded Chief Friday event, TFT_eSPI port.

## Hardware-only checklist (cannot be verified in CI)

- [ ] Touch: tap buttons and the title bar; if taps land mirrored flip `TOUCH_FLIP_X/Y`, if offset adjust `TOUCH_MIN/MAX_X/Y` in `src/main.cpp` (taps print `[TOUCH]` to serial)
- [ ] GIFs animate and PNGs show correct colours (if red and blue look swapped, call `tft.setSwapBytes(true)` in `setup()`)
- [ ] SD card mounts (`[SD] OK` on serial) and art appears; frame rate is smooth enough
- [ ] Panel size: serial prints `[TFT] WxH`; 480x320 means the 3.5" layout is active
- [ ] Save writes to `/saves/slot1.sav` and loads after a power cycle
- [ ] Backlight lights immediately on power-up
