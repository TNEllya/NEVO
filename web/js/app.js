/**
 * NEVO Server Management — Application v4
 * Targeted DOM updates + real-time charts + batch kick + channel CRUD + server control + SSE live logs.
 * Poll interval: 1s. Font: MiSans.
 */
(function () {
  "use strict";

  const state = {
    currentPage: "dashboard",
    serverOnline: false,
    polling: null,
    pollInterval: 1000,
    data: { status: null, sessions: [], channels: [], config: null, metrics: null, logs: [] },
    history: { cpu: [], memory: [], maxPoints: 180 },
    chartCtx: null,
    selectedSessions: new Set(),
    selectedChannels: new Set(),
    sse: null,
  };

  const $ = (sel) => document.querySelector(sel);
  const $$ = (sel) => document.querySelectorAll(sel);

  const dom = {
    content: $("#content"),
    pageTitle: $("#page-title"),
    navLinks: $$("#sidebar-nav a"),
    connectionDot: $("#connection-dot"),
    connectionLabel: $("#connection-label"),
    uptimeDisplay: $("#uptime-display"),
    btnRefresh: $("#btn-refresh"),
    btnShutdown: $("#btn-shutdown"),
    menuOpen: $("#menu-open"),
    menuClose: $("#menu-close"),
    sidebar: $("#sidebar"),
    toastContainer: $(".toast-container"),
    modalRoot: $("#modal-root"),
  };

  // ============================================================
  // API
  // ============================================================
  async function api(endpoint, method = "GET", body = null) {
    const opts = { method, headers: { "Content-Type": "application/json" } };
    if (body) opts.body = JSON.stringify(body);
    try {
      const res = await fetch(endpoint, opts);
      return await res.json();
    } catch {
      return { status: "error", data: { message: "Network error" } };
    }
  }

  async function fetchStatus() {
    const r = await api("/api/status");
    state.serverOnline = r.status === "ok";
    state.data.status = r.data || null;
    updateConnectionIndicator();
    return r;
  }

  async function fetchSessions() {
    const r = await api("/api/sessions");
    state.data.sessions = r.data?.sessions || [];
    return r;
  }

  async function fetchChannels() {
    const r = await api("/api/channels");
    state.data.channels = r.data?.channels || [];
    return r;
  }

  async function fetchConfig() {
    const r = await api("/api/config");
    state.data.config = r.data || null;
    return r;
  }

  async function fetchMetrics() {
    const r = await api("/api/metrics");
    const m = r.data || null;
    state.data.metrics = m;
    if (m) {
      const now = Date.now();
      state.history.cpu.push({ t: now, v: m.cpu_percent ?? 0 });
      state.history.memory.push({ t: now, v: m.memory_percent });
      if (m.memory_mb > 0 && m.memory_percent <= 0) {
        state.history.memory[state.history.memory.length - 1].v = m.memory_mb;
      }
      if (state.history.cpu.length > state.history.maxPoints) {
        state.history.cpu.shift();
        state.history.memory.shift();
      }
    }
    return r;
  }

  async function fetchLogs() {
    const r = await api("/api/logs?limit=100");
    state.data.logs = r.data || [];
    return r;
  }

  async function fetchAll() {
    await Promise.all([fetchStatus(), fetchSessions(), fetchChannels(), fetchConfig(), fetchMetrics(), fetchLogs()]);
    updatePageData();
  }

  // ============================================================
  // SSE Live Logs
  // ============================================================
  function connectSSE() {
    if (state.sse) { try { state.sse.close(); } catch {} state.sse = null; }
    try {
      const es = new EventSource("/api/logs/stream");
      es.onmessage = function (e) {
        try {
          const entry = JSON.parse(e.data);
          if (entry && entry.timestamp) {
            state.data.logs.push(entry);
            if (state.data.logs.length > 200) state.data.logs.shift();
            if (state.currentPage === "server") updateServerPage();
          }
        } catch {}
      };
      es.onerror = function () {
        setTimeout(connectSSE, 5000);
      };
      state.sse = es;
    } catch {}
  }

  // ============================================================
  // Connection indicator
  // ============================================================
  function updateConnectionIndicator() {
    dom.connectionDot.className = "dot " + (state.serverOnline ? "online" : "offline");
    dom.connectionLabel.textContent = state.serverOnline ? "服务运行中" : "未连接";
  }

  // ============================================================
  // Polling
  // ============================================================
  function startPolling() { stopPolling(); state.polling = setInterval(fetchAll, state.pollInterval); }
  function stopPolling() { if (state.polling) { clearInterval(state.polling); state.polling = null; } }

  // ============================================================
  // Toast & Modal
  // ============================================================
  function toast(message, type = "info") {
    const el = document.createElement("div");
    el.className = "toast " + type;
    el.setAttribute("role", "alert");
    el.innerHTML = "<span>" + escapeHtml(message) + "</span>";
    dom.toastContainer.appendChild(el);
    setTimeout(function () { el.classList.add("fade-out"); el.addEventListener("animationend", function () { el.remove(); }); }, 3500);
  }

  function showModal(title, body, actions) {
    dom.modalRoot.innerHTML = '<div class="modal-overlay" role="dialog" aria-modal="true" aria-labelledby="modal-title"><div class="modal"><h3 id="modal-title">' + escapeHtml(title) + '</h3><div>' + body + '</div><div class="modal-actions">' + actions + '</div></div></div>';
    var overlay = dom.modalRoot.querySelector(".modal-overlay");
    overlay.addEventListener("click", function (e) { if (e.target === overlay) closeModal(); });
    var cancelBtn = overlay.querySelector(".modal-cancel");
    if (cancelBtn) cancelBtn.focus();
  }

  function closeModal() { dom.modalRoot.innerHTML = ""; }

  function confirmAction(title, message, confirmLabel, onConfirm) {
    showModal(title,
      "<p>" + escapeHtml(message) + "</p>",
      '<button class="btn modal-cancel">取消</button><button class="btn danger" id="modal-confirm-btn">' + escapeHtml(confirmLabel) + '</button>');
    document.getElementById("modal-confirm-btn").addEventListener("click", function () {
      closeModal();
      onConfirm();
    });
  }

  // ============================================================
  // Utilities
  // ============================================================
  function escapeHtml(str) { var d = document.createElement("div"); d.textContent = str; return d.innerHTML; }
  function formatUptime(seconds) {
    if (!seconds || seconds <= 0) return "—";
    var d = Math.floor(seconds / 86400), h = Math.floor((seconds % 86400) / 3600), m = Math.floor((seconds % 3600) / 60);
    if (d > 0) return d + "d " + h + "h " + m + "m";
    if (h > 0) return h + "h " + m + "m " + (seconds % 60) + "s";
    return m + "m " + (seconds % 60) + "s";
  }
  function formatNumber(n) {
    if (n === null || n === undefined) return "—";
    if (n >= 1000000) return (n / 1000000).toFixed(1) + "M";
    if (n >= 1000) return (n / 1000).toFixed(1) + "K";
    return String(n);
  }
  function statusBadge(val) {
    if (val === "authenticated") return '<span class="badge green">在线</span>';
    if (val === "authenticating") return '<span class="badge amber">认证中</span>';
    return '<span class="badge neutral">—</span>';
  }
  function timeAgo(ts) {
    var diff = Date.now() - ts;
    if (diff < 60000) return Math.floor(diff / 1000) + "s 前";
    if (diff < 3600000) return Math.floor(diff / 60000) + "m 前";
    if (diff < 86400000) return Math.floor(diff / 3600000) + "h 前";
    return new Date(ts).toLocaleString();
  }
  function logBadge(status) {
    return status === "success" ? '<span class="badge green">成功</span>' : '<span class="badge red">失败</span>';
  }
  function actionLabel(action) {
    var map = { kick_user: "踢出用户", kick_batch: "批量踢出", disconnect_all: "断开所有", shutdown: "关闭服务器",
      restart: "重启服务器", start: "启动服务器", config: "修改配置", ban: "封禁用户", ssl: "SSL配置",
      channel_create: "创建频道", channel_delete: "删除频道", channel_update: "更新频道",
      channel_batch_delete: "批量删除频道", channel_reorder: "频道排序" };
    return map[action] || action;
  }

  // ============================================================
  // Routing
  // ============================================================
  function navigate(page) {
    state.currentPage = page;
    state.selectedSessions.clear();
    state.selectedChannels.clear();
    window.location.hash = page;
    dom.navLinks.forEach(function (a) {
      if (a.dataset.page === page) { a.classList.add("active"); a.setAttribute("aria-current", "page"); }
      else { a.classList.remove("active"); a.removeAttribute("aria-current"); }
    });
    dom.pageTitle.textContent = pageLabels[page] || page;
    renderFullPage();
  }

  var pageLabels = { dashboard: "仪表盘", sessions: "会话管理", channels: "频道", metrics: "性能监控", config: "系统配置", server: "服务器控制" };

  // ============================================================
  // Full page render (only on navigation)
  // ============================================================
  function renderFullPage() {
    var page = state.currentPage;
    var builder = pageBuilders[page];
    dom.content.innerHTML = '<div class="page-section">' + (builder ? builder() : "") + "</div>";
    if (page === "metrics") initChartCanvas();
  }

  var pageBuilders = {
    dashboard: buildDashboard,
    sessions: buildSessionsPage,
    channels: buildChannelsPage,
    metrics: buildMetricsPage,
    config: buildConfigPage,
    server: buildServerPage,
  };

  // ============================================================
  // Targeted data update (on poll)
  // ============================================================
  function updatePageData() {
    var s = state.data.status || {};
    dom.uptimeDisplay.textContent = formatUptime(s.uptime_ms ? Math.floor(s.uptime_ms / 1000) : 0);
    var updater = pageUpdaters[state.currentPage];
    if (updater) updater();
  }

  var pageUpdaters = {
    dashboard: updateDashboard,
    sessions: updateSessionsPage,
    channels: updateChannelsPage,
    metrics: updateMetrics,
    config: noop,
    server: updateServerPage,
  };

  function noop() {}

  // ============================================================
  // Dashboard
  // ============================================================
  function buildDashboard() {
    var s = state.data.status || {};
    var online = state.serverOnline;
    var ipv4 = s.ipv4 || "—";
    var ipv6 = s.ipv6 || "—";
    return '\
      <div class="stat-grid" role="region" aria-label="服务指标">\
        <div class="card stat-card">\
          <div class="stat-label">服务状态</div>\
          <div class="stat-value" data-bind="server-status" data-val="' + (online ? 1 : 0) + '">' + (online ? "● 运行中" : "○ 已停止") + '</div>\
        </div>\
        <div class="card stat-card">\
          <div class="stat-label">在线用户</div>\
          <div class="stat-value" data-bind="online-users">' + formatNumber(s.authenticated_users ?? 0) + '</div>\
          <div class="stat-sub" data-bind="active-sessions">活跃会话 ' + formatNumber(s.clients ?? 0) + '</div>\
        </div>\
        <div class="card stat-card">\
          <div class="stat-label">频道数</div>\
          <div class="stat-value" data-bind="channel-count">' + formatNumber(s.channels ?? 0) + '</div>\
        </div>\
        <div class="card stat-card">\
          <div class="stat-label">数据包转发</div>\
          <div class="stat-value" data-bind="packets">' + formatNumber(s.packets_relayed ?? 0) + '</div>\
          <div class="stat-sub" data-bind="uptime-sub">运行时间 ' + formatUptime(s.uptime_ms ? Math.floor(s.uptime_ms / 1000) : 0) + '</div>\
        </div>\
      </div>\
      <div class="card" style="margin-bottom:28px">\
        <div class="card-header"><h2>网络地址</h2></div>\
        <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:16px;padding:16px">\
          <div>\
            <div class="stat-label" style="margin-bottom:4px">IPv4 地址</div>\
            <div class="config-val" id="dash-ipv4" style="font-size:15px">' + escapeHtml(ipv4) + '</div>\
          </div>\
          <div>\
            <div class="stat-label" style="margin-bottom:4px">IPv6 地址</div>\
            <div class="config-val" id="dash-ipv6" style="font-size:15px">' + escapeHtml(ipv6) + '</div>\
          </div>\
        </div>\
      </div>\
      ' + buildQuickActions() + '\
      <div id="dash-sessions">' + buildSessionsTable(true) + '</div>\
      <div id="dash-channels">' + buildChannelsTable(true) + '</div>';
  }

  function updateDashboard() {
    var s = state.data.status || {};
    var online = state.serverOnline;
    bindEl("server-status", function (el) {
      el.textContent = online ? "● 运行中" : "○ 已停止";
      el.style.color = online ? "var(--green)" : "var(--red)";
    });
    bindEl("online-users", function (el) { el.textContent = formatNumber(s.authenticated_users ?? 0); });
    bindEl("active-sessions", function (el) { el.textContent = "活跃会话 " + formatNumber(s.clients ?? 0); });
    bindEl("channel-count", function (el) { el.textContent = formatNumber(s.channels ?? 0); });
    bindEl("packets", function (el) { el.textContent = formatNumber(s.packets_relayed ?? 0); });
    bindEl("uptime-sub", function (el) { el.textContent = "运行时间 " + formatUptime(s.uptime_ms ? Math.floor(s.uptime_ms / 1000) : 0); });
    var ipv4El = document.getElementById("dash-ipv4");
    if (ipv4El) ipv4El.textContent = s.ipv4 || "—";
    var ipv6El = document.getElementById("dash-ipv6");
    if (ipv6El) ipv6El.textContent = s.ipv6 || "—";
    patchHtml("dash-sessions", buildSessionsTable(true));
    patchHtml("dash-channels", buildChannelsTable(true));
  }

  function buildQuickActions() {
    return '<div class="card" style="margin-bottom:28px"><div class="card-header"><h2>快捷操作</h2></div><div style="display:flex;gap:12px;flex-wrap:wrap"><button class="btn" onclick="NEVO.quickAction(\'refresh\')">刷新数据</button><button class="btn" onclick="NEVO.quickAction(\'disconnectAll\')">断开所有连接</button></div></div>';
  }

  // ============================================================
  // Sessions (enhanced with batch select)
  // ============================================================
  function buildSessionsTable(compact) {
    var sessions = state.data.sessions || [];
    var title = compact ? "在线会话" : "会话管理";
    var showAll = !compact;

    if (!sessions.length) {
      return '<div class="card" style="margin-bottom:28px"><div class="card-header"><h2>' + title + '</h2></div><div class="empty-state"><div class="empty-icon" aria-hidden="true">—</div><div class="empty-title">暂无在线会话</div><div class="empty-desc">等待用户连接后将在此处显示</div></div></div>';
    }

    if (showAll) {
      var checkedCount = sessions.filter(function (s) { return state.selectedSessions.has(s.session_id); }).length;
      var batchBar = checkedCount > 0
        ? '<div class="batch-bar"><span>已选 <strong>' + checkedCount + '</strong> / ' + sessions.length + ' 个会话</span><div><button class="btn small danger" onclick="NEVO.batchKick()">批量踢出</button><button class="btn small" onclick="NEVO.clearSelectedSessions()">取消选择</button></div></div>'
        : '<div class="batch-bar"><span>' + sessions.length + ' 个在线会话</span><div><button class="btn small" onclick="NEVO.selectAllSessions()">全选</button></div></div>';
    } else {
      var batchBar = '';
    }

    var display = compact ? sessions.slice(0, 5) : sessions;
    var rows = display.map(function (s) {
      var checked = state.selectedSessions.has(s.session_id) ? " checked" : "";
      return '<tr><td style="width:36px">' + (showAll ? '<input type="checkbox" class="row-checkbox"' + checked + ' data-sid="' + s.session_id + '" onchange="NEVO.toggleSession(this)" aria-label="选择 ' + escapeHtml(s.username || "—") + '">' : "") + '</td><td style="font-family:var(--font-mono);color:var(--text-primary)">' + escapeHtml(s.username || "—") + '</td><td>' + escapeHtml(s.address || "—") + '</td><td>' + escapeHtml(s.channel || "—") + '</td><td>' + statusBadge(s.status) + '</td>' + (showAll ? '<td style="text-align:right"><button class="btn small danger" onclick="NEVO.kickUser(' + s.session_id + ',\'' + escapeHtml(s.username) + '\')">踢出</button></td>' : "") + '</tr>';
    }).join("");

    var footer = compact && sessions.length > 5 ? '<div style="text-align:center;padding:12px;font-size:12px;color:var(--text-muted)">显示 5/' + sessions.length + ' — <a href="#sessions" style="color:var(--accent);cursor:pointer" onclick="NEVO.navigate(\'sessions\')">查看全部</a></div>' : "";
    return '<div class="card" style="margin-bottom:28px"><div class="card-header"><h2>' + title + '</h2><span class="badge blue">' + sessions.length + ' 在线</span></div>' + batchBar + '<div class="table-wrap"><table aria-label="会话列表"><thead><tr>' + (showAll ? '<th style="width:36px"></th>' : "") + '<th>用户名</th><th>地址</th><th>频道</th><th>状态</th>' + (showAll ? '<th style="text-align:right">操作</th>' : "") + '</tr></thead><tbody>' + rows + '</tbody></table></div>' + footer + '</div>';
  }

  function buildSessionsPage() {
    return '<div class="section">' + buildSessionsTable(false) + '</div>';
  }

  function updateSessionsPage() {
    patchHtml("dash-sessions", buildSessionsTable(true));
  }

  // ============================================================
  // Channels (full CRUD)
  // ============================================================
  function buildChannelsTable(compact) {
    var channels = state.data.channels || [];
    var title = compact ? "频道" : "频道管理";
    var showAll = !compact;

    if (!channels.length) {
      return '<div class="card" style="margin-bottom:28px"><div class="card-header"><h2>' + title + '</h2></div><div class="empty-state"><div class="empty-icon" aria-hidden="true">—</div><div class="empty-title">暂无频道</div><div class="empty-desc">点击"新建频道"创建第一个频道</div></div></div>';
    }

    if (showAll) {
      var checkedCount = channels.filter(function (c) { return state.selectedChannels.has(c.channel_id); }).length;
      var batchBar = '<div class="batch-bar"><span>' + (checkedCount > 0 ? '已选 <strong>' + checkedCount + '</strong> / ' + channels.length + ' 个频道' : channels.length + ' 个频道') + '</span><div>';
      if (checkedCount > 0) {
        batchBar += '<button class="btn small danger" onclick="NEVO.batchDeleteChannels()">批量删除</button><button class="btn small" onclick="NEVO.clearSelectedChannels()">取消选择</button>';
      } else {
        batchBar += '<button class="btn small" onclick="NEVO.selectAllChannels()">全选</button>';
      }
      batchBar += '<button class="btn small primary" onclick="NEVO.showChannelModal()">+ 新建频道</button></div></div>';
    } else {
      var batchBar = '';
    }

    var rows = channels.map(function (c) {
      var checked = state.selectedChannels.has(c.channel_id) ? " checked" : "";
      return '<tr><td style="width:36px">' + (showAll ? '<input type="checkbox" class="row-checkbox"' + checked + ' data-cid="' + c.channel_id + '" onchange="NEVO.toggleChannel(this)" aria-label="选择频道">' : "") + '</td><td style="font-family:var(--font-mono);color:var(--accent)">' + escapeHtml(c.channel_name || "—") + '</td><td><code class="config-val">' + (c.channel_id ?? "—") + '</code></td><td>' + (c.parent_id ? '<code class="config-val">' + c.parent_id + '</code>' : '<span style="color:var(--text-muted)">根频道</span>') + '</td><td>' + (c.user_count ? '<span class="badge ' + (c.user_count > 0 ? 'green' : 'neutral') + '">' + c.user_count + '</span>' : '<span class="badge neutral">0</span>') + '</td>' + (showAll ? '<td style="text-align:right"><div class="btn-group"><button class="btn small" onclick="NEVO.showChannelModal(\'' + c.channel_id + '\')">编辑</button><button class="btn small danger" onclick="NEVO.deleteChannel(\'' + c.channel_id + '\',\'' + escapeHtml(c.channel_name || "") + '\')">删除</button></div></td>' : "") + '</tr>';
    }).join("");

    return '<div class="card" style="margin-bottom:28px"><div class="card-header"><h2>' + title + '</h2></div>' + batchBar + '<div class="table-wrap"><table aria-label="频道列表"><thead><tr>' + (showAll ? '<th style="width:36px"></th>' : "") + '<th>名称</th><th>ID</th><th>父频道</th><th>用户数</th>' + (showAll ? '<th style="text-align:right;width:160px">操作</th>' : "") + '</tr></thead><tbody>' + rows + '</tbody></table></div></div>';
  }

  function buildChannelsPage() {
    return '<div class="section">' + buildChannelsTable(false) + '</div>';
  }

  function updateChannelsPage() {
    patchHtml("dash-channels", buildChannelsTable(true));
  }

  // ============================================================
  // Channel Modal (create/edit)
  // ============================================================
  function showChannelModal(channelId) {
    var channel = null;
    if (channelId) {
      channel = (state.data.channels || []).find(function (c) { return c.channel_id == channelId; });
    }
    var isEdit = !!channel;
    var title = isEdit ? "编辑频道" : "新建频道";
    var name = channel ? escapeHtml(channel.channel_name || "") : "";
    var parentId = channel ? (channel.parent_id || "") : "";
    var sortOrder = channel ? (channel.sort_order || 0) : 0;
    var parentOptions = (state.data.channels || []).filter(function (c) { return c.channel_id != channelId; }).map(function (c) {
      return '<option value="' + c.channel_id + '"' + (c.channel_id == parentId ? " selected" : "") + '>' + escapeHtml(c.channel_name || c.channel_id) + '</option>';
    }).join("");

    var body = '\
      <form id="channel-form" onsubmit="return false">\
        <div class="form-group">\
          <label for="ch-name">频道名称</label>\
          <input id="ch-name" type="text" value="' + name + '" maxlength="64" placeholder="输入频道名称" required>\
          <div class="error-msg" id="ch-name-error"></div>\
        </div>\
        <div class="form-group">\
          <label for="ch-parent">父频道</label>\
          <select id="ch-parent"><option value="">— 根频道 —</option>' + parentOptions + '</select>\
        </div>\
        <div class="form-group">\
          <label for="ch-sort">排序权重</label>\
          <input id="ch-sort" type="number" value="' + sortOrder + '" min="0" max="9999">\
          <div class="hint">数值越小越靠前</div>\
        </div>\
      </form>';

    showModal(title, body,
      '<button class="btn modal-cancel">取消</button><button class="btn primary" id="modal-channel-save">' + (isEdit ? "保存修改" : "创建频道") + '</button>');

    document.getElementById("modal-channel-save").addEventListener("click", function () {
      var nameVal = document.getElementById("ch-name").value.trim();
      var parentVal = document.getElementById("ch-parent").value;
      var sortVal = parseInt(document.getElementById("ch-sort").value, 10) || 0;

      if (!nameVal) {
        var err = document.getElementById("ch-name-error");
        err.textContent = "频道名称不能为空";
        err.classList.add("visible");
        return;
      }

      if (isEdit) {
        NEVO.updateChannel(channelId, nameVal, parentVal || null, sortVal);
      } else {
        NEVO.createChannel(nameVal, parentVal || null, sortVal);
      }
    });
  }

  // ============================================================
  // Server Control page
  // ============================================================
  function buildServerPage() {
    var s = state.data.status || {};
    var cfg = state.data.config || {};
    var online = state.serverOnline;
    var logs = state.data.logs || [];

    var statusCard = '\
      <div class="card">\
        <div class="card-header"><h2>服务器状态</h2></div>\
        <div style="display:flex;flex-direction:column;gap:14px">\
          <div class="toggle-row">\
            <div><div class="toggle-label">运行状态</div><div class="toggle-desc" id="svr-status-desc">' + (online ? "服务器正在运行" : "服务器已停止") + '</div></div>\
            <span class="badge ' + (online ? 'green' : 'red') + '" id="svr-status-badge">' + (online ? '● 运行中' : '○ 已停止') + '</span>\
          </div>\
          <div class="toggle-row">\
            <div><div class="toggle-label">TCP 端口</div><div class="toggle-desc">控制通道</div></div>\
            <code class="config-val" id="svr-tcp-port">' + (cfg.tcp_port || "—") + '</code>\
          </div>\
          <div class="toggle-row">\
            <div><div class="toggle-label">UDP 端口</div><div class="toggle-desc">语音数据</div></div>\
            <code class="config-val" id="svr-udp-port">' + (cfg.udp_port || "—") + '</code>\
          </div>\
          <div class="toggle-row">\
            <div><div class="toggle-label">运行时长</div></div>\
            <span id="svr-uptime" style="font-family:var(--font-mono);font-size:13px">' + formatUptime(s.uptime_ms ? Math.floor(s.uptime_ms / 1000) : 0) + '</span>\
          </div>\
        </div>\
      </div>';

    var controlCard = '\
      <div class="card" style="margin-top:24px">\
        <div class="card-header"><h2>服务器控制</h2></div>\
        <div style="display:flex;gap:12px;flex-wrap:wrap">\
          <button class="btn primary" onclick="NEVO.restartServer()"' + (online ? "" : " disabled") + '>\
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M21 2v6h-6"/><path d="M3 12a9 9 0 0 1 15.36-6.36L21 8"/><path d="M3 22v-6h6"/><path d="M21 12a9 9 0 0 1-15.36 6.36L3 16"/></svg>\
            重启服务器\
          </button>\
          <button class="btn danger" onclick="NEVO.shutdownServer()"' + (online ? "" : " disabled") + '>\
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>\
            关闭服务器\
          </button>\
          <button class="btn" onclick="NEVO.startServer()"' + (online ? " disabled" : "") + ' style="color:var(--green);border-color:var(--green)">\
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><polygon points="5 3 19 12 5 21 5 3"/></svg>\
            启动服务器\
          </button>\
        </div>\
        <div id="svr-msg" style="margin-top:12px;font-size:13px"></div>\
      </div>';

    var logRows = logs.length === 0
      ? '<div class="empty-state" style="padding:30px"><div class="empty-title">暂无操作记录</div></div>'
      : '<div class="table-wrap"><table aria-label="操作日志"><thead><tr><th>时间</th><th>操作</th><th>详情</th><th>状态</th></tr></thead><tbody>' +
        logs.slice().reverse().map(function (l) {
          return '<tr><td style="white-space:nowrap;font-family:var(--font-mono);font-size:11px">' + timeAgo(l.timestamp) + '</td><td>' + actionLabel(l.action) + '</td><td style="max-width:260px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + escapeHtml(l.detail) + '</td><td>' + logBadge(l.status) + '</td></tr>';
        }).join("") + '</tbody></table></div>';

    var logCard = '\
      <div class="card" style="margin-top:24px">\
        <div class="card-header"><h2>操作日志</h2><button class="btn small" onclick="NEVO.clearLogs()">清空</button></div>\
        <div id="svr-logs">' + logRows + '</div>\
      </div>';

    return '<div class="split-grid"><div>' + statusCard + controlCard + '</div><div>' + logCard + '</div></div>';
  }

  function updateServerPage() {
    var s = state.data.status || {};
    var cfg = state.data.config || {};
    var online = state.serverOnline;
    setText("svr-status-desc", online ? "服务器正在运行" : "服务器已停止");
    var badge = document.getElementById("svr-status-badge");
    if (badge) {
      badge.textContent = online ? "● 运行中" : "○ 已停止";
      badge.className = "badge " + (online ? "green" : "red");
    }
    setText("svr-tcp-port", cfg.tcp_port || "—");
    setText("svr-udp-port", cfg.udp_port || "—");
    setText("svr-uptime", formatUptime(s.uptime_ms ? Math.floor(s.uptime_ms / 1000) : 0));
    var restartBtn = document.querySelector("#main-area button[onclick*='restartServer']");
    var shutdownBtn = document.querySelector("#main-area button[onclick*='shutdownServer']");
    var startBtn = document.querySelector("#main-area button[onclick*='startServer']");
    if (restartBtn) restartBtn.disabled = !online;
    if (shutdownBtn) shutdownBtn.disabled = !online;
    if (startBtn) startBtn.disabled = online;

    var logs = state.data.logs || [];
    var logContainer = document.getElementById("svr-logs");
    if (logContainer) {
      if (logs.length === 0) {
        logContainer.innerHTML = '<div class="empty-state" style="padding:30px"><div class="empty-title">暂无操作记录</div></div>';
      } else {
        logContainer.innerHTML = '<div class="table-wrap"><table aria-label="操作日志"><thead><tr><th>时间</th><th>操作</th><th>详情</th><th>状态</th></tr></thead><tbody>' +
          logs.slice().reverse().map(function (l) {
            return '<tr><td style="white-space:nowrap;font-family:var(--font-mono);font-size:11px">' + timeAgo(l.timestamp) + '</td><td>' + actionLabel(l.action) + '</td><td style="max-width:260px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + escapeHtml(l.detail) + '</td><td>' + logBadge(l.status) + '</td></tr>';
          }).join("") + '</tbody></table></div>';
      }
    }
  }

  // ============================================================
  // Config (unchanged)
  // ============================================================
  function buildConfigPage() {
    var c = state.data.config || {};
    return '\
      <div class="config-grid">\
        <div class="card">\
          <div class="card-header"><h2>通用设置</h2></div>\
          <form id="config-form-general" onsubmit="NEVO.saveConfig(event)">\
            <div class="form-group"><label for="cfg-server-name">服务器名称</label><input id="cfg-server-name" type="text" value="' + escapeHtml(c.server_name || 'NEVO Server') + '" maxlength="64"></div>\
            <div class="form-group"><label for="cfg-welcome">欢迎消息</label><input id="cfg-welcome" type="text" value="' + escapeHtml(c.welcome_message || '') + '" maxlength="256" placeholder="用户登录后显示的欢迎消息"></div>\
            <div class="form-group"><label for="cfg-max-users">最大用户数</label><input id="cfg-max-users" type="number" value="' + (c.max_users || 100) + '" min="1" max="10000"></div>\
            <div class="form-group"><label for="cfg-log-level">日志级别</label><select id="cfg-log-level">' + ["debug","info","warn","error"].map(function (lv) { return '<option value="' + lv + '"' + (c.log_level===lv?" selected":"") + '>' + lv.charAt(0).toUpperCase()+lv.slice(1) + '</option>'; }).join("") + '</select></div>\
            <div class="form-group"><label for="cfg-admin-pwd">管理员密码</label><input id="cfg-admin-pwd" type="password" placeholder="留空不修改" maxlength="128" autocomplete="new-password"><div class="hint">' + (c.admin_password_set?'<span style="color:var(--green)">已设置</span>':'<span style="color:var(--amber)">未设置</span>') + '</div></div>\
            <button type="submit" class="btn primary">保存设置</button>\
            <div class="error-msg" id="config-error"></div>\
          </form>\
        </div>\
        <div class="card">\
          <div class="card-header"><h2>服务端信息</h2></div>\
          <div style="display:flex;flex-direction:column;gap:14px">\
            <div class="toggle-row"><div><div class="toggle-label">TCP 端口</div><div class="toggle-desc">控制通道端口</div></div><code class="config-val">' + (c.tcp_port||"—") + '</code></div>\
            <div class="toggle-row"><div><div class="toggle-label">UDP 端口</div><div class="toggle-desc">语音数据端口</div></div><code class="config-val">' + (c.udp_port||"—") + '</code></div>\
            <div class="toggle-row"><div><div class="toggle-label">SSL/TLS</div><div class="toggle-desc">传输加密状态</div></div><span class="badge ' + (c.ssl_enabled?'green':'neutral') + '">' + (c.ssl_enabled?'已启用':'未启用') + '</span></div>\
            <div class="toggle-row"><div><div class="toggle-label">管理员密码</div><div class="toggle-desc">ControlServer 认证</div></div><span class="badge ' + (c.admin_password_set?'green':'amber') + '">' + (c.admin_password_set?'已设置':'未设置') + '</span></div>\
          </div>\
        </div>\
      </div>';
  }

  // ============================================================
  // Metrics page
  // ============================================================
  function buildMetricsPage() {
    var m = state.data.metrics || {};
    var cpu = m.cpu_percent ?? -1;
    var memPct = m.memory_percent ?? 0;
    var memMB = m.memory_mb ?? 0;
    var diskUsed = m.disk_total_gb > 0 ? m.disk_total_gb - m.disk_free_gb : 0;
    var diskPct = m.disk_total_gb > 0 ? (diskUsed / m.disk_total_gb * 100) : 0;

    function gauge(id, label, value, max, unit, color) {
      var pct = max > 0 ? Math.min(100, Math.max(0, (value / max) * 100)) : 0;
      return '<div class="metric-gauge"><div class="gauge-label">' + label + '</div><div class="gauge-track"><div class="gauge-fill" id="' + id + '-fill" style="width:' + pct + '%;background:' + color + '"></div></div><div class="gauge-value"><span class="gauge-num" id="' + id + '-num">' + (cpu >= 0 ? value : '—') + '</span><span class="gauge-unit">' + unit + '</span></div></div>';
    }

    return '\
      <div class="section-title">系统资源</div>\
      <div class="stat-grid" role="region" aria-label="系统资源">\
        <div class="card">' + gauge("gauge-cpu", "CPU 使用率", cpu, 100, "%", cpu > 80 ? "var(--red)" : cpu > 50 ? "var(--amber)" : "var(--green)") + '</div>\
        <div class="card">' + gauge("gauge-mem", "内存 (进程)", memMB, memMB > 0 ? Math.max(memMB, (m.disk_total_gb || 1) * 50) : 100, "MB" + (memPct > 0 ? " (" + memPct + "%)" : ""), memPct > 80 ? "var(--red)" : memPct > 50 ? "var(--amber)" : "var(--blue)") + '</div>\
        <div class="card">' + gauge("gauge-disk", "磁盘已用", diskUsed, m.disk_total_gb || 1, "GB / " + (m.disk_total_gb || '—') + " GB", diskPct > 90 ? "var(--red)" : diskPct > 70 ? "var(--amber)" : "var(--blue)") + '</div>\
        <div class="card"><div style="text-align:center"><div class="stat-label">网络连接数</div><div class="stat-value" id="metric-conns">' + (m.connections >= 0 ? m.connections : '—') + '</div></div></div>\
      </div>\
      <div class="section-title" style="margin-top:28px">历史趋势</div>\
      <div class="card">\
        <div style="display:flex;gap:24px;align-items:center;margin-bottom:12px;flex-wrap:wrap">\
          <span style="display:flex;align-items:center;gap:6px;font-size:12px;color:var(--text-secondary)"><span style="display:inline-block;width:10px;height:10px;border-radius:2px;background:var(--green)"></span> CPU</span>\
          <span style="display:flex;align-items:center;gap:6px;font-size:12px;color:var(--text-secondary)"><span style="display:inline-block;width:10px;height:10px;border-radius:2px;background:var(--blue)"></span> 内存</span>\
        </div>\
        <canvas id="metrics-chart" width="800" height="220" style="width:100%;height:220px" aria-label="CPU与内存历史趋势图"></canvas>\
      </div>\
      <div class="section-title" style="margin-top:28px">进程详情</div>\
      <div class="card">\
        <div style="display:grid;grid-template-columns:repeat(4,1fr);gap:20px;text-align:center">\
          <div><div class="stat-label">PID</div><div class="stat-value" id="metric-pid" style="font-size:22px">' + (m.pid || '—') + '</div></div>\
          <div><div class="stat-label">线程数</div><div class="stat-value" id="metric-threads" style="font-size:22px">' + (m.threads || '—') + '</div></div>\
          <div><div class="stat-label">句柄数</div><div class="stat-value" id="metric-handles" style="font-size:22px">' + (m.handles || '—') + '</div></div>\
          <div><div class="stat-label">网络连接</div><div class="stat-value" id="metric-conns2" style="font-size:22px">' + (m.connections >= 0 ? m.connections : '—') + '</div></div>\
        </div>\
      </div>\
      ' + (m.error ? '<div class="card" style="margin-top:16px;border-color:var(--amber);background:var(--amber-dim)"><p style="font-size:13px;color:var(--amber)"><strong>提示：</strong>' + escapeHtml(m.error) + '</p></div>' : "");
  }

  function updateMetrics() {
    var m = state.data.metrics || {};
    var cpu = m.cpu_percent ?? -1;
    updateGauge("gauge-cpu", cpu, 100, cpu > 80 ? "var(--red)" : cpu > 50 ? "var(--amber)" : "var(--green)");
    var memMB = m.memory_mb ?? 0;
    var memPct = m.memory_percent ?? 0;
    var memMax = memMB > 0 ? Math.max(memMB, 500) : 100;
    var memColor = memPct > 80 ? "var(--red)" : memPct > 50 ? "var(--amber)" : "var(--blue)";
    updateGauge("gauge-mem", memMB, memMax, memColor, memMB > 0 ? "MB" + (memPct > 0 ? " (" + memPct + "%)" : "") : "—");
    var diskUsed = m.disk_total_gb > 0 ? m.disk_total_gb - m.disk_free_gb : 0;
    var diskPct = m.disk_total_gb > 0 ? (diskUsed / m.disk_total_gb * 100) : 0;
    var diskMax = m.disk_total_gb || 1;
    updateGauge("gauge-disk", diskUsed, diskMax, diskPct > 90 ? "var(--red)" : diskPct > 70 ? "var(--amber)" : "var(--blue)", "GB / " + (m.disk_total_gb || '—') + " GB");
    setText("metric-conns", m.connections >= 0 ? String(m.connections) : "—");
    setText("metric-conns2", m.connections >= 0 ? String(m.connections) : "—");
    setText("metric-pid", m.pid ? String(m.pid) : "—");
    setText("metric-threads", m.threads ? String(m.threads) : "—");
    setText("metric-handles", m.handles ? String(m.handles) : "—");
    drawChart();
  }

  function updateGauge(id, value, max, color, unitOverride) {
    var fill = document.getElementById(id + "-fill");
    var num = document.getElementById(id + "-num");
    if (!fill || !num) return;
    var pct = max > 0 ? Math.min(100, Math.max(0, (value / max) * 100)) : 0;
    fill.style.width = pct + "%";
    fill.style.background = color || "var(--accent)";
    num.textContent = value >= 0 ? String(value) : "—";
    if (unitOverride) {
      var unit = num.nextElementSibling;
      if (unit && unit.classList.contains("gauge-unit")) unit.textContent = unitOverride;
    }
  }

  function setText(id, text) { var el = document.getElementById(id); if (el) el.textContent = text; }
  function bindEl(name, fn) { var el = document.querySelector('[data-bind="' + name + '"]'); if (el) fn(el); }
  function patchHtml(id, html) { var el = document.getElementById(id); if (!el) return; var prev = el.innerHTML; if (prev !== html) el.innerHTML = html; }

  // ============================================================
  // Canvas line chart
  // ============================================================
  function initChartCanvas() {
    state.chartCtx = null;
    setTimeout(function () {
      var canvas = document.getElementById("metrics-chart");
      if (canvas) {
        canvas.width = canvas.offsetWidth * (window.devicePixelRatio || 1);
        canvas.height = 220 * (window.devicePixelRatio || 1);
        state.chartCtx = canvas.getContext("2d");
        drawChart();
      }
    }, 100);
  }

  function drawChart() {
    if (!state.chartCtx) return;
    var ctx = state.chartCtx;
    var canvas = ctx.canvas;
    var dpr = window.devicePixelRatio || 1;
    var W = canvas.width / dpr;
    var H = canvas.height / dpr;
    var pad = { top: 20, right: 20, bottom: 30, left: 48 };
    var pw = W - pad.left - pad.right;
    var ph = H - pad.top - pad.bottom;
    ctx.save();
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = "#0f141b";
    ctx.fillRect(pad.left, pad.top, pw, ph);
    ctx.strokeStyle = "#1e2938";
    ctx.lineWidth = 0.5;
    for (var i = 0; i <= 4; i++) {
      var y = pad.top + (ph * i / 4);
      ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(pad.left + pw, y); ctx.stroke();
    }
    for (var i = 0; i <= 5; i++) {
      var x = pad.left + (pw * i / 5);
      ctx.beginPath(); ctx.moveTo(x, pad.top); ctx.lineTo(x, pad.top + ph); ctx.stroke();
    }
    ctx.fillStyle = "#556677";
    ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.textAlign = "right";
    for (var i = 0; i <= 4; i++) {
      ctx.fillText((100 - i * 25) + "%", pad.left - 8, pad.top + ph * i / 4 + 4);
    }
    var history = state.history;
    drawLine(ctx, history.cpu, pad, pw, ph, "#34d399", 0, 100);
    drawLine(ctx, history.memory, pad, pw, ph, "#60a5fa", 0, 100);
    ctx.restore();
  }

  function drawLine(ctx, data, pad, pw, ph, color, yMin, yMax) {
    if (!data || data.length < 2) return;
    var range = yMax - yMin || 1;
    var now = Date.now();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.8;
    ctx.lineJoin = "round";
    ctx.beginPath();
    var started = false;
    for (var i = 0; i < data.length; i++) {
      var x = pad.left + pw * (1 - (now - data[i].t) / 180000);
      if (x < pad.left) continue;
      var y = pad.top + ph * (1 - (data[i].v - yMin) / range);
      if (!started) { ctx.moveTo(x, y); started = true; }
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
    if (data.length > 0) {
      var last = data[data.length - 1];
      var lx = pad.left + pw * (1 - (now - last.t) / 180000);
      var ly = pad.top + ph * (1 - (last.v - yMin) / range);
      ctx.fillStyle = color;
      ctx.beginPath(); ctx.arc(lx, ly, 3, 0, Math.PI * 2); ctx.fill();
    }
  }

  // ============================================================
  // Actions
  // ============================================================
  window.NEVO = {
    navigate: navigate,

    // Refresh
    refresh: async function () { await fetchAll(); toast("数据已刷新", "success"); },

    // Quick actions
    quickAction: async function (action) {
      if (action === "refresh") { await fetchAll(); toast("数据已刷新", "success"); }
      else if (action === "disconnectAll") {
        confirmAction("断开所有连接", "此操作将断开所有已连接的客户端。确定继续？", "确认断开", async function () {
          var r = await api("/api/disconnect_all", "POST");
          if (r.status === "ok") toast("已断开所有连接", "success");
          else toast(r.data?.message || "操作失败", "error");
          await fetchAll();
        });
      }
    },

    // ---- User Kick (individual + batch) ----
    kickUser: function (sessionId, username) {
      confirmAction("踢出用户", "确定踢出用户 " + escapeHtml(username) + "（会话 " + sessionId + "）？", "踢出", async function () {
        var r = await api("/api/kick", "POST", { session_id: sessionId, username: username });
        if (r.status === "ok") toast("已踢出 " + username, "success");
        else toast(r.data?.message || "操作失败", "error");
        state.selectedSessions.delete(sessionId);
        await fetchAll();
      });
    },

    toggleSession: function (cb) {
      var sid = parseInt(cb.dataset.sid, 10);
      if (cb.checked) state.selectedSessions.add(sid);
      else state.selectedSessions.delete(sid);
      renderFullPage();
    },

    selectAllSessions: function () {
      (state.data.sessions || []).forEach(function (s) { state.selectedSessions.add(s.session_id); });
      renderFullPage();
    },

    clearSelectedSessions: function () {
      state.selectedSessions.clear();
      renderFullPage();
    },

    batchKick: function () {
      var ids = Array.from(state.selectedSessions);
      if (ids.length === 0) { toast("请先选择要踢出的用户", "info"); return; }
      confirmAction("批量踢出", "确定踢出 " + ids.length + " 个选中的用户？此操作不可撤销。", "批量踢出", async function () {
        var r = await api("/api/kick_batch", "POST", { sessions: ids });
        var ok = r.data?.results?.filter(function (r2) { return r2.status === "ok"; }).length || 0;
        toast("踢出完成：" + ok + " 成功 / " + ids.length + " 总计", ok === ids.length ? "success" : "info");
        state.selectedSessions.clear();
        await fetchAll();
      });
    },

    // ---- Channel CRUD ----
    showChannelModal: function (channelId) {
      showChannelModal(channelId);
    },

    createChannel: async function (name, parentId, sortOrder) {
      var r = await api("/api/channel/create", "POST", { name: name, parent_id: parentId, sort_order: sortOrder });
      closeModal();
      if (r.status === "ok") { toast("频道已创建", "success"); await fetchAll(); }
      else toast(r.data?.message || "创建失败", "error");
    },

    updateChannel: async function (channelId, name, parentId, sortOrder) {
      var r = await api("/api/channel/update", "POST", { channel_id: channelId, name: name, parent_id: parentId, sort_order: sortOrder });
      closeModal();
      if (r.status === "ok") { toast("频道已更新", "success"); await fetchAll(); }
      else toast(r.data?.message || "更新失败", "error");
    },

    deleteChannel: function (channelId, name) {
      confirmAction("删除频道", "确定删除频道 \"" + escapeHtml(name) + "\"？此操作不可撤销。", "删除", async function () {
        var r = await api("/api/channel/delete", "POST", { channel_id: channelId });
        if (r.status === "ok") toast("频道已删除", "success");
        else toast(r.data?.message || "删除失败", "error");
        state.selectedChannels.delete(channelId);
        await fetchAll();
      });
    },

    toggleChannel: function (cb) {
      var cid = cb.dataset.cid;
      if (cb.checked) state.selectedChannels.add(cid);
      else state.selectedChannels.delete(cid);
      renderFullPage();
    },

    selectAllChannels: function () {
      (state.data.channels || []).forEach(function (c) { state.selectedChannels.add(c.channel_id); });
      renderFullPage();
    },

    clearSelectedChannels: function () {
      state.selectedChannels.clear();
      renderFullPage();
    },

    batchDeleteChannels: function () {
      var ids = Array.from(state.selectedChannels);
      if (ids.length === 0) { toast("请先选择要删除的频道", "info"); return; }
      confirmAction("批量删除频道", "确定删除 " + ids.length + " 个选中的频道？此操作不可撤销。", "批量删除", async function () {
        var r = await api("/api/channel/batch_delete", "POST", { channel_ids: ids });
        var ok = r.data?.results?.filter(function (r2) { return r2.status === "ok"; }).length || 0;
        toast("删除完成：" + ok + " 成功 / " + ids.length + " 总计", ok === ids.length ? "success" : "info");
        state.selectedChannels.clear();
        await fetchAll();
      });
    },

    // ---- Server Control ----
    restartServer: function () {
      confirmAction("重启服务器", "确定重启 NEVO 服务器？所有客户端将被断开连接，服务将在几秒后恢复。", "确认重启", async function () {
        toast("正在重启服务器...", "info");
        var r = await api("/api/restart", "POST");
        if (r.status === "ok") toast("服务器已重启", "success");
        else toast(r.data?.message || "重启失败", "error");
        setTimeout(fetchAll, 2000);
      });
    },

    shutdownServer: function () {
      confirmAction("关闭服务器", "确定关闭 NEVO 服务器？所有客户端将被断开连接。", "确认关闭", async function () {
        var r = await api("/api/shutdown", "POST");
        if (r.status === "ok") toast("服务器正在关闭...", "info");
        else toast(r.data?.message || "操作失败", "error");
        setTimeout(fetchAll, 1000);
      });
    },

    startServer: async function () {
      toast("正在启动服务器...", "info");
      var r = await api("/api/start", "POST");
      if (r.status === "ok") {
        toast("服务器已启动", "success");
        setTimeout(fetchAll, 2000);
      } else {
        toast(r.data?.message || "启动失败 — 请手动启动 nevo_server.exe", "error");
      }
    },

    clearLogs: function () {
      state.data.logs = [];
      updateServerPage();
      toast("日志显示已清空（服务器端保留）", "info");
    },

    // ---- Config ----
    saveConfig: async function (e) {
      e.preventDefault();
      var body = {
        server_name: document.getElementById("cfg-server-name").value.trim(),
        welcome_message: document.getElementById("cfg-welcome").value.trim(),
        max_users: parseInt(document.getElementById("cfg-max-users").value, 10),
        log_level: document.getElementById("cfg-log-level").value,
      };
      var pwd = document.getElementById("cfg-admin-pwd").value;
      if (pwd) body.admin_password = pwd;
      var r = await api("/api/config", "POST", body);
      if (r.status === "ok") { toast("配置已保存", "success"); document.getElementById("cfg-admin-pwd").value = ""; await fetchAll(); }
      else {
        var err = document.getElementById("config-error");
        if (err) { err.textContent = r.data?.message || "保存失败"; err.classList.add("visible"); }
        toast(r.data?.message || "保存失败", "error");
      }
    },

    closeModal: closeModal,
  };

  // ============================================================
  // Event bindings
  // ============================================================
  dom.btnRefresh.addEventListener("click", function () { window.NEVO.refresh(); });
  dom.btnShutdown.addEventListener("click", function () { window.NEVO.shutdownServer(); });
  dom.navLinks.forEach(function (a) { a.addEventListener("click", function (e) { e.preventDefault(); navigate(a.dataset.page); if (window.innerWidth <= 768) dom.sidebar.classList.remove("open"); }); });
  dom.menuOpen.addEventListener("click", function () { dom.sidebar.classList.add("open"); });
  dom.menuClose.addEventListener("click", function () { dom.sidebar.classList.remove("open"); });
  window.addEventListener("hashchange", function () { var page = window.location.hash.replace("#", "") || "dashboard"; if (state.currentPage !== page) navigate(page); });
  document.addEventListener("keydown", function (e) { if (e.key === "Escape") closeModal(); });
  document.addEventListener("keydown", function (e) { if ((e.ctrlKey && e.key === "r") || e.key === "F5") { e.preventDefault(); window.NEVO.refresh(); } });
  window.addEventListener("resize", function () { if (state.currentPage === "metrics") initChartCanvas(); });

  function init() {
    var hash = window.location.hash.replace("#", "") || "dashboard";
    navigate(hash);
    startPolling();
    connectSSE();
  }

  init();
})();
