# Wiring CSV into the Engine

Next engine refactor steps:

1. During setup, load:
- `/data/events.csv`
- `/data/locations.csv`

2. Replace hardcoded event selection with:
- filter by `location_id`
- filter by `requires_flag`
- filter by `blocks_flag`
- filter by reputation min/max
- choose first matching variant

3. Populate snapshot from selected CSV row:
- `eventTitle`
- `eventBody` (from `body_file` fallback)
- `eventImagePath` from `portrait_bmp`
- location/background path from `background_bmp`
- `currentTone`
- `visibleChoices`

4. On choice resolution:
- apply deltas from CSV
- set optional flags
- load `result_file` for footer/result text

5. Travel screen:
- use `locations.csv` for location name and travel text defaults

This is the point where the game stops depending on hardcoded story tables.
