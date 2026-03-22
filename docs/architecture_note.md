# 8D Chess C++ Engine Architecture Note

## Extracted From The HTML Prototype

The prototype behavior in `8d-chess-v45.23.html` establishes these engine-facing rules:

- Chess modes run from 2D through 8D.
- Player count is `2^(dimensions - 1)`.
- Each player owns one back rank at `x = 0` on a hypercube corner across the remaining axes.
- Back-rank layout is classical chess: `R N B Q K B N R`.
- Pawns are generated once per unique direction from a player's corner toward opponent corners.
- Pawn forward motion uses that direction vector.
- Pawn capture advances one forward step plus one orthogonal step on any axis not already used by the forward vector.
- Promotion is deterministic:
  - promote to queen if the player has no queen
  - otherwise promote to knight
- Capturing a king eliminates that player and transfers all of the victim's non-king pieces to the captor.
- Slice and instrument systems are presentation aids, not rules.
- The prototype's simulation path eliminates a player who starts their turn with no legal moves.

## Engine/UI Split

This C++ drop keeps rules and state in the engine, and leaves these systems on the Unity side:

- setup overlay and player slot UI
- exterior jewel/cosmic rendering
- slice panel layout and instrument presentation
- move animation, glow, trails, audio
- diagnostics, recovery, and packaging UX

The engine exposes enough data to drive those systems:

- full state snapshots
- legal moves
- move application
- slice-filtered board queries
- deterministic seeded simulation hooks

## Protocol Surface

The included protocol executable currently supports line-oriented commands:

- `ping`
- `new_game dimensions=<2-8> [seed=<int>]`
- `get_state`
- `get_slice_view view=0,1 fixed=2:0,3:7`
- `get_legal_moves from=0,1`
- `apply_move from=0,1 to=0,2`
- `simulate_games count=<n> dimensions=<2-8> max_moves=<n> [seed=<int>]`

Responses are JSON so Unity can parse them without depending on the HTML prototype.

## Ambiguities Kept Explicit

These areas are intentionally isolated for later tuning rather than hidden behind invented rules:

- no castling
- no en passant
- no check-based legality filtering yet
- no formal checkmate/stalemate distinction beyond the prototype-aligned "no legal moves" elimination rule
- AI is heuristic and simulation-focused, not yet a production-strength search engine

Those are the main places to extend once the Unity bridge is wired and validated against the prototype visually.
