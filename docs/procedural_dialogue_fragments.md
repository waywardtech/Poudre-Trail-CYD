# Procedural Dialogue Fragments (Sentence-Level Assembly)

Goal: assemble event text from reusable sentence fragments so dialogue feels less repeated without requiring a separate full prose file for every emotional shade.

## Recommended structure

Each event can define fragment slots such as:
- `intro_fragment`
- `observation_fragment`
- `warning_fragment`
- `offer_fragment`
- `closing_fragment`

At runtime, the engine selects one fragment from each slot based on:
- event tone
- NPC memory
- current flags
- weighted randomness

## Example assembled output

1. intro: `Janis looks up from the counter and takes in the road dust on your boots.`
2. observation: `He can already tell whether you came for help or for advantage.`
3. warning: `He answers carefully, naming the crossings that hold after runoff.`
4. offer: `He points out the safer road without pretending the valley is simple.`
5. closing: `The lesson is given plainly, but not freely.`

## Suggested data model

Use a fragments CSV or text dictionary keyed by:
- `npc`
- `slot`
- `tone`
- `memory_band`
- `weight`

Example columns:
- `fragment_id`
- `npc`
- `slot`
- `tone`
- `memory_band`
- `weight`
- `text`

## Runtime flow

1. Resolve event and final tone.
2. Determine memory band, e.g. hostile / wary / neutral / known / trusted.
3. Build a weighted pool of fragments for each slot.
4. Select one fragment per slot.
5. Concatenate in slot order.
6. Apply existing token replacement if needed.

## Why this matters

This keeps dialogue responsive and varied while preserving authorial control. It also works cleanly with the weighted event system and attitude drift already planned.
