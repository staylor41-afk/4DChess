/**
 * chess_render.cpp — 8D Chess WebAssembly Rendering Engine
 * © Samuel M G Taylor, 2003 — 8D Chess
 *
 * Compiled to WebAssembly via Emscripten; provides:
 *   1. wasm_cull_visible()       — fast 8D cell visibility culling (replaces slow JS loops)
 *   2. wasm_build_face_verts()   — multi-face shaded cell geometry for all dimensions
 *   3. Rotation management       — full 3-axis rotation per layer with smooth quaternion support
 *
 * Build (see build.bat / build.sh):
 *   em++ chess_render.cpp -O3 -msimd128 --no-entry -s WASM=1 \
 *       -s EXPORTED_FUNCTIONS=[...] -s EXPORTED_RUNTIME_METHODS=[...] \
 *       -s INITIAL_MEMORY=33554432 -o chess_render.js
 */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <emscripten/emscripten.h>

// ═══════════════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════════════

static const float CELL_SCALE    = 14.0f;
static const float CELL_SCALE_6  = 14.0f * 1.05f;  // 6D+ spacing
static const float DEPTH_OFFSET  = 4000.0f;
static const float DEPTH_RANGE   = 8000.0f;
static const float AMBIENT       = 0.08f;           // minimum face brightness

// Buffer limits — enough for any zoomed-in view of 8D
static const int MAX_VISIBLE     = 16384;
static const int VERTS_PER_CELL  = 18;              // 3 faces × 2 triangles × 3 verts
static const int MAX_VERTS       = MAX_VISIBLE * VERTS_PER_CELL;

// ═══════════════════════════════════════════════════════════════════════
// Rotation Layer
// ═══════════════════════════════════════════════════════════════════════

struct RotLayer {
    float rotX = 0.0f;   // tilt (X-axis rotation)
    float rotY = 0.0f;   // spin (Y/Z-axis rotation)
    float rotZ = 0.0f;   // roll (Z-axis roll, new — enables full 3-axis control)
    float mat[9] = {1,0,0, 0,1,0, 0,0,1};  // row-major 3x3 rotation matrix

    // Recompute mat from rotX/rotY/rotZ.
    // Exactly mirrors the JS projectCell() math for rotX/rotY;
    // rotZ adds a post-roll for the new third axis.
    void recompute() {
        float cY = cosf(rotY), sY = sinf(rotY);
        float cX = cosf(rotX), sX = sinf(rotX);

        // Base matrix (mirrors JS exactly):
        //   Row 0 (screen X): [ cosY,       sinY,        0   ]
        //   Row 1 (screen Y): [-sinY*cosX,  cosY*cosX,  -sinX]
        //   Row 2 (screen Z): [-sinY*sinX,  cosY*sinX,   cosX]
        float r[9] = {
             cY,        sY,       0.0f,
            -sY * cX,  cY * cX,  -sX,
            -sY * sX,  cY * sX,   cX
        };

        if (fabsf(rotZ) < 1e-5f) {
            memcpy(mat, r, 9 * sizeof(float));
            return;
        }

        // Post-multiply by Rz(rotZ) to add roll:
        // Rz = [cZ,  sZ, 0]
        //      [-sZ, cZ, 0]
        //      [0,   0,  1]
        float cZ = cosf(rotZ), sZ = sinf(rotZ);
        // mat = r * Rz
        for (int row = 0; row < 3; row++) {
            float a = r[row*3+0], b = r[row*3+1], c = r[row*3+2];
            mat[row*3+0] = a * cZ - b * sZ;
            mat[row*3+1] = a * sZ + b * cZ;
            mat[row*3+2] = c;
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════
// Global State
// ═══════════════════════════════════════════════════════════════════════

static RotLayer g_local;   // col/row/z cube rotation
static RotLayer g_bld;     // w/v/u building cluster rotation
static RotLayer g_city;    // t/s city grid rotation

static float g_wSpread = 1.0f, g_vSpread = 1.0f, g_uSpread = 1.0f;
static float g_tSpread = 1.0f, g_sSpread = 1.0f;
static int   g_mode    = 4;   // 2–8
static int   g_sz      = 8;   // board size per axis
static int   g_cw      = 800, g_ch = 600;
static float g_zoom    = 1.0f, g_panX = 400.0f, g_panY = 300.0f;

// ═══════════════════════════════════════════════════════════════════════
// Output Buffers (accessible from JS via Wasm heap pointers)
// ═══════════════════════════════════════════════════════════════════════

static uint8_t g_coord_buf[MAX_VISIBLE * 8];        // visible cell coords (uint8, 8 per cell)
static float   g_color_in[MAX_VISIBLE * 4];         // JS writes cell colors here
static float   g_pos_buf[MAX_VERTS * 2];            // face vertex positions [x,y] per vert
static float   g_col_buf[MAX_VERTS * 4];            // face vertex colors [r,g,b,a] per vert
static float   g_dep_buf[MAX_VERTS];                // face vertex depths (for gl_Position.z)

static int g_visible_count = 0;
static int g_vert_count    = 0;

// ═══════════════════════════════════════════════════════════════════════
// 8D Projection  (C++ mirror of JS projectCell())
// ═══════════════════════════════════════════════════════════════════════

struct Vec3 { float x, y, z; };

// Project a cell corner at (col+dx, row+dy, z+dz) in local cube space,
// adding a pre-computed building offset (avoids recomputing outer layers
// for every corner of every cell in the same building).
static inline Vec3 project_local_corner(
    float col, float row, float z,
    float dx,  float dy,  float dz,
    Vec3 bld_offset)
{
    const float* m = g_local.mat;
    float cx = (col + dx) - 3.5f;
    float cy = (row + dy) - 3.5f;
    float cz = (z  + dz) - 2.0f;

    float px = (m[0]*cx + m[1]*cy + m[2]*cz) * CELL_SCALE;
    float py = (m[3]*cx + m[4]*cy + m[5]*cz) * CELL_SCALE;
    float pz =  m[6]*cx + m[7]*cy + m[8]*cz;

    return { bld_offset.x + px,
             bld_offset.y + py,
             bld_offset.z + pz };
}

// Compute the building-layer offset for (w,v,u,t,s) — called once per building.
// Returns the screen-space offset that separates this building from the origin.
static Vec3 compute_building_offset(int w, int v, int u, int t, int s) {
    if (g_mode <= 3) return {0, 0, 0};

    const float S = CELL_SCALE;
    const float S6 = CELL_SCALE_6;
    const float HZ = (g_sz - 1) * 0.5f;

    if (g_mode == 4) {
        float cubeSpX = (g_sz + 1) * S * 1.05f;
        return { (w - HZ) * cubeSpX, 0.0f, 0.0f };
    }

    if (g_mode == 5) {
        float cubeSpX = (g_sz + 1) * S * 1.05f;
        float cubeSpY = (g_sz + 1) * S * 1.1f;
        return { (w - HZ) * cubeSpX, (v - HZ) * cubeSpY, 0.0f };
    }

    if (g_mode == 6) {
        float cubeSpX  = (g_sz + 1) * S6 * g_wSpread;
        float cubeSpY  = (g_sz + 1) * S6 * 1.08f * g_vSpread;
        float boardSpU = (g_sz + 1) * S6 * 1.55f * g_uSpread;
        float mw = w - HZ, mv = v - HZ, mu = u - HZ;
        float ox  = mw * cubeSpX  + mu * boardSpU * 0.36f;
        float oy  = mv * cubeSpY  + mu * boardSpU * (-0.78f);
        float dep = mu * 26.0f;
        return { ox, oy, dep };
    }

    // 7D / 8D
    float cubeSpX = (g_sz + 1) * S6 * g_wSpread;
    float cubeSpY = (g_sz + 1) * S6 * 1.05f * g_vSpread;
    float cubeSpU = (g_sz + 1) * S6 * g_uSpread;
    float mw = w - HZ, mv = v - HZ, mu = u - HZ;

    // Building sub-rotation (mirrors JS bldRotX/bldRotY)
    const float* bm = g_bld.mat;
    float mr  = bm[0]*mw + bm[1]*mv;     // rotated W
    float mvr = bm[3]*mw + bm[4]*mv;     // rotated V
    float mu2 = mvr * bm[7] - mu * bm[8]; // rotated U via bldRotX
    float mr2 = mr;
    float mv2 = mvr * bm[4] - mu * bm[5];
    float mu3 = mvr * bm[7] + mu * bm[8];

    // Recompute exactly as JS:
    float cbY = cosf(g_bld.rotY), sbY = sinf(g_bld.rotY);
    float cbX = cosf(g_bld.rotX), sbX = sinf(g_bld.rotX);
    float mr_  = mw * cbY + mv * sbY;
    float mvr_ = -mw * sbY + mv * cbY;
    float mu_  = mu;
    float mr2_ = mr_;
    float mv2_ = mvr_ * cbX - mu_ * sbX;
    float mu3_ = mvr_ * sbX + mu_ * cbX;

    float bldOx  = mr2_ * cubeSpX + mu3_ * cubeSpU * 0.18f;
    float bldOy  = mv2_ * cubeSpY + mu3_ * cubeSpU * (-0.15f);
    float bldDep = mu3_ * 8.0f;

    // City layout (t/s axes)
    float mt = t - HZ;
    float ms = (g_mode == 8) ? (s - HZ) : 0.0f;

    float majorSpan = cubeSpX * 7.2f + cubeSpU * 1.0f;
    float minorSpan = cubeSpY * 4.7f + cubeSpU * 1.25f;
    float tSpan     = majorSpan * g_tSpread;
    float sSpan     = ((g_mode == 8) ? majorSpan * 0.82f : majorSpan * 0.78f) * g_sSpread;

    float cosYaw  = cosf(g_city.rotY), sinYaw  = sinf(g_city.rotY);
    float cosTilt = cosf(g_city.rotX), sinTilt = sinf(g_city.rotX);

    float tDirX  = cosYaw * tSpan;
    float tDirY  = (sinYaw * tSpan * 0.34f + minorSpan) * cosTilt;
    float tDepth = (minorSpan + fabsf(sinYaw) * tSpan * 0.08f) * sinTilt * 0.85f;

    float sDirX  = -sinYaw * sSpan * 0.92f;
    float sDirY  = (cosYaw * sSpan * 0.30f + minorSpan * 1.10f) * cosTilt;
    float sDepth = (minorSpan * 1.05f + fabsf(cosYaw) * sSpan * 0.06f) * sinTilt * 0.65f;

    float cityX   = mt * tDirX  + ms * sDirX;
    float cityY   = mt * tDirY  + ms * sDirY;
    float cityDep = mt * tDepth + ms * sDepth;

    return {
        bldOx + cityX,
        bldOy + cityY,
        bldDep + cityDep
    };
}

// Full 8D projection (used for culling center points)
static Vec3 project_8d(float col, float row, float z,
                       int w, int v, int u, int t, int s)
{
    Vec3 bld = compute_building_offset(w, v, u, t, s);
    return project_local_corner(col, row, z, 0, 0, 0, bld);
}

// ═══════════════════════════════════════════════════════════════════════
// Face Shading
// ═══════════════════════════════════════════════════════════════════════

// Compute the shading brightness for a face with given local-space normal.
// Returns < 0 if the face is back-facing (should not be drawn).
// Uses the rotation matrix's row 2 (screen-Z direction) as the view direction.
static float face_shade(float nx, float ny, float nz) {
    const float* m = g_local.mat;
    // Screen-Z component of the rotated normal = dot(N, row2_of_mat)
    float screenZ = nx * m[6] + ny * m[7] + nz * m[8];
    if (screenZ <= 0.0f) return -1.0f;  // back-facing

    // Normalize relative to top face brightness (cosX), then blend with ambient
    float topBright = m[8];  // cosX — screen-Z of top normal (0,0,1)
    float norm = screenZ / fmaxf(topBright, 0.2f);  // 0..1 relative to top
    // Top face (nz==1, others 0) yields norm=1; side faces yield norm<1
    return AMBIENT + (1.0f - AMBIENT) * fminf(norm, 1.0f);
}

// Brightness of top/side faces given current rotation
struct FaceBrightness {
    float top;     // top face   (normal 0,0,1)
    float left;    // left face  (normal -1,0,0)  — visible when rotY > 0
    float right;   // right face (normal +1,0,0)  — visible when rotY < 0
    float front;   // front face (normal 0,+1,0)  — visible when cosY > 0
    float back;    // back face  (normal 0,-1,0)   — visible when cosY < 0
};

static FaceBrightness compute_face_brightness() {
    return {
        fmaxf(0.0f, face_shade( 0, 0, 1)),   // top
        fmaxf(0.0f, face_shade(-1, 0, 0)),   // left
        fmaxf(0.0f, face_shade( 1, 0, 0)),   // right
        fmaxf(0.0f, face_shade( 0, 1, 0)),   // front
        fmaxf(0.0f, face_shade( 0,-1, 0))    // back
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Geometry Builder Helpers
// ═══════════════════════════════════════════════════════════════════════

static int g_vout = 0;  // current write index into pos/col/dep buffers

static inline float encode_depth(float pz) {
    return (pz + DEPTH_OFFSET) / DEPTH_RANGE;
}

// Write one triangle (3 vertices) to the output buffers.
static inline void emit_triangle(
    Vec3 p0, Vec3 p1, Vec3 p2,
    float r, float g, float b, float a)
{
    if (g_vout + 3 > MAX_VERTS) return;

    float d0 = encode_depth(p0.z);
    float d1 = encode_depth(p1.z);
    float d2 = encode_depth(p2.z);

    int pi = g_vout * 2;
    int ci = g_vout * 4;
    int di = g_vout;

    g_pos_buf[pi+0] = p0.x; g_pos_buf[pi+1] = p0.y;
    g_pos_buf[pi+2] = p1.x; g_pos_buf[pi+3] = p1.y;
    g_pos_buf[pi+4] = p2.x; g_pos_buf[pi+5] = p2.y;

    g_col_buf[ci+0] = r; g_col_buf[ci+1] = g; g_col_buf[ci+2] = b; g_col_buf[ci+3] = a;
    g_col_buf[ci+4] = r; g_col_buf[ci+5] = g; g_col_buf[ci+6] = b; g_col_buf[ci+7] = a;
    g_col_buf[ci+8] = r; g_col_buf[ci+9] = g; g_col_buf[ci+10]= b; g_col_buf[ci+11]= a;

    g_dep_buf[di+0] = d0;
    g_dep_buf[di+1] = d1;
    g_dep_buf[di+2] = d2;

    g_vout += 3;
}

// Emit a quad as 2 triangles (P0=BL, P1=BR, P2=TR, P3=TL)
static inline void emit_quad(
    Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3,
    float r, float g, float b, float a)
{
    emit_triangle(p0, p1, p2, r, g, b, a);
    emit_triangle(p0, p2, p3, r, g, b, a);
}

// ═══════════════════════════════════════════════════════════════════════
// Build Face Geometry for One Cell
// ═══════════════════════════════════════════════════════════════════════

static void build_cell_faces(
    int col, int row, int z,
    Vec3 bld,
    float br, float bg, float bb, float ba,
    const FaceBrightness& fb)
{
    float fc = col, fr = row, fz = z;

    // ── Top face (z+1 plane) ──
    if (fb.top > 0.0f) {
        Vec3 c00 = project_local_corner(fc,   fr,   fz, 0, 0, 1, bld);
        Vec3 c10 = project_local_corner(fc,   fr,   fz, 1, 0, 1, bld);
        Vec3 c11 = project_local_corner(fc,   fr,   fz, 1, 1, 1, bld);
        Vec3 c01 = project_local_corner(fc,   fr,   fz, 0, 1, 1, bld);
        float shade = fb.top;
        emit_quad(c00, c10, c11, c01, br*shade, bg*shade, bb*shade, ba);
    }

    // ── Lateral face (left or right, whichever faces viewer) ──
    float lateralShade = (fb.left > fb.right) ? fb.left : fb.right;
    float lateralX     = (fb.left > fb.right) ? 0.0f : 1.0f;  // col offset for the facing side
    if (lateralShade > 0.01f) {
        Vec3 b00 = project_local_corner(fc, fr,   fz, lateralX, 0, 0, bld);
        Vec3 b10 = project_local_corner(fc, fr,   fz, lateralX, 1, 0, bld);
        Vec3 b11 = project_local_corner(fc, fr,   fz, lateralX, 1, 1, bld);
        Vec3 b01 = project_local_corner(fc, fr,   fz, lateralX, 0, 1, bld);
        float shade = lateralShade;
        emit_quad(b00, b10, b11, b01, br*shade, bg*shade, bb*shade, ba);
    }

    // ── Front/back face (row direction, whichever faces viewer) ──
    float frontShade = (fb.front > fb.back) ? fb.front : fb.back;
    float frontY     = (fb.front > fb.back) ? 1.0f : 0.0f;  // row offset for the facing side
    if (frontShade > 0.01f) {
        Vec3 f00 = project_local_corner(fc, fr, fz, 0, frontY, 0, bld);
        Vec3 f10 = project_local_corner(fc, fr, fz, 1, frontY, 0, bld);
        Vec3 f11 = project_local_corner(fc, fr, fz, 1, frontY, 1, bld);
        Vec3 f01 = project_local_corner(fc, fr, fz, 0, frontY, 1, bld);
        float shade = frontShade;
        emit_quad(f00, f10, f11, f01, br*shade, bg*shade, bb*shade, ba);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Cube-Structure LOD Buffers
// ═══════════════════════════════════════════════════════════════════════
// Intermediate zoom level: each (w,v,u) cube rendered as a 3D glass box.
// 7D = 8 buildings in a line (t=0..7, s=0).
// 8D = 64 buildings in a city grid (t=0..7, s=0..7).

// Cube occupancy: flat array indexed w + SZ*(v + SZ*(u + SZ*(t + SZ*s)))
// SZ=8 → max 8^5 = 32768 entries
static const int MAX_CUBE_OCC = 32768;
static float g_cube_occ_r  [MAX_CUBE_OCC];   // R of occupying player colour
static float g_cube_occ_g  [MAX_CUBE_OCC];   // G
static float g_cube_occ_b  [MAX_CUBE_OCC];   // B
static bool  g_cube_occ_set[MAX_CUBE_OCC];   // true if any piece lives here

// LOD vertex output (separate from cell-face geometry — mutually exclusive)
// Each visible cube emits 18 verts (3 face quads × 2 tris × 3 verts).
// Edge glow is handled by the fragment shader via UV coordinates — no edge
// geometry needed, giving a 4.4× vertex-count reduction vs the old approach.
static const int MAX_LOD_CUBES         = 5000;
static const int LOD_VERTS_PER_CUBE    = 18;   // 3 faces × 2 triangles × 3 verts
static const int MAX_LOD_VERTS         = MAX_LOD_CUBES * LOD_VERTS_PER_CUBE;  // 90 000

static float g_lod_pos_buf[MAX_LOD_VERTS * 2];   // world-space XY per vertex
static float g_lod_col_buf[MAX_LOD_VERTS * 4];   // RGBA per vertex
static float g_lod_dep_buf[MAX_LOD_VERTS];        // depth per vertex
static float g_lod_uv_buf [MAX_LOD_VERTS * 2];   // UV ∈ [0,1] per vertex (for edge glow)
static int   g_lod_vert_count = 0;
static int   g_lod_vout       = 0;

// ── LOD emit helpers ────────────────────────────────────────────────────
// UV coordinates are passed per vertex for the fragment shader edge glow.
// UV ∈ [0,1]: 0 or 1 at face edges, 0.5 at center.
// The fragment shader uses edgeDist = min(min(u,1-u), min(v,1-v)) to
// compute a smooth cyan glow at all face edges — no separate edge geometry needed.

static inline void emit_lod_tri(
    Vec3 p0, Vec3 p1, Vec3 p2,
    float r, float g, float b, float a,
    float u0, float v0, float u1, float v1, float u2, float v2)
{
    if (g_lod_vout + 3 > MAX_LOD_VERTS) return;
    int pi = g_lod_vout * 2, ci = g_lod_vout * 4, di = g_lod_vout, ui = g_lod_vout * 2;
    g_lod_pos_buf[pi+0]=p0.x; g_lod_pos_buf[pi+1]=p0.y;
    g_lod_pos_buf[pi+2]=p1.x; g_lod_pos_buf[pi+3]=p1.y;
    g_lod_pos_buf[pi+4]=p2.x; g_lod_pos_buf[pi+5]=p2.y;
    for (int v = 0; v < 3; v++) {
        g_lod_col_buf[ci + v*4+0]=r; g_lod_col_buf[ci + v*4+1]=g;
        g_lod_col_buf[ci + v*4+2]=b; g_lod_col_buf[ci + v*4+3]=a;
    }
    g_lod_dep_buf[di+0]=encode_depth(p0.z);
    g_lod_dep_buf[di+1]=encode_depth(p1.z);
    g_lod_dep_buf[di+2]=encode_depth(p2.z);
    g_lod_uv_buf[ui+0]=u0; g_lod_uv_buf[ui+1]=v0;
    g_lod_uv_buf[ui+2]=u1; g_lod_uv_buf[ui+3]=v1;
    g_lod_uv_buf[ui+4]=u2; g_lod_uv_buf[ui+5]=v2;
    g_lod_vout += 3;
}

// Emit a quad as 2 triangles (p0=TL, p1=TR, p2=BR, p3=BL).
// UV: TL=(0,0) TR=(1,0) BR=(1,1) BL=(0,1) — edges land exactly on UV 0 or 1.
static inline void emit_lod_quad(
    Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3,
    float r, float g, float b, float a)
{
    emit_lod_tri(p0, p1, p2, r, g, b, a,  0,0, 1,0, 1,1);
    emit_lod_tri(p0, p2, p3, r, g, b, a,  0,0, 1,1, 0,1);
}

// ═══════════════════════════════════════════════════════════════════════
// Exported API
// ═══════════════════════════════════════════════════════════════════════

extern "C" {

EMSCRIPTEN_KEEPALIVE
void wasm_init(int cw, int ch, int sz, int mode) {
    g_cw   = cw;  g_ch  = ch;
    g_sz   = sz;  g_mode = mode;
}

// Set all rotation angles and recompute matrices.
// lrx/lry/lrz = local cube; brx/bry/brz = building; crx/cry = city.
EMSCRIPTEN_KEEPALIVE
void wasm_set_rotations(float lrx, float lry, float lrz,
                        float brx, float bry, float brz,
                        float crx, float cry, float crz)
{
    g_local.rotX = lrx; g_local.rotY = lry; g_local.rotZ = lrz;
    g_bld.rotX   = brx; g_bld.rotY   = bry; g_bld.rotZ   = brz;
    g_city.rotX  = crx; g_city.rotY  = cry; g_city.rotZ  = crz;
    g_local.recompute();
    g_bld.recompute();
    g_city.recompute();
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_spreads(float ws, float vs, float us, float ts, float ss) {
    g_wSpread = ws; g_vSpread = vs; g_uSpread = us;
    g_tSpread = ts; g_sSpread = ss;
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_viewport(float zoom, float panX, float panY) {
    g_zoom = zoom; g_panX = panX; g_panY = panY;
}

// ───────────────────────────────────────────────────────────────────────
// wasm_cull_visible — fast visibility culling for all 8D modes.
// Fills g_coord_buf with [col,row,z,w,v,u,t,s] for each visible cell.
// Returns the number of visible cells.
//
// This replaces the slow JS ensureWglVisibleGeometry() 6-level loop.
// Speed advantage: C++ inline math vs repeated JS function calls,
// plus pre-computed building offsets (avoids redundant outer-layer math).
// ───────────────────────────────────────────────────────────────────────
EMSCRIPTEN_KEEPALIVE
int wasm_cull_visible() {
    const float CW  = (float)g_cw;
    const float CH  = (float)g_ch;
    const float MAR = 120.0f * g_zoom;
    const float S6  = CELL_SCALE_6;
    const int   SZ  = g_sz;
    const float ZOOM = g_zoom;

    // Rough bounding radius of a single cube (for building-level culling)
    float cubeR = 92.0f * ZOOM * fmaxf(fmaxf(g_wSpread, g_vSpread), fmaxf(g_uSpread, 1.0f));

    // Rough bounding radius of a full building cluster (for city-level culling)
    float bldR = cubeR * SZ * 1.5f;

    int wMax = (g_mode >= 4) ? SZ : 1;
    int vMax = (g_mode >= 5) ? SZ : 1;
    int uMax = (g_mode >= 6) ? SZ : 1;
    int tMax = (g_mode >= 7) ? SZ : 1;
    int sMax = (g_mode >= 8) ? SZ : 1;

    int count = 0;

    for (int s = 0; s < sMax && count < MAX_VISIBLE; s++)
    for (int t = 0; t < tMax && count < MAX_VISIBLE; t++) {

        // City-level cull: is this building group on screen?
        Vec3 btc = project_8d(3.5f, 3.5f, 2.5f, 0, 0, 0, t, s);
        float btsx = btc.x * ZOOM + g_panX;
        float btsy = btc.y * ZOOM + g_panY;
        if (btsx + bldR < 0 || btsx - bldR > CW ||
            btsy + bldR < 0 || btsy - bldR > CH) continue;

        for (int u = 0; u < uMax && count < MAX_VISIBLE; u++)
        for (int v = 0; v < vMax && count < MAX_VISIBLE; v++)
        for (int w = 0; w < wMax && count < MAX_VISIBLE; w++) {

            // Cube-level cull
            Vec3 cc = project_8d(3.5f, 3.5f, 2.5f, w, v, u, t, s);
            float csx = cc.x * ZOOM + g_panX;
            float csy = cc.y * ZOOM + g_panY;
            if (csx + cubeR < 0 || csx - cubeR > CW ||
                csy + cubeR < 0 || csy - cubeR > CH) continue;

            // Pre-compute building offset once for this (w,v,u,t,s)
            Vec3 bld = compute_building_offset(w, v, u, t, s);

            // Cell-level cull: check each cell center
            for (int z = 0; z < SZ && count < MAX_VISIBLE; z++)
            for (int row = 0; row < SZ && count < MAX_VISIBLE; row++)
            for (int col = 0; col < SZ && count < MAX_VISIBLE; col++) {
                Vec3 pc = project_local_corner(col + 0.5f, row + 0.5f, (float)z,
                                               0, 0, 0, bld);
                float sx = pc.x * ZOOM + g_panX;
                float sy = pc.y * ZOOM + g_panY;
                if (sx < -MAR || sx > CW + MAR ||
                    sy < -MAR || sy > CH + MAR) continue;

                int idx = count * 8;
                g_coord_buf[idx+0] = (uint8_t)col;
                g_coord_buf[idx+1] = (uint8_t)row;
                g_coord_buf[idx+2] = (uint8_t)z;
                g_coord_buf[idx+3] = (uint8_t)w;
                g_coord_buf[idx+4] = (uint8_t)v;
                g_coord_buf[idx+5] = (uint8_t)u;
                g_coord_buf[idx+6] = (uint8_t)t;
                g_coord_buf[idx+7] = (uint8_t)s;
                count++;
            }
        }
    }

    g_visible_count = count;
    return count;
}

// ───────────────────────────────────────────────────────────────────────
// wasm_build_face_verts — build face-shaded vertex geometry for all
// visible cells (using g_coord_buf from the last wasm_cull_visible call
// and g_color_in for per-cell colors).
//
// Outputs to g_pos_buf, g_col_buf, g_dep_buf.
// Returns total number of vertices written.
//
// Visual enhancements over the current flat-quad approach:
//   • 3 faces per cell (top + 2 visible sides) = proper 3D cube appearance
//   • Physically-based face brightness from view-direction dot normal
//   • Depth values correctly computed per vertex for GPU depth test
// ───────────────────────────────────────────────────────────────────────
EMSCRIPTEN_KEEPALIVE
int wasm_build_face_verts() {
    g_vout = 0;

    FaceBrightness fb = compute_face_brightness();

    for (int i = 0; i < g_visible_count; i++) {
        const uint8_t* c = &g_coord_buf[i * 8];
        int col = c[0], row = c[1], z = c[2];
        int w   = c[3], v   = c[4], u = c[5], t = c[6], s = c[7];

        float cr = g_color_in[i*4+0];
        float cg = g_color_in[i*4+1];
        float cb = g_color_in[i*4+2];
        float ca = g_color_in[i*4+3];

        Vec3 bld = compute_building_offset(w, v, u, t, s);
        build_cell_faces(col, row, z, bld, cr, cg, cb, ca, fb);
    }

    g_vert_count = g_vout;
    return g_vout;
}

// ─── Buffer pointer accessors (used by JS to read Wasm memory) ──────────

EMSCRIPTEN_KEEPALIVE uint8_t* wasm_get_coord_ptr()   { return g_coord_buf; }
EMSCRIPTEN_KEEPALIVE float*   wasm_get_color_in_ptr() { return g_color_in; }
EMSCRIPTEN_KEEPALIVE float*   wasm_get_pos_ptr()      { return g_pos_buf; }
EMSCRIPTEN_KEEPALIVE float*   wasm_get_col_ptr()      { return g_col_buf; }
EMSCRIPTEN_KEEPALIVE float*   wasm_get_dep_ptr()      { return g_dep_buf; }
EMSCRIPTEN_KEEPALIVE int      wasm_get_visible_count(){ return g_visible_count; }
EMSCRIPTEN_KEEPALIVE int      wasm_get_vert_count()   { return g_vert_count; }

// ─── Rotation matrix getters (for GLSL uniforms or JS use) ──────────────
// Returns a pointer to a static 9-float buffer (row-major 3×3 mat).

EMSCRIPTEN_KEEPALIVE float* wasm_get_local_mat()  { return g_local.mat; }
EMSCRIPTEN_KEEPALIVE float* wasm_get_bld_mat()    { return g_bld.mat; }
EMSCRIPTEN_KEEPALIVE float* wasm_get_city_mat()   { return g_city.mat; }

// ─── Smooth drag-rotation support ──────────────────────────────────────
// Apply an incremental rotation about an arbitrary axis to a layer.
// layer: 0=local, 1=building, 2=city
// ax,ay,az: world-space axis (should be normalised by JS)
// angle: radians
EMSCRIPTEN_KEEPALIVE
void wasm_apply_rotation(int layer, float ax, float ay, float az, float angle) {
    RotLayer* rl = (layer == 0) ? &g_local : (layer == 1) ? &g_bld : &g_city;

    // Convert axis-angle to quaternion, then compose with current Euler rotation.
    // We do this by updating rotX/rotY/rotZ via a simple incremental update.
    // For axes perpendicular to the existing ones, this gives smooth all-axis rotation:

    float len = sqrtf(ax*ax + ay*ay + az*az);
    if (len < 1e-7f) return;
    ax /= len; ay /= len; az /= len;

    // Decompose axis into influence on each Euler angle:
    // rotY (spin) responds to az (vertical axis) and ±ax
    // rotX (tilt) responds to ay (side axis)
    // rotZ (roll) responds to the remaining component
    rl->rotY += az * angle;
    rl->rotX += ay * angle;
    rl->rotZ += ax * angle;
    rl->recompute();
}

// Get Euler angles for a layer (for backward compat with JS rotation system)
EMSCRIPTEN_KEEPALIVE
void wasm_get_euler(int layer, float* outRX, float* outRY, float* outRZ) {
    const RotLayer* rl = (layer == 0) ? &g_local : (layer == 1) ? &g_bld : &g_city;
    *outRX = rl->rotX;
    *outRY = rl->rotY;
    *outRZ = rl->rotZ;
}

// ─── Cube-Structure LOD API ─────────────────────────────────────────────

// Clear all cube occupancy data (call once before setting new piece positions).
EMSCRIPTEN_KEEPALIVE
void wasm_lod_clear_occ() {
    memset(g_cube_occ_set, 0, sizeof(g_cube_occ_set));
}

// Mark cube (w,v,u,t,s) as occupied by a piece with colour (r,g,b) ∈ [0,1].
// Multiple pieces in the same cube overwrite — last colour wins (fine for LOD).
EMSCRIPTEN_KEEPALIVE
void wasm_lod_set_cube_occ(int w, int v, int u, int t, int s,
                            float r, float g, float b)
{
    const int SZ = g_sz;
    if (w<0||w>=SZ||v<0||v>=SZ||u<0||u>=SZ||t<0||t>=SZ||s<0||s>=SZ) return;
    int idx = w + SZ*(v + SZ*(u + SZ*(t + SZ*s)));
    g_cube_occ_r  [idx] = r;
    g_cube_occ_g  [idx] = g;
    g_cube_occ_b  [idx] = b;
    g_cube_occ_set[idx] = true;
}

// ─── wasm_build_lod_cube_verts ─────────────────────────────────────────
// Build blue-glass cube-structure LOD geometry for the intermediate zoom range.
//
// Layout:
//   7D → 8 buildings in a line   (t = 0..SZ-1, s = 0)
//   8D → 64 buildings in a grid  (t = 0..SZ-1, s = 0..SZ-1)
// Each building shows its SZ³ (w,v,u) cubes as 3D isometric glass boxes.
//
// tFrac: 0.0 at GALAXY_TOP zoom, 1.0 at STRUCT_TOP zoom
//        Controls face opacity and edge brightness.
//
// Outputs to g_lod_pos_buf / g_lod_col_buf / g_lod_dep_buf.
// Returns number of vertices written.
// ───────────────────────────────────────────────────────────────────────
EMSCRIPTEN_KEEPALIVE
int wasm_build_lod_cube_verts(float tFrac) {
    g_lod_vout = 0;

    const int   SZ   = g_sz;
    const float ZOOM = g_zoom;
    const float CW   = (float)g_cw, CH = (float)g_ch;

    // Blue-glass material constants (matches bridge.js glass style)
    const float GLASS_R = 0.06f, GLASS_G = 0.20f, GLASS_B = 0.50f;
    const float faceAlpha = 0.18f + tFrac * 0.18f;   // 0.18 → 0.36  (translucent fill)
    const float edgeAlpha = 0.55f + tFrac * 0.38f;   // 0.55 → 0.93  (glowing outline)
    const float specAlpha = 0.30f + tFrac * 0.30f;   // specular on top-TL edge

    // (Edge colours and EDGE_HW removed — edge glow handled by fragment shader via UV)

    const int sMax = (g_mode == 8) ? SZ : 1;
    const int tMax = SZ;

    // ── Collect and depth-sort buildings ──────────────────────────────
    struct BldEntry { int t, s; float depth; };
    BldEntry bldOrder[64];
    int bldCount = 0;

    for (int s = 0; s < sMax; s++) {
        for (int t = 0; t < tMax; t++) {
            // Use cube (0,0,0) as the reference point for building centre
            Vec3 r00 = project_local_corner(3.5f, 3.5f, 3.5f, 0, 0, 0,
                                             compute_building_offset(0, 0, 0, t, s));
            float bsx = r00.x * ZOOM + g_panX;
            float bsy = r00.y * ZOOM + g_panY;
            const float bldR = 320.0f * ZOOM;
            if (bsx+bldR<0||bsx-bldR>CW||bsy+bldR<0||bsy-bldR>CH) continue;
            bldOrder[bldCount++] = {t, s, r00.z};
        }
    }
    // Back → front (painter's algorithm for glass transparency)
    std::sort(bldOrder, bldOrder + bldCount,
              [](const BldEntry& a, const BldEntry& b){ return a.depth < b.depth; });

    // ── Per-building rendering ────────────────────────────────────────
    struct CubeEntry { float cx, cy, cz, fr, fg, fb; float depth; };
    CubeEntry cubes[512];   // up to SZ³ = 512 cubes per building

    for (int bi = 0; bi < bldCount; bi++) {
        const int t = bldOrder[bi].t, s = bldOrder[bi].s;

        // 4 reference projections → linear basis vectors in world space.
        // Avoids calling compute_building_offset for every one of 512 cubes.
        Vec3 ref00  = project_local_corner(3.5f,3.5f,3.5f, 0,0,0, compute_building_offset(0,0,0,t,s));
        Vec3 ref10  = project_local_corner(3.5f,3.5f,3.5f, 0,0,0, compute_building_offset(1,0,0,t,s));
        Vec3 ref01  = project_local_corner(3.5f,3.5f,3.5f, 0,0,0, compute_building_offset(0,1,0,t,s));
        Vec3 ref001 = project_local_corner(3.5f,3.5f,3.5f, 0,0,0, compute_building_offset(0,0,1,t,s));

        // W / V / U step vectors (world space per 1-cube step)
        float dwX=ref10.x-ref00.x, dwY=ref10.y-ref00.y, dwZ=ref10.z-ref00.z;
        float dvX=ref01.x-ref00.x, dvY=ref01.y-ref00.y, dvZ=ref01.z-ref00.z;
        float duX=ref001.x-ref00.x,duY=ref001.y-ref00.y,duZ=ref001.z-ref00.z;

        // Approximate cube screen size from W-step magnitude
        float cubeSize = sqrtf(dwX*dwX + dwY*dwY) * ZOOM;
        if (cubeSize < 1.2f) continue;

        // Half-steps define the top-face parallelogram
        float hwX=dwX*0.5f, hwY=dwY*0.5f;
        float hvX=dvX*0.5f, hvY=dvY*0.5f;

        // U-direction drop for side faces — clamp to minimum visible depth
        float minDrop = 1.5f / ZOOM;
        float faceDropX = duX * 0.48f;
        float faceDropY = (fabsf(duY)*0.48f < minDrop) ? minDrop*(duY<0?-1:1) : duY*0.48f;
        float faceDropZ = duZ * 0.48f;

        // ── Collect visible cubes ────────────────────────────────────
        int cubeCount = 0;
        for (int u = 0; u < SZ && cubeCount < 512; u++) {
            for (int v = 0; v < SZ && cubeCount < 512; v++) {
                for (int w = 0; w < SZ && cubeCount < 512; w++) {
                    float cx = ref00.x + w*dwX + v*dvX + u*duX;
                    float cy = ref00.y + w*dwY + v*dvY + u*duY;
                    float cz = ref00.z + w*dwZ + v*dvZ + u*duZ;

                    float scx = cx*ZOOM + g_panX, scy = cy*ZOOM + g_panY;
                    if (scx+cubeSize<-10||scx-cubeSize>CW+10) continue;
                    if (scy+cubeSize<-10||scy-cubeSize>CH+10) continue;

                    // Occupancy → colour (60% player, 40% glass base)
                    int occIdx = w + SZ*(v + SZ*(u + SZ*(t + SZ*s)));
                    float fr, fg, fb;
                    if (g_cube_occ_set[occIdx]) {
                        fr = g_cube_occ_r[occIdx]*0.60f + GLASS_R*0.40f;
                        fg = g_cube_occ_g[occIdx]*0.60f + GLASS_G*0.40f;
                        fb = g_cube_occ_b[occIdx]*0.60f + GLASS_B*0.40f;
                    } else {
                        fr = GLASS_R; fg = GLASS_G; fb = GLASS_B;
                    }
                    cubes[cubeCount++] = {cx, cy, cz, fr, fg, fb, cz};
                }
            }
        }

        // Sort cubes back → front within this building
        std::sort(cubes, cubes + cubeCount,
                  [](const CubeEntry& a, const CubeEntry& b){ return a.depth < b.depth; });

        // ── Emit glass geometry for each cube ────────────────────────
        // Guard: if this building would overflow the buffer, stop.
        if (g_lod_vout + cubeCount * LOD_VERTS_PER_CUBE > MAX_LOD_VERTS) break;

        for (int i = 0; i < cubeCount; i++) {
            const CubeEntry& e = cubes[i];
            const float cx=e.cx, cy=e.cy, cz=e.cz;
            const float fr=e.fr, fg=e.fg, fb=e.fb;

            // Top-face parallelogram corners (world space, TL/TR/BR/BL order)
            Vec3 TL={cx-hwX-hvX, cy-hwY-hvY, cz};
            Vec3 TR={cx+hwX-hvX, cy+hwY-hvY, cz};
            Vec3 BR={cx+hwX+hvX, cy+hwY+hvY, cz};
            Vec3 BL={cx-hwX+hvX, cy-hwY+hvY, cz};

            // Dropped corners (side-face bases, shifted by U-direction drop)
            Vec3 TL2={TL.x+faceDropX, TL.y+faceDropY, TL.z+faceDropZ};
            Vec3 TR2={TR.x+faceDropX, TR.y+faceDropY, TR.z+faceDropZ};
            Vec3 BR2={BR.x+faceDropX, BR.y+faceDropY, BR.z+faceDropZ};
            Vec3 BL2={BL.x+faceDropX, BL.y+faceDropY, BL.z+faceDropZ};

            // ── 3 face quads with UV ∈ [0,1] ──────────────────────────
            // Edge glow is handled entirely in the fragment shader via UV.
            // UV (0 or 1 at corners) → edgeDist = 0 at edges → full glow.
            // No separate edge geometry needed.

            // Top face (full glass colour)
            emit_lod_quad(TL, TR, BR, BL,  fr,        fg,        fb,        faceAlpha);
            // Right side (50% brightness — facing viewer less)
            emit_lod_quad(TR, TR2, BR2, BR, fr*0.50f, fg*0.50f, fb*0.55f, faceAlpha*0.85f);
            // Left side  (35% brightness — furthest from light)
            emit_lod_quad(BL, BL2, TL2, TL, fr*0.35f, fg*0.35f, fb*0.40f, faceAlpha*0.85f);
        }
    }

    g_lod_vert_count = g_lod_vout;
    return g_lod_vout;
}

// ─── LOD buffer pointer accessors ──────────────────────────────────────
EMSCRIPTEN_KEEPALIVE float* wasm_get_lod_pos_ptr()    { return g_lod_pos_buf; }
EMSCRIPTEN_KEEPALIVE float* wasm_get_lod_col_ptr()    { return g_lod_col_buf; }
EMSCRIPTEN_KEEPALIVE float* wasm_get_lod_dep_ptr()    { return g_lod_dep_buf; }
EMSCRIPTEN_KEEPALIVE float* wasm_get_lod_uv_ptr()     { return g_lod_uv_buf;  }
EMSCRIPTEN_KEEPALIVE int    wasm_get_lod_vert_count() { return g_lod_vert_count; }

} // extern "C"
