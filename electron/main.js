/**
 * ND Chess — Electron Main Process
 * Samuel M G Taylor
 *
 * Architecture:
 *   1. Local HTTP file-server (port 3000) serves the entire game root.
 *      file:// cannot reliably load .wasm or make cross-origin fetch to engine;
 *      HTTP solves both problems.
 *
 *   2. Python engine (engine_py.py, port 8769) — committee AI, 2-128 players,
 *      any dimension 2-8.  Started as a child process; killed on quit.
 *
 *   3. C++ engine (chess_engine[.exe], port 8765) — legacy bridge for older
 *      HTML game files.  Optional: app runs fine without it.
 *
 *   4. BrowserWindow → http://localhost:3000/start.html
 *      The launcher lets the user pick dimensions and human player count,
 *      then opens visualizer.html?dims=N&humans=M.
 */

const { app, BrowserWindow, shell, Menu, ipcMain } = require('electron');
const path   = require('path');
const fs     = require('fs');
const http   = require('http');
const { spawn } = require('child_process');

// ─── Paths ────────────────────────────────────────────────────────────────────

// In packaged mode resources live at process.resourcesPath;
// in dev mode they live one directory above the electron/ folder.
const GAME_ROOT = app.isPackaged
  ? process.resourcesPath
  : path.resolve(__dirname, '..');

// ─── Ports ───────────────────────────────────────────────────────────────────

const FILE_PORT    = 3000;
const ENGINE_PORT  = 8765;   // C++ engine (legacy)
const PY_PORT      = 8769;   // Python committee engine (new visualizer)
const PUZZLE_PORT  = 8770;   // C++ collapse-puzzle engine
const POLL_MS      = 120;
const ENGINE_WAIT  = 9000;

// ─── MIME ─────────────────────────────────────────────────────────────────────

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.css':  'text/css',
  '.json': 'application/json',
  '.png':  'image/png',
  '.jpg':  'image/jpeg',
  '.svg':  'image/svg+xml',
  '.ico':  'image/x-icon',
  '.woff2':'font/woff2',
  '.woff': 'font/woff',
  '.ttf':  'font/ttf',
  '.csv':  'text/csv',
  '.txt':  'text/plain',
};

// ─── Local file server ────────────────────────────────────────────────────────

let fileServer = null;

function startFileServer() {
  return new Promise((resolve, reject) => {
    fileServer = http.createServer((req, res) => {
      let urlPath = req.url.split('?')[0];
      try { urlPath = decodeURIComponent(urlPath); } catch {}
      if (urlPath === '/' || urlPath === '') urlPath = '/start.html';

      const filePath = path.join(GAME_ROOT, urlPath.replace(/\//g, path.sep));
      if (!filePath.startsWith(GAME_ROOT)) {
        res.writeHead(403); res.end('Forbidden'); return;
      }

      fs.readFile(filePath, (err, data) => {
        if (err) { res.writeHead(404); res.end('Not found: ' + urlPath); return; }
        const ext  = path.extname(filePath).toLowerCase();
        const mime = MIME[ext] || 'application/octet-stream';
        res.writeHead(200, {
          'Content-Type':                mime,
          'Access-Control-Allow-Origin': '*',
          'Cross-Origin-Opener-Policy':  'same-origin',
          'Cross-Origin-Embedder-Policy':'unsafe-none',
        });
        res.end(data);
      });
    });

    fileServer.listen(FILE_PORT, '127.0.0.1', () => {
      console.log(`[electron] File server → http://127.0.0.1:${FILE_PORT}/`);
      resolve();
    });
    fileServer.on('error', reject);
  });
}

// ─── C++ engine (legacy) ─────────────────────────────────────────────────────

let engineProcess = null;

function findEngineBinary() {
  const candidates = [
    path.join(GAME_ROOT, 'chess_engine.exe'),
    path.join(GAME_ROOT, 'chess_engine'),
    path.join(process.resourcesPath || '', 'chess_engine.exe'),
    path.join(process.resourcesPath || '', 'chess_engine'),
  ];
  return candidates.find(p => { try { return fs.statSync(p).isFile(); } catch { return false; } }) || null;
}

function startCppEngine() {
  const bin = findEngineBinary();
  if (!bin) { console.log('[electron] No C++ engine — legacy bridge inactive.'); return null; }
  console.log(`[electron] C++ engine: ${bin}`);
  const proc = spawn(bin, [String(ENGINE_PORT)], {
    cwd: path.dirname(bin), stdio: ['ignore','pipe','pipe'],
  });
  proc.stdout.on('data', d => console.log('[cpp]', d.toString().trimEnd()));
  proc.stderr.on('data', d => console.error('[cpp]', d.toString().trimEnd()));
  proc.on('exit', (code) => { console.log(`[cpp] exit ${code}`); engineProcess = null; });
  return proc;
}

// ─── Collapse puzzle engine (C++) ────────────────────────────────────────────

let puzzleProcess = null;

function startPuzzleEngine() {
  const candidates = [
    path.join(GAME_ROOT, 'puzzle_engine.exe'),
    path.join(GAME_ROOT, 'puzzle_engine'),
    path.join(process.resourcesPath || '', 'puzzle_engine.exe'),
    path.join(process.resourcesPath || '', 'puzzle_engine'),
  ];
  const bin = candidates.find(p => { try { return fs.statSync(p).isFile(); } catch { return false; } });
  if (!bin) { console.log('[electron] puzzle_engine not found — puzzle mode unavailable.'); return null; }
  console.log(`[electron] Puzzle engine: ${bin}`);
  const proc = spawn(bin, [String(PUZZLE_PORT)], {
    cwd: path.dirname(bin), stdio: ['ignore', 'pipe', 'pipe'],
  });
  proc.stdout.on('data', d => console.log('[puzzle]', d.toString().trimEnd()));
  proc.stderr.on('data', d => console.error('[puzzle]', d.toString().trimEnd()));
  proc.on('error', err => console.error('[puzzle] start failed:', err.message));
  proc.on('exit', code => { console.log(`[puzzle] exit ${code}`); puzzleProcess = null; });
  return proc;
}

// ─── Python committee engine ──────────────────────────────────────────────────

let pyProcess = null;

function startPyEngine() {
  const candidates = [
    path.join(GAME_ROOT, 'engine_py.py'),
    path.join(process.resourcesPath || '', 'engine_py.py'),
  ];
  const script = candidates.find(p => { try { return fs.statSync(p).isFile(); } catch { return false; } });
  if (!script) { console.log('[electron] engine_py.py not found.'); return null; }

  const py = process.platform === 'win32' ? 'python' : 'python3';
  console.log(`[electron] Python engine: ${script} :${PY_PORT}`);
  const proc = spawn(py, [script, String(PY_PORT)], {
    cwd: path.dirname(script), stdio: ['ignore','pipe','pipe'],
  });
  proc.stdout.on('data', d => console.log('[8d-py]', d.toString().trimEnd()));
  proc.stderr.on('data', d => console.error('[8d-py]', d.toString().trimEnd()));
  proc.on('error', err => console.error('[8d-py] start failed:', err.message));
  proc.on('exit', (code) => { console.log(`[8d-py] exit ${code}`); pyProcess = null; });
  return proc;
}

function waitForPort(port, timeoutMs) {
  return new Promise(resolve => {
    const deadline = Date.now() + timeoutMs;
    const ping = () => {
      http.get(`http://127.0.0.1:${port}/ping`, res => {
        if (res.statusCode === 200) { resolve(true); return; }
        retry();
      }).on('error', retry);
    };
    const retry = () => {
      if (Date.now() >= deadline) { resolve(false); return; }
      setTimeout(ping, POLL_MS);
    };
    ping();
  });
}

// ─── Window ───────────────────────────────────────────────────────────────────

let mainWindow = null;

async function createWindow() {
  mainWindow = new BrowserWindow({
    width:  1440,
    height: 920,
    minWidth:  900,
    minHeight: 600,
    backgroundColor: '#010308',
    title: 'ND Chess',
    ...(process.platform === 'darwin' ? {
      titleBarStyle: 'hiddenInset',
      trafficLightPosition: { x: 14, y: 14 },
    } : {}),
    webPreferences: {
      preload:              path.join(__dirname, 'preload.js'),
      contextIsolation:     true,
      nodeIntegration:      false,
      webSecurity:          true,
      backgroundThrottling: false,
    },
  });

  Menu.setApplicationMenu(buildMenu());

  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('http')) shell.openExternal(url);
    return { action: 'deny' };
  });

  // Cmd+Option+I / F12 → DevTools
  mainWindow.webContents.on('before-input-event', (_, input) => {
    if (input.type !== 'keyDown') return;
    const dev = input.key === 'F12' || (input.key === 'i' && input.meta && input.alt);
    if (dev) {
      mainWindow.webContents.isDevToolsOpened()
        ? mainWindow.webContents.closeDevTools()
        : mainWindow.webContents.openDevTools({ mode: 'detach' });
    }
  });

  mainWindow.on('closed', () => { mainWindow = null; });
  await mainWindow.loadURL(`http://127.0.0.1:${FILE_PORT}/start.html`);
}

// ─── Menu ─────────────────────────────────────────────────────────────────────

function openVis(dims, humans = 0) {
  mainWindow?.loadURL(
    `http://127.0.0.1:${FILE_PORT}/visualizer.html?dims=${dims}&humans=${humans}`
  );
}

async function restartPyEngine() {
  if (pyProcess) { pyProcess.kill(); pyProcess = null; await new Promise(r => setTimeout(r, 600)); }
  pyProcess = startPyEngine();
  if (pyProcess) await waitForPort(PY_PORT, 8000);
}

async function restartPuzzleEngine() {
  if (puzzleProcess) { puzzleProcess.kill(); puzzleProcess = null; await new Promise(r => setTimeout(r, 400)); }
  puzzleProcess = startPuzzleEngine();
  if (puzzleProcess) await waitForPort(PUZZLE_PORT, 6000);
}

function buildMenu() {
  const mac = process.platform === 'darwin';
  return Menu.buildFromTemplate([
    ...(mac ? [{ label: app.name, submenu: [
      { role: 'about' }, { type: 'separator' },
      { role: 'hide' }, { role: 'hideOthers' }, { role: 'unhide' },
      { type: 'separator' }, { role: 'quit' },
    ]}] : []),
    { label: 'Game', submenu: [
      { label: 'Launcher',           accelerator: 'CmdOrCtrl+L',
        click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/start.html`) },
      { type: 'separator' },
      { label: '2D chess  (2 players)',   click: () => openVis(2) },
      { label: '3D chess  (4 players)',   click: () => openVis(3) },
      { label: '4D chess  (8 players)',   click: () => openVis(4) },
      { label: '5D chess  (16 players)',  click: () => openVis(5) },
      { label: '6D chess  (32 players)',  click: () => openVis(6) },
      { label: '7D chess  (64 players)',  click: () => openVis(7) },
      { label: '8D chess  (128 players)', click: () => openVis(8) },
      { type: 'separator' },
      { label: 'Restart AI Engine', click: restartPyEngine },
      { type: 'separator' },
      { label: 'Collapse Puzzles', submenu: [
        { label: '2D Puzzles', click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/puzzle.html?dims=2`) },
        { label: '3D Puzzles', click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/puzzle.html?dims=3`) },
        { label: '4D Puzzles', click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/puzzle.html?dims=4`) },
        { label: '5D Puzzles', click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/puzzle.html?dims=5`) },
        { label: '6D Puzzles', click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/puzzle.html?dims=6`) },
        { label: '7D Puzzles', click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/puzzle.html?dims=7`) },
        { label: '8D Puzzles', click: () => mainWindow?.loadURL(`http://127.0.0.1:${FILE_PORT}/puzzle.html?dims=8`) },
        { type: 'separator' },
        { label: 'Restart Puzzle Engine', click: restartPuzzleEngine },
      ]},
    ]},
    { label: 'View', submenu: [
      { role: 'reload' }, { role: 'forceReload' }, { type: 'separator' },
      { role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' },
      { type: 'separator' }, { role: 'togglefullscreen' },
    ]},
    { label: 'Window', submenu: [
      { role: 'minimize' }, { role: 'zoom' },
      ...(mac ? [{ type: 'separator' }, { role: 'front' }] : []),
    ]},
  ]);
}

// ─── IPC ─────────────────────────────────────────────────────────────────────

ipcMain.handle('bridge:status', async () => {
  if (!pyProcess || pyProcess.killed) return { running: false, source: 'none' };
  return new Promise(resolve => {
    http.get(`http://127.0.0.1:${PY_PORT}/ping`, res => {
      resolve({ running: res.statusCode === 200, source: 'spawned', port: PY_PORT });
    }).on('error', err => resolve({ running: false, source: 'spawned', lastError: err.message }));
  });
});

ipcMain.handle('bridge:restart', async () => {
  await restartPyEngine();
  return { ok: !!pyProcess, port: PY_PORT };
});

ipcMain.handle('puzzle:status', async () => {
  if (!puzzleProcess || puzzleProcess.killed) return { running: false };
  return new Promise(resolve => {
    http.get(`http://127.0.0.1:${PUZZLE_PORT}/ping`, res => {
      resolve({ running: res.statusCode === 200, port: PUZZLE_PORT });
    }).on('error', err => resolve({ running: false, lastError: err.message }));
  });
});

ipcMain.handle('puzzle:restart', async () => {
  await restartPuzzleEngine();
  return { ok: !!puzzleProcess, port: PUZZLE_PORT };
});

ipcMain.handle('window:captureClipboard', async () => {
  if (!mainWindow) return false;
  try { return (await mainWindow.webContents.capturePage()).toDataURL(); }
  catch { return false; }
});

// ─── GPU ─────────────────────────────────────────────────────────────────────

app.commandLine.appendSwitch('ignore-gpu-blocklist');
app.commandLine.appendSwitch('enable-gpu-rasterization');
app.commandLine.appendSwitch('enable-zero-copy');
app.commandLine.appendSwitch('enable-features', 'SharedArrayBuffer');

// ─── Lifecycle ────────────────────────────────────────────────────────────────

app.whenReady().then(async () => {
  await startFileServer();

  engineProcess = startCppEngine();
  if (engineProcess) {
    const ok = await waitForPort(ENGINE_PORT, ENGINE_WAIT);
    console.log(ok ? '[electron] C++ engine ready.' : '[electron] C++ engine timed out.');
  }

  pyProcess = startPyEngine();
  if (pyProcess) {
    console.log('[electron] Waiting for Python engine…');
    const ok = await waitForPort(PY_PORT, ENGINE_WAIT);
    console.log(ok ? `[electron] Python engine ready :${PY_PORT}` : '[electron] Python engine timed out.');
  }

  puzzleProcess = startPuzzleEngine();
  if (puzzleProcess) {
    console.log('[electron] Waiting for puzzle engine…');
    const ok = await waitForPort(PUZZLE_PORT, ENGINE_WAIT);
    console.log(ok ? `[electron] Puzzle engine ready :${PUZZLE_PORT}` : '[electron] Puzzle engine timed out.');
  }

  await createWindow();
  app.on('activate', () => { if (!mainWindow) createWindow(); });
});

function cleanup() {
  try { if (engineProcess)  engineProcess.kill();  } catch {}
  try { if (pyProcess)      pyProcess.kill();      } catch {}
  try { if (puzzleProcess)  puzzleProcess.kill();  } catch {}
  try { if (fileServer)     fileServer.close();    } catch {}
}

app.on('window-all-closed', () => { cleanup(); if (process.platform !== 'darwin') app.quit(); });
app.on('will-quit', cleanup);
