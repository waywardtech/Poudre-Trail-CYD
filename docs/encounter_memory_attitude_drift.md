# Encounter Memory + NPC Attitude Drift

Add these concepts to the engine and CSV layer:

## Engine-side memory
- `npcMemoryJanis_`
- `npcMemoryFriday_`
- `npcMemoryMason_`
- `lastEncounteredNpc_`
- `encounterCountByEventId_`

## Suggested behavior
- Positive or negative choices add to long-term NPC memory.
- Memory influences future tone selection alongside current reputation.
- Attitude drift slowly decays toward neutral over travel steps unless reinforced.

## Example rules
- Janis remembers practical respect and useful trade.
- Friday remembers whether warnings were heard or dismissed.
- Mason remembers compliance and whether the player becomes predictable.

## Drift model
Each travel step:
- if memory > 0, reduce by 1 toward 0 every N steps
- if memory < 0, increase by 1 toward 0 every N steps

This keeps the valley responsive without making one mistake permanent forever.

## CSV extension ideas
Optional fields:
- `memory_target`
- `memory_delta`
- `tone_bias`
- `attitude_floor`
- `attitude_ceiling`

These let content influence attitude over time without hardcoding every relationship in the firmware.
