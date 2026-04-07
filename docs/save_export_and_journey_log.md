# Save Export + Run Summary (Player Journey Log)

Goal: let each playthrough produce a readable journey log and a portable save export that captures the state of the run in a way humans can inspect, compare, and share.

## Save export

In addition to the compact machine save, generate a human-readable export with:
- route progress
- resources
- reputations
- memory values
- arc stages
- major flags
- current queued follow-up state
- active event row if present

Recommended format:
- plain text or JSON
- one export file per run under `/exports/`

Suggested filenames:
- `run_001_save.txt`
- `run_001_state.json`

## Journey log

Record major moments as the run proceeds:
- event entered
- choice selected
- result text shown
- follow-up queued
- major flag set
- arc advanced
- ending generated

Each log entry should include:
- travel step
- location
- event id
- concise prose summary

Suggested runtime path:
- `/exports/run_001_journey_log.txt`

## Example log entry

`Step 3 | Council Tree | friday_counsel | Heard Friday's warning and chose to listen.`

## Engine hook

1. Add a `journeyLog_` string list or rolling text buffer.
2. Append entries after every major event resolution.
3. On save/export, write both current state and journey log to SD.
4. On victory, append the synthesized epilogue to the journey log.

## Why this matters

This turns each playthrough into a document of the player's path through the valley. It also makes balancing and narrative debugging much easier, because the system's behavior becomes inspectable after the fact.
