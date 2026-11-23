# Poudre Trail v2 design brief

v2 is a reskin. The story, engine and save format stay as they are in v1.0; the look and feel are redesigned around the 2.8" CYD's real capabilities, a contemporary interface, and larger, more realistic, gently animated art. Tracking issue: #5.

## 1. What the hardware can do (measured)

Measured on a 2.8" ESP32-2432S028R (ILI9341, SPI at 27 MHz, no PSRAM) with the v2 branch's `-DCYD_BENCH=1` build:

| Measurement | Result |
|---|---|
| Free heap after boot | 255 KB, largest block 110 KB |
| Redraw the full 320x240 screen | 47 ms, about 21 fps |
| Full-screen 16-bit off-screen buffer (153 KB) | does not fit |
| Half-screen 16-bit buffer (320x120, 77 KB) | fits; pushed in 25 ms |
| Full-screen 8-bit or 4-bit buffer | fits (77 / 38 KB); pushed in 53 ms |
| Small sprite, 48x32 16-bit | 3 KB; pushed in 1 ms |
| SD card read (SPI at 4 MHz) | 412 KB/s |

What follows from this:

- **Never animate the whole screen.** A full redraw is 47 ms, so full-screen animation tops out around 20 fps and stutters. v1's full-screen GIFs work only because they are small and slow.
- **Animate small regions over a still picture.** A 48x32 sprite costs 1 ms, so a dozen small moving things still run at 30+ fps in full colour.
- **Full colour is affordable for stills.** A 320x240 16-bit picture is 150 KB on the card; at 412 KB/s it loads in about 0.4 s. Raising the SD clock (the card was mounted at a conservative 4 MHz; 20 MHz is typical) should cut that to around 0.1 s. To try first.
- **Compose in bands when layering is needed.** A 320x120 16-bit band (77 KB) fits in memory, so the top or bottom half can be composed off-screen without flicker.

## 2. Rendering approach: still scene plus small loops

Each scene is:

1. **A full-screen background** in full 16-bit colour, streamed from the SD card once when the scene opens (raw RGB565 or PNG; whichever decodes faster on the device, to be measured).
2. **A few small animated layers** ("loops"): short sprite-sheet animations drawn over the background at fixed spots, each restoring the background behind it from a saved patch before drawing the next frame. Examples: birds crossing the sky, a camp fire, a bird shifting on a branch, water glinting at the ford, smoke from a chimney, grass moving.
3. **The interface** drawn on top: a title bar and a bottom sheet with text and choices.

Loops follow one rule: **minimal, slow, natural motion.** Each loop is 4 to 12 frames, plays at 6 to 12 fps, and covers a small area (up to about 64x48). The scene should read as a still painting that is quietly alive, never as a cartoon.

If memory or card speed forces it, the fallbacks, in order, are: fewer loop frames; 8-bit palette sprites (256 colours per sprite) for the loops only; then a reduced-colour background with ordered dithering.

## 3. Interface: a contemporary design, after Apple's Human Interface Guidelines

The reference is Apple's [Human Interface Guidelines](https://developer.apple.com/design/human-interface-guidelines/) for iOS, adapted to a 2.8" 320x240 resistive screen (about 143 pixels per inch).

**Principles carried over**

- **Content first.** The art fills the screen; interface elements sit over it, not beside it. (v1 shows art in a small box next to the text.)
- **Clear hierarchy.** One title, one block of text, one set of choices per screen. Status details move out of the way until needed.
- **Consistency.** The same components in the same places on every screen: title bar at the top, sheet at the bottom, primary choice in the same spot.
- **Feedback.** Every tap gets a visible pressed state immediately, before the screen changes.

**Adapted specifics**

- **Touch targets at least 40 px tall.** Apple's 44 pt minimum is about 7 mm; at 143 ppi that is about 40 px. The resistive panel is less precise than a phone's, so this is a floor, not a goal. v1's buttons are 18 to 26 px.
- **Typography.** Replace the 5x7 bitmap font with smooth anti-aliased fonts. TFT_eSPI can render `.vlw` smooth fonts loaded from the SD card. Use one open-licence sans-serif family (candidates: Inter, Source Sans 3, Atkinson Hyperlegible) in three sizes: title (about 20 px), body (about 14 px), caption (about 11 px). Check legibility on the real panel before committing.
- **Layout.** An 8 px spacing grid, 12 px screen margins, and rounded corners of 10 to 12 px on sheets and buttons.
- **Bottom sheet.** Story text and choices sit in a sheet anchored to the bottom of the screen over the art, like an iOS sheet: a dark translucent-looking panel (the art behind it darkened in software, since true transparency is too costly) with a small grabber bar. Long text pages inside the sheet. The upper part of the screen stays clear for the art and its loops.
- **Status as a compact bar.** Food, health, morale and the rest become small icons with values in a slim top bar; tapping it opens a full status sheet with the reputations.
- **Colour.** A restrained palette drawn from the landscape: dusk blue, cottonwood green, sandstone, river grey, with one accent (warm amber) reserved for the primary action and progress. Text is white or near-white on dark sheets for contrast.
- **Motion in the interface.** Short and functional only: a sheet sliding up (about 150 ms) and pressed states. No decorative transitions that would block input.

**Screens to design:** title, event (art plus sheet with 2 to 3 choices), result ("what changed"), travel menu, camp, trade, map, status sheet, ending.

## 4. Art direction: realistic, grounded in the historical record

Goal: replace v1's stylised art with **more realistic, painterly illustrations** of the Cache la Poudre valley in 1864, researched from period sources so places, buildings, clothing and landscape are plausible.

**Style:** realistic and painterly, in the spirit of 19th-century western landscape painting and illustration (for example the watercolours William Henry Jackson painted from his survey sketches), rather than pixel art. Natural light by time of day: dawn, midday, dusk, night by the fire.

**Production:** each scene is drawn at 320x240 (full screen) with the bottom-sheet area kept visually quiet, plus small sprite sheets for its loops. The look is checked on the real panel, not only on a monitor: the ILI9341's colours and contrast differ from a PC screen.

**Sources to research** (check each item's rights and reuse terms before using it as a direct reference):

| Subject | Sources found so far |
|---|---|
| Antoine Janis, Laporte | Photograph of Janis with Northern Arapaho men, Brady-Handy collection, Library of Congress ([NPS](https://www.nps.gov/people/joseph-antoine-janis.htm)). His log cabin survives in Library Park, Fort Collins ([Wikipedia](https://en.wikipedia.org/wiki/Antoine_Janis)). Laporte history ([losthistory.net](https://losthistory.net/over-land/laporte.html), [FCMoD trail tour](https://fcmod.org/trails/tour2/tour2-laporte.html)). |
| Chief Friday (Warshinun), Arapaho | An 1859-60 photograph of four Arapaho chiefs (identification uncertain) and an 1880 engraving ([Fort Collins History Connection](https://history.fcgov.com/explore/friday), [People of the Poudre, Burris](https://www.fcgov.com/historicpreservation/pdf/poudre.pdf)), a c.1880 studio portrait possibly of Friday ([Carlisle Indian School archive](https://carlisleindian.dickinson.edu/node/25362)), and a Poudre ethnohistory ([Poudre Heritage Alliance](https://poudreheritage.org/wp-content/uploads/poudreethnohistory_full1.pdf)). Portray Arapaho people with care and, ideally, review by Arapaho cultural representatives. |
| Virginia Dale stage station | William Henry Jackson's 1867 painting and 1870 photograph, History Colorado ([History Colorado](https://historycolorado.org/node/3273), [NRHP nomination](https://npgallery.nps.gov/AssetDetail/NRIS/85002562)). The station building still stands. |
| Camp Collins and the 1864 flood | An 1864 map of Camp Collins at CSU Special Collections ([PEHC](https://pehc.colostate.edu/?p=1523)), a Van Lennep sketch of the camp ([Fort Collins History Connection](https://history.fcgov.com/galleries/camp-collins)), and accounts of the June 1864 flood ([PEHC](https://pehc.colostate.edu/?p=1442), [city history](https://history.fcgov.com/explore/city-history)). No photographs of the 1864 flood are known; Denver Public Library flood photos are from 1904. |
| Landscape, buildings, daily life | The Fort Collins Museum of Discovery archive: over 200,000 local photographs and about 1,500 maps; digital scans can be ordered ([collection](https://history.fcgov.com/collections/historic-photographs)). |

**Still to research:** 1860s Overland Trail wagons and harness, settler and army clothing of 1864, Arapaho dress and lodges of the period, the valley's plants and animals (plains cottonwood, willow, magpies, red-tailed hawks, pronghorn, elk) for the loops, and the landforms near each stop.

## 5. Order of work

1. **Prototype one scene end to end** on the device: Laporte, with a full-screen background, two loops (birds, chimney smoke), the new bottom sheet and smooth fonts. Measure load time, frame rate and memory; decide raw RGB565 versus PNG backgrounds and the SD clock.
2. **Build the component set:** title bar, status bar, bottom sheet, buttons with pressed state, result card, paging.
3. **Research and draw the art,** scene by scene, checking each on the panel.
4. **Port every screen** to the new components.
5. **Release v2.0.0:** full tap-through on hardware, then merge `v2` into `main`.

## 6. Open questions

- Which font family reads best on this panel at 11 to 14 px?
- Raw RGB565 or PNG for backgrounds: card space versus decode time?
- How many loops per scene before frame rate or memory suffers?
- Who draws the art (commissioned, hand-drawn, or generated then painted over), and under what licence?
