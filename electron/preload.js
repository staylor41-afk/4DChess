const { contextBridge, ipcRenderer, clipboard, nativeImage } = require('electron');

const desktopApi = {
  getBridgeStatus: () => ipcRenderer.invoke('bridge:status'),
  restartBridge: () => ipcRenderer.invoke('bridge:restart'),
  captureWindowToClipboard: () => ipcRenderer.invoke('window:captureClipboard'),
  copyImageDataUrl: (dataUrl) => {
    if (typeof dataUrl !== 'string' || !dataUrl.startsWith('data:image/')) return false;
    const image = nativeImage.createFromDataURL(dataUrl);
    if (image.isEmpty()) return false;
    clipboard.writeImage(image);
    return true;
  },
  onBridgeStatus: handler => {
    const wrapped = (_event, status) => handler(status);
    ipcRenderer.on('bridge-status', wrapped);
    return () => ipcRenderer.removeListener('bridge-status', wrapped);
  },
};

contextBridge.exposeInMainWorld('eightdDesktop', desktopApi);
