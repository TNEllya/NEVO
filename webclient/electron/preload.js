const { contextBridge, ipcRenderer } = require('electron');

// Expose safe window-control APIs to the renderer process
contextBridge.exposeInMainWorld('electronAPI', {
  minimizeWindow: () => ipcRenderer.send('window-minimize'),
  maximizeWindow: () => ipcRenderer.send('window-maximize'),
  closeWindow: () => ipcRenderer.send('window-close'),
  onMaximizedChange: (callback) => {
    ipcRenderer.on('window-is-maximized', (_event, value) => callback(value));
  },
});

// Online updater API
contextBridge.exposeInMainWorld('updaterAPI', {
  appVersion: () => ipcRenderer.invoke('app:version'),
  checkNow: () => ipcRenderer.invoke('updater:check'),
  download: () => ipcRenderer.invoke('updater:download'),
  restartToApply: () => ipcRenderer.invoke('updater:restart'),
  getStatus: () => ipcRenderer.invoke('updater:status'),
  getLog: () => ipcRenderer.invoke('updater:log'),
  onState: (callback) => {
    const listener = (_event, data) => callback(data);
    ipcRenderer.on('updater:state', listener);
    return () => ipcRenderer.removeListener('updater:state', listener);
  },
  onProgress: (callback) => {
    const listener = (_event, data) => callback(data);
    ipcRenderer.on('updater:progress', listener);
    return () => ipcRenderer.removeListener('updater:progress', listener);
  },
  probeRoutes: () => ipcRenderer.invoke('updater:probe'),
  setAutoCheck: (enabled) => ipcRenderer.invoke('updater:set-auto-check', enabled),
  onProbeResult: (callback) => {
    const listener = (_event, data) => callback(data);
    ipcRenderer.on('updater:probe-result', listener);
    return () => ipcRenderer.removeListener('updater:probe-result', listener);
  },
});
