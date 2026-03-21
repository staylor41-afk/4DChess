@echo off
REM ═══════════════════════════════════════════════════════════════════
REM  8D Chess — Wasm Rendering Module Build Script (Windows)
REM  Requires Emscripten SDK: https://emscripten.org/docs/getting_started/
REM
REM  Quick setup:
REM    git clone https://github.com/emscripten-core/emsdk.git
REM    cd emsdk
REM    emsdk install latest
REM    emsdk activate latest
REM    emsdk_env.bat           (adds em++ to PATH for this session)
REM
REM  Then run this script from the render/ directory:
REM    cd "F:\8d chess\render"
REM    build.bat
REM ═══════════════════════════════════════════════════════════════════

setlocal

set OUTFILE=chess_render.js

set EXPORTS=["_wasm_init","_wasm_set_rotations","_wasm_set_spreads","_wasm_set_viewport","_wasm_cull_visible","_wasm_build_face_verts","_wasm_get_coord_ptr","_wasm_get_color_in_ptr","_wasm_get_pos_ptr","_wasm_get_col_ptr","_wasm_get_dep_ptr","_wasm_get_visible_count","_wasm_get_vert_count","_wasm_get_local_mat","_wasm_get_bld_mat","_wasm_get_city_mat","_wasm_apply_rotation","_wasm_get_euler","_wasm_lod_clear_occ","_wasm_lod_set_cube_occ","_wasm_build_lod_cube_verts","_wasm_get_lod_pos_ptr","_wasm_get_lod_col_ptr","_wasm_get_lod_dep_ptr","_wasm_get_lod_uv_ptr","_wasm_get_lod_vert_count","_malloc","_free"]

set RUNTIME=["ccall","cwrap","HEAPU8","HEAPF32","HEAP32"]

em++ chess_render.cpp ^
  -O3 ^
  -msimd128 ^
  --no-entry ^
  -s WASM=1 ^
  -s MODULARIZE=1 ^
  -s "EXPORT_NAME=ChessRenderModule" ^
  -s "EXPORTED_FUNCTIONS=%EXPORTS%" ^
  -s "EXPORTED_RUNTIME_METHODS=%RUNTIME%" ^
  -s INITIAL_MEMORY=67108864 ^
  -s ALLOW_MEMORY_GROWTH=1 ^
  -s ENVIRONMENT=web ^
  -s SINGLE_FILE=0 ^
  --bind ^
  -o %OUTFILE%

if %ERRORLEVEL% EQU 0 (
  echo.
  echo [OK] Build successful:
  echo      chess_render.js   ^(Emscripten glue^)
  echo      chess_render.wasm ^(compiled module^)
  echo.
  echo Place both files in: "F:\8d chess\render\"
  echo Then open 8d-chess-v45.57-wasm-render.html in a browser.
) else (
  echo.
  echo [ERROR] Build failed — check em++ is in PATH.
  echo Run:  emsdk_env.bat  inside your emsdk folder first.
)

endlocal
pause
