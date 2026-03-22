# C++ Migration Roadmap

This roadmap tracks the staged transfer of heavy simulation work from the HTML game to the headless C++ engine.

## Goal

Keep the HTML build focused on:

- rendering
- camera/navigation
- replay controls
- logs and player inspection

Move heavy work to C++:

- move generation
- move selection
- diplomacy
- alliance/trust updates
- replay generation
- Evo batch running
- board-state analysis

## Priority Order

### Phase 1: Live AI turn service

- Expose C++ protocol commands for:
  - current-player AI move suggestion
  - turn threat summary
  - legal move lists for selected pieces
- Use this first for spectator-mode AI turns.

### Phase 2: Gameplay analysis offload

- king threat / attacked-square analysis
- elimination checks
- score and piece-count summaries
- board leader / threat ranking

### Phase 3: Diplomacy offload

- message selection
- public/private channel decisions
- trust/alliance bookkeeping
- betrayal memory
- recipient fatigue / anti-repetition logic

### Phase 4: Replay-first high-D pipeline

- full offline replay export
- checkpoint states
- event markers
- segmented move/message history
- HTML replay theater

### Phase 5: Visual-prep offload

- slice payloads
- occupancy summaries
- ghost-board hint payloads
- player intel summaries

### Phase 6: Full live engine mode

- HTML becomes a thin renderer/UI layer
- C++ becomes the authoritative game engine for AI games

## Hotspots Still In HTML

- `getLegal(...)` across many candidate pieces
- enemy threat scans for king danger
- AI move scoring loops
- diplomacy prep and trust propagation
- repeated slice threat analysis
- Evo Lab simulation loops

## Immediate Next Integration

1. Wire `suggest_ai_turn` into the C++ protocol.
2. Let the HTML call it for spectator/high-D AI turns.
3. Compare browser responsiveness before/after.
