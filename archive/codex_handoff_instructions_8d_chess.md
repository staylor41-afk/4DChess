# Codex handoff instructions — 8D Chess project

## purpose

This document is a handoff for Codex to take over development of a multidimensional chess project currently centered around a large HTML/JS prototype, plus early Unity/bootstrap work and several engine/protocol experiments.

The goal is **not** to replace the existing game identity. The goal is to preserve what already works and move it into a stronger architecture that supports:

- full game rules from 2D through 8D
- playable boards and slice views
- large higher-dimensional exterior visualizations
- instrument mode for high-dimensional play
- attractive move animation
- AI simulation tooling
- evolutionary AI tooling
- Windows packaging as an executable

The HTML prototype is the most important reference point for current UX, game feel, controls, and feature scope.

---

## authoritative references

### primary source of truth
- `8d-chess-v45.23.html`
  - this file contains the current playable prototype, UI, controls, simulation tools, and much of the intended product surface.
  - preserve its identity, especially:
    - classic chess icons
    - dark neon piece glow
    - jewel-like / cosmic exterior presentation
    - slice controls
    - instrument mode
    - Sim Lab
    - Evo Lab

### important bootstrap work already created in chat
There are also generated support bundles and experiments from prior work. They are not all production-ready, but they capture useful architecture ideas:
- Unity bootstrap UI bundles (`v30`, `v31`, `v32`, `v33 clean`)
- engine protocol experiments
- scene bootstrap / diagnostics / recovery ideas
- move animation and instrument-toggle ideas
- slice caching / status export / promotion / no-legal-move handling concepts
- visual identity notes:
  - Silmaril-like exterior board
  - classic neon icons for pieces
  - jewel-like higher-D display
  - instrument-style optional aids

Treat these as design notes and partial prototypes, not final truth.

---

## product vision

### core game identity
The project is an 8D chess game that should feel like:
- classic chess in piece recognizability
- dark neon / luminous in piece styling
- jewel-like, suspended, cosmic in large higher-dimensional exterior presentation

Important visual principle:
- the **glow of the neon pieces is the light within the structure**
- the exterior presentation should not wash out playability
- the board shell can be jewel-like and luminous, but gameplay readability comes first

### playable scope
The game should support:
- 2D
- 3D
- 4D
- 5D
- 6D
- 7D
- 8D

with escalating player counts and visualization complexity.

### interaction modes
Support both:
1. **direct play mode**
2. **instrument mode**
   - especially important for 6D+
   - lets the player use slice/instrument panels to understand position in high dimensions

### research / simulation scope
The project should also support:
- headless or semi-headless simulations
- personality-based AI comparison
- evolutionary AI runs
- saved logs / replay data

---

## current state of the project

### what exists now
1. A large HTML prototype with:
   - top bar controls
   - setup overlay
   - player slots
   - multiple game modes listed in UI
   - 2D–8D mode selector
   - slice row / instrument panels
   - exterior board view
   - move log
   - Sim Lab
   - Evo Lab
   - rotation / spread controls
   - UI for trails, turn trails, sounds, exterior toggle, slice toggle, instrument toggle, save log

2. A fresh Unity project has been started.
   - bootstrap scene work was partially set up
   - recovery / diagnostics UI has been added
   - no real engine binary was successfully connected yet

3. A bootstrap-only Unity bundle was created and a clean variant (`v33 clean`) compiled in a fresh project.
   - it is board-agnostic
   - it helps startup, diagnostics, preflight, recovery
   - it does **not** provide the actual chess engine

4. An experimental protocol engine project was created.
   - it is a real engine skeleton
   - it is not a full-rules engine
   - it supports commands like `ping`, `new_game`, `get_state`, `get_slice_view`, `get_legal_moves`, `apply_move`
   - it uses N-dimensional coordinates
   - it is suitable as a protocol starting point, not as the final rules engine

### what is missing
- a full-rules engine
- a complete formalized ruleset document in code form
- a stable connection between frontend and engine
- production-grade visualization integration
- packaging pipeline for a clean Windows executable
- robust tests

---

## non-negotiable design constraints

### preserve the HTML game's identity
Do **not** redesign this into generic sci-fi chess.
Keep these:
- classic chess icon readability
- dark neon player-color glow
- jewel-like / cosmic macro presentation
- slice control and instrument feel
- simulation / evo tooling

### engine and UI separation
The final system should separate:
- **engine / rules / game state**
- **rendering / UI / visualization**
- **simulation / AI**
- **tooling / logs / exports**

### packageability
The final architecture must be easy to package as a Windows executable, ideally with:
- one frontend application
- a clearly bundled engine binary or embedded engine layer
- clear diagnostics on first run
- graceful failure when the engine is missing or broken

---

## recommended architecture

## preferred path
Use a **real engine core** plus a **client/frontend**.

### recommended layers

#### 1. engine core
Responsible for:
- board state
- move legality
- turn order
- player elimination / inheritance rules
- promotion
- king danger / check state
- no-legal-move resolution
- logs and state serialization
- simulation hooks
- AI hooks

#### 2. protocol bridge
Responsible for:
- `ping`
- `new_game`
- `get_state`
- `get_slice_view`
- `get_legal_moves`
- `apply_move`
- simulation commands
- evo lab commands
- export commands

#### 3. frontend
Responsible for:
- setup UI
- 2D–8D board controls
- slice/instrument panels
- exterior rendering
- move animation
- player HUD
- diagnostics
- settings / assists / instrument toggles

#### 4. AI / sim layer
Responsible for:
- AI personalities
- simulation batch runs
- Evo Lab breeding / mutation / scoring
- replay and metrics export

---

## rules engine requirements

Codex should create a **full-rules engine implementation** based on the user's intended game, using the HTML prototype and prior design notes as references.

### engine requirements
Implement:

- multidimensional coordinate system for 2D–8D
- board / piece representation that scales with dimension count
- configurable player count by mode
- turn order logic
- legal move generation
- capture logic
- king threat detection
- elimination logic
- inheritance of pieces on king capture if that is part of the intended rules
- promotion handling
- no-legal-move handling
- state save/load
- replay log output
- simulation-friendly execution

### rule fidelity
Codex should **read the HTML prototype closely** and infer existing implemented behavior where possible rather than inventing a totally different game.

Where rules are ambiguous:
- preserve behavior already present in the HTML prototype if discoverable
- otherwise isolate the ambiguity in a clearly marked configuration or TODO
- do not silently invent hidden rule changes

### dimensional consistency
The engine must support the full ladder:
- 2D
- 3D
- 4D
- 5D
- 6D
- 7D
- 8D

with setup generation and rendering/export support appropriate to each mode.

---

## visualization requirements

### board presentation
There should be two related but distinct visual modes:

#### 1. local / playable board view
- direct board interaction
- classic chess readability
- crisp piece icons
- clear move destinations
- move animations that feel elegant

#### 2. exterior higher-dimensional view
- visually reminiscent of a great jewel or suspended cosmic artifact
- large-scale dimensional structure should feel curated, luminous, and legible
- must not overpower the actual pieces
- piece glow should feel like the internal light source

### piece styling
Preserve:
- classic chess icons
- dark body / neon edge glow
- player-colored light
- readable silhouettes

If symbolic LOD is used at large zoom distance:
- use gem-like or sigil-like symbolic versions of the **classic piece identities**
- do not replace them with generic blobs

### move animation
Implement attractive move animation in the main 3D visualization:
- smooth arc or elegant translation
- spaces the piece moved through get one illumination effect
- landing square gets a stronger, distinct illumination effect
- the animation must stay readable and not feel flashy for its own sake

---

## instrument mode requirements

Instrument mode is important and should be treated as a first-class system, not an afterthought.

### intent
In high dimensions, the player needs tools analogous to flying on instruments:
- slice awareness
- location awareness
- warning systems
- optional aids that can be turned off

### requirements
Implement:
- viewed-slice illumination effect in the larger visualization
- warning lights / enemy threat indicators
- current player status panel
- optional toggles for these aids
- instrument preset profiles

### important interaction rule
Turning instruments off should affect **presentation only**, not:
- legality
- engine state
- threat calculation
- turn order

### presets
Support presets like:
- Full IFR
- Minimal Assist
- Exterior Beauty

Persist these settings if possible.

---

## sim lab requirements

The HTML prototype already exposes a Sim Lab concept. Preserve and strengthen it.

### required features
- run batches of AI vs AI games
- choose game sizes / dimensions
- choose max moves per game
- choose personality generation mode
- collect metrics
- save results

### desired outputs
- survival / win rates
- trait performance summaries
- logs of representative runs
- downloadable outputs for later analysis

---

## evo lab requirements

The HTML prototype already exposes Evo Lab. Preserve and strengthen it.

### required features
- population size
- generations
- games per generation
- mode choice
- move limit choice
- mutation rate
- crossover method
- survival rate
- trait caps / constraints
- champion export / import

### engine/API support needed
The engine should support:
- deterministic seeded runs when requested
- batch simulation
- personality genome representation
- mutation and crossover hooks
- fitness scoring

### outputs
- champion roster
- evolution history
- trait convergence
- downloadable JSON exports
- optional full move-log generation for selected runs

---

## packaging and deployment requirements

### Windows-first
Primary packaging target is Windows on the user's Omen PC.

### required packaging behavior
- frontend should start cleanly
- engine path should be predictable
- if engine is external, bundle it in a known location
- if startup fails, show diagnostics and recovery tools
- packaged build should surface:
  - build stamp
  - diagnostics
  - engine status
  - runtime recovery options

### keep the successful bootstrap concepts
Preserve these ideas from earlier work:
- preflight validator
- startup sequence
- diagnostics panel
- recovery panel
- runtime health overlay
- build stamp
- engine path fallback / recovery if needed

---

## clean implementation tasks for Codex

## phase 1 — audit and extraction
1. Audit `8d-chess-v45.23.html`
2. Identify:
   - current state model
   - current move rules
   - UI-only logic vs engine logic
   - AI logic
   - simulation logic
   - evo logic
3. Write a concise architecture note:
   - what exists
   - what should be extracted
   - what should remain UI-side

## phase 2 — engine creation
1. Create a proper engine module
2. Move or reimplement rules there
3. Create tests for:
   - state transitions
   - legal move generation
   - captures
   - promotion
   - elimination
   - no-legal-move behavior
4. Create a clean API/protocol

## phase 3 — frontend integration
1. Connect the frontend to the engine
2. Preserve:
   - setup overlay
   - controls
   - logs
   - slice mode
   - instrument mode
   - exterior view
3. Replace brittle direct logic with engine-backed state where needed

## phase 4 — AI / Sim / Evo
1. Extract or formalize AI personality system
2. Create batch sim support
3. Create evo support
4. Create export/save flows

## phase 5 — packaging
1. Create a reliable Windows packaging process
2. Bundle diagnostics
3. Make first-run failures recoverable
4. Document build steps

---

## explicit quality bar

Codex should optimize for:

- truthfulness over pretending completeness
- preserving the game's identity
- architectural cleanliness
- testability
- packaging reliability
- clear logs and diagnostics
- incremental milestones instead of giant rewrites with no checkpoints

---

## what not to do

- do not throw away the HTML prototype's visual identity
- do not replace classic chess icons with generic sci-fi shapes
- do not make the macro glow overpower actual play readability
- do not hard-code assumptions that only work in 2D
- do not make instrument toggles affect rules
- do not claim “full-rules” if there are unresolved rule ambiguities
- do not bury simulation and evo lab as secondary features; they are core research tools

---

## first milestone Codex should hit

The first truly valuable milestone is:

- real engine running
- frontend connected
- 2D game playable
- slice/state protocol working
- one legal move animates cleanly
- logs update correctly
- packaged Windows build launches

After that:
- extend confidently to 3D–8D
- then instrument mode
- then full Sim Lab and Evo Lab hardening

---

## final instruction to Codex

Use the uploaded HTML prototype as the primary reference artifact for the current behavior and product surface.

Preserve the game's identity:
- classic icons
- dark neon pieces
- jewel-like higher-dimensional exterior
- instrument flying feel in high dimensions
- simulation and evo tooling

Refactor toward a real engine-backed architecture without losing what makes the existing prototype special.
