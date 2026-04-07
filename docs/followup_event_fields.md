# Follow-up Event CSV Fields

Add these fields to the event CSV schema:

- `followup_event_id`
- `followup_delay`
- `followup_requires_flag`
- `followup_result_file`

## Semantics

- `followup_event_id`: the next event to queue after this event's result is acknowledged
- `followup_delay`: number of travel steps before the follow-up is eligible
- `followup_requires_flag`: optional extra flag gate before the follow-up can fire
- `followup_result_file`: optional authored text to use when handing off into the follow-up sequence

## Engine flow

1. Resolve event row
2. Apply choice deltas and result text
3. Store follow-up fields in engine state
4. Show RESULT screen
5. On continue:
   - if follow-up is queued and eligible, enter it
   - otherwise return to travel
