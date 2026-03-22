const { app, BrowserWindow, ipcMain, clipboard } = require('electron');
const path = require('path');
const fs = require('fs');
const http = require('http');
const { spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const BRIDGE_PORT = 8765;
const BRIDGE_URL = `http://127.0.0.1:${BRIDGE_PORT}`;
function newestBridgeExe() {
  const dir = path.join(ROOT, 'build-cl');
  if (!fs.existsSync(dir)) {
    return path.join(dir, 'eightd_bridge.exe');
  }
  const files = fs.readdirSync(dir)
    .filter(name => /^eightd_bridge(?!_test).*\.exe$/i.test(name))
    .map(name => {
      const full = path.join(dir, name);
      return { name, full, mtime: fs.statSync(full).mtimeMs };
    })
    .sort((a, b) => b.mtime - a.mtime);
  if (!files.length) {
    return path.join(dir, 'eightd_bridge.exe');
  }
  return files[0].full;
}
let mainWindow = null;
let bridgeProcess = null;
let bridgeState = {
  desired: true,
  running: false,
  source: 'none',
  lastError: '',
  exe: newestBridgeExe(),
};

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function pingBridge(timeoutMs = 500) {
  return new Promise(resolve => {
    const req = http.get(`${BRIDGE_URL}/ping`, { timeout: timeoutMs }, res => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', chunk => { body += chunk; });
      res.on('end', () => {
        resolve(res.statusCode === 200 && body.includes('"ok"'));
      });
    });
    req.on('error', () => resolve(false));
    req.on('timeout', () => {
      req.destroy();
      resolve(false);
    });
  });
}

function newestHtmlBuild() {
  const files = fs.readdirSync(ROOT)
    .filter(name => /^8d-chess-v.*\.html$/i.test(name))
    .map(name => {
      const full = path.join(ROOT, name);
      return { name, full, mtime: fs.statSync(full).mtimeMs };
    })
    .sort((a, b) => b.mtime - a.mtime);
  if (!files.length) {
    throw new Error('No 8D Chess HTML build found in workspace root.');
  }
  return files[0].full;
}

async function ensureBridgeRunning() {
  const bridgeExe = newestBridgeExe();
  bridgeState.exe = bridgeExe;
  bridgeState.desired = true;
  if (await pingBridge()) {
    bridgeState.running = true;
    bridgeState.source = 'existing';
    bridgeState.lastError = '';
    return true;
  }
  if (!fs.existsSync(bridgeExe)) {
    bridgeState.running = false;
    bridgeState.source = 'missing';
    bridgeState.lastError = `Bridge executable not found: ${bridgeExe}`;
    return false;
  }
  if (!bridgeProcess || bridgeProcess.killed) {
    bridgeProcess = spawn(bridgeExe, [], {
      cwd: path.dirname(bridgeExe),
      windowsHide: true,
      stdio: 'ignore',
    });
    bridgeProcess.on('exit', code => {
      bridgeState.running = false;
      bridgeState.source = 'exited';
      bridgeState.lastError = code == null ? 'Bridge exited.' : `Bridge exited with code ${code}.`;
      bridgeProcess = null;
      sendBridgeStatus();
    });
    bridgeProcess.on('error', err => {
      bridgeState.running = false;
      bridgeState.source = 'error';
      bridgeState.lastError = err.message || String(err);
      sendBridgeStatus();
    });
  }
  for (let i = 0; i < 24; i++) {
    if (await pingBridge(700)) {
      bridgeState.running = true;
      bridgeState.source = 'spawned';
      bridgeState.lastError = '';
      return true;
    }
    await sleep(250);
  }
  bridgeState.running = false;
  bridgeState.source = 'timeout';
  bridgeState.lastError = `Timed out waiting for ${path.basename(bridgeExe)} to answer on port 8765.`;
  return false;
}

function stopBridge() {
  if (bridgeProcess && !bridgeProcess.killed) {
    bridgeProcess.kill();
  }
  bridgeProcess = null;
}

function sendBridgeStatus() {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send('bridge-status', bridgeState);
  }
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1600,
    height: 980,
    minWidth: 1200,
    minHeight: 760,
    backgroundColor: '#06080f',
    autoHideMenuBar: false,
    webPreferences: {
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js'),
    },
  });

  const htmlFile = newestHtmlBuild();
  mainWindow.loadFile(htmlFile);
  mainWindow.webContents.on('did-finish-load', () => {
    sendBridgeStatus();
  });
}

ipcMain.handle('bridge:status', async () => {
  const running = await pingBridge(300);
  bridgeState.running = running;
  if (running && bridgeState.source === 'none') bridgeState.source = 'existing';
  return bridgeState;
});

ipcMain.handle('bridge:restart', async () => {
  stopBridge();
  bridgeState.running = false;
  bridgeState.source = 'restarting';
  bridgeState.lastError = '';
  const ok = await ensureBridgeRunning();
  sendBridgeStatus();
  return { ok, ...bridgeState };
});
ipcMain.handle('window:captureClipboard', async () => {
  if (!mainWindow || mainWindow.isDestroyed()) return false;
  const image = await mainWindow.capturePage();
  if (!image || image.isEmpty()) return false;
  clipboard.writeImage(image);
  return true;
});

app.whenReady().then(async () => {
  await ensureBridgeRunning();
  createWindow();
});

app.on('before-quit', () => {
  stopBridge();
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});
