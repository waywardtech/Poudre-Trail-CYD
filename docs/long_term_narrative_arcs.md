# Long-Term Narrative Arcs (Character Evolution Over Time)

Goal: allow recurring NPCs and the valley itself to evolve over the course of a playthrough, so relationships and tone change in ways that feel cumulative rather than purely reactive.

## Arc state ideas

- `arcJanis`
- `arcFriday`
- `arcMason`
- `arcValley`

Each arc can be represented as:
- stage 0: baseline
- stage 1: emerging shift
- stage 2: established change
- stage 3: irreversible turn

## Recommended triggers

Arc progression can be based on:
- repeated memory patterns
- major flags
- cumulative reputation
- follow-up chain completion
- weighted rare encounters witnessed

## Example arcs

### Janis
- 0: practical distance
- 1: cautious recognition
- 2: quiet investment in your survival
- 3: treats you as someone with obligations to the valley

### Friday
- 0: guarded diplomacy
- 1: testing whether you listen
- 2: selective trust
- 3: speaks to you as someone who will carry consequences forward

### Mason
- 0: transactional interest
- 1: expectation of usefulness
- 2: assumption of alignment
- 3: pressure, leverage, or ownership logic

### Valley
- 0: passage and uncertainty
- 1: signs of strain
- 2: visible reordering after flood / traffic / power shift
- 3: the old balance is no longer recoverable in the same form

## Engine hook

1. Evaluate arc progression after major results, not after every minor choice.
2. Store current arc stage in engine state and save/load.
3. Use arc stage to:
- unlock new variants
- bias dialogue fragments
- change travel text
- alter endings and epilogues

## Why this matters

This turns the game from a chain of incidents into a true evolving story. The player is not only choosing actions; they are participating in longer changes in people and place.
