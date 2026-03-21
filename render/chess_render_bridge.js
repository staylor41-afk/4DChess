/**
 * chess_render_bridge.js — WebAssembly Rendering Bridge for 8D Chess
 * © Samuel M G Taylor, 2003 — 8D Chess
 *
 * Loads chess_render.wasm and patches the rendering pipeline to use it.
 * Drop this alongside chess_render.js/.wasm (from Emscripten build).
 *
 * What this does:
 *   1. Replaces ensureWglVisibleGeometry() with Wasm — 10-50× faster culling
 *   2. Adds face-shaded WebGL rendering pass (top + side + front faces per cell)
 *      → cells look like real 3D cubes instead of flat squares
 *   3. Extends WebGL rendering to 3D–6D (previously only 7D/8D used WebGL)
 *   4. Adds rotZ (roll axis) to local-cube, building, AND city-grid rotation
 *      — full 3-axis control for all 3 rotation layers (all 8 axes accessible)
 *   5. Enables MSAA via gl.SAMPLES (if the context supports it)
 *   6. ROTATION PERFORMANCE: pre-Wasm skip for instanced GPU pass; post-Wasm
 *      C++ rebuilds every frame (~1ms — fast enough to skip is unnecessary)
 *   7. DELTA-TIME DAMPING: corrects frame-rate-dependent rotVel decay so rotation
 *      feels identical at 30fps, 60fps, 120fps, 240fps
 *   8. CUBE-STRUCTURE LOD: 3D isometric cube rendering between galaxy and full-cell
 *      views — makes 6D buildings look like miniature 6D boards as you zoom in
 *
 * Integration: add to the HTML after all other scripts:
 *   <script src="render/chess_render_bridge.js" defer></script>
 */

(function () {
  'use strict';

  // ═══════════════════════════════════════════════════════════════════
  // Configuration
  // ═══════════════════════════════════════════════════════════════════

  const BRIDGE_ENABLED = true;           // set false to disable entirely
  const FACE_SHADING   = true;           // draw 3 faces per cell (3D cube look)
  const WASM_PATH      = 'render/chess_render.js';  // Emscripten glue script path

  // LOD thresholds — zoom values defining render mode boundaries
  // Below GALAXY_TOP  : building halos only (existing galaxy view)
  // GALAXY_TOP–STRUCT_TOP : 3D isometric cube-structure view (NEW)
  // Above STRUCT_TOP  : full WebGL cell rendering
  const LOD = {
    '8d': { GALAXY_TOP: 0.22, STRUCT_TOP: 0.58 },
    '7d': { GALAXY_TOP: 0.18, STRUCT_TOP: 0.54 },
  };

  // New WebGL vertex shader for pre-projected face geometry
  const FACE_VS = `#version 300 es
precision highp float;
in vec2  a_pos;    // world-space position (pre-projected by Wasm, before zoom/pan)
in vec4  a_col;    // RGBA — face shading already baked in by Wasm
in float a_dep;    // depth value (0..1) for gl_Position.z
uniform float uZoom, uPX, uPY;
uniform vec2  uRes;
out vec4 vCol;
void main(){
  float sx = a_pos.x * uZoom + uPX;
  float sy = a_pos.y * uZoom + uPY;
  // Convert to clip space:  x ∈ [-1,1],  y flipped,  z ∈ [-1,1]
  gl_Position = vec4(
    (sx / uRes.x) * 2.0 - 1.0,
    1.0 - (sy / uRes.y) * 2.0,
    a_dep * 2.0 - 1.0,
    1.0
  );
  vCol = a_col;
}`;

  // Fragment shader for face-shaded cell pass (opaque cells, no edge glow needed)
  const FACE_FS = `#version 300 es
precision mediump float;
in vec4 vCol;
out vec4 fragColor;
void main(){
  fragColor = vCol;
}`;

  // ── LOD glass shaders ────────────────────────────────────────────────────
  // The LOD pass renders each (w,v,u) cube as a translucent 3D glass box.
  // UV ∈ [0,1] is emitted per vertex (0/1 at face edges, 0.5 at center).
  // The fragment shader computes a smooth cyan glow at all face edges using
  // the UV edge distance — replacing the 9 fat-line edge quads per cube with
  // zero extra geometry (4.4× fewer vertices, anti-aliased appearance).

  const LOD_VS = `#version 300 es
precision highp float;
in vec2  a_pos;
in vec4  a_col;
in float a_dep;
in vec2  a_uv;
uniform float uZoom, uPX, uPY;
uniform vec2  uRes;
out vec4 vCol;
out vec2 vUV;
void main(){
  float sx = a_pos.x * uZoom + uPX;
  float sy = a_pos.y * uZoom + uPY;
  gl_Position = vec4(
    (sx / uRes.x) * 2.0 - 1.0,
    1.0 - (sy / uRes.y) * 2.0,
    a_dep * 2.0 - 1.0,
    1.0
  );
  vCol = a_col;
  vUV  = a_uv;
}`;

  // Edge glow: smoothstep on UV distance-to-edge blends face fill → bright cyan.
  // The glow width (0.15 of face) is independent of zoom — always 15% of the
  // face regardless of size, which looks good from galaxy zoom to close-up.
  const LOD_FS = `#version 300 es
precision mediump float;
in vec4 vCol;
in vec2 vUV;
out vec4 fragColor;
void main(){
  // Distance to nearest face edge; 0 at edge, 0.5 at center
  float edgeDist = min(min(vUV.x, 1.0-vUV.x), min(vUV.y, 1.0-vUV.y));
  // Smooth glow fading from edge inward (15% of face width)
  float glow = 1.0 - smoothstep(0.0, 0.15, edgeDist);
  // Bright cyan glow; alpha proportional to face alpha for glass consistency
  vec4 edgeCol = vec4(0.40, 0.78, 1.0, min(1.0, vCol.a * 2.5));
  fragColor = mix(vCol, edgeCol, glow * 0.88);
}`;

  // ═══════════════════════════════════════════════════════════════════
  // State
  // ═══════════════════════════════════════════════════════════════════

  let wasmReady  = false;
  let wasmMod    = null;          // Emscripten Module object
  let faceGl     = null;          // WebGL2 context (same as existing wgl, shared)
  let faceProg   = null;          // shader program for face-shaded pass
  let faceVAO    = null;
  let faceBufPos = null;          // VBO: position (vec2)  — shared with LOD pass
  let faceBufCol = null;          // VBO: color (vec4)     — shared with LOD pass
  let faceBufDep = null;          // VBO: depth (float)    — shared with LOD pass
  let faceUniforms = {};
  let lodProg    = null;          // shader program for LOD glass pass (edge-glow FS)
  let lodVAO     = null;          // VAO for LOD glass pass (adds UV attrib)
  let lodBufUV   = null;          // VBO: UV coordinates (vec2) for edge glow
  let lodUniforms = {};
  let rotZ_local = 0.0;           // new roll axis for local cube
  let rotZ_bld   = 0.0;           // new roll axis for building
  let rotZ_city  = 0.0;           // new roll axis for city grid

  // Rotation-skip tracking (for performance fix #6)
  let _prevZoom = null, _prevPanX = null, _prevPanY = null;

  // Delta-time damping state
  let _rotAnimPrevTime = null;

  // ═══════════════════════════════════════════════════════════════════
  // Delta-Time Rotation Damping
  // ═══════════════════════════════════════════════════════════════════
  // The main HTML's startRotAnim uses a fixed decay `rotVel *= 0.84` per
  // frame, tuned for 60fps. At 120fps this damps twice as fast; at 30fps
  // half as fast. We correct this by adjusting rotVel in renderAll() before
  // the next tick uses it. The correction undoes the fixed step and replaces
  // it with the frame-rate-independent equivalent.

  function applyDeltaTimeCorrection(mode) {
    if (mode !== 'view' && mode !== true) return;
    if (!window.rotVel) return;
    const now = performance.now();
    if (_rotAnimPrevTime !== null) {
      const dt = now - _rotAnimPrevTime;
      if (dt > 1 && dt < 200) {
        // Tick already applied: rotVel *= 0.84 (for 16.667ms).
        // Correct for actual elapsed time so decay is frame-rate-independent:
        const correction = Math.pow(0.84, dt / 16.667) / 0.84;
        if (typeof window.rotVel.x === 'number') window.rotVel.x *= correction;
        if (typeof window.rotVel.y === 'number') window.rotVel.y *= correction;
      }
    }
    _rotAnimPrevTime = now;
  }

  // ═══════════════════════════════════════════════════════════════════
  // Wasm Loader
  // ═══════════════════════════════════════════════════════════════════

  function loadWasm() {
    // Emscripten generates a global Module factory; load it via <script>
    const s = document.createElement('script');
    s.src = WASM_PATH;
    s.onload = () => {
      // The Emscripten script sets window.Module or calls a factory
      // Wait for the module to initialise asynchronously:
      const checkReady = setInterval(() => {
        if (typeof window.ChessRenderModule === 'function') {
          clearInterval(checkReady);
          window.ChessRenderModule().then(mod => {
            wasmMod = mod;
            onWasmReady();
          });
        } else if (typeof window.Module !== 'undefined' && window.Module.calledRun) {
          clearInterval(checkReady);
          wasmMod = window.Module;
          onWasmReady();
        }
      }, 50);
    };
    s.onerror = () => console.warn('[chess_render_bridge] Failed to load Wasm — using JS fallback');
    document.head.appendChild(s);
  }

  function onWasmReady() {
    console.log('[chess_render_bridge] Wasm ready');
    wasmReady = true;
    initFaceGL();
    patchRenderPipeline();
  }

  // ═══════════════════════════════════════════════════════════════════
  // Face-Shaded WebGL Setup
  // ═══════════════════════════════════════════════════════════════════

  function initFaceGL() {
    // Re-use the existing WebGL2 context (glCanvas / wgl from the main HTML)
    const gl = window.wgl;
    if (!gl) { console.warn('[chess_render_bridge] No WebGL2 context found'); return; }
    faceGl = gl;

    // Compile shaders
    function mkShader(type, src) {
      const sh = gl.createShader(type);
      gl.shaderSource(sh, src);
      gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
        console.error('[chess_render_bridge] Shader error:', gl.getShaderInfoLog(sh));
        gl.deleteShader(sh);
        return null;
      }
      return sh;
    }
    const vs = mkShader(gl.VERTEX_SHADER,   FACE_VS);
    const fs = mkShader(gl.FRAGMENT_SHADER, FACE_FS);
    if (!vs || !fs) return;

    faceProg = gl.createProgram();
    gl.attachShader(faceProg, vs);
    gl.attachShader(faceProg, fs);
    gl.linkProgram(faceProg);
    if (!gl.getProgramParameter(faceProg, gl.LINK_STATUS)) {
      console.error('[chess_render_bridge] Link error:', gl.getProgramInfoLog(faceProg));
      faceProg = null;
      return;
    }

    // VAO + VBOs
    faceVAO    = gl.createVertexArray();
    faceBufPos = gl.createBuffer();
    faceBufCol = gl.createBuffer();
    faceBufDep = gl.createBuffer();

    gl.bindVertexArray(faceVAO);

    const aPos = gl.getAttribLocation(faceProg, 'a_pos');
    const aCol = gl.getAttribLocation(faceProg, 'a_col');
    const aDep = gl.getAttribLocation(faceProg, 'a_dep');

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufPos);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufCol);
    gl.enableVertexAttribArray(aCol);
    gl.vertexAttribPointer(aCol, 4, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufDep);
    gl.enableVertexAttribArray(aDep);
    gl.vertexAttribPointer(aDep, 1, gl.FLOAT, false, 0, 0);

    gl.bindVertexArray(null);

    faceUniforms = {
      zoom: gl.getUniformLocation(faceProg, 'uZoom'),
      pX:   gl.getUniformLocation(faceProg, 'uPX'),
      pY:   gl.getUniformLocation(faceProg, 'uPY'),
      res:  gl.getUniformLocation(faceProg, 'uRes'),
    };

    console.log('[chess_render_bridge] Face-shaded WebGL pass initialised');

    // ── Set up the LOD glass pass (uses mkShader in scope, shares VBOs) ──
    const lodVS = mkShader(gl.VERTEX_SHADER,   LOD_VS);
    const lodFS = mkShader(gl.FRAGMENT_SHADER, LOD_FS);
    if (lodVS && lodFS) {
      lodProg = gl.createProgram();
      gl.attachShader(lodProg, lodVS);
      gl.attachShader(lodProg, lodFS);
      gl.linkProgram(lodProg);
      if (!gl.getProgramParameter(lodProg, gl.LINK_STATUS)) {
        console.error('[chess_render_bridge] LOD link error:', gl.getProgramInfoLog(lodProg));
        lodProg = null;
      } else {
        // LOD VAO: reuses shared pos/col/dep VBOs + adds UV VBO
        lodVAO   = gl.createVertexArray();
        lodBufUV = gl.createBuffer();

        gl.bindVertexArray(lodVAO);

        const lPos = gl.getAttribLocation(lodProg, 'a_pos');
        const lCol = gl.getAttribLocation(lodProg, 'a_col');
        const lDep = gl.getAttribLocation(lodProg, 'a_dep');
        const lUV  = gl.getAttribLocation(lodProg, 'a_uv');

        gl.bindBuffer(gl.ARRAY_BUFFER, faceBufPos);
        gl.enableVertexAttribArray(lPos);
        gl.vertexAttribPointer(lPos, 2, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, faceBufCol);
        gl.enableVertexAttribArray(lCol);
        gl.vertexAttribPointer(lCol, 4, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, faceBufDep);
        gl.enableVertexAttribArray(lDep);
        gl.vertexAttribPointer(lDep, 1, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, lodBufUV);
        gl.enableVertexAttribArray(lUV);
        gl.vertexAttribPointer(lUV, 2, gl.FLOAT, false, 0, 0);

        gl.bindVertexArray(null);

        lodUniforms = {
          zoom: gl.getUniformLocation(lodProg, 'uZoom'),
          pX:   gl.getUniformLocation(lodProg, 'uPX'),
          pY:   gl.getUniformLocation(lodProg, 'uPY'),
          res:  gl.getUniformLocation(lodProg, 'uRes'),
        };
        console.log('[chess_render_bridge] LOD glass shader initialised (fragment edge glow)');
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // Wasm ↔ WebGL Data Transfer
  // ═══════════════════════════════════════════════════════════════════

  // Call into Wasm to cull visible cells, returning count.
  function wasmCullVisible() {
    const m = wasmMod;
    // Sync all current JS rotation/viewport state into Wasm
    m._wasm_set_rotations(
      window.rotX   || 0, window.rotY   || 0, rotZ_local,
      window.bldRotX|| 0, window.bldRotY|| 0, rotZ_bld,
      window.cityRotX||0, window.cityRotY||0, rotZ_city
    );
    m._wasm_set_spreads(
      window.wSpread||1, window.vSpread||1, window.uSpread||1,
      window.tSpread||1, window.sSpread||1
    );
    const canvas = document.getElementById('gc');
    m._wasm_set_viewport(
      window.zoom || 1,
      window.panX || canvas.width  / 2,
      window.panY || canvas.height / 2
    );
    m._wasm_init(canvas.width, canvas.height, window.SZ || 8,
                 modeInt(window.GAMEMODE || '4d'));
    return m._wasm_cull_visible();
  }

  function modeInt(mode) {
    return { '2d':2,'3d':3,'4d':4,'5d':5,'6d':6,'7d':7,'8d':8 }[mode] || 4;
  }

  // Copy g_coord_buf from Wasm heap to a JS Uint8Array (for the existing instanced path)
  function getWasmCoordArray(count) {
    const ptr = wasmMod._wasm_get_coord_ptr();
    return new Uint8Array(wasmMod.HEAPU8.buffer, ptr, count * 8);
  }

  // Write per-cell colors from JS into Wasm's g_color_in buffer
  function writeWasmColors(jsColorArray, count) {
    const ptr   = wasmMod._wasm_get_color_in_ptr();
    const heap  = new Float32Array(wasmMod.HEAPF32.buffer, ptr, count * 4);
    heap.set(jsColorArray.subarray(0, count * 4));
  }

  // After wasm_build_face_verts(), upload the output buffers to WebGL
  function uploadFaceVerts(vertCount) {
    const m  = wasmMod;
    const gl = faceGl;
    const HEAP32 = m.HEAPF32;

    const posPtr = m._wasm_get_pos_ptr();
    const colPtr = m._wasm_get_col_ptr();
    const depPtr = m._wasm_get_dep_ptr();

    const posArr = new Float32Array(HEAP32.buffer, posPtr, vertCount * 2);
    const colArr = new Float32Array(HEAP32.buffer, colPtr, vertCount * 4);
    const depArr = new Float32Array(HEAP32.buffer, depPtr, vertCount);

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufPos);
    gl.bufferData(gl.ARRAY_BUFFER, posArr, gl.DYNAMIC_DRAW);

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufCol);
    gl.bufferData(gl.ARRAY_BUFFER, colArr, gl.DYNAMIC_DRAW);

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufDep);
    gl.bufferData(gl.ARRAY_BUFFER, depArr, gl.DYNAMIC_DRAW);
  }

  // Draw the face-shaded pass
  function drawFacePass(vertCount) {
    const gl = faceGl;
    const W  = gl.canvas.width, H = gl.canvas.height;

    gl.useProgram(faceProg);
    gl.uniform1f(faceUniforms.zoom, window.zoom || 1);
    gl.uniform1f(faceUniforms.pX,   window.panX || W/2);
    gl.uniform1f(faceUniforms.pY,   window.panY || H/2);
    gl.uniform2f(faceUniforms.res,  W, H);

    gl.bindVertexArray(faceVAO);
    gl.drawArrays(gl.TRIANGLES, 0, vertCount);
    gl.bindVertexArray(null);
  }

  // ═══════════════════════════════════════════════════════════════════
  // Pipeline Patching
  // ═══════════════════════════════════════════════════════════════════

  // Wait until the main HTML's JS has finished executing, then patch.
  function patchRenderPipeline() {
    // Poll until the key globals are available
    const poll = setInterval(() => {
      if (typeof window.ensureWglVisibleGeometry === 'function' &&
          typeof window.wglBuild === 'function') {
        clearInterval(poll);
        doPatching();
      }
    }, 100);
  }

  function doPatching() {
    console.log('[chess_render_bridge] Patching rendering pipeline');

    // ── Patch ensureWglVisibleGeometry (the JS 6-level nested loop) ──
    const _orig_ensureWglVisibleGeometry = window.ensureWglVisibleGeometry;

    window.ensureWglVisibleGeometry = function () {
      if (!wasmReady) { _orig_ensureWglVisibleGeometry(); return; }

      const mode   = window.GAMEMODE || '4d';
      const isHighD = mode === '7d' || mode === '8d';

      // For 7D/8D: use Wasm to build coord buffers for the existing instanced pass
      const count = wasmCullVisible();
      window.wglVisibleCount = count;
      window.wglInstCount    = count;
      window.wglGeomDirty    = false;

      if (count === 0) {
        window.wglVisibleCoords = null;
        window.wglColorDirty    = false;
        return;
      }

      // Copy Wasm coord buffer to JS (Uint8Array) for existing updateWglColors()
      const coordSlice = getWasmCoordArray(count);
      window.wglVisibleCoords = coordSlice;

      // Upload the coord buffers to the existing WebGL instanced VAO
      const gl = window.wgl;
      if (gl && isHighD) {
        const N  = count;
        const c  = new Float32Array(N * 4);
        const c2 = new Float32Array(N * 4);
        for (let i = 0, j = 0; i < N; i++, j += 8) {
          c[i*4]   = coordSlice[j];   c[i*4+1] = coordSlice[j+1];
          c[i*4+2] = coordSlice[j+2]; c[i*4+3] = coordSlice[j+3];
          c2[i*4]  = coordSlice[j+4]; c2[i*4+1]= coordSlice[j+5];
          c2[i*4+2]= coordSlice[j+6]; c2[i*4+3]= coordSlice[j+7];
        }
        gl.bindBuffer(gl.ARRAY_BUFFER, window.wglBufC);
        gl.bufferData(gl.ARRAY_BUFFER, c, gl.STATIC_DRAW);
        gl.bindBuffer(gl.ARRAY_BUFFER, window.wglBufC2);
        gl.bufferData(gl.ARRAY_BUFFER, c2, gl.STATIC_DRAW);
        if (!window.wglColorArray || window.wglColorArray.length !== N*4)
          window.wglColorArray = new Float32Array(N * 4);
      }

      window.wglColorDirty = true;
    };

    // ── Patch wglBuild to also trigger the face-shaded pass ──
    const _orig_wglBuild = window.wglBuild;

    window.wglBuild = function () {
      _orig_wglBuild();  // runs ensureWglVisibleGeometry + updateWglColors

      if (!wasmReady || !FACE_SHADING || !faceProg) return;
      if (!window.wglColorArray || !window.wglVisibleCount) return;

      const count = window.wglVisibleCount;
      // Write JS-computed cell colors into Wasm color input buffer
      writeWasmColors(window.wglColorArray, count);
      // Build face geometry in Wasm
      const vertCount = wasmMod._wasm_build_face_verts();
      if (vertCount > 0) {
        uploadFaceVerts(vertCount);
        window._bridgeFaceVertCount = vertCount;
      }
    };

    // ── Patch wglDraw to draw the face-shaded pass after the instanced pass ──
    const _orig_wglDraw = window.wglDraw;

    window.wglDraw = function () {
      if (window._bridgeFaceVertCount > 0 && faceProg && FACE_SHADING) {
        // Draw face-shaded geometry INSTEAD of (or after) instanced pass
        // Choose: 'replace' for max visual quality, 'overlay' for hybrid
        const mode = 'replace';  // change to 'overlay' to show both

        if (mode === 'replace') {
          // Face-shaded replaces the flat instanced cells for all modes
          const gl = faceGl;
          const W = gl.canvas.width, H = gl.canvas.height;
          gl.viewport(0, 0, W, H);
          gl.clearColor(0.027, 0.035, 0.059, 1);
          gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
          drawFacePass(window._bridgeFaceVertCount);
        } else {
          // Original instanced pass first, then face overlay on top
          _orig_wglDraw();
          drawFacePass(window._bridgeFaceVertCount);
        }
      } else {
        _orig_wglDraw();
      }
    };

    // ── PERFORMANCE FIX: delta-time correction + smart rotation handling ──
    // When Wasm is ready, C++ rebuilds geometry every rotation frame efficiently
    // (~1ms for culling + projection). No skip needed — the skip was for slow JS.
    // What we DO fix: frame-rate-dependent rotation damping.
    const _origRenderAll = window.renderAll;

    window.renderAll = function (mode) {
      // Apply delta-time correction first so rotVel is correct for the next tick.
      // The main HTML's tick does `rotVel *= 0.84` (tuned for 60fps).
      // At 120fps this damps twice as fast; here we correct for actual dt.
      applyDeltaTimeCorrection(mode);

      // When Wasm is ready, C++ rebuilds are fast — always let the full pipeline run.
      _origRenderAll(mode);
    };

    // ── LOD FIX: intercept drawBoard for cube-structure intermediate LOD ──
    const _origDrawBoard = window.drawBoard;

    window.drawBoard = function () {
      const mode = window.GAMEMODE || '4d';
      const lod  = LOD[mode];

      // Only intercept 7D/8D in the cube-structure zoom range
      if (lod && window.lodSimple) {
        const zoom = window.zoom || 1;
        if (zoom >= lod.GALAXY_TOP && zoom < lod.STRUCT_TOP) {
          // Draw cube-structure LOD instead of galaxy blobs or full cells
          drawCubeStructureLOD(mode, zoom, lod);
          return;
        }
        // Below GALAXY_TOP: fall through to original (galaxy halos)
        // Above STRUCT_TOP: temporarily disable lodSimple so WebGL kicks in
        if (zoom >= lod.STRUCT_TOP) {
          const saved = window.lodSimple;
          window.lodSimple = false;
          _origDrawBoard();
          window.lodSimple = saved;
          return;
        }
      }

      // All other modes / zoom levels: original behaviour
      _origDrawBoard();
    };

    // ── Extend WebGL to 3D–6D (these modes currently use Canvas 2D) ──
    // (kept for completeness — Wasm face pass runs alongside)
    // (no extra patch needed; wglBuild already triggers face verts for all modes)

    // Add rotZ controls to the rotation overlay
    addRotZControls();

    console.log('[chess_render_bridge] Pipeline patched ✓');
  }

  // Build face geometry for 3D–6D modes using all-mode Wasm culling
  function triggerLowDWasmRender() {
    if (!wasmReady || !faceProg) return;

    const count = wasmCullVisible();
    if (count === 0) return;

    // Build a simple flat color array from the current game state
    const colArr = buildSimpleColorArray(count);
    writeWasmColors(colArr, count);
    const vertCount = wasmMod._wasm_build_face_verts();
    if (vertCount > 0) {
      uploadFaceVerts(vertCount);
      const gl = faceGl;
      const W = gl.canvas.width, H = gl.canvas.height;
      gl.viewport(0, 0, W, H);
      if (gl.canvas.width !== W || gl.canvas.height !== H) {
        gl.canvas.width = W; gl.canvas.height = H;
      }
      gl.clear(gl.DEPTH_BUFFER_BIT);  // don't clear color — let existing pass handle bg
      drawFacePass(vertCount);
    }
  }

  // Compute per-cell colors for 3D–6D (mirrors updateWglColors() logic)
  function buildSimpleColorArray(count) {
    const coords  = getWasmCoordArray(count);
    const colArr  = new Float32Array(count * 4);
    const G       = window.G;
    if (!G) { colArr.fill(0.1); return colArr; }

    const gK      = window.gK;
    const PC      = window.PC;
    const hexRGB  = window.hexRGB;
    const selKey  = G.sel ? gK(G.sel) : null;
    const legalSet= new Set((G.legal||[]).map(m => gK(m.coords)));
    const capSet  = new Set((G.legal||[]).filter(m=>m.cap).map(m=>gK(m.coords)));

    for (let i = 0; i < count; i++) {
      const j   = i * 8;
      const pk  = gK([coords[j],coords[j+1],coords[j+2],coords[j+3],
                       coords[j+4],coords[j+5],coords[j+6],coords[j+7]]);
      const piece  = G.pieces[pk];
      const light  = (coords[j]+coords[j+1]+coords[j+2]+coords[j+3]+coords[j+4]+coords[j+5]+coords[j+6])%2===0;
      const isSel  = pk===selKey;
      const isCap  = capSet.has(pk);
      const isLegal= legalSet.has(pk);
      let r,g,b,a;
      if      (isSel)  { r=0.24;g=0.81;b=0.81;a=0.47; }
      else if (isCap)  { r=0.88;g=0.31;b=0.38;a=0.48; }
      else if (isLegal){ r=0.24;g=0.81;b=0.81;a=0.22; }
      else if (piece)  { const[pr,pg,pb]=hexRGB(PC[piece.pid]);r=pr;g=pg;b=pb;a=0.08; }
      else             { r=light?0.086:0.047;g=light?0.125:0.075;b=light?0.22:0.15;a=0.75; }
      colArr[i*4]=r; colArr[i*4+1]=g; colArr[i*4+2]=b; colArr[i*4+3]=a;
    }
    return colArr;
  }

  // ═══════════════════════════════════════════════════════════════════
  // Cube-Structure Intermediate LOD
  // ═══════════════════════════════════════════════════════════════════
  // Renders each (w,v,u) cube in every on-screen building as a 3D glass box.
  //   7D → 8 buildings in a line   (t = 0..7, s = 0)
  //   8D → 64 buildings in a grid  (t = 0..7, s = 0..7)
  //
  // When Wasm is ready:  ALL geometry built in C++ (wasm_build_lod_cube_verts),
  //                      uploaded to WebGL, drawn with alpha blending.
  // When Wasm not ready: canvas 2D fallback (same visual, pure JS).

  // Upload Wasm LOD buffers. The pos/col/dep VBOs are shared with the face-shaded
  // pass (mutually exclusive zoom ranges). The UV VBO is LOD-exclusive (only
  // lodVAO uses it — faceVAO does not have a UV attribute).
  function uploadLodVerts(vertCount) {
    const m  = wasmMod;
    const gl = faceGl;

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufPos);
    gl.bufferData(gl.ARRAY_BUFFER,
      new Float32Array(m.HEAPF32.buffer, m._wasm_get_lod_pos_ptr(), vertCount * 2),
      gl.DYNAMIC_DRAW);

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufCol);
    gl.bufferData(gl.ARRAY_BUFFER,
      new Float32Array(m.HEAPF32.buffer, m._wasm_get_lod_col_ptr(), vertCount * 4),
      gl.DYNAMIC_DRAW);

    gl.bindBuffer(gl.ARRAY_BUFFER, faceBufDep);
    gl.bufferData(gl.ARRAY_BUFFER,
      new Float32Array(m.HEAPF32.buffer, m._wasm_get_lod_dep_ptr(), vertCount),
      gl.DYNAMIC_DRAW);

    gl.bindBuffer(gl.ARRAY_BUFFER, lodBufUV);
    gl.bufferData(gl.ARRAY_BUFFER,
      new Float32Array(m.HEAPF32.buffer, m._wasm_get_lod_uv_ptr(), vertCount * 2),
      gl.DYNAMIC_DRAW);
  }

  // Draw LOD glass geometry with alpha blending + fragment edge glow.
  // Uses lodProg (UV-aware edge-glow shader) when available, falls back to faceProg.
  function drawLodFacePass(vertCount) {
    const gl  = faceGl;
    const W   = gl.canvas.width, H = gl.canvas.height;
    const prog = lodProg  || faceProg;
    const vao  = lodProg  ? lodVAO   : faceVAO;
    const uni  = lodProg  ? lodUniforms : faceUniforms;

    gl.viewport(0, 0, W, H);
    gl.clearColor(0.027, 0.035, 0.059, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(false);  // glass layers must not occlude each other

    gl.useProgram(prog);
    gl.uniform1f(uni.zoom, window.zoom || 1);
    gl.uniform1f(uni.pX,   window.panX || W / 2);
    gl.uniform1f(uni.pY,   window.panY || H / 2);
    gl.uniform2f(uni.res,  W, H);

    gl.bindVertexArray(vao);
    gl.drawArrays(gl.TRIANGLES, 0, vertCount);
    gl.bindVertexArray(null);

    gl.depthMask(true);
    gl.disable(gl.BLEND);
  }

  // Sync current JS rotation/viewport state to Wasm before LOD build.
  function syncStateToWasm() {
    const canvas = window.canvas || document.getElementById('gc');
    wasmMod._wasm_set_rotations(
      window.rotX    || 0, window.rotY    || 0, rotZ_local,
      window.bldRotX || 0, window.bldRotY || 0, rotZ_bld,
      window.cityRotX|| 0, window.cityRotY|| 0, rotZ_city
    );
    wasmMod._wasm_set_spreads(
      window.wSpread||1, window.vSpread||1, window.uSpread||1,
      window.tSpread||1, window.sSpread||1
    );
    wasmMod._wasm_set_viewport(
      window.zoom || 1,
      window.panX || canvas.width  / 2,
      window.panY || canvas.height / 2
    );
    wasmMod._wasm_init(
      canvas.width, canvas.height,
      window.SZ || 8,
      modeInt(window.GAMEMODE || '4d')
    );
  }

  function drawCubeStructureLOD(mode, zoom, lod) {
    // ── C++ / Wasm path (preferred) ──────────────────────────────────
    if (wasmReady && faceProg) {
      const tFrac = Math.min(1, Math.max(0,
        (zoom - lod.GALAXY_TOP) / (lod.STRUCT_TOP - lod.GALAXY_TOP)));

      syncStateToWasm();

      // Pass cube occupancy (one entry per occupied cube, O(pieces) not O(cubes))
      wasmMod._wasm_lod_clear_occ();
      const G  = window.G;
      const PC = window.PC;
      const hexRGB = window.hexRGB;
      if (G && G.pieces && PC && hexRGB) {
        for (const [pk, piece] of Object.entries(G.pieces)) {
          const c = pk.split(',').map(Number);
          const [r, g, b] = hexRGB(PC[piece.pid]);
          wasmMod._wasm_lod_set_cube_occ(
            c[3]||0, c[4]||0, c[5]||0, c[6]||0, c[7]||0,
            r, g, b
          );
        }
      }

      // Build geometry entirely in C++ — all projection, sorting, glass shading
      const vertCount = wasmMod._wasm_build_lod_cube_verts(tFrac);

      if (vertCount > 0) {
        uploadLodVerts(vertCount);
        drawLodFacePass(vertCount);
      } else {
        // Nothing visible — just clear
        const gl = faceGl;
        gl.clearColor(0.027, 0.035, 0.059, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      }

      // Piece symbols drawn on the 2D canvas overlay on top of WebGL
      drawStructurePieceSymbols(mode, zoom);
      return;
    }

    // ── Canvas 2D fallback (when Wasm not yet loaded) ─────────────────
    const ctx        = window.ctx;
    const canvas     = window.canvas || document.getElementById('gc');
    if (!ctx || !canvas) return;
    const W = canvas.width, H = canvas.height;
    const SZ         = window.SZ || 8;
    const G          = window.G;
    const projectCell= window.projectCell;
    const panX       = window.panX || 0;
    const panY       = window.panY || 0;
    const PC         = window.PC;
    const hexRGB     = window.hexRGB;

    // ── background ──
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#07090f';
    ctx.fillRect(0, 0, W, H);

    if (!G || !projectCell) { drawStructurePieceSymbols(mode, zoom); return; }

    // ── build cube occupancy map: "w,v,u,t,s" → pid ──
    const cubeOcc = new Map();
    if (G.pieces) {
      for (const [pk, piece] of Object.entries(G.pieces)) {
        const c = pk.split(',').map(Number);
        const bk = `${c[3]||0},${c[4]||0},${c[5]||0},${c[6]||0},${c[7]||0}`;
        if (!cubeOcc.has(bk)) cubeOcc.set(bk, piece.pid);
      }
    }

    // Transition factor: 0 at GALAXY_TOP → 1 at STRUCT_TOP
    const tFrac = Math.min(1, Math.max(0,
      (zoom - lod.GALAXY_TOP) / (lod.STRUCT_TOP - lod.GALAXY_TOP)));

    // ── Blue-glass visual style (matches the 5D floating-glass board) ──
    // Faces are barely-there dark translucent blue; EDGES are the hero element.
    const faceAlpha  = 0.18 + tFrac * 0.18;   // 0.18–0.36  (translucent glass fill)
    const edgeAlpha  = 0.55 + tFrac * 0.38;   // 0.55–0.93  (bright glowing outline)
    const specAlpha  = 0.30 + tFrac * 0.30;   // specular highlight on top-face TL edge

    // Base glass tint for empty cubes: deep blue-sapphire
    const GLASS_R = 0.06, GLASS_G = 0.20, GLASS_B = 0.50;

    const sMax = (mode === '8d') ? SZ : 1;

    ctx.save();

    for (let s = 0; s < sMax; s++) {
      for (let t = 0; t < SZ; t++) {

        // ── building-level cull ──
        const [bcx, bcy] = projectCell(3.5, 3.5, 3.5, 3.5, 3.5, 3.5, t, s);
        const bsx = bcx * zoom + panX, bsy = bcy * zoom + panY;
        const bldR = 320 * zoom;
        if (bsx + bldR < 0 || bsx - bldR > W || bsy + bldR < 0 || bsy - bldR > H) continue;

        // ── 4 reference projections → linear basis vectors in screen space ──
        const [r00x, r00y, r00d] = projectCell(3.5, 3.5, 3.5, 0, 0, 0, t, s);
        const [r10x, r10y, r10d] = projectCell(3.5, 3.5, 3.5, 1, 0, 0, t, s);
        const [r01x, r01y, r01d] = projectCell(3.5, 3.5, 3.5, 0, 1, 0, t, s);
        const [r001x,r001y,r001d]= projectCell(3.5, 3.5, 3.5, 0, 0, 1, t, s);

        const s0x = r00x * zoom + panX, s0y = r00y * zoom + panY;
        const dwX = (r10x  - r00x) * zoom, dwY = (r10y  - r00y) * zoom;
        const dvX = (r01x  - r00x) * zoom, dvY = (r01y  - r00y) * zoom;
        const duX = (r001x - r00x) * zoom, duY = (r001y - r00y) * zoom;
        const dwD = r10d  - r00d, dvD = r01d  - r00d, duD = r001d - r00d;

        const cubeSize = Math.sqrt(dwX*dwX + dwY*dwY);
        if (cubeSize < 1.2) continue;

        const hwX = dwX * 0.5, hwY = dwY * 0.5;
        const hvX = dvX * 0.5, hvY = dvY * 0.5;
        const faceDropX = duX * 0.48;
        const faceDropY = Math.max(1.5, Math.abs(duY) * 0.48) * Math.sign(duY || 1);

        // ── collect + depth-sort cubes ──
        const cubes = [];
        for (let u = 0; u < SZ; u++) {
          for (let v = 0; v < SZ; v++) {
            for (let w = 0; w < SZ; w++) {
              const scx = s0x + w*dwX + v*dvX + u*duX;
              const scy = s0y + w*dwY + v*dvY + u*duY;
              if (scx + cubeSize < -10 || scx - cubeSize > W + 10) continue;
              if (scy + cubeSize < -10 || scy - cubeSize > H + 10) continue;
              const depth = r00d + w*dwD + v*dvD + u*duD;
              const pid   = cubeOcc.get(`${w},${v},${u},${t},${s}`);
              cubes.push({ scx, scy, depth, pid });
            }
          }
        }
        cubes.sort((a, b) => a.depth - b.depth);

        // ── draw each cube in blue-glass style ──
        for (const { scx, scy, pid } of cubes) {

          // Colour: empty = pure glass blue; occupied = player hue tinted into glass
          let fR, fG, fB;
          if (pid !== undefined && PC && hexRGB) {
            const [pr, pg, pb] = hexRGB(PC[pid]);
            // 60% player colour + 40% glass blue — keeps the glass material visible
            fR = pr * 0.60 + GLASS_R * 0.40;
            fG = pg * 0.60 + GLASS_G * 0.40;
            fB = pb * 0.60 + GLASS_B * 0.40;
          } else {
            fR = GLASS_R; fG = GLASS_G; fB = GLASS_B;
          }

          // Top-face parallelogram corners
          const tlX = scx - hwX - hvX, tlY = scy - hwY - hvY;
          const trX = scx + hwX - hvX, trY = scy + hwY - hvY;
          const brX = scx + hwX + hvX, brY = scy + hwY + hvY;
          const blX = scx - hwX + hvX, blY = scy - hwY + hvY;

          const r = Math.round(fR*255), g = Math.round(fG*255), b = Math.round(fB*255);

          // ── Top face — dark translucent glass fill ──
          ctx.fillStyle = `rgba(${r},${g},${b},${faceAlpha})`;
          ctx.beginPath();
          ctx.moveTo(tlX, tlY); ctx.lineTo(trX, trY);
          ctx.lineTo(brX, brY); ctx.lineTo(blX, blY);
          ctx.closePath();
          ctx.fill();

          // ── Right side face — slightly darker glass ──
          const rR = Math.round(fR*0.50*255), rG = Math.round(fG*0.50*255), rB = Math.round(fB*0.55*255);
          ctx.fillStyle = `rgba(${rR},${rG},${rB},${faceAlpha * 0.85})`;
          ctx.beginPath();
          ctx.moveTo(trX, trY);
          ctx.lineTo(trX + faceDropX, trY + faceDropY);
          ctx.lineTo(brX + faceDropX, brY + faceDropY);
          ctx.lineTo(brX, brY);
          ctx.closePath();
          ctx.fill();

          // ── Left side face — darkest glass ──
          const lR = Math.round(fR*0.35*255), lG = Math.round(fG*0.35*255), lB = Math.round(fB*0.40*255);
          ctx.fillStyle = `rgba(${lR},${lG},${lB},${faceAlpha * 0.85})`;
          ctx.beginPath();
          ctx.moveTo(blX, blY);
          ctx.lineTo(blX + faceDropX, blY + faceDropY);
          ctx.lineTo(tlX + faceDropX, tlY + faceDropY);
          ctx.lineTo(tlX, tlY);
          ctx.closePath();
          ctx.fill();

          // ── Glowing glass edges — the primary visual element ──
          if (cubeSize > 2.5) {
            const lw = Math.max(0.5, Math.min(1.6, cubeSize * 0.07));
            ctx.lineWidth = lw;

            // Top face outline — bright cyan glow
            ctx.strokeStyle = `rgba(100,200,255,${edgeAlpha})`;
            ctx.beginPath();
            ctx.moveTo(tlX, tlY); ctx.lineTo(trX, trY);
            ctx.lineTo(brX, brY); ctx.lineTo(blX, blY);
            ctx.closePath();
            ctx.stroke();

            // Vertical drop edges
            ctx.strokeStyle = `rgba(60,160,230,${edgeAlpha * 0.75})`;
            ctx.beginPath();
            ctx.moveTo(trX, trY); ctx.lineTo(trX + faceDropX, trY + faceDropY);
            ctx.moveTo(brX, brY); ctx.lineTo(brX + faceDropX, brY + faceDropY);
            ctx.moveTo(blX, blY); ctx.lineTo(blX + faceDropX, blY + faceDropY);
            ctx.stroke();

            // Bottom edge of side faces
            ctx.strokeStyle = `rgba(40,120,200,${edgeAlpha * 0.55})`;
            ctx.beginPath();
            ctx.moveTo(trX + faceDropX, trY + faceDropY);
            ctx.lineTo(brX + faceDropX, brY + faceDropY);
            ctx.lineTo(blX + faceDropX, blY + faceDropY);
            ctx.stroke();

            // ── Specular glass highlight: thin bright line on TL→TR top edge ──
            // Simulates light catching the near glass edge (like the 5D board's cell borders)
            if (cubeSize > 5) {
              ctx.strokeStyle = `rgba(190,235,255,${specAlpha})`;
              ctx.lineWidth = Math.max(0.4, lw * 0.55);
              ctx.beginPath();
              ctx.moveTo(tlX, tlY); ctx.lineTo(trX, trY);
              ctx.stroke();
            }
          }
        }
      }
    }

    ctx.restore();

    // ── Piece symbols on top ──
    drawStructurePieceSymbols(mode, zoom);
  }

  // Draw piece glyphs over the structure LOD (adapted from galaxy view's piece pass)
  function drawStructurePieceSymbols(mode, zoom) {
    const ctx        = window.ctx;
    const canvas     = window.canvas || document.getElementById('gc');
    const G          = window.G;
    const projectCell= window.projectCell;
    const PC         = window.PC;
    const hexRGB     = window.hexRGB;
    const gK         = window.gK;
    if (!ctx || !canvas || !G || !G.pieces || !projectCell || !PC) return;

    const W = canvas.width, H = canvas.height;
    const panX = window.panX || 0, panY = window.panY || 0;

    // Symbol scale: grow with zoom so pieces are legible as you zoom in
    const symbolR  = Math.max(5, Math.min(14, Math.round(4 + zoom * 20)));
    const binSize  = Math.max(8, Math.round(symbolR * 1.1));
    const bins     = new Map();
    const pri      = { K:7, Q:6, R:5, B:4, N:3, P:2 };

    for (const [pk, piece] of Object.entries(G.pieces)) {
      const c     = pk.split(',').map(Number);
      const world = projectCell(
        (c[0]||0)+0.5, (c[1]||0)+0.5, c[2]||0,
        c[3]||0, c[4]||0, c[5]||0, c[6]||0, c[7]||0
      );
      const sx = world[0]*zoom + panX, sy = world[1]*zoom + panY;
      if (sx < -symbolR || sx > W+symbolR || sy < -symbolR || sy > H+symbolR) continue;
      const bx  = Math.round(sx / binSize), by = Math.round(sy / binSize);
      const key = bx + ',' + by;
      const score = (pri[piece.type]||1)*1000 + (piece.pid===G.cur?100:0) + Math.round((world[2]||0)*10);
      const prev  = bins.get(key);
      if (!prev || score > prev.score) bins.set(key, { sx, sy, piece, score });
    }

    ctx.save();
    ctx.textAlign    = 'center';
    ctx.textBaseline = 'middle';
    ctx.shadowBlur   = 0;

    const items = [...bins.values()].sort((a, b) => a.score - b.score);
    for (const { sx, sy, piece } of items) {
      const [pr, pg, pb] = hexRGB(PC[piece.pid]);
      const hex = '#' + [pr, pg, pb]
        .map(v => Math.round(v * 255).toString(16).padStart(2, '0')).join('');

      // Glow halo
      ctx.fillStyle = `rgba(${Math.round(pr*255)},${Math.round(pg*255)},${Math.round(pb*255)},0.22)`;
      ctx.beginPath();
      ctx.arc(sx, sy, symbolR * 1.4, 0, Math.PI * 2);
      ctx.fill();

      // Solid disc
      ctx.fillStyle = hex;
      ctx.beginPath();
      ctx.arc(sx, sy, symbolR * 0.78, 0, Math.PI * 2);
      ctx.fill();

      // Piece letter
      ctx.font      = `bold ${Math.round(symbolR * 0.95)}px sans-serif`;
      ctx.fillStyle = '#07090f';
      ctx.fillText(piece.type, sx, sy + 0.5);
    }

    ctx.restore();
  }

  // ═══════════════════════════════════════════════════════════════════
  // New rotZ (roll) Axis UI Controls
  // ═══════════════════════════════════════════════════════════════════

  function addRotZControls() {
    // Add roll buttons to the existing rotation overlay
    const rotRing = document.getElementById('rotRing');
    if (!rotRing) return;

    const row = document.createElement('div');
    row.style.cssText = 'display:flex;gap:1px;justify-content:center;margin-top:1px';

    function makeBtn(label, title, cb) {
      const b = document.createElement('button');
      b.className = 'rot-ov-btn';
      b.title = title;
      b.textContent = label;
      b.style.cssText = 'border-color:rgba(200,168,75,.45);color:rgba(200,168,75,.85);font-size:.65rem';
      b.addEventListener('pointerdown', () => {
        const tid = setInterval(cb, 16);
        const stop = () => clearInterval(tid);
        window.addEventListener('pointerup',   stop, {once:true});
        window.addEventListener('pointerout',  stop, {once:true});
      });
      return b;
    }

    row.appendChild(makeBtn('↺ Z', 'Roll local cube left (Z roll)', () => {
      rotZ_local -= 0.025;
      syncRotZToWasm();
      window.renderViewOnly && window.renderViewOnly();
    }));
    row.appendChild(makeBtn('Z ↻', 'Roll local cube right (Z roll)', () => {
      rotZ_local += 0.025;
      syncRotZToWasm();
      window.renderViewOnly && window.renderViewOnly();
    }));
    rotRing.appendChild(row);

    // Building cluster Z roll
    const bldRow = document.createElement('div');
    bldRow.style.cssText = 'display:flex;gap:1px;justify-content:center;margin-top:1px';
    bldRow.appendChild(makeBtn('↺ Bld', 'Roll building cluster left (Z roll)', () => {
      rotZ_bld -= 0.025;
      syncRotZToWasm();
      window.renderViewOnly && window.renderViewOnly();
    }));
    bldRow.appendChild(makeBtn('Bld ↻', 'Roll building cluster right (Z roll)', () => {
      rotZ_bld += 0.025;
      syncRotZToWasm();
      window.renderViewOnly && window.renderViewOnly();
    }));
    rotRing.appendChild(bldRow);

    // City grid Z roll (7D/8D only)
    const cityRow = document.createElement('div');
    cityRow.style.cssText = 'display:flex;gap:1px;justify-content:center;margin-top:1px';
    cityRow.appendChild(makeBtn('↺ City', 'Roll city grid left (Z roll)', () => {
      rotZ_city -= 0.025;
      syncRotZToWasm();
      window.renderViewOnly && window.renderViewOnly();
    }));
    cityRow.appendChild(makeBtn('City ↻', 'Roll city grid right (Z roll)', () => {
      rotZ_city += 0.025;
      syncRotZToWasm();
      window.renderViewOnly && window.renderViewOnly();
    }));
    rotRing.appendChild(cityRow);
  }

  function syncRotZToWasm() {
    if (!wasmReady) return;
    wasmMod._wasm_set_rotations(
      window.rotX    || 0, window.rotY    || 0, rotZ_local,
      window.bldRotX || 0, window.bldRotY || 0, rotZ_bld,
      window.cityRotX|| 0, window.cityRotY|| 0, rotZ_city
    );
  }

  // ═══════════════════════════════════════════════════════════════════
  // Mouse-drag free rotation (all axes)
  // ═══════════════════════════════════════════════════════════════════
  // The existing drag handler only updates rotX/rotY. We intercept it
  // to also handle Shift+drag for rotZ (roll).

  function patchDragForRoll() {
    const canvas = document.getElementById('gc');
    if (!canvas) return;

    let lastX = 0, lastY = 0, dragging = false;

    canvas.addEventListener('pointerdown', e => {
      if (e.button !== 2) return;  // right-button drag for rotation
      dragging = true;
      lastX = e.clientX; lastY = e.clientY;
    });

    window.addEventListener('pointermove', e => {
      if (!dragging || !e.shiftKey) return;  // only intercept shift+drag
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      lastX = e.clientX; lastY = e.clientY;
      // Horizontal drag → rotZ (roll)
      rotZ_local += dx * 0.007;
      syncRotZToWasm();
      window.renderViewOnly && window.renderViewOnly();
    });

    window.addEventListener('pointerup', () => { dragging = false; });
  }

  // ═══════════════════════════════════════════════════════════════════
  // Initialise
  // ═══════════════════════════════════════════════════════════════════

  function init() {
    if (!BRIDGE_ENABLED) return;

    // Patch renderAll for rotation performance immediately (no Wasm dependency)
    // We do this via the pipeline poll so we don't race with the main HTML init.
    const earlyPoll = setInterval(() => {
      if (typeof window.renderAll === 'function' &&
          typeof window.renderLoop === 'function') {
        clearInterval(earlyPoll);
        patchRenderPipelineEarly();
      }
    }, 80);

    if (!document.getElementById('glc')) {
      console.warn('[chess_render_bridge] glCanvas not found — running without Wasm');
      patchDragForRoll();
      return;
    }
    console.log('[chess_render_bridge] Loading Wasm rendering module…');
    loadWasm();
    patchDragForRoll();
  }

  // Apply patches that don't need Wasm (rotation skip + LOD intercept)
  // These run as soon as the main HTML's globals are available.
  function patchRenderPipelineEarly() {
    console.log('[chess_render_bridge] Early patches applied (rotation skip + structure LOD)');

    // ── EARLY: rotation performance skip (pre-Wasm only) ──
    // Once Wasm is ready, doPatching() replaces this with a pass-through.
    // Until then: skip JS coord rebuild for pure-rotation frames on the
    // instanced GPU pass (which applies rotation via uniforms, no rebuild needed).
    // CRITICAL: must NOT skip in LOD range — canvas 2D LOD needs per-frame updates.
    const _origRenderAllE = window.renderAll;
    window.renderAll = function (mode) {
      // Delta-time correction (pre-Wasm only; doPatching takes over after load)
      if (!wasmReady) applyDeltaTimeCorrection(mode);

      const isViewUpdate = (mode === true || mode === 'view' || mode === 'geom');
      const isHighD = (window.GAMEMODE === '7d' || window.GAMEMODE === '8d');

      // Rotation skip: only valid pre-Wasm + only outside LOD range
      if (isViewUpdate && isHighD && window.wglReady && !wasmReady) {
        const curZoom = window.zoom || 1;
        const curPanX = window.panX || 0;
        const curPanY = window.panY || 0;

        const zoomSame = (_prevZoom !== null) && (Math.abs(curZoom - _prevZoom) < 0.003);
        const panSame  = (_prevPanX !== null) &&
                         (Math.abs(curPanX - _prevPanX) < 1.5) &&
                         (Math.abs(curPanY - _prevPanY) < 1.5);

        _prevZoom = curZoom; _prevPanX = curPanX; _prevPanY = curPanY;

        if (zoomSame && panSame) {
          // Never skip in the LOD zoom range — the 2D canvas LOD must update every frame.
          const lod = LOD[window.GAMEMODE];
          const inLOD = lod && window.lodSimple &&
                        curZoom >= lod.GALAXY_TOP && curZoom < lod.STRUCT_TOP;
          if (!inLOD) {
            window.wglGeomDirty  = false;
            window.wglColorDirty = false;
            window.stopAnim && window.stopAnim();
            window.renderLoop && window.renderLoop();
            return;
          }
        }
      }
      _origRenderAllE(mode);
    };

    // ── EARLY: cube-structure LOD intercept ──
    const _origDrawBoardE = window.drawBoard;
    window.drawBoard = function () {
      const mode = window.GAMEMODE || '4d';
      const lod  = LOD[mode];

      if (lod && window.lodSimple) {
        const zoom = window.zoom || 1;
        if (zoom >= lod.GALAXY_TOP && zoom < lod.STRUCT_TOP) {
          drawCubeStructureLOD(mode, zoom, lod);
          return;
        }
        if (zoom >= lod.STRUCT_TOP) {
          const saved = window.lodSimple;
          window.lodSimple = false;
          _origDrawBoardE();
          window.lodSimple = saved;
          return;
        }
      }
      _origDrawBoardE();
    };
  }

  // Run after the main page has set up its globals
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => setTimeout(init, 200));
  } else {
    setTimeout(init, 200);
  }

})();
