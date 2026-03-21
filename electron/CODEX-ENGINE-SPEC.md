# 8D Chess — C++ Engine Server Spec for Codex

## Overview

The game HTML (`8d-chess-v45.68-diplomacy-comms-visible.html`) contains a client called
the **ENGINE_BRIDGE** that makes HTTP GET requests to `http://127.0.0.1:8765`.

Your C++ binary (`chess_engine.exe` / `chess_engine`) must:
1. Start an HTTP server on `127.0.0.1:8765`
2. Respond to `GET /ping` with status 200 (used by Electron to know you're ready)
3. Respond to all the endpoints below with `Content-Type: application/json`
4. Always include CORS header: `Access-Control-Allow-Origin: *`

The engine bridge only activates for **6D, 7D, and 8D chess** games.
For 2D–5D the game uses its built-in JS engine, so those modes are unaffected.

---

## Coordinate Format

Coordinates are **0-indexed comma-separated integers**, one value per dimension.

Examples (8D):
- `0,0,0,0,0,0,0,0` = a1 in the first city, building, floor, row, etc.
- `7,7,0,0,0,0,0,0` = h8 on the first board

The number of dimensions N = 6, 7, or 8 depending on game mode.

Axis names (for display only, not used in coords): `x y z w v u t s`

---

## REST API Endpoints

### `GET /ping`
Health check. Return HTTP 200 with body `{"ok":true}`.
Called by Electron's `waitForEngine()` to know when you're ready to accept games.

---

### `GET /new_game?dimensions=N&diplomacy=0|1&comm=human|ai`
Start a new game.

| Param       | Type    | Description                              |
|-------------|---------|------------------------------------------|
| `dimensions`| int     | 6, 7, or 8                               |
| `diplomacy` | 0 or 1  | Whether diplomacy mode is active         |
| `comm`      | string  | `human` = human vs human, `ai` = include AI players |

**Response:**
```json
{
  "ok": true,
  "players": 4,
  "dimensions": 8
}
```

---

### `GET /apply_move?from=COORD&to=COORD`
Apply a move. Both `from` and `to` are URL-encoded comma-separated coords.

Example: `/apply_move?from=0%2C0%2C0%2C0%2C0%2C0%2C0%2C0&to=0%2C1%2C0%2C0%2C0%2C0%2C0%2C0`
(i.e., `from=0,0,0,0,0,0,0,0&to=0,1,0,0,0,0,0,0`)

**Response:**
```json
{
  "ok": true,
  "capture": false,          // true if a piece was captured
  "next_player": 1           // 0-indexed player index whose turn it is now
}
```

---

### `GET /legal_moves?from=COORD`
Return all legal destination coordinates for the piece at `from`.

**Response:**
```json
{
  "ok": true,
  "moves": [
    { "to": [0,1,0,0,0,0,0,0], "cap": false },
    { "to": [0,2,0,0,0,0,0,0], "cap": false }
  ]
}
```
Each move: `to` is an array of ints (one per dimension), `cap` is true if it's a capture.

---

### `GET /turn_analysis?player=PID&mode=threats`
Analyse threats against player `PID` (0-indexed).

**Response:**
```json
{
  "ok": true,
  "player": 0,
  "threats": [
    { "from": [3,3,0,0,0,0,0,0], "to": [3,7,0,0,0,0,0,0], "attacker_pid": 1 }
  ],
  "in_check": false
}
```

---

### `GET /player_summaries`
High-level summary of all players (material, threat level, etc.).

**Response:**
```json
{
  "ok": true,
  "summaries": [
    { "pid": 0, "name": "P1", "material": 39, "threat_level": 0.2, "alive": true },
    { "pid": 1, "name": "P2", "material": 39, "threat_level": 0.1, "alive": true }
  ]
}
```

---

### `GET /player_intel?player=PID`
Detailed intelligence for player `PID`.

**Response:**
```json
{
  "ok": true,
  "pid": 0,
  "pieces": [
    { "type": "Queen", "coord": [3,0,0,0,0,0,0,0], "value": 9 }
  ],
  "controlled_cells": 42
}
```

---

### `GET /diplomacy_snapshot`
Current diplomacy state (alliances, wars, treaties).

**Response:**
```json
{
  "ok": true,
  "relations": [
    { "pid_a": 0, "pid_b": 1, "status": "neutral" },
    { "pid_a": 0, "pid_b": 2, "status": "allied" }
  ]
}
```
Status values: `"neutral"`, `"allied"`, `"war"`.

---

### `GET /apply_ai_turn?player=PID`
Have the AI compute and apply a move for player `PID`.

**Response:**
```json
{
  "ok": true,
  "from": [3,0,0,0,0,0,0,0],
  "to":   [3,4,0,0,0,0,0,0],
  "capture": false,
  "next_player": 1
}
```

---

### `GET /visible_cells?...`  (optional / advanced)
Returns which cells are visible to a given player in fog-of-war mode.
Query params vary; the game constructs them automatically.

**Minimum viable response:**
```json
{ "ok": true, "cells": null }
```
Returning `null` cells disables fog-of-war and shows the full board.

---

### `GET /slice_view?view=...&fixed=...`  (optional / advanced)
Returns a 2D slice of the N-D board for the panel display.

**Minimum viable response:**
```json
{ "ok": true, "slice": null }
```
Returning `null` falls back to the JS slice renderer.

---

## Error responses

If anything goes wrong, return HTTP 200 (not 500) with:
```json
{ "ok": false, "error": "description of what went wrong" }
```
The game handles `ok: false` gracefully and disables the bridge for that session.

---

## CORS headers (required on every response)

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET
Content-Type: application/json
```

---

## Electron integration

The Electron `main.js`:
1. Looks for `chess_engine.exe` (Windows) or `chess_engine` (Unix) in the folder
   **next to** the `electron/` directory (i.e., in `F:/8d chess/`).
2. Spawns it with `--port 8765`.
3. Polls `GET /ping` until 200, then opens the game window.
4. Kills the process cleanly when the user closes the app.

Your binary should:
- Accept `--port N` command-line argument
- Bind to `127.0.0.1` only (not 0.0.0.0)
- Exit cleanly on SIGTERM / Ctrl+C
- Log to stdout (Electron pipes this to the dev console)

---

## Recommended C++ HTTP library

For a quick implementation: **cpp-httplib** (single-header, MIT license)
```
https://github.com/yhirose/cpp-httplib
```

Minimal server skeleton:
```cpp
#define CPPHTTPLIB_OPENSSL_SUPPORT  // remove if no TLS needed
#include "httplib.h"
#include <nlohmann/json.hpp>        // or any JSON lib

int main(int argc, char** argv) {
    int port = 8765;
    for (int i = 1; i < argc - 1; i++)
        if (std::string(argv[i]) == "--port") port = std::stoi(argv[i+1]);

    httplib::Server svr;

    // CORS helper
    auto cors = [](httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET");
    };

    svr.Get("/ping", [&](const httplib::Request&, httplib::Response& res) {
        cors(res);
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Get("/new_game", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);
        int dims = std::stoi(req.get_param_value("dimensions"));
        // … initialise your game state …
        res.set_content("{\"ok\":true}", "application/json");
    });

    // … add remaining endpoints …

    std::cout << "[engine] Listening on 127.0.0.1:" << port << std::endl;
    svr.listen("127.0.0.1", port);
    return 0;
}
```

---

## Quick-start for Codex

1. Build `chess_engine.exe` and place it in `F:/8d chess/`
2. `cd "F:/8d chess/electron" && npm install && npm start`
3. The game window opens; start a **6D, 7D, or 8D Chess** game
4. Open DevTools (Ctrl+Shift+I) → Console; look for `ENGINE_BRIDGE_ACTIVE = true`
5. `/ping`, `/new_game`, `/legal_moves` are the first three to get working

The rendering (WebGL + chess_render.wasm) is handled entirely by Claude's
`render/chess_render_bridge.js` and has nothing to do with the engine server.
