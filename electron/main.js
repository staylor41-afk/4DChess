/**
 * 8D Chess — Electron Main Process
 * Samuel M G Taylor
 *
 * Architecture:
 *   1. Spin up a local HTTP file server (port 3000) serving the game root.
 *      This is CRITICAL — file:// cannot load .wasm reliably in Electron.
 *      HTTP lets chess_render.wasm stream correctly and avoids all CORS issues.
 *
 *   2. Spawn chess_engine.exe (port 8765) when available.
 *      The game's ENGINE_BRIDGE_URL points to 127.0.0.1:8765.
 *      Without the engine, the game falls back to its JS AI (slower).
 *
 *   3. Open a BrowserWindow at http://localhost:3000/
 *
 * Why the game froze before:
 *   - file:// loading silently broke wasm streaming → fell back to slow JS
 *   - JS AI for 12-player 8D runs on main thread → blocks rendering frames
 *   - HTTP serving + C++ engine offloads both problems
 */

const { app, BrowserWindow, shell, dialog, Menu } = require('electron');
const path   = require('path');
const fs     = require('fs');
const http   = require('http');
const { spawn } = require('child_process');

// ─── Config ───────────────────────────────────────────────────────────────────

const GAME_ROOT    = path.resolve(__dirname, '..');
const FILE_PORT    = 3000;
const ENGINE_PORT  = 8765;
const POLL_MS      = 100;
const ENGINE_WAIT  = 8000;  // ms to wait for engine to bind

// Always open the latest HTML
const LATEST_HTML  = latestGameHtml();

// ─── MIME types ───────────────────────────────────────────────────────────────

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.wasm': 'application/wasm',          // MUST be application/wasm for streaming compile
  '.css':  'text/css',
  '.json': 'application/json',
  '.png':  'image/png',
  '.jpg':  'image/jpeg',
  '.svg':  'image/svg+xml',
  '.ico':  'image/x-icon',
  '.woff2':'font/woff2',
  '.woff': 'font/woff',
  '.ttf':  'font/ttf',
};

// ─── Local file server ────────────────────────────────────────────────────────

let fileServer = null;

function startFileServer() {
  return new Promise((resolve, reject) => {
    fileServer = http.createServer((req, res) => {
      // Strip query string, decode URI
      let urlPath = req.url.split('?')[0];
      try { urlPath = decodeURIComponent(urlPath); } catch {}

      // Root → latest game HTML
      if (urlPath === '/' || urlPath === '') urlPath = '/' + LATEST_HTML;

      const filePath = path.join(GAME_ROOT, urlPath.replace(/\//g, path.sep));

      // Security: must stay within GAME_ROOT
      if (!filePath.startsWith(GAME_ROOT)) {
        res.writeHead(403); res.end('Forbidden'); return;
      }

      fs.readFile(filePath, (err, data) => {
        if (err) {
          res.writeHead(404); res.end('Not found: ' + urlPath); return;
        }
        const ext  = path.extname(filePath).toLowerCase();
        const mime = MIME[ext] || 'application/octet-stream';
        res.writeHead(200, {
          'Content-Type':  mime,
          // Allow the page to fetch from localhost:8765 (engine) without CORS error
          'Access-Control-Allow-Origin': '*',
          // Needed for SharedArrayBuffer (future feature); harmless otherwise
          'Cross-Origin-Opener-Policy':  'same-origin',
          'Cross-Origin-Embedder-Policy':'require-corp',
        });
        res.end(data);
      });
    });

    fileServer.listen(FILE_PORT, '127.0.0.1', () => {
      console.log(`[electron] File server: http://127.0.0.1:${FILE_PORT}/`);
      resolve();
    });
    fileServer.on('error', reject);
  });
}

// ─── Engine binary ────────────────────────────────────────────────────────────

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

function startEngine() {
  const bin = findEngineBinary();
  if (!bin) {
    console.log('[electron] No chess_engine binary — engine bridge inactive (JS AI fallback).');
    return null;
  }
  console.log(`[electron] Starting engine: ${bin}`);
  const proc = spawn(bin, ['--port', String(ENGINE_PORT)], {
    cwd: path.dirname(bin),
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  proc.stdout.on('data', d => console.log('[engine]', d.toString().trimEnd()));
  proc.stderr.on('data', d => console.error('[engine]', d.toString().trimEnd()));
  proc.on('error', err => console.error('[engine] Failed to start:', err.message));
  proc.on('exit', (code, sig) => {
    console.log(`[engine] Exited code=${code} sig=${sig}`);
    engineProcess = null;
  });
  return proc;
}

function waitForEngine(timeoutMs = ENGINE_WAIT) {
  return new Promise(resolve => {
    const deadline = Date.now() + timeoutMs;
    function ping() {
      http.get(`http://127.0.0.1:${ENGINE_PORT}/ping`, res => {
        if (res.statusCode === 200) { resolve(true); return; }
        retry();
      }).on('error', retry);
    }
    function retry() {
      if (Date.now() >= deadline) { resolve(false); return; }
      setTimeout(ping, POLL_MS);
    }
    ping();
  });
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

function latestGameHtml() {
  try {
    const files = fs.readdirSync(path.resolve(__dirname, '..'))
      .filter(f => /^8d-chess-v[\d.]+.*\.html$/i.test(f))
      .sort((a, b) => {
        // Sort by version number
        const va = a.match(/v([\d.]+)/)?.[1].split('.').map(Number) || [];
        const vb = b.match(/v([\d.]+)/)?.[1].split('.').map(Number) || [];
        for (let i = 0; i < Math.max(va.length, vb.length); i++) {
          const d = (va[i] || 0) - (vb[i] || 0);
          if (d) return d;
        }
        return 0;
      });
    const latest = files[files.length - 1];
    console.log(`[electron] Latest game: ${latest}`);
    return latest;
  } catch {
    return '8d-chess-v45.70-diplomacy-comms-visible.html';
  }
}

// ─── Window ───────────────────────────────────────────────────────────────────

let mainWindow = null;

async function createWindow() {
  mainWindow = new BrowserWindow({
    width:           1440,
    height:          920,
    minWidth:        900,
    minHeight:       600,
    backgroundColor: '#07090f',
    title:           '8D Chess',
    webPreferences: {
      preload:           path.join(__dirname, 'preload.js'),
      contextIsolation:  true,
      nodeIntegration:   false,
      webSecurity:       true,   // safe — we're on localhost now, not file://
      // Extra GPU/rendering flags for smooth WebGL
      backgroundThrottling: false,  // don't throttle rAF when window is in bg
    },
  });

  // Remove default menu (saves screen space, avoids accidental keyboard nav)
  Menu.setApplicationMenu(null);

  // External links → OS browser
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('http')) shell.openExternal(url);
    return { action: 'deny' };
  });

  // F12 opens DevTools for debugging
  mainWindow.webContents.on('before-input-event', (_, input) => {
    if (input.key === 'F12' && input.type === 'keyDown') {
      mainWindow.webContents.isDevToolsOpened()
        ? mainWindow.webContents.closeDevTools()
        : mainWindow.webContents.openDevTools({ mode: 'detach' });
    }
  });

  mainWindow.on('closed', () => { mainWindow = null; });

  const url = `http://127.0.0.1:${FILE_PORT}/`;
  console.log(`[electron] Loading: ${url}`);
  await mainWindow.loadURL(url);
}

// ─── App lifecycle ────────────────────────────────────────────────────────────

app.commandLine.appendSwitch('enable-features', 'SharedArrayBuffer');
app.commandLine.appendSwitch('ignore-gpu-blocklist');          // force GPU on
app.commandLine.appendSwitch('enable-gpu-rasterization');
app.commandLine.appendSwitch('enable-zero-copy');

app.whenReady().then(async () => {
  // 1. Start local file server (must come first)
  await startFileServer();

  // 2. Start engine (optional), wait for it to be ready
  engineProcess = startEngine();
  if (engineProcess) {
    console.log('[electron] Waiting for engine…');
    const ok = await waitForEngine();
    console.log(ok ? '[electron] Engine ready.' : '[electron] Engine timeout — using JS AI.');
  }

  // 3. Open window
  await createWindow();

  app.on('activate', () => { if (!mainWindow) createWindow(); });
});

function cleanup() {
  if (engineProcess) { engineProcess.kill(); engineProcess = null; }
  if (fileServer)    { fileServer.close();   fileServer    = null; }
}

app.on('window-all-closed', () => { cleanup(); if (process.platform !== 'darwin') app.quit(); });
app.on('will-quit', cleanup);
