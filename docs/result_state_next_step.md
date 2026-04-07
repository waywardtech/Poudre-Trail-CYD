# Next Step: Stable CSV Event State + Result State

Additions to make the CSV system stable and readable:

1. Track the active CSV event row in engine state:
- `currentEventRowIndex_`
- select it once in `queueEvent()`
- do not recompute it in `snapshot()`
- clear it after the event resolves
- persist it in save/load

2. Add a post-choice result mode or result state:
- after applying a CSV choice, load `result_file`
- show that authored consequence text before returning to travel
- keep the selected event row stable while the player reads pages

This prevents event variant text, portrait, or tone from changing mid-read.
