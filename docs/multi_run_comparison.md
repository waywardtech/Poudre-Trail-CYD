# Multi-Run Comparison (Compare Two Journeys)

Goal: let the player load two exported runs and compare them side-by-side to see where their paths diverged, which relationships changed, and how different endings were produced.

## Source files

The comparison viewer reads from two exports:
- `/exports/run_A_journey_log.txt`
- `/exports/run_B_journey_log.txt`
- optional state exports for each run

## Recommended comparison views

1. **Step comparison**
   - show run A and run B entries for the same step index side by side
   - highlight if the location or event id differs

2. **Ending comparison**
   - compare synthesized epilogues
   - summarize the major reason they diverged

3. **State comparison**
   - compare final:
     - reputation
     - memory
     - arcs
     - major flags

## Suggested UI mode

Add a new mode:
- `COMPARE`

Snapshot additions:
- `compareLeftTitle`
- `compareLeftBody`
- `compareRightTitle`
- `compareRightBody`
- `compareIndex`
- `compareCount`

## Engine state additions

- `compareLeftEntries_[]`
- `compareRightEntries_[]`
- `compareLeftCount_`
- `compareRightCount_`
- `compareIndex_`

## Parsing strategy

Journey-log lines already use a simple pattern:
`Step 3 | Council Tree | friday_counsel | Heard Friday's warning and chose to listen.`

Split into:
- step number
- location
- event id
- summary

## Why this matters

This turns runs into comparable stories. The player can see not only what happened, but where a choice, relationship, or arc pushed one journey away from another.
