# Ending Generator (Epilogue Synthesis Based on Arcs + Memory)

Goal: generate a playthrough-specific epilogue that reflects the player's long-term relationships, contradictions, witnessed events, and the valley's evolving condition.

## Inputs

Use these categories when synthesizing the ending:

- character arcs:
  - `arcJanis`
  - `arcFriday`
  - `arcMason`
- valley arc:
  - `arcValley`
- memory state:
  - `memJanis`
  - `memFriday`
  - `memMason`
- reputation state:
  - `repJanis`
  - `repFriday`
  - `repMason`
- major flags:
  - `heard_friday`
  - `moral_contradiction`
  - `saw_flood_damage`
  - `helped_virginia`
  - `traded_with_mason`
  - `witnessed_council_tree_gathering`

## Recommended structure

Build the epilogue from ordered sections:

1. arrival line
2. valley condition line
3. Janis line
4. Friday line
5. Mason line
6. self / consequence line
7. closing image

Each section should be chosen from small authored text pools using the final arc + memory state.

## Example synthesis logic

### Arrival
- if survival was hard: emphasize cost and exhaustion
- if supplies remained high: emphasize preparedness
- if contradiction flag is set: emphasize unsettled moral balance

### Valley condition
- `arcValley` 0-1: uncertainty and passage
- `arcValley` 2: visible reordering after flood / traffic / pressure
- `arcValley` 3: older balance no longer recoverable in the same form

### Janis
- low arc/memory: he remains cautious distance in memory
- mid arc/memory: he regards you as someone who learned something
- high arc/memory: he regards you as someone bound by obligations to the valley

### Friday
- low arc/memory: his counsel remains partly withheld
- mid arc/memory: he tested and sometimes trusted you
- high arc/memory: he speaks to you as someone who now carries consequences forward

### Mason
- low arc/memory: he saw only a passing traveler
- mid arc/memory: he considered you useful
- high arc/memory: he expected alignment, debt, or future compliance

### Self / consequence
- if contradiction flag: stress divided tables and moral tension
- if Friday path dominates: stress difficult understanding
- if Mason path dominates: stress provision, compromise, and implication
- if balanced: stress ambiguity and unresolved change

## Engine hook

1. At victory, call `buildEpilogue()` instead of using a single hardcoded footer.
2. Store the synthesized output in a long body string for the final screen.
3. Reuse the existing paged text renderer.

## Why this matters

This turns the ending from a generic win text into a real accounting of the playthrough. The player sees not only where they arrived, but what sort of person arrived there, and what condition they helped leave behind.
