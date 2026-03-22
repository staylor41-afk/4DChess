# C++ Evo Lab Contract

This document defines the first compatibility contract between the headless C++ simulator and the HTML 8D Chess viewer.

## Scope

- Supported dimensions: `2D` through `8D`
- Primary goal: fast, headless batch simulation
- Secondary goal: emit research logs that the HTML game can later import or mirror

## CLI

The headless runner is exposed as `eightd_evo`.

### Single game

```text
eightd_evo mode=single dimensions=5 max_moves=200 checkpoint=50 segment=1000 seed=42 output=run.json text_output=run.txt
```

### Batch / Evo-style run

```text
eightd_evo mode=run dimensions=8 generations=10 games=25 max_moves=2000 until_winner=false checkpoint=100 segment=1000 seed=42 output=batch.json champions=champions.json
```

## JSON shapes

### `GameLog`

The single-game path emits a JSON object with these top-level keys:

- `title`
- `generated_at`
- `game`
- `mode`
- `dimensions`
- `players`
- `move_cap`
- `snapshot`
- `winner`
- `moves_played`
- `players_remaining`
- `captures`
- `player_roster`
- `research_checkpoints`
- `replay_events`
- `replay_segments`
- `move_log`
- `chat_log`

The current headless runner can also write a plain-text companion log for single runs.
Each checkpoint now includes a serialized `state` snapshot so the HTML viewer can jump visually through a replay without re-simulating the game.
Replay segments are intended to hold the local story between checkpoints, usually in `1000`-move chunks.

This intentionally mirrors the HTML log structure closely enough that we can later add a loader without rethinking the export shape.

### `EvoBatchResult`

The batch path emits:

- `config`
- `totalGames`
- `totalMoves`
- `winsByPlayer`
- `champions`
- `runLogs`

### `ChampionRecord`

Champion rows mirror the HTML champion export naming:

- `rank`
- `label`
- `aggression`
- `foresight`
- `defensiveness`
- `patience`
- `regicide`
- `vendetta`
- `greed`
- `chaos`
- `attention`
- `allianceComfort`
- `fitness`
- `avgMoveMs`
- `avgPower`
- `speedGrade`
- `powerGrade`

## Current limitations

- Diplomacy/alliance state is currently lightweight and heuristic rather than full HTML parity
- No replay importer in HTML yet
- Personality values are generated for contract compatibility, not evolved meaningfully
- Champion fitness is still a lightweight heuristic

## Next steps

1. Add diplomacy/alliance state to the C++ game model.
2. Match HTML move and score exports more closely.
3. Add checkpoint and transcript file output helpers.
4. Add HTML-side import/replay for C++ logs.
