/**
 * 8D Chess — Electron Preload
 *
 * Exposes safe Electron context to the renderer (game HTML).
 * contextIsolation: true means the renderer cannot access Node.js directly,
 * but anything exposed via contextBridge IS available as window.electronBridge.
 *
 * The game HTML can check `window.electronBridge?.isElectron === true`
 * to know it is running inside Electron vs a plain browser.
 */
const { contextBridge } = require('electron');

contextBridge.exposeInMainWorld('electronBridge', {
  isElectron: true,
  platform:   process.platform,  // 'win32' | 'darwin' | 'linux'
  version:    process.env.npm_package_version || '45.68.0',
});
