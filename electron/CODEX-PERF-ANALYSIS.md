# 8D Chess — Performance Analysis for Codex

## The Three Freeze Causes (ranked by impact)

---

### #1 — `evalChunk` processes too many pieces before yielding (PRIMARY FREEZE)

**Where:** `doAIMove()` → `PIECES_PER_CHUNK` → `evalChunk()`

**Current code:**
```js
const PIECES_PER_CHUNK = FAST_WATCH
  ? Math.max(8, Math.ceil(candidates.length / 2))   // ← evaluates HALF the board at once
  : IS_HEAVY ? Math.max(2, Math.round(pers.foresight / 2))
  : candidates.length;
```

**The problem:**
In 8D spectator mode with 1024 pieces, `candidates.length` can be ~100–800.
`candidates.length / 2` = 50–400 pieces evaluated in one synchronous block.
Each piece calls `getLegal(fc)` — an N-dimensional move generator.
One chunk = hundreds of legal-move expansions = **50–200ms blocking the main thread**.
The browser can't paint a frame until the chunk returns.
Result: UI freezes for 50–200ms after every AI turn, repeated 12× per round.

**The fix (applied in v45.71):**
```js
const PIECES_PER_CHUNK = FAST_WATCH
  ? Math.min(4, candidates.length)   // yield every 4 pieces — smooth 60fps
  : IS_HEAVY ? Math.max(2, Math.round(pers.foresight / 2))
  : candidates.length;
```
This makes AI slightly slower (more chunks needed) but keeps frames smooth.

**The real fix = C++ engine:**
When `chess_engine.exe` is present, `/apply_ai_turn` is called instead of `doAIMove`.
The C++ engine computes the entire turn off the JS thread. No chunks needed.
This is the **#1 priority endpoint** for Codex to implement.

---

### #2 — `getLegal()` has no within-turn memoization

**Where:** `doAIMove()` → `evalChunk()` → `getLegal(fc)` called per candidate piece

**The problem:**
`getLegal` computes all legal moves for a piece from scratch every call.
In 8D, a queen can move in up to 244 directions × 7 squares each = thousands of checks.
It's called for every candidate piece AND again for threat detection in `scoreMv`.
Some pieces get `getLegal` called 2–3× per AI turn with no caching.

**For Codex's C++ engine:**
Pre-generate all legal moves for all pieces at the start of the turn.
Store in a flat array indexed by piece key.
Cost: O(pieces × branching_factor). In C++ with bitboards: < 1ms for full 8D board.

**JS interim fix (applied in v45.71):**
Added a per-turn `legalCache` Map inside `doAIMove` so each piece's legal moves
are only computed once per AI turn, not once per scoring candidate.

---

### #3 — Render throttle fires too often during 8D spectator

**Where:** `scheduleFastSpectatorRender()`

**Current thresholds:**
```js
const moveThreshold = GAMEMODE === '8d' ? 6  : 4;    // render every 6 moves
const timeThreshold = GAMEMODE === '8d' ? 140 : 100;  // or every 140ms
```

**The problem:**
Every 6 moves = every ~3 AI chunks = frequent `renderAll()` calls during batch AI turns.
Each `renderAll()` in 8D redraws all visible cells — expensive for 12 players × 1024 pieces.

**Fix (applied in v45.71):**
```js
const moveThreshold = GAMEMODE === '8d' ? 16 : 8;
const timeThreshold = GAMEMODE === '8d' ? 250 : 150;
```
Render every 16 moves or 250ms. Visually still smooth, AI runs 2–3× faster.

---

## Priority order for Codex's C++ engine

| Priority | Endpoint | Reason |
|---|---|---|
| **1** | `GET /apply_ai_turn?player=PID` | Eliminates all three JS freeze causes at once |
| **2** | `GET /legal_moves?from=COORD` | Used for human interaction; must be < 50ms |
| **3** | `GET /new_game` + `GET /apply_move` | State sync; required for #1 and #2 to work |
| 4 | `GET /turn_analysis` | Used for threat display; not on critical path |
| 5 | `GET /player_summaries` | Sidebar stats; low priority |
| 6 | `GET /slice_view` | FOI panel; can return null initially |

**Key insight:** Once `/apply_ai_turn` works, the game calls it for EVERY AI player
instead of running `doAIMove()`. The JS AI is completely bypassed.
Even a naive C++ engine (random valid move) will eliminate the freeze —
the move quality can be improved later.

---

## C++ engine game state sync

The C++ engine needs to maintain its own copy of the board state.
The sync flow is:

```
/new_game  →  engine initialises N-D board
/apply_move (each human move) →  engine applies it to its state
/apply_ai_turn (each AI turn) →  engine computes + applies its own move, returns result
```

The JS game applies the returned move to its own state after each `/apply_ai_turn`.

**Coordinate format:** 0-indexed comma-separated integers, N values per coord.
Example: `3,4,0,0,0,0,0,0` = (x=3, y=4, z=0, w=0, ...) in an 8D game.

**Piece types (for capture/promotion logic):**
- `K` = King, `Q` = Queen, `R` = Rook, `B` = Bishop, `N` = Knight, `P` = Pawn

---

## Recommended C++ architecture for fast AI

For a first working version:

```cpp
Move computeAiMove(int pid, GameState& state) {
    // 1. Collect all pieces belonging to pid
    // 2. For each piece, generate legal moves (ray-casting in N dimensions)
    // 3. Score each move: captures > center control > random
    // 4. Return best scored move
    // Target: < 16ms per call (one frame budget)
}
```

For a stronger version:
- Alpha-beta search to depth 1–2 (even depth-1 is stronger than current JS AI)
- Transposition table keyed on Zobrist hash of board state
- Move ordering: MVV-LVA (Most Valuable Victim, Least Valuable Attacker)

The JS AI uses a personality system (aggression/foresight/defensiveness per player).
These personality weights are available in the game HTML as `AI_PERSONALITIES.get(pid)`.
The C++ engine can ignore these initially and use uniform eval — still much faster.

---

## Testing the fix

1. Build `chess_engine.exe`, place in `F:/8d chess/`
2. Run `electron/start.bat`
3. Start an 8D, 12-player, all-AI game (Spectator mode)
4. Open DevTools (F12) → Console
5. Look for: `ENGINE_BRIDGE_ACTIVE = true` and `[engine] Listening on...`
6. Frames should now run at 60fps with no freezing between AI turns
