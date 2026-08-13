/* NEVO Web Client — Gateway client (WebSocket <-> Python gateway) */
const Gateway = (() => {
  let ws = null;
  let url = '';
  let reqId = 0;
  const pending = new Map();        // req_id -> {resolve, reject}
  const listeners = {};             // event -> Set(fn)
  let reconnectTimer = null;
  let manualClose = false;
  let statusListeners = new Set();

  function buildUrl() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${proto}//${location.host}/ws`;
  }

  function emitStatus(s) { statusListeners.forEach(fn => { try { fn(s); } catch (e) {} }); }

  function on(event, fn) {
    (listeners[event] = listeners[event] || new Set()).add(fn);
  }
  function off(event, fn) { if (listeners[event]) listeners[event].delete(fn); }

  function send(command, params) {
    return new Promise((resolve, reject) => {
      if (!ws || ws.readyState !== 1) { reject(new Error('未连接到网关')); return; }
      const id = ++reqId;
      pending.set(id, { resolve, reject });
      ws.send(JSON.stringify({ id, command, params: params || {} }));
      // timeout safety
      setTimeout(() => {
        if (pending.has(id)) {
          pending.delete(id);
          reject(new Error('请求超时'));
        }
      }, 15000);
    });
  }

  function handleMessage(raw) {
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }
    if (msg.type === 'response') {
      const p = pending.get(msg.req_id);
      if (p) {
        pending.delete(msg.req_id);
        if (msg.status === 'ok') p.resolve(msg.data);
        else p.reject(Object.assign(new Error((msg.data && msg.data.message) || '错误'), { code: msg.data && msg.data.code }));
      }
      return;
    }
    if (msg.type === 'event') {
      const fns = listeners[msg.event];
      if (fns) fns.forEach(fn => { try { fn(msg.data || {}); } catch (e) { console.error(e); } });
    }
  }

  function connect() {
    manualClose = false;
    url = buildUrl();
    if (ws) { try { ws.close(); } catch {} }
    emitStatus('connecting');
    try {
      ws = new WebSocket(url);
    } catch (e) {
      emitStatus('closed');
      scheduleReconnect();
      return;
    }
    ws.onopen = () => { emitStatus('open'); };
    ws.onmessage = (ev) => handleMessage(ev.data);
    ws.onerror = () => { /* status handled by close */ };
    ws.onclose = () => {
      emitStatus('closed');
      // reject pending
      pending.forEach((p) => p.reject(new Error('连接关闭')));
      pending.clear();
      if (!manualClose) scheduleReconnect();
    };
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(() => { reconnectTimer = null; connect(); }, 2500);
  }

  function close() {
    manualClose = true;
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    if (ws) { try { ws.close(); } catch {} }
  }

  function onStatus(fn) { statusListeners.add(fn); return () => statusListeners.delete(fn); }

  return { connect, close, send, on, off, onStatus,
    get ready() { return ws && ws.readyState === 1; } };
})();
