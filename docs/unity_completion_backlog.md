# Unity Completion Backlog

## Core engine integration
- Keep the C++ engine as the single source of truth for rules, turn order, captures, promotion, castling, en passant, and legality.
- Expand protocol coverage for richer view requests, trail data, move metadata, and simulation data needed by the Unity frontend.
- Add stronger protocol error reporting and engine crash recovery paths in the Unity bridge.
- Add deterministic regression test cases that compare Unity-visible state against known engine responses.

## Presentation architecture
- Keep `2D board`, `3D/4D exterior`, and `slice/instrument` as distinct presentation paths under one coordinator.
- Finish making higher-dimensional `Slice` mode equal `Exterior + docked instruments` instead of a separate world-space mode.
- Move any remaining higher-dimensional display-specific logic out of individual presenters into shared controllers.
- Add persistent presentation preferences so the game can remember whether the player prefers exterior, slice, or auto mode.

## Interaction model
- Finish making the exterior renderer fully interactive, using the shared `GameInteractionController`.
- Support selecting pieces from exterior view, slice view, and 2D board view without diverging rules/UI state.
- Add shared highlight, legal-target, capture-target, and check-warning data that all renderers can consume.
- Add hover feedback and explicit invalid-move feedback in Unity.
- Add keyboard shortcuts and on-screen affordances for common presentation and slice operations.

## Slice instrument system
- Replace prototype world-space slice panels for 3D/4D with fully docked HTML-style slice windows.
- Preserve user slice axis/fixed-axis choices across refreshes, moves, and mode switches.
- Add more HTML-like slice controls: named slice windows, axis toggles, fixed-axis sliders, live badges, and clearer coordinate summaries.
- Add “follow selection” behavior so slice windows can optionally track the selected piece’s neighborhood.
- Add fallback slice-grid behavior for 5D-8D while the full docked instrument system grows upward.

## Camera and navigation
- Add presentation-owned camera presets with better initial focus, yaw, pitch, and zoom per dimension/mode.
- Add stable first-run framing for 3D and 4D that matches the intended HTML composition more closely.
- Improve orbit/pan/zoom behavior when switching between exterior and slice-assisted views.
- Add named camera presets and reset-view controls.

## Higher-dimensional exterior renderer
- Replace placeholder exterior geometry with HTML-faithful glass-like hanging boards and luminous lattice framing.
- Correct coordinate-to-world placement for 3D and 4D so pieces occupy the expected planes and clusters.
- Improve cluster spacing and composition for the 4D “line of 8 cubes in D”.
- Expand the dedicated exterior renderer beyond 4D once 3D/4D are stable.
- Add depth treatment so pieces remain legible inside the exterior structure.

## Piece rendering
- Replace letter glyphs with proper chess silhouettes or sprite billboards.
- Add stronger piece glow, core light, halo, and readable distance scaling.
- Preserve player color identity across all renderers.
- Add selection and danger treatment that reads clearly without overwhelming the board.

## UI and game flow
- Continue matching the HTML startup and in-game UI layout while keeping the faster engine backend.
- Add proper runtime mode toggle buttons instead of relying mainly on keyboard shortcuts.
- Add player list, score, move log, and pawn-set panels in a more HTML-faithful way.
- Add new game, pause, trails, turn trails, and slice controls in the Unity UI layer.

## Trails, logs, and readability aids
- Add shared move trail data and turn trail rendering.
- Add check warnings, king danger, and threat/readability instruments.
- Add move log formatting that mirrors the HTML game more closely.
- Add optional “beauty mode” vs “instrument-heavy mode” presets.

## Performance
- Cache slice requests and invalidate only what is needed after each move.
- Avoid rebuilding entire exterior hierarchies when only highlights or selection change.
- Add object pooling for markers, captions, and piece visuals.
- Profile Unity allocations and rendering cost for 4D+ boards.

## Testing and validation
- Add play-mode sanity tests for mode switching, new game startup, and engine refresh flow.
- Add regression checks for selection -> legal moves -> apply move across board, exterior, and slice instruments.
- Verify that each supported dimension starts in a sane presentation and camera pose.
- Test packaged Windows builds, not just the Unity editor path.

## Release prep
- Clean up debug/prototype UI that should not ship.
- Add version/build display tied to engine and Unity client revisions.
- Document folder layout and packaging steps for the engine executable alongside the Unity build.
- Prepare a prioritized art pass after the structural path is stable.
