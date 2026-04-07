# NPC Personality Profiles (Voice, Cadence, Bias)

Goal: give each recurring NPC a consistent verbal identity that influences token resolution, fragment selection, and procedural dialogue assembly.

## Core profile fields

- `npc_id`
- `voice_register` (plain / formal / clipped / intimate / guarded)
- `cadence` (measured / abrupt / layered / sparse)
- `bias` (practical / diplomatic / transactional / suspicious / protective)
- `verbosity` (low / medium / high)
- `metaphor_tendency` (low / medium / high)
- `patience` (low / medium / high)

## Recommended profiles

### Janis
- voice_register: plain
- cadence: measured
- bias: practical
- verbosity: medium
- metaphor_tendency: low
- patience: medium

### Friday
- voice_register: guarded
- cadence: layered
- bias: diplomatic
- verbosity: medium
- metaphor_tendency: medium
- patience: high

### Mason
- voice_register: formal
- cadence: clipped
- bias: transactional
- verbosity: low
- metaphor_tendency: low
- patience: low

## How profiles affect runtime text

- `voice_register` influences word choice and contractions
- `cadence` influences sentence length and fragment ordering
- `bias` affects what kinds of observations or offers are favored
- `verbosity` controls how many optional fragments can be included
- `metaphor_tendency` controls how often figurative fragments are eligible
- `patience` affects how sharply a wary or contradicted tone escalates

## Engine hook

1. Resolve NPC profile for the active event.
2. Blend profile with current tone and memory band.
3. Select tokens and sentence fragments from profile-compatible pools.
4. Assemble final body text before pagination.

## Why this matters

This keeps Janis from sounding like Mason, and Friday from sounding like either of them, even when they occupy similar event structures. The player experiences not just different information, but different minds.
