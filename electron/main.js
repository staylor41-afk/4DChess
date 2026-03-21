/**
 * 8D Chess — Electron Main Process
 * Samuel M G Taylor
 *
 * Responsibilities:
 *   1. Spawn the C++ chess engine server (chess_engine.exe / chess_engine)
 *      which listens on http://127.0.0.1:8765
 *   2. Open a BrowserWindow loading the game HTML
 *   3. Kill the engine cleanly on exit
 *
 * The game's ENGINE_BRIDGE_URL is already set to 'http://127.0.0.1:8765'.
 * Once the engine process is up and responding, ENGINE_BRIDGE_ACTIVE goes
 * true automatically on the first /new_game call from the HTML.
 *
 * Codex: see CODEX-ENGINE-SPEC.md for the REST API your C++ server must implement.
 */

const { app, BrowserWindow, shell, dialog } = require('electron');
const path  = require('path');
const fs    = require('fs');
const http  = require('http');
const { spawn } = require('child_process');

// ─── Config ──────────────────────────────────────────────────────────────────

const ENGINE_PORT = 8765;
const ENGINE_READY_POLL_MS  = 100;   // how often we ping /ping while waiting
const ENGINE_READY_TIMEOUT  = 8000;  // give up after 8 s (engine boot time)
const GAME_HTML = path.resolve(__dirname, '..', '8d-chess-v45.68-diplomacy-comms-visible.html');

// ─── Engine process management ────────────────────────────────────────────────

let engineProcess = null;

/** Find the engine binary (Windows .exe or Unix binary). */
function findEngineBinary() {
  const candidates = [
    path.join(__dirname, '..', 'chess_engine.exe'),    // dev: sibling of electron/
    path.join(__dirname, '..', 'chess_engine'),
    path.join(process.resourcesPath || '', 'chess_engine.exe'),  // packaged
    path.join(process.resourcesPath || '', 'chess_engine'),
  ];
  return candidates.find(p => {
    try { return fs.statSync(p).isFile(); } catch { return false; }
  }) || null;
}

/** Spawn the engine process and return it (or null if no binary found). */
function startEngine() {
  const bin = findEngineBinary();
  if (!bin) {
    console.log('[electron] No chess_engine binary found — engine bridge inactive.');
    console.log('[electron] Build chess_engine and place it next to the electron/ folder.');
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
  proc.on('exit',  (code, sig) => {
    console.log(`[engine] Exited — code=${code} signal=${sig}`);
    engineProcess = null;
  });

  return proc;
}

/**
 * Poll GET http://127.0.0.1:8765/ping until it returns 200, or timeout.
 * Resolves true when ready, false on timeout.
 */
function waitForEngine(timeoutMs = ENGINE_READY_TIMEOUT) {
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
      setTimeout(ping, ENGINE_READY_POLL_MS);
    }
    ping();
  });
}

// ─── Window ───────────────────────────────────────────────────────────────────

let mainWindow = null;

async function createWindow() {
  mainWindow = new BrowserWindow({
    width:           1440,
    height:          900,
    minWidth:        800,
    minHeight:       600,
    backgroundColor: '#07090f',
    title:           '8D Chess',
    webPreferences: {
      preload:          path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration:  false,
      // Allow file:// page to make http://localhost requests.
      // The chess_render.wasm also needs to be fetchable from file://.
      webSecurity:      false,
    },
  });

  // Open external links in the OS browser, not Electron.
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('http')) shell.openExternal(url);
    return { action: 'deny' };
  });

  mainWindow.on('closed', () => { mainWindow = null; });

  if (!fs.existsSync(GAME_HTML)) {
    dialog.showErrorBox('8D Chess', `Game HTML not found:\n${GAME_HTML}`);
    app.quit();
    return;
  }

  await mainWindow.loadFile(GAME_HTML);
}

// ─── App lifecycle ────────────────────────────────────────────────────────────

app.whenReady().then(async () => {
  // Start engine first, wait for it to bind its port, then open the window.
  // This ensures ENGINE_BRIDGE_ACTIVE can go true on the very first /new_game.
  engineProcess = startEngine();

  if (engineProcess) {
    console.log('[electron] Waiting for engine to accept connections…');
    const ready = await waitForEngine();
    if (ready) {
      console.log(`[electron] Engine ready on port ${ENGINE_PORT}`);
    } else {
      console.warn('[electron] Engine did not become ready in time — starting anyway.');
    }
  }

  await createWindow();

  app.on('activate', () => {
    if (!mainWindow) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (engineProcess) {
    engineProcess.kill();
    engineProcess = null;
  }
  if (process.platform !== 'darwin') app.quit();
});

app.on('will-quit', () => {
  if (engineProcess) {
    engineProcess.kill();
    engineProcess = null;
  }
});
