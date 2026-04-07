# Community Run Gallery (Browse Shared Runs)

Goal: let players browse a gallery of shared exported runs in a browser, open them in the web viewer, and optionally compare them.

## Recommended architecture

### Shared run bundle
Each run is exported as a JSON bundle, for example:
- `runs/run_001_bundle.json`

### Gallery manifest
Maintain a static manifest file that lists published runs:
- `web_viewer/gallery_manifest.json`

Suggested fields per entry:
- `id`
- `title`
- `author`
- `summary`
- `run_url`
- `epilogue_excerpt`
- `created_at`
- `tags`

Example:
```json
{
  "id": "run_001",
  "title": "Listened at Council Tree",
  "author": "waywardtech",
  "summary": "High Friday trust, low Mason influence, flood witness path.",
  "run_url": "../runs/run_001_bundle.json",
  "epilogue_excerpt": "You reached Fort Collins worn thin by the road...",
  "created_at": "2026-04-06",
  "tags": ["Friday", "Flood", "Valley Arc 2"]
}
```

### Gallery viewer
Add a static page:
- `web_viewer/gallery.html`

Capabilities:
- load the gallery manifest
- render cards for each shared run
- open run in the existing viewer
- optionally select two runs for comparison

## Why this matters

This turns exported runs into a community artifact. Players can browse each other's journeys, compare outcomes, and treat the game as an evolving archive of historical narrative possibilities.
