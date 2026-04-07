# Weighted Event Selection + Randomness

Add these optional CSV fields to events:

- `weight`
- `cooldown_steps`
- `rarity_tag`
- `once_only`

## Recommended semantics

- `weight`: relative chance among all eligible events at a location
- `cooldown_steps`: event cannot repeat until N travel steps have passed
- `rarity_tag`: optional label like `common`, `uncommon`, `rare`
- `once_only`: if true, event can only fire once per save

## Selection flow

1. Filter events by:
- location
- flags
- rep bounds
- follow-up state
- cooldown / once-only eligibility

2. Build a weighted pool from all remaining events.
3. Roll random selection from the weights.
4. Record the chosen row as the active event row.
5. Record visit / fire count metadata for cooldown and once-only logic.

## Why this matters

This prevents the valley from feeling deterministic on every run while still keeping story logic grounded.
