const { app, BrowserWindow, ipcMain, session } = require('electron');
const { spawn } = require('child_process');
const path = require('path');
const http = require('http');
const net = require('net');
const fs = require('fs');
const updater = require('./updater.js');

const GATEWAY_HOST = '127.0.0.1';
const GATEWAY_PORT = 8088;
const GATEWAY_URL = `http://${GATEWAY_HOST}:${GATEWAY_PORT}`;

let mainWindow = null;
let gatewayProcess = null;
let isQuitting = false;

// Determine the gateway executable path.
// In packaged app: process.resourcesPath/nevo_gateway/nevo_gateway.exe
// In dev (running from electron/ dir): ../build_pyinstaller/nevo_gateway/nevo_gateway.exe
function getGatewayExePath() {
  const exeName = process.platform === 'win32' ? 'nevo_gateway.exe' : 'nevo_gateway';
  // Packaged: resources/nevo_gateway/nevo_gateway.exe
  const packagedPath = path.join(process.resourcesPath, 'nevo_gateway', exeName);
  if (fs.existsSync(packagedPath)) {
    return packagedPath;
  }
  // Dev: ../build_pyinstaller/nevo_gateway/nevo_gateway.exe
  const devPath = path.join(__dirname, '..', 'build_pyinstaller', 'nevo_gateway', exeName);
  return devPath;
}

// Check if the gateway HTTP server is responding
function waitForGateway(maxAttempts = 60, intervalMs = 500) {
  return new Promise((resolve, reject) => {
    let attempts = 0;
    const check = () => {
      attempts++;
      const req = http.get(`${GATEWAY_URL}/`, { timeout: 2000 }, (res) => {
        res.resume();
        if (res.statusCode === 200) {
          resolve();
        } else if (attempts < maxAttempts) {
          setTimeout(check, intervalMs);
        } else {
          reject(new Error(`Gateway returned status ${res.statusCode}`));
        }
      });
      req.on('error', () => {
        if (attempts < maxAttempts) {
          setTimeout(check, intervalMs);
        } else {
          reject(new Error('Gateway did not start within timeout'));
        }
      });
      req.on('timeout', () => {
        req.destroy();
        if (attempts < maxAttempts) {
          setTimeout(check, intervalMs);
        } else {
          reject(new Error('Gateway connection timeout'));
        }
      });
    };
    check();
  });
}

// Check if a TCP port is already in use (gateway might already be running)
function isPortInUse(port, host = '127.0.0.1') {
  return new Promise((resolve) => {
    const tester = net.createConnection({ port, host });
    tester.on('connect', () => {
      tester.destroy();
      resolve(true);
    });
    tester.on('error', () => {
      resolve(false);
    });
  });
}

function startGateway() {
  const exePath = getGatewayExePath();
  console.log(`[NEVO] Starting gateway: ${exePath}`);

  gatewayProcess = spawn(exePath, [], {
    cwd: path.dirname(exePath),
    env: {
      ...process.env,
      NEVO_WEB_HOST: GATEWAY_HOST,
      NEVO_WEB_PORT: String(GATEWAY_PORT),
    },
    windowsHide: false,
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  gatewayProcess.stdout.on('data', (data) => {
    console.log(`[Gateway] ${data.toString().trim()}`);
  });

  gatewayProcess.stderr.on('data', (data) => {
    console.error(`[Gateway ERR] ${data.toString().trim()}`);
  });

  gatewayProcess.on('exit', (code) => {
    console.log(`[Gateway] Process exited with code ${code}`);
    if (!isQuitting) {
      // Gateway crashed unexpectedly
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.loadURL(`data:text/html,<h1 style="font-family:sans-serif;color:#333;padding:40px;">Gateway process stopped (code ${code}). Please restart the app.</h1>`);
      }
    }
  });
}

function stopGateway() {
  if (gatewayProcess && !gatewayProcess.killed) {
    console.log('[NEVO] Stopping gateway...');
    try {
      // On Windows, use taskkill to ensure child processes are terminated
      if (process.platform === 'win32') {
        spawn('taskkill', ['/pid', gatewayProcess.pid, '/f', '/t'], {
          windowsHide: true,
          stdio: 'ignore',
        });
      } else {
        gatewayProcess.kill('SIGTERM');
      }
    } catch (e) {
      console.error('[NEVO] Error stopping gateway:', e);
    }
    gatewayProcess = null;
  }
}

async function createWindow() {
  // Check if gateway is already running (e.g. from a previous instance)
  const alreadyRunning = await isPortInUse(GATEWAY_PORT, GATEWAY_HOST);

  if (!alreadyRunning) {
    startGateway();
    try {
      console.log('[NEVO] Waiting for gateway to be ready...');
      await waitForGateway();
      console.log('[NEVO] Gateway is ready!');
    } catch (err) {
      console.error('[NEVO] Failed to start gateway:', err.message);
    }
  } else {
    console.log('[NEVO] Gateway already running, connecting to it.');
  }

  const appIcon = fs.existsSync(path.join(__dirname, 'app-icon.ico'))
    ? path.join(__dirname, 'app-icon.ico')
    : undefined;

  mainWindow = new BrowserWindow({
    width: 1280,
    height: 800,
    minWidth: 900,
    minHeight: 600,
    title: 'NEVO Web Client',
    icon: appIcon,
    backgroundColor: '#0F0D0A',
    autoHideMenuBar: true,
    frame: false,                       // Hide native title bar to match Oopz-style UI
    titleBarStyle: 'hidden',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      preload: path.join(__dirname, 'preload.js'),
    },
  });

  // 启动时清除 Electron 缓存，避免旧版本 JS/CSS 被复用。
  // 注意：不清理 localstorage —— 其中保存用户设置（主题/服务器收藏/音量/自动检测开关），
  // 清理会导致每次重启后用户设置丢失（T-06 修复）。
  try {
    await session.defaultSession.clearCache();
    await session.defaultSession.clearStorageData({ storages: ['appcache', 'cookies', 'filesystem', 'indexdb', 'shadercache', 'websql', 'serviceworkers'] });
    console.log('[NEVO] Renderer cache cleared');
  } catch (e) {
    console.error('[NEVO] Failed to clear cache:', e);
  }

  mainWindow.loadURL(GATEWAY_URL);

  // 在线更新：定时检测 + 注册 IPC
  startUpdaterService(mainWindow);

  // DevTools disabled for production
  // mainWindow.webContents.openDevTools({ mode: 'detach' });

  // Forward window-state changes to renderer for custom title bar
  mainWindow.on('maximize', () => {
    mainWindow.webContents.send('window-is-maximized', true);
  });
  mainWindow.on('unmaximize', () => {
    mainWindow.webContents.send('window-is-maximized', false);
  });

  // Prevent in-window navigation and open external links in system browser
  mainWindow.webContents.on('will-navigate', (event, url) => {
    if (url === GATEWAY_URL || url.startsWith(GATEWAY_URL + '/')) {
      return;
    }
    event.preventDefault();
    if (url.startsWith('http://') || url.startsWith('https://')) {
      require('electron').shell.openExternal(url);
    }
  });

  // Open external links in default browser
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('http://') || url.startsWith('https://')) {
      require('electron').shell.openExternal(url);
      return { action: 'deny' };
    }
    return { action: 'deny' };
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

// Application version: packaged reads package.json buildVersion (changes with each
// release/update), dev (unpackaged `electron .`) shows "dev".
const APP_VERSION = app.isPackaged
  ? (JSON.parse(fs.readFileSync(path.join(__dirname, 'package.json'), 'utf-8')).buildVersion || '0.0.0')
  : 'dev';
ipcMain.handle('app:version', () => APP_VERSION);

// Custom title-bar controls
ipcMain.on('window-minimize', () => {
  if (mainWindow) mainWindow.minimize();
});
ipcMain.on('window-maximize', () => {
  if (!mainWindow) return;
  if (mainWindow.isMaximized()) {
    mainWindow.unmaximize();
  } else {
    mainWindow.maximize();
  }
});
ipcMain.on('window-close', () => {
  if (mainWindow) mainWindow.close();
});

// Ensure only a single instance of the app is running
const gotTheLock = app.requestSingleInstanceLock();
if (!gotTheLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore();
      mainWindow.focus();
    }
  });

  app.whenReady().then(createWindow);

  app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') {
      app.quit();
    }
  });

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });

  app.on('before-quit', () => {
    isQuitting = true;
    stopGateway();
  });

  process.on('exit', () => {
    stopGateway();
  });
}

// ============================================================
// Online updater service
// ============================================================
const updateEngine = new updater.UpdateEngine();

let updaterServiceReady = false;
let updaterAutoCheck = true;

function checkNowQuiet() {
  if (!updaterAutoCheck) return;
  updateEngine.checkForUpdates().catch(() => {});
}

function startUpdaterService(win) {
  const send = (channel, data) => {
    if (win && !win.isDestroyed()) win.webContents.send(channel, data);
  };
  if (updaterServiceReady) {
    send('updater:state', { state: updateEngine.state, currentVersion: updateEngine.currentVersion });
    return;
  }
  updaterServiceReady = true;

  updateEngine.onState((oldState, newState) => send('updater:state', { state: newState }));
  updateEngine.onProgress((percent, speed, downloaded, total) => {
    send('updater:progress', { percent, speed, downloaded, total });
  });
  send('updater:state', { state: updateEngine.state, currentVersion: updateEngine.currentVersion });

  ipcMain.handle('updater:check', async () => {
    try { return { ok: true, info: await updateEngine.checkForUpdates() }; }
    catch (e) { return { ok: false, error: e.message }; }
  });
  ipcMain.handle('updater:download', async () => {
    try { return { ok: true, result: await updateEngine.downloadUpdate() }; }
    catch (e) { return { ok: false, error: e.message }; }
  });
  ipcMain.handle('updater:restart', () => { updateEngine.restartToApply(); return { ok: true }; });
  ipcMain.handle('updater:status', () => ({
    state: updateEngine.state,
    currentVersion: updateEngine.currentVersion,
    info: updateEngine._manifest ? { version: updateEngine._manifest.version, mode: updateEngine._mode, source: updateEngine._source } : null,
  }));
  ipcMain.handle('updater:log', () => updater.readUpdateLog(updateEngine.baseDir));

  // 多线路测速：返回线路状态列表
  ipcMain.handle('updater:probe', async () => {
    try {
      const results = await updateEngine.probeAllRoutes();
      return { ok: true, routes: results };
    } catch (e) { return { ok: false, error: e.message }; }
  });

  // 自动检测开关：控制定时任务
  ipcMain.handle('updater:set-auto-check', (_e, enabled) => {
    updaterAutoCheck = !!enabled;
    return { ok: true, autoCheck: updaterAutoCheck };
  });

  // 定时检测（首次 30s 后，之后每小时；受 autoCheck 控制）
  setTimeout(checkNowQuiet, 30000);
  setInterval(checkNowQuiet, updater.CFG.checkIntervalMs);
}
