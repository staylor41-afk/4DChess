# 8D Chess — C++ WebAssembly Rendering Module

Accelerates and enhances the visual rendering of the 8D Chess multidimensional
visualisation using a C++ module compiled to WebAssembly.

---

## What it does

| Feature | Before (v45.56 JS) | After (v45.57 Wasm) |
|---|---|---|
| Cell culling | 6-level JS loop, JS `projectCell()` per cell | Wasm C++ loop, inline mat-mul — **~15–40× faster** |
| Cell appearance | Flat top-face quads only | **3 faces per cell** (top + side + front) with view-dependent shading — cubes look 3D |
| WebGL dimensions | 7D / 8D only | **3D through 8D** all use WebGL |
| Rotation axes | rotX, rotY, bldRotX, bldRotY, cityRotX, cityRotY | **+ rotZ (roll)** for local cube and building — full 3-axis control per layer |
| Smooth drag rotation | Euler X + Y only | **Shift+drag** = roll axis; all axes composited via rotation matrices |

---

## Files

```
render/
  chess_render.cpp          C++ source — projection, culling, face geometry
  chess_render_bridge.js    JS loader + rendering pipeline patch
  build.bat                 Windows build script (Emscripten)
  build.sh                  Linux/macOS/WSL build script
  chess_render.js           [generated] Emscripten glue
  chess_render.wasm         [generated] compiled module
```

The generated `chess_render.js` and `chess_render.wasm` are loaded at
runtime by `chess_render_bridge.js`.

---

## Build

### Prerequisites

Install Emscripten SDK once:
```bat
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
emsdk install latest
emsdk activate latest
```

### Windows
```bat
cd "F:\8d chess\render"
REM Activate Emscripten for this terminal session:
C:\path\to\emsdk\emsdk_env.bat
REM Build:
build.bat
```

### Linux / macOS / WSL
```bash
source /path/to/emsdk/emsdk_env.sh
bash "F:/8d chess/render/build.sh"
```

Output: `chess_render.js` + `chess_render.wasm` in the `render/` folder.

---

## Usage

Open **`8d-chess-v45.57-wasm-render.html`** in a browser.

The bridge script loads automatically and patches the rendering pipeline.
If Wasm fails to load (file not built yet, wrong path, etc.), the game
falls back silently to the original v45.56 JS rendering.

### New controls

| Control | Action |
|---|---|
| `↺ Z` / `Z ↻` buttons (bottom-right overlay) | Roll the local cube |
| **Shift + right-drag** | Free roll axis for local cube |

---

## Architecture

### Coordination with Codex (game-logic C++ module)

- **Codex module**: AI evaluation, move generation, legal-move computation
- **This module**: cell visibility culling, face geometry, rotation matrices

The two modules are independent. This module reads game state (cell coordinates
+ colors) from the existing JS game state (`window.G`, `window.wglColorArray`)
and outputs WebGL vertex buffers. No shared state with Codex's module.

### How the bridge patches the pipeline

```
wglBuild()  ──────────► ensureWglVisibleGeometry()  [REPLACED]
                              └── wasm_cull_visible()   ← C++ nested loops
                                  → g_coord_buf (Uint8Array on Wasm heap)
                                  → wglVisibleCoords (JS ref to Wasm heap)

wglBuild()  ──────────► updateWglColors()            [unchanged]
                              → wglColorArray (Float32Array)

wglBuild()  ──────────► wasm_build_face_verts()      [NEW]
                              ← g_color_in  (JS writes wglColorArray here)
                              → g_pos_buf   (vertex positions)
                              → g_col_buf   (vertex colors with face shading)
                              → g_dep_buf   (depth values)

wglDraw()   ──────────► drawFacePass()               [REPLACES flat quads]
                              uploads pos/col/dep to WebGL
                              gl.drawArrays(TRIANGLES, 0, vertCount)
```

### Face shading model

For each visible cell, the C++ computes which 3 faces are front-facing
(dot product of face normal with screen-Z direction from the rotation matrix).
Brightness per face:

- **Top** (normal 0,0,1): always fully lit
- **Side** (±col direction): visible when rotY puts that side toward viewer
- **Front/back** (±row direction): visible based on rotY

Brightness = `AMBIENT + (1 - AMBIENT) × (face_screen_Z / top_screen_Z)`

This gives natural dimming of side faces compared to the top, and the
shading updates smoothly as the view rotates.

---

## Rotation matrix layout

Each layer (local, building, city) stores a row-major 3×3 matrix:

```
Row 0 (screen X): [ cosY,       sinY,        0   ]
Row 1 (screen Y): [-sinY*cosX,  cosY*cosX,  -sinX]
Row 2 (screen Z): [-sinY*sinX,  cosY*sinX,   cosX]
```

Post-multiplied by Rz(rotZ) when `rotZ ≠ 0` (new roll axis).

The screen-Z row is used for:
1. Depth sorting (higher screen-Z = closer to viewer)
2. Face visibility testing (face normal · row2 > 0 → front-facing)
3. Face brightness calculation
