# Multi-Event Chains + Follow-up Events

Add these CSV fields to support chained narrative beats:

- `followup_event_id`
- `followup_delay`
- `followup_requires_flag`
- `followup_result_file`

## Recommended pattern

1. Resolve the current event row normally.
2. After a choice is applied, if that choice or row specifies a `followup_event_id`, store it in engine state.
3. Show the current result text first.
4. On continue, if a queued follow-up event exists, enter that event instead of returning to travel.
5. If no queued follow-up exists, return to travel.

## Engine state additions

- `queuedFollowupEventId_`
- `queuedFollowupDelay_`
- `currentEventRowIndex_`
- `resultText_`

## Why this matters

This lets one moment lead into another without hardcoding chains in firmware. For example:
- Friday warning -> flood witness -> Mason response
- burial observance -> later community reaction
- Mason trade -> later expectation or pressure
