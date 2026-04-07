# Dynamic Dialogue Variation (Tone-Based Text Swapping)

Goal: let one event body adapt its wording based on tone, NPC memory, and attitude drift without duplicating entire event rows.

## Recommended approach

Use lightweight text tokens inside dialogue files, then resolve them at runtime.

Example source text:

`Janis [GREETING] and studies the road dust on your boots before he speaks.`

## Token families

- `GREETING`
- `WARNING`
- `OFFER`
- `REACTION`
- `FAREWELL`

## Runtime mapping by tone

### OPEN
- GREETING -> `nods once`
- WARNING -> `speaks plainly`
- OFFER -> `lays out the safer road without ceremony`

### WARY
- GREETING -> `looks up but does not smile`
- WARNING -> `answers carefully`
- OFFER -> `gives only what must be said`

### PRESSURED
- GREETING -> `barely glances up`
- WARNING -> `speaks in clipped phrases`
- OFFER -> `cuts to what matters`

### CONTRADICTED
- GREETING -> `reads your face before your words`
- WARNING -> `tests what table you mean to keep`
- OFFER -> `offers help that is not neutral`

## Engine hook

1. Load the dialogue body file.
2. Determine final tone from event tone + memory modifiers.
3. Replace tokens using a tone dictionary before pagination.

## Why this matters

This creates line-level variation while keeping content files manageable. It also makes Janis, Friday, and Mason feel responsive without requiring a fully separate prose file for every small attitude change.
