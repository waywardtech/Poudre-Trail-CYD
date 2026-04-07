# Replay Viewer (Reconstruct Run Step-by-Step)

Goal: let the player reopen a saved journey log and step through the run as an interactive recap rather than just reading a flat text file.

## Source material

The replay viewer reads from:
- `/exports/run_XXX_journey_log.txt`
- optional `/exports/run_XXX_save.txt`
- optional synthesized epilogue

## Recommended viewer flow

1. New title-screen menu item: `Replay Viewer`
2. Select an exported run file
3. Parse each journey-log line into a replay entry
4. Show entries one at a time or page by page
5. Allow:
- next entry
- previous entry
- jump to ending
- exit to title

## Suggested replay entry format

Each line already follows a readable structure:

`Step 3 | Council Tree | friday_counsel | Heard Friday's warning and chose to listen.`

Parse into:
- step number
- location
- event id
- summary text

## UI integration

Add a new game mode:
- `REPLAY`

Snapshot additions:
- `replayTitle`
- `replayBody`
- `replayIndex`
- `replayCount`

## Engine state additions

- `replayEntries_[]`
- `replayCount_`
- `replayIndex_`

## Why this matters

This turns exported runs into an on-device story viewer. The player can revisit their path through the valley, compare outcomes across runs, and treat each run as a narrative artifact rather than disposable state.
