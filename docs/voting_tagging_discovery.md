# Voting / Tagging / Discovery System

Goal: help players find interesting community runs by supporting lightweight voting, richer tags, and simple discovery views such as featured, recent, rare-path, and high-contrast runs.

## Gallery manifest extensions

Extend each gallery entry with:
- `votes`
- `featured_score`
- `discovery_tags`
- `path_markers`
- `rarity_score`
- `comparison_hooks`

Example fields:
- `votes`: integer score or count
- `featured_score`: derived ranking used for gallery sorting
- `discovery_tags`: tags like `Friday`, `Flood`, `Contradiction`, `Rare Burial`, `Mason Pressure`
- `path_markers`: compact markers derived from final arcs / flags
- `rarity_score`: computed from rare events encountered
- `comparison_hooks`: keys used to suggest similar or contrasting runs

## Recommended discovery views

1. `Featured Runs`
   - sort by `featured_score`

2. `Recent Runs`
   - sort by `created_at`

3. `Rare Paths`
   - sort by `rarity_score`

4. `Strong Contrasts`
   - pair runs with opposite path markers

5. `By Character`
   - filter by tags such as `Janis`, `Friday`, `Mason`, `Virginia`, `Valley`

## Voting model

For a static-hosted gallery, treat voting as metadata managed outside the CYD and written back into the manifest.

Simple options:
- manual curation
- GitHub issue reactions or comments as vote source
- external lightweight backend later if needed

## Why this matters

This helps the gallery surface notable journeys instead of becoming a flat archive. Players can discover rare historical paths, emotionally intense runs, or runs centered on specific relationships and compare them with their own.
