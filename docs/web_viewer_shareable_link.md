# Export Runs to Web Viewer / Shareable Link

Goal: let a run export be viewed outside the CYD in a browser, and optionally shared as a simple link or static page.

## Recommended architecture

### Export format
Generate a portable JSON artifact per run, for example:
- `/exports/run_001_bundle.json`

Include:
- journey log entries
- final state snapshot
- arcs
- memory
- reputation
- major flags
- epilogue text

### Web viewer
Create a lightweight static web viewer that can:
- load one exported JSON bundle
- render the journey log step-by-step
- show final state summary
- show synthesized epilogue
- optionally compare two bundles side-by-side

Suggested repo structure:
- `web_viewer/index.html`
- `web_viewer/viewer.js`
- `web_viewer/styles.css`

### Shareable link options

1. **Static-hosted file path**
   - upload JSON export to GitHub Pages, Netlify, or similar
   - link format: `viewer.html?run=run_001_bundle.json`

2. **Embedded export blob**
   - base64 or compressed JSON in query string or fragment
   - practical only for small runs

3. **Gist / raw file URL**
   - store export externally and load by URL parameter

## Engine hook

1. Add JSON export writer on device.
2. Write all relevant run data into one bundle file.
3. Reuse the same bundle for replay viewer, comparison mode, and browser viewer.

## Why this matters

This turns each run into a portable narrative artifact. A player can finish on the CYD, then open the full run in a browser, share it, compare it, or archive it without needing the original device.
