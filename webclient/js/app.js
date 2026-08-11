/**
 * NEVO Web Client — Application Logic
 * Handles WebSocket communication, page routing, UI interactions,
 * media engine integration (NevoMedia), and i18n (NevoI18n).
 */

(function () {
  'use strict';

  // Global error handler — 显示未捕获的异常
  window.addEventListener('error', (e) => {
    console.error('[NEVO] Uncaught error:', e.error || e.message);
    document.body.insertAdjacentHTML('afterbegin',
      '<div style="position:fixed;top:0;left:0;right:0;z-index:99999;padding:8px 16px;background:#c0392b;color:#fff;font-family:monospace;font-size:12px;">JS Error: ' + e.message + ' (' + e.filename + ':' + e.lineno + ')</div>');
  });

  // ============================================================
  // i18n shortcut
  // ============================================================
  const t = (key, ...args) => window.NevoI18n ? window.NevoI18n.t(key, ...args) : key;

  // ============================================================
  // State
  // ============================================================
  const state = {
    ws: null,
    wsConnected: false,
    reconnTry: 0,
    connected: false,
    inChannel: false,
    username: '',
    userId: 0,
    isAdmin: false,
    isAdminAuthed: false,
    channels: [],
    channelUsers: [],
    currentChannelId: 0,
    currentChannelName: '',
    isMuted: false,
    isDeafened: false,
    serverName: 'NEVO Server',
    latency: 0,
    // Video call
    videoCallActive: false,
    currentCallId: 0,
    callPeerId: 0,
    callPeerName: '',
    callStartTime: 0,
    callTimerInterval: null,
    localStream: null,
    // Incoming call
    incomingCallId: 0,
    incomingCallerName: '',
    // Pending command responses
    pendingCallbacks: new Map(),
    nextReqId: 1,
    // Input mode
    inputMode: 'continuous',
    pttKey: null,
    pttActive: false,
    // PTT key capture
    capturingPTTKey: false,
    // Screen share
    screenSharing: false,
    // File list
    fileList: [],
    // Server quick access
    serverFavorites: [],
    serverRecent: [],
  };

  // Expose state for media.js
  window.NevoApp = { state };

  // ============================================================
  // DOM helpers
  // ============================================================
  const $ = (id) => document.getElementById(id);
  const el = (tag, attrs = {}, children = []) => {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === 'class') node.className = v;
      else if (k === 'style') node.style.cssText = v;
      else if (k === 'html') node.innerHTML = v;
      else node.setAttribute(k, v);
    }
    for (const c of [].concat(children)) {
      if (c == null) continue;
      node.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    }
    return node;
  };

  // ============================================================
  // Toast notifications
  // ============================================================
  function toast(message, type = 'info', duration = 3000) {
    const container = $('toast-container');
    const tNode = el('div', { class: `toast ${type}` }, message);
    container.appendChild(tNode);
    setTimeout(() => {
      tNode.style.opacity = '0';
      tNode.style.transition = 'opacity 0.3s';
      setTimeout(() => tNode.remove(), 300);
    }, duration);
  }

  // ============================================================
  // Sound effects
  // ============================================================
  function playSoundEffect(src) {
    try {
      const audio = new Audio(src);
      audio.volume = (parseInt(getSetting('output_volume', '100'), 10) || 100) / 100;
      audio.play().catch(() => {});
    } catch (e) { console.error('[APP] Sound effect error:', e); }
  }

  // ============================================================
  // Page routing
  // ============================================================
  function showPage(pageId) {
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    $(pageId).classList.add('active');
  }

  // ============================================================
  // WebSocket client
  // ============================================================
  function connectWebSocket() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${location.host}/ws`;
    state.ws = new WebSocket(wsUrl);

    state.ws.onopen = () => {
      state.wsConnected = true;
      state.reconnTry = 0; // 重连成功后复位退避计数
      console.log('[WS] Connected');
    };

    state.ws.onclose = () => {
      state.wsConnected = false;
      console.log('[WS] Disconnected');
      if (state.connected) {
        // 指数退避重连：3s -> 6s -> 12s -> 24s -> 30s(封顶)，避免风暴式重连
        const delay = Math.min(30000, 3000 * Math.pow(2, state.reconnTry));
        state.reconnTry = Math.min(state.reconnTry + 1, 5);
        toast(t('连接断开，正在重连...'), 'error');
        setTimeout(() => { if (!state.wsConnected) connectWebSocket(); }, delay);
      }
    };

    state.ws.onerror = (err) => console.error('[WS] Error:', err);

    state.ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        if (msg.event) {
          handleEvent(msg.event, msg.data);
        } else if (msg.id !== undefined) {
          const cb = state.pendingCallbacks.get(msg.id);
          if (cb) { state.pendingCallbacks.delete(msg.id); cb(msg); }
        }
      } catch (e) { console.error('[WS] Parse error:', e); }
    };
  }

  function sendCommand(action, params = {}) {
    return new Promise((resolve) => {
      if (!state.ws || state.ws.readyState !== WebSocket.OPEN) {
        resolve({ ok: false, error: 'WebSocket not connected' });
        return;
      }
      const id = state.nextReqId++;
      state.pendingCallbacks.set(id, resolve);
      state.ws.send(JSON.stringify({ action, params, id }));
      setTimeout(() => {
        if (state.pendingCallbacks.has(id)) {
          state.pendingCallbacks.delete(id);
          resolve({ ok: false, error: t('请求超时') });
        }
      }, 15000);
    });
  }

  // ============================================================
  // Event handlers (from backend via gateway)
  // ============================================================
  function handleEvent(event, data) {
    switch (event) {
      case 'ws_connected':
        console.log('[WS] Gateway ready, client_available:', data.client_available);
        break;
      case 'state_changed': handleStateChanged(data); break;
      case 'channel_list':
        state.channels = data.channels || [];
        syncCurrentChannel();
        renderChannelList();
        renderAdminPanel();
        break;
      case 'user_joined': handleUserJoined(data.user); break;
      case 'user_left': handleUserLeft(data.user_id); break;
      case 'user_speaking': handleUserSpeaking(data.user_id, data.speaking); break;
      case 'chat_message': addChatMessage(data); break;
      case 'server_message': addSystemMessage(data.text); break;
      case 'error': toast(data.message || t('服务器错误'), 'error'); break;
      case 'latency_update':
        state.latency = data.latency_ms || 0;
        updateLatencyDisplay();
        break;
      case 'video_call_incoming': handleIncomingCall(data); break;
      case 'video_call_established': handleCallEstablished(data); break;
      case 'video_call_ended': handleCallEnded(data); break;
      case 'video_call_error': toast(data.message || t('视频通话错误'), 'error'); break;
      case 'admin_auth_result': handleAdminAuthResult(data); break;
      case 'admin_action_result': handleAdminActionResult(data); break;
      case 'file_upload_response': handleFileUploadResponse(data); break;
      case 'file_list': state.fileList = data.files || []; renderFileList(); break;
      case 'screen_share_state': handleScreenShareState(data); break;
      case 'voice_frame':
        if (window.NevoMedia) window.NevoMedia.handleVoiceFrame(data);
        break;
      case 'video_frame':
        if (window.NevoMedia) window.NevoMedia.handleVideoFrame(data);
        break;
    }
  }

  function syncCurrentChannel() {
    const channel = state.channels.find(item =>
      (item.users || []).some(user => user.id === state.userId)
    );
    if (!channel) return;
    state.currentChannelId = channel.id;
    state.currentChannelName = channel.name;
    state.channelUsers = channel.users || [];
    renderVoiceUsers();
    updateMyStatus();
    startVoiceEngine();
  }

  function handleStateChanged(data) {
    const newState = data.state;
    if (newState === 'disconnected') {
      if (state.connected) toast(t('已断开连接'), 'error');
      state.connected = false;
      state.inChannel = false;
      stopVoiceEngine();
      showPage('page-connect');
    } else if (newState === 'connected') {
      state.connected = true;
      state.inChannel = false;
    } else if (newState === 'in_channel') {
      state.inChannel = true;
      $('chat-input').disabled = false;
    }
    updateMyStatus();
  }

  function handleUserJoined(user) {
    if (!user) return;
    const existing = state.channelUsers.find(u => u.id === user.id);
    if (!existing) state.channelUsers.push(user);
    renderVoiceUsers();
    addSystemMessage(t('{0} 加入了频道', user.username));
    if (getSetting('desktop_notify', false)) {
      if (window.NevoMedia) window.NevoMedia.showNotification('NEVO', t('{0} 加入了频道', user.username));
    }
  }

  function handleUserLeft(userId) {
    const user = state.channelUsers.find(u => u.id === userId);
    state.channelUsers = state.channelUsers.filter(u => u.id !== userId);
    renderVoiceUsers();
    if (user) addSystemMessage(t('{0} 离开了频道', user.username));
  }

  function handleUserSpeaking(userId, speaking) {
    const userEl = document.querySelector('.voice-user[data-user-id="' + userId + '"]');
    if (!userEl) return;
    const isMe = state.userId && userId === state.userId;
    // 本地麦克风正在运行时，由本地波形条直接驱动；避免服务端回环覆盖本地真实波形
    if (isMe && window.NevoMedia && window.NevoMedia.state.voiceActive) return;

    const bars = userEl.querySelector('.speaking-bars');
    const waveform = userEl.querySelector('.waveform-bar');
    const avatar = userEl.querySelector('.vu-avatar');

    if (speaking) {
      // 远程/跨设备用户说话时显示统一的波形条动画，隐藏原来的 speaking-bars
      if (bars) bars.style.display = 'none';
      if (waveform) {
        waveform.classList.add('speaking');
        waveform.style.display = 'flex';
      }
      if (avatar) avatar.classList.add('speaking-ring');
    } else {
      // 静默时恢复默认显示：远程显示 speaking-bars，自己保留 waveform-bar
      if (bars) bars.style.display = isMe ? 'none' : 'flex';
      if (waveform) {
        waveform.classList.remove('speaking');
        waveform.style.display = isMe ? 'flex' : 'none';
      }
      if (avatar) avatar.classList.remove('speaking-ring');
    }
  }

  function handleIncomingCall(data) {
    state.incomingCallId = data.call_id;
    state.incomingCallerName = data.caller_name || t('未知用户');
    $('ic-name').textContent = state.incomingCallerName;
    $('ic-avatar').textContent = (state.incomingCallerName[0] || '?').toUpperCase();
    $('incoming-call-overlay').classList.add('show');
    toast(t('{0} {1}', state.incomingCallerName, t('邀请你视频通话')), 'info', 5000);
    if (getSetting('call_ring', true) && window.NevoMedia) window.NevoMedia.playSound('call');
  }

  function handleCallEstablished(data) {
    state.videoCallActive = true;
    state.currentCallId = data.call_id;
    state.callPeerId = data.peer_id;
    state.callStartTime = Date.now();
    state.callTimerInterval = setInterval(updateCallTimer, 1000);
    showPage('page-video-call');
    // Start local camera + video encoding
    startVideoCallMedia();
    const peer = state.channelUsers.find(u => u.id === data.peer_id);
    if (peer) {
      state.callPeerName = peer.username;
      $('video-remote-name').textContent = peer.username;
      $('video-remote-avatar').textContent = (peer.username[0] || '?').toUpperCase();
    }
    toast(t('视频通话已建立'), 'success');
  }

  function handleCallEnded(data) {
    state.videoCallActive = false;
    state.currentCallId = 0;
    if (state.callTimerInterval) { clearInterval(state.callTimerInterval); state.callTimerInterval = null; }
    stopVideoCallMedia();
    showPage('page-main');
    toast(t('通话已结束'), 'info');
  }

  function updateCallTimer() {
    if (!state.callStartTime) return;
    const elapsed = Math.floor((Date.now() - state.callStartTime) / 1000);
    const mins = Math.floor(elapsed / 60);
    const secs = elapsed % 60;
    $('video-timer').textContent = (mins < 10 ? '0' : '') + mins + ':' + (secs < 10 ? '0' : '') + secs;
  }

  // ============================================================
  // Media engine integration (NevoMedia)
  // ============================================================
  async function startVoiceEngine() {
    if (!window.NevoMedia || state.voiceStarting || window.NevoMedia.state.voiceActive) return;
    state.voiceStarting = true;
    try {
      const inputDevice = getSetting('input_device', '');
      await window.NevoMedia.startVoice(inputDevice);
      // Apply input mode
      window.NevoMedia.setInputMode(state.inputMode);
      // Show self waveform
      $('my-waveform').style.display = 'flex';
    } catch (e) {
      console.error('[APP] Failed to start voice:', e);
      toast(t('无法访问麦克风') + ': ' + (e.message || e.name), 'error');
    } finally {
      state.voiceStarting = false;
    }
  }

  function stopVoiceEngine() {
    if (!window.NevoMedia) return;
    window.NevoMedia.stopVoice();
    $('my-waveform').style.display = 'none';
  }

  async function startVideoCallMedia() {
    if (!window.NevoMedia) { startLocalCamera(); return; }
    try {
      const cameraId = getSetting('camera_device', '');
      const res = getSetting('resolution', '1280x720');
      const [w, h] = res.split('x').map(Number);
      const fps = parseInt(getSetting('fps', '30'), 10);
      // Start local camera preview in PiP
      await startLocalCamera();
      // Start video encoding + transmission（复用 PiP 已打开的本地流，避免重复请求摄像头）
      await window.NevoMedia.startVideo(cameraId, w || 640, h || 480, fps, state.localStream);
      // Set remote video canvas
      const remoteCanvas = $('video-remote-canvas');
      window.NevoMedia.setRemoteVideoCanvas(remoteCanvas);
      remoteCanvas.style.display = 'block';
    } catch (e) {
      console.error('[APP] Failed to start video call media:', e);
      toast(t('摄像头不可用') + ': ' + (e.message || e.name), 'error');
    }
  }

  function stopVideoCallMedia() {
    if (window.NevoMedia) window.NevoMedia.stopVideo();
    stopLocalCamera();
    $('video-remote-canvas').style.display = 'none';
    if (state.screenSharing) { if (window.NevoMedia) window.NevoMedia.stopScreenShare(); state.screenSharing = false; }
  }

  // ============================================================
  // Local camera (getUserMedia) for PiP preview
  // ============================================================
  async function startLocalCamera() {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ video: getVideoConstraints(), audio: false });
      state.localStream = stream;
      const pip = $('video-self-pip');
      pip.innerHTML = '';
      const video = el('video', { autoplay: true, muted: true, playsinline: true });
      video.srcObject = stream;
      pip.appendChild(video);
    } catch (e) {
      console.error('Camera access failed:', e);
      $('video-self-pip').innerHTML = `
        <div class="pip-placeholder">
          <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="var(--state-error)" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>
          <span class="pip-label">${t('摄像头不可用')}</span>
        </div>`;
      toast(t('摄像头不可用') + ': ' + (e.message || e.name), 'error');
    }
  }

  function stopLocalCamera() {
    if (state.localStream) {
      state.localStream.getTracks().forEach(t => t.stop());
      state.localStream = null;
    }
    $('video-self-pip').innerHTML = `
      <div class="pip-placeholder">
        <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="var(--color-text-muted)" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M23 7l-7 5 7 5V7z"/><rect x="1" y="5" width="15" height="14" rx="2" ry="2"/></svg>
        <span class="pip-label">${t('摄像头未开启')}</span>
      </div>`;
  }

  // ============================================================
  // UI rendering
  // ============================================================
  function getAvatarColor(username) {
    const colors = ['bg-primary', 'bg-info', 'bg-warning', 'bg-muted'];
    let hash = 0;
    for (let i = 0; i < (username || '').length; i++) hash = username.charCodeAt(i) + ((hash << 5) - hash);
    return colors[Math.abs(hash) % colors.length];
  }

  function getInitials(name) { return (name || '?')[0].toUpperCase(); }

  function renderChannelList() {
    const container = $('channel-list');
    container.innerHTML = '';
    if (state.channels.length > 0) {
      const section = el('div', { class: 'channel-section' });
      section.appendChild(el('div', { class: 'channel-section-header' }, [el('span', { class: 'caption' }, t('语音频道'))]));
      for (const ch of state.channels) {
        const isActive = ch.id === state.currentChannelId;
        const userCount = (ch.users || []).length;
        const item = el('div', { class: `channel-item ${isActive ? 'active' : ''}`, 'data-channel-id': ch.id }, [
          el('span', { class: 'nevo-icon', style: 'width:16px; height:16px; color: var(--color-text-muted);' }, [svgVolumeIcon()]),
          el('span', { class: 'ch-name' }, ch.name),
          userCount > 0 ? el('span', { class: 'ch-count' }, String(userCount)) : null,
        ]);
        item.addEventListener('dblclick', () => joinChannel(ch.id));
        // Right-click for admin context menu
        item.addEventListener('contextmenu', (e) => {
          e.preventDefault();
          if (state.isAdminAuthed) { adminRenameChannel(ch); }
        });
        section.appendChild(item);
      }
      container.appendChild(section);
    }
  }

  function svgVolumeIcon() {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('width', '16'); svg.setAttribute('height', '16');
    svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor'); svg.setAttribute('stroke-width', '2');
    svg.setAttribute('stroke-linecap', 'round'); svg.setAttribute('stroke-linejoin', 'round');
    svg.innerHTML = `<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07"/>`;
    return svg;
  }

  function renderVoiceUsers() {
    const container = $('voice-users');
    // 成员列表即将重建，重置波形 DOM 缓存，避免旧引用
    if (window.NevoMedia && window.NevoMedia.resetWaveformCache) window.NevoMedia.resetWaveformCache();
    container.innerHTML = '';
    if (!state.inChannel || state.channelUsers.length === 0) {
      container.innerHTML = `<div class="voice-empty">${t('当前频道暂无其他成员')}</div>`;
      return;
    }
    for (const user of state.channelUsers) {
      const isMe = user.id === state.userId;
      const isMuted = user.muted || (isMe && state.isMuted);
      const isDeafened = user.deafened || (isMe && state.isDeafened);
      const userAttrs = { class: `voice-user ${isMe ? 'is-me' : ''} ${isMuted ? 'muted' : ''}`, 'data-user-id': user.id };
      if (isMe) userAttrs.id = 'voice-user-self';
      const userEl = el('div', userAttrs);
      userEl.appendChild(el('div', { class: `vu-avatar ${getAvatarColor(user.username)}` }, getInitials(user.username)));
      userEl.appendChild(el('span', { class: 'vu-name' }, user.username + (isMe ? ` (${t('我')})` : '')));
      const actions = el('div', { class: 'vu-actions' });
      if (isMuted) actions.appendChild(el('div', { class: 'vu-action mic-off', title: t('已静音') }, [micOffIcon()]));
      if (isDeafened) actions.appendChild(el('div', { class: 'vu-action deafened', title: t('已关闭音频') }, [deafenIcon()]));
      userEl.appendChild(actions);
      // Waveform bars：自己显示，其他人隐藏
      const wf = el('div', { class: 'waveform-bar', style: isMe ? 'display:flex;' : 'display:none;' });
      for (let i = 0; i < 4; i++) wf.appendChild(el('div', { class: 'wf-bar', style: 'height: 4px;' }));
      userEl.appendChild(wf);
      // Speaking bars：远程说话时播放动画，自己隐藏避免和波形重复
      const bars = el('div', { class: 'speaking-bars', style: isMe ? 'display:none;' : 'display:flex;' });
      for (let i = 0; i < 4; i++) bars.appendChild(el('div', { class: 'bar', style: 'height: 4px;' }));
      userEl.appendChild(bars);
      // Video call button for other users
      if (!isMe && !state.videoCallActive) {
        const callBtn = el('div', { class: 'vu-action', style: 'color: var(--color-primary); cursor: pointer; margin-left: 4px;', title: t('视频通话') }, [videoCallIcon()]);
        callBtn.addEventListener('click', (e) => { e.stopPropagation(); startVideoCall(user.id, user.username); });
        userEl.appendChild(callBtn);
      }
      container.appendChild(userEl);
    }
    if (state.inChannel && !state.videoCallActive && state.channelUsers.length > 1) {
      const callBtn = el('div', { class: 'voice-call-btn' }, [videoCallIcon(), el('span', {}, t('视频通话'))]);
      const otherUser = state.channelUsers.find(u => u.id !== state.userId);
      if (otherUser) { callBtn.addEventListener('click', () => startVideoCall(otherUser.id, otherUser.username)); container.appendChild(callBtn); }
    }
  }

  function micOffIcon() {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('width', '14'); svg.setAttribute('height', '14');
    svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor'); svg.setAttribute('stroke-width', '2');
    svg.setAttribute('stroke-linecap', 'round'); svg.setAttribute('stroke-linejoin', 'round');
    svg.innerHTML = `<line x1="1" y1="1" x2="23" y2="23"/><path d="M9 9v3a3 3 0 0 0 5.12 2.12M15 9.34V4a3 3 0 0 0-5.94-.6"/><path d="M17 16.95A7 7 0 0 1 5 12v-2m14 0v2a7 7 0 0 1-.11 1.23"/><line x1="12" y1="19" x2="12" y2="23"/>`;
    return svg;
  }

  function deafenIcon() {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('width', '14'); svg.setAttribute('height', '14');
    svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor'); svg.setAttribute('stroke-width', '2');
    svg.setAttribute('stroke-linecap', 'round'); svg.setAttribute('stroke-linejoin', 'round');
    svg.innerHTML = `<path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3zM3 19a2 2 0 0 0 2 2h1a2 2 0 0 0 2-2v-3a2 2 0 0 0-2-2H3z"/>`;
    return svg;
  }

  function videoCallIcon() {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('width', '16'); svg.setAttribute('height', '16');
    svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor'); svg.setAttribute('stroke-width', '2');
    svg.setAttribute('stroke-linecap', 'round'); svg.setAttribute('stroke-linejoin', 'round');
    svg.innerHTML = `<polygon points="23 7 16 12 23 17 23 7"/><rect x="1" y="5" width="15" height="14" rx="2" ry="2"/>`;
    return svg;
  }

  function updateMyStatus() {
    $('my-username').textContent = state.username || '—';
    $('my-avatar').textContent = getInitials(state.username);
    $('my-avatar').className = `user-avatar ${getAvatarColor(state.username)}`;
    let statusText = t('在线');
    if (state.isMuted && state.isDeafened) statusText = t('已静音') + '/' + t('已关闭音频');
    else if (state.isMuted) statusText = t('已静音');
    else if (state.isDeafened) statusText = t('已关闭音频');
    else if (state.inChannel) statusText = state.currentChannelName || t('语音频道');
    $('my-status').textContent = statusText;
    $('btn-mute').classList.toggle('active', state.isMuted);
    $('btn-deafen').classList.toggle('deafened', state.isDeafened);
    $('header-channel-name').textContent = state.currentChannelName || t('未连接频道');
    $('voice-channel-label').textContent = state.inChannel
      ? `${t('语音频道')} / ${state.currentChannelName}` : t('未连接语音频道');
    $('server-name-display').textContent = state.serverName || 'NEVO Server';
    // Show admin rail item if admin
    $('rail-admin').style.display = state.isAdmin ? 'flex' : 'none';
  }

  function updateLatencyDisplay() {
    const el2 = $('latency-value');
    if (!el2) return;
    el2.textContent = state.latency > 0 ? `${state.latency}ms` : '--';
    const display = $('latency-display');
    if (display) {
      if (state.latency > 200) display.style.color = 'var(--state-error)';
      else if (state.latency > 100) display.style.color = 'var(--state-warning)';
      else display.style.color = 'var(--color-text-muted)';
    }
  }

  // ============================================================
  // Chat messages (with rich text: code blocks, URLs, emoji)
  // ============================================================
  function escapeHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function formatChatText(text) {
    const codeParts = [];
    let html = String(text == null ? '' : text);
    // 1) 先提取代码块/行内代码，避免后续 URL 链接化与转义破坏代码内容
    html = html.replace(/```([\s\S]*?)```/g, (m, code) => {
      codeParts.push({ isBlock: true, code });
      return '@@NEVO_CODE_' + (codeParts.length - 1) + '@@';
    });
    html = html.replace(/`([^`]+)`/g, (m, code) => {
      codeParts.push({ isBlock: false, code });
      return '@@NEVO_CODE_' + (codeParts.length - 1) + '@@';
    });
    // 2) 转义 HTML（含引号，防止 URL 属性注入导致 XSS）
    html = escapeHtml(html);
    // 3) URL 链接化（排除空白与引号字符）
    html = html.replace(/(https?:\/\/[^\s<>"']+)/g,
      '<a href="$1" target="_blank" rel="noopener" class="msg-link">$1</a>');
    // 4) 还原代码片段
    html = html.replace(/@@NEVO_CODE_(\d+)@@/g, (m, i) => {
      const part = codeParts[+i];
      if (!part) return m;
      const escaped = escapeHtml(part.code);
      return part.isBlock
        ? '<pre class="msg-code">' + escaped + '</pre>'
        : '<code class="msg-inline-code">' + escaped + '</code>';
    });
    return html;
  }

  function addChatMessage(data) {
    const container = $('messages');
    const isMe = data.user_id === state.userId;
    const msg = el('div', { class: 'message fade-in' });
    msg.appendChild(el('div', { class: `msg-avatar ${getAvatarColor(data.username)}` }, getInitials(data.username)));
    const body = el('div', { class: 'msg-body' });
    const header = el('div', { class: 'msg-header' });
    header.appendChild(el('span', { class: 'msg-username', style: `color: ${isMe ? 'var(--color-primary)' : 'var(--state-info)'};` }, data.username));
    let ts = Number(data.timestamp);
    if (!ts || ts <= 0) ts = Date.now();
    else if (ts < 1e12) ts *= 1000; // 兼容秒级时间戳
    const time = new Date(ts);
    header.appendChild(el('span', { class: 'msg-time' }, (time.getHours() < 10 ? '0' : '') + time.getHours() + ':' + (time.getMinutes() < 10 ? '0' : '') + time.getMinutes()));
    body.appendChild(header);
    const textP = el('p', { class: 'msg-text' });
    textP.innerHTML = formatChatText(data.text);
    body.appendChild(textP);
    msg.appendChild(body);
    container.appendChild(msg);
    container.scrollTop = container.scrollHeight;
    // Play message sound + notification
    if (!isMe && getSetting('msg_sound', true) && window.NevoMedia) window.NevoMedia.playSound('message');
    if (!isMe && getSetting('desktop_notify', false) && window.NevoMedia) {
      window.NevoMedia.showNotification(data.username, data.text);
    }
  }

  function addSystemMessage(text) {
    const container = $('messages');
    const msg = el('div', { class: 'message-system fade-in' });
    msg.appendChild(el('div', { class: 'line' }));
    msg.appendChild(el('span', { class: 'text' }, text));
    msg.appendChild(el('div', { class: 'line' }));
    container.appendChild(msg);
    container.scrollTop = container.scrollHeight;
  }

  // ============================================================
  // Emoji Panel
  // ============================================================
  const EMOJI_DATA = {
    smileys: ['😀','😄','😁','😊','😍','🥰','😎','🤔','😂','🤣','😅','🙂','😇','🤩','😴','🤤','😪','😓','😏','😬'],
    gestures: ['👍','👎','👌','✌️','🤞','🤟','🤘','👏','🙌','🤝','🙏','💪','✊','👊','🤛','🤜','👋','🤚','✋','🖐️'],
    animals: ['🐶','🐱','🐭','🐹','🐰','🦊','🐻','🐼','🐨','🐯','🦁','🐮','🐷','🐸','🐵','🐔','🐧','🐦','🦆','🦅'],
    food: ['🍔','🍟','🍕','🌭','🥪','🌮','🌯','🍜','🍣','🍱','🍙','🍘','🍚','🍰','🎂','🍦','🍩','🍪','🍫','🍬'],
    activities: ['⚽','🏀','🏈','⚾','🎾','🏐','🏉','🎱','🏓','🏸','🥅','🏒','🎯','🎮','🎲','🎵','🎶','🎤','🎧','🎸'],
    symbols: ['❤️','🧡','💛','💚','💙','💜','🖤','🤍','💔','❣️','💕','💞','💓','💗','💖','💘','💯','✨','🔥','⭐'],
  };

  function initEmojiPanel() {
    const grid = $('emoji-grid');
    const cats = document.querySelectorAll('.emoji-cat');
    function renderCat(cat) {
      grid.innerHTML = '';
      EMOJI_DATA[cat].forEach(e => {
        const btn = el('button', { class: 'emoji-item' }, e);
        btn.addEventListener('click', () => {
          const input = $('chat-input');
          const start = input.selectionStart || input.value.length;
          const end = input.selectionEnd || input.value.length;
          input.value = input.value.slice(0, start) + e + input.value.slice(end);
          input.focus();
          const pos = start + e.length;
          input.setSelectionRange(pos, pos);
        });
        grid.appendChild(btn);
      });
    }
    renderCat('smileys');
    cats.forEach(c => c.addEventListener('click', () => {
      cats.forEach(x => x.classList.remove('active'));
      c.classList.add('active');
      renderCat(c.dataset.cat);
    }));
  }

  // ============================================================
  // Admin Panel
  // ============================================================
  function openAdminPanel() {
    $('admin-modal-overlay').classList.add('show');
    if (state.isAdminAuthed) showAdminActions(); else showAdminAuthForm();
  }

  function closeAdminPanel() { $('admin-modal-overlay').classList.remove('show'); }

  function showAdminAuthForm() {
    $('admin-auth-form').style.display = 'block';
    $('admin-actions').style.display = 'none';
  }

  function showAdminActions() {
    $('admin-auth-form').style.display = 'none';
    $('admin-actions').style.display = 'block';
    renderAdminPanel();
  }

  async function doAdminAuth() {
    const password = $('admin-password-input').value;
    if (!password) { toast(t('请输入管理员密码'), 'error'); return; }
    await sendCommand('admin_auth', { password });
  }

  function handleAdminAuthResult(data) {
    if (data.success) {
      state.isAdminAuthed = true;
      state.isAdmin = true;
      toast(t('管理员认证成功'), 'success');
      showAdminActions();
      updateMyStatus();
    } else {
      toast(t('管理员认证失败') + ': ' + (data.message || ''), 'error');
    }
  }

  function handleAdminActionResult(data) {
    toast(data.success ? t('操作成功') : t('操作失败') + ': ' + (data.message || ''),
      data.success ? 'success' : 'error');
    if (data.success) renderAdminPanel();
  }

  function renderAdminPanel() {
    if (!state.isAdminAuthed) return;
    // Channel list
    const chList = $('admin-channel-list');
    chList.innerHTML = '';
    state.channels.forEach(ch => {
      const item = el('div', { class: 'admin-channel-item' }, [
        el('span', { class: 'admin-ch-name' }, ch.name),
        el('div', { class: 'admin-ch-actions' }, [
          el('button', { class: 'nevo-btn nevo-btn-sm', title: t('重命名频道'), onclick: () => adminRenameChannel(ch) }, '✎'),
          el('button', { class: 'nevo-btn nevo-btn-sm nevo-btn-danger', title: t('删除频道'), onclick: () => adminDeleteChannel(ch) }, '✕'),
        ]),
      ]);
      chList.appendChild(item);
    });
    // User list (all users in all channels)
    const userList = $('admin-user-list');
    userList.innerHTML = '';
    const allUsers = new Map();
    state.channels.forEach(ch => {
      (ch.users || []).forEach(u => { if (!allUsers.has(u.id)) allUsers.set(u.id, Object.assign({}, u, { channelName: ch.name })); });
    });
    allUsers.forEach(user => {
      const item = el('div', { class: 'admin-user-item' }, [
        el('span', { class: 'admin-user-name' }, user.username),
        el('span', { class: 'admin-user-channel' }, user.channelName),
        el('div', { class: 'admin-user-actions' }, [
          el('button', { class: 'nevo-btn nevo-btn-sm', title: t('移动用户'), onclick: () => adminMoveUser(user) }, '→'),
          el('button', { class: 'nevo-btn nevo-btn-sm', title: t('踢出用户'), onclick: () => adminKickUser(user) }, '⏏'),
          el('button', { class: 'nevo-btn nevo-btn-sm nevo-btn-danger', title: t('封禁用户'), onclick: () => adminBanUser(user) }, '🚫'),
        ]),
      ]);
      userList.appendChild(item);
    });
  }

  async function adminCreateChannel() {
    const name = $('admin-channel-name').value.trim();
    if (!name) { toast(t('请输入频道名称'), 'error'); return; }
    await sendCommand('create_channel', { name, parent_id: 0 });
    $('admin-channel-name').value = '';
  }

  async function adminSetServerName() {
    const name = $('admin-server-name').value.trim();
    if (!name) { toast(t('请输入新的服务器名称'), 'error'); return; }
    await sendCommand('set_server_name', { server_name: name });
    state.serverName = name;
    updateMyStatus();
    $('admin-server-name').value = '';
  }

  async function adminRenameChannel(ch) {
    const newName = prompt(t('请输入新频道名称'), ch.name);
    if (newName && newName !== ch.name) await sendCommand('rename_channel', { channel_id: ch.id, new_name: newName });
  }

  async function adminDeleteChannel(ch) {
    if (confirm(t('确定要删除此频道吗？此操作不可撤销。'))) await sendCommand('delete_channel', { channel_id: ch.id });
  }

  async function adminKickUser(user) {
    const reason = prompt(`${t('踢出用户')} ${user.username}:`, '');
    if (reason !== null) await sendCommand('kick_user', { user_id: user.id, reason });
  }

  async function adminBanUser(user) {
    const reason = prompt(`${t('封禁用户')} ${user.username}:`, '');
    if (reason !== null) await sendCommand('ban_user', { user_id: user.id, reason, expires_at: 0 });
  }

  async function adminMoveUser(user) {
    const chNames = state.channels.map(c => `${c.id}:${c.name}`).join('\n');
    const target = prompt(`${t('移动用户')} ${user.username} ${t('到频道ID')}:`, '');
    if (target) await sendCommand('move_user', { user_id: user.id, channel_id: parseInt(target, 10) });
  }

  // ============================================================
  // File Transfer
  // ============================================================
  async function loadFileList() {
    await sendCommand('file_list', { channel_id: state.currentChannelId });
    // 服务端通过 file_list 事件异步返回列表
  }

  function renderFileList() {
    const container = $('file-list');
    container.innerHTML = '';
    if (state.fileList.length === 0) {
      container.innerHTML = `<div class="file-empty">${t('无文件')}</div>`;
      return;
    }
    state.fileList.forEach(f => {
      const item = el('div', { class: 'file-item' }, [
        el('div', { class: 'file-icon' }, '📄'),
        el('div', { class: 'file-info' }, [
          el('div', { class: 'file-name' }, f.filename || f.name || 'unknown'),
          el('div', { class: 'file-size' }, formatFileSize(f.size || f.file_size || 0)),
        ]),
        el('div', { class: 'file-actions' }, [
          el('button', { class: 'nevo-btn nevo-btn-sm', title: t('下载'), onclick: () => downloadFile(f) }, '⬇'),
          el('button', { class: 'nevo-btn nevo-btn-sm nevo-btn-danger', title: t('删除'), onclick: () => deleteFile(f) }, '✕'),
        ]),
      ]);
      container.appendChild(item);
    });
  }

  function formatFileSize(bytes) {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  }

  async function uploadFile(file) {
    const resp = await sendCommand('file_upload', {
      channel_id: state.currentChannelId,
      filename: file.name,
      file_size: file.size,
    });
    if (resp.ok) {
      toast(t('操作成功') + ': ' + t('文件上传请求已发送'), 'success');
      // Note: actual file data transfer would happen via a separate channel
    } else {
      toast(t('操作失败') + ': ' + (resp.error || ''), 'error');
    }
  }

  async function downloadFile(f) {
    // File download via gateway would need a separate HTTP endpoint
    toast(t('下载') + ': ' + (f.filename || f.name || ''), 'info');
  }

  async function deleteFile(f) {
    const fileId = f.id || f.file_id;
    if (confirm(t('删除') + ' ' + (f.filename || f.name || '') + '?')) {
      await sendCommand('file_delete', { file_id: fileId });
      loadFileList();
    }
  }

  function handleFileUploadResponse(data) {
    if (data.success) {
      toast(t('操作成功'), 'success');
      loadFileList();
    } else {
      toast(t('操作失败') + ': ' + (data.message || ''), 'error');
    }
  }

  // ============================================================
  // Screen Share
  // ============================================================
  async function toggleScreenShare() {
    if (state.screenSharing) {
      if (window.NevoMedia) window.NevoMedia.stopScreenShare();
      state.screenSharing = false;
      await sendCommand('screen_share_stop', { channel_id: state.currentChannelId });
      $('vc-screen').classList.remove('active');
      $('vc-screen').classList.add('inactive');
      toast(t('屏幕共享') + ' OFF', 'info');
    } else {
      try {
        if (window.NevoMedia) await window.NevoMedia.startScreenShare();
        state.screenSharing = true;
        await sendCommand('screen_share_start', {
          channel_id: state.currentChannelId,
          source_type: 0, source_title: 'Screen',
          width: 1920, height: 1080, fps: 15,
        });
        $('vc-screen').classList.add('active');
        $('vc-screen').classList.remove('inactive');
        toast(t('屏幕共享') + ' ON', 'success');
      } catch (e) {
        toast(t('屏幕共享') + ': ' + (e.message || e.name), 'error');
      }
    }
  }

  function handleScreenShareState(data) {
    if (data.is_sharing) {
      toast(`${data.source_title || t('屏幕共享')}`, 'info');
    }
  }

  // ============================================================
  // Actions
  // ============================================================
  async function doLogin() {
    try {
      const host = $('input-host').value.trim() || '127.0.0.1';
      const port = parseInt($('input-port').value.trim() || '24430', 10);
      const username = $('input-username').value.trim();
      const password = $('input-password').value;
      if (!username) { $('connect-error').textContent = t('请输入用户名'); $('connect-error').classList.add('show'); return; }
      $('connect-error').classList.remove('show');
      const btn = $('btn-connect');
      btn.disabled = true; btn.textContent = t('连接中...');
      console.log('[LOGIN] Sending login command', { host, port, username, wsReady: state.ws ? state.ws.readyState : null });
      const resp = await sendCommand('login', { host, port, username, password });
      console.log('[LOGIN] Response:', resp);
      if (resp.ok) {
        state.username = resp.username || username;
        state.userId = resp.user_id || 0;
        state.isAdmin = resp.is_admin || false;
        state.connected = true;
        syncCurrentChannel();
        // Save to recent servers
        saveRecentServer(host, port, username);
        renderServerQuickAccess();
        updateMyStatus();
        showPage('page-main');
        toast(`${t('已连接')} ${state.username}`, 'success');
        addSystemMessage(t('欢迎来到 NEVO 服务器'));
        if (getSetting('msg_sound', true) && window.NevoMedia) window.NevoMedia.playSound('connect');
        // Request notification permission if enabled
        if (getSetting('desktop_notify', false) && window.NevoMedia) window.NevoMedia.requestNotificationPermission();
      } else {
        $('connect-error').textContent = resp.error || t('连接失败');
        $('connect-error').classList.add('show');
        toast(resp.error || t('连接失败'), 'error');
      }
      btn.disabled = false; btn.textContent = t('连接');
    } catch (err) {
      console.error('[LOGIN] Error:', err);
      toast('Login error: ' + err.message, 'error');
      const btn = $('btn-connect');
      if (btn) { btn.disabled = false; btn.textContent = t('连接'); }
    }
  }

  async function joinChannel(channelId) {
    const resp = await sendCommand('join_channel', { channel_id: channelId });
    if (resp.ok) {
      state.currentChannelId = channelId;
      state.currentChannelName = resp.channel_name || '';
      $('messages').innerHTML = '';
      addSystemMessage(`${t('已加入')} #${state.currentChannelName}`);
      const ch = state.channels.find(c => c.id === channelId);
      if (ch) { state.channelUsers = ch.users || []; renderVoiceUsers(); }
      updateMyStatus();
      renderChannelList();
      // Play join channel sound effect
      playSoundEffect('/sounds/join_channel.mp3');
      // Start voice engine
      await startVoiceEngine();
    } else {
      toast(resp.error || t('加入频道失败'), 'error');
    }
  }

  async function leaveChannel() {
    stopVoiceEngine();
    await sendCommand('leave_channel');
    state.currentChannelId = 0;
    state.currentChannelName = '';
    state.channelUsers = [];
    state.inChannel = false;
    $('chat-input').disabled = true;
    renderVoiceUsers();
    updateMyStatus();
    renderChannelList();
    toast(t('已离开频道'), 'info');
  }

  async function sendChat() {
    const input = $('chat-input');
    const text = input.value.trim();
    if (!text) return;
    input.value = '';
    await sendCommand('send_chat', { text, channel_id: state.currentChannelId });
    input.focus();
  }

  async function toggleMute() {
    state.isMuted = !state.isMuted;
    await sendCommand('toggle_mute', { muted: state.isMuted });
    updateMyStatus();
    renderVoiceUsers();
    toast(state.isMuted ? t('已静音') : t('已取消静音'), 'info', 1500);
  }

  async function toggleDeafen() {
    state.isDeafened = !state.isDeafened;
    if (state.isDeafened) state.isMuted = true;
    await sendCommand('toggle_deafen', { deafened: state.isDeafened });
    updateMyStatus();
    renderVoiceUsers();
    toast(state.isDeafened ? t('已关闭音频') : t('已恢复音频'), 'info', 1500);
  }

  async function startVideoCall(peerId, peerName) {
    const resp = await sendCommand('start_video_call', { callee_id: peerId });
    if (resp.ok) {
      state.currentCallId = resp.call_id;
      state.callPeerName = peerName;
      toast(`${t('正在呼叫')} ${peerName}...`, 'info');
    } else {
      toast(resp.error || t('发起通话失败'), 'error');
    }
  }

  async function acceptVideoCall() {
    $('incoming-call-overlay').classList.remove('show');
    const resp = await sendCommand('accept_video_call', { call_id: state.incomingCallId });
    if (!resp.ok) toast(resp.error || t('接听失败'), 'error');
  }

  async function rejectVideoCall() {
    $('incoming-call-overlay').classList.remove('show');
    await sendCommand('reject_video_call', { call_id: state.incomingCallId });
    toast(t('已拒绝通话'), 'info');
  }

  async function hangupVideoCall() {
    if (state.currentCallId) await sendCommand('hangup_video_call', { call_id: state.currentCallId });
    stopVideoCallMedia();
    state.videoCallActive = false;
    state.currentCallId = 0;
    if (state.callTimerInterval) { clearInterval(state.callTimerInterval); state.callTimerInterval = null; }
    showPage('page-main');
  }

  async function disconnect() {
    stopVoiceEngine();
    await sendCommand('disconnect');
    state.connected = false;
    state.inChannel = false;
    state.channels = [];
    state.channelUsers = [];
    showPage('page-connect');
    if (getSetting('msg_sound', true) && window.NevoMedia) window.NevoMedia.playSound('disconnect');
  }

  // ============================================================
  // Server Quick Access (favorites + recent)
  // ============================================================
  const SERVERS_KEY = 'nevo_servers';

  function loadSavedServers() {
    try {
      const data = JSON.parse(localStorage.getItem(SERVERS_KEY) || '{}');
      state.serverFavorites = data.favorites || [];
      state.serverRecent = data.recent || [];
    } catch (_) { state.serverFavorites = []; state.serverRecent = []; }
  }

  function saveSavedServers() {
    try {
      localStorage.setItem(SERVERS_KEY, JSON.stringify({
        favorites: state.serverFavorites, recent: state.serverRecent,
      }));
    } catch (_) {}
  }

  function saveRecentServer(host, port, username) {
    const entry = { host, port, username, time: Date.now() };
    state.serverRecent = state.serverRecent.filter(s => !(s.host === host && s.port === port));
    state.serverRecent.unshift(entry);
    state.serverRecent = state.serverRecent.slice(0, 5);
    saveSavedServers();
  }

  function renderServerQuickAccess() {
    const container = $('server-quick-access');
    const list = $('sqa-list');
    const activeTabEl = document.querySelector('.sqa-tab.active');
    const activeTab = activeTabEl && activeTabEl.dataset ? activeTabEl.dataset['sqa-tab'] : 'favorites';
    const servers = activeTab === 'favorites' ? state.serverFavorites : state.serverRecent;
    if (servers.length === 0) {
      container.style.display = 'none';
      return;
    }
    container.style.display = 'block';
    list.innerHTML = '';
    servers.forEach(s => {
      const connectBtn = el('button', { class: 'sqa-connect' }, t('连接'));
      connectBtn.addEventListener('click', () => {
        $('input-host').value = s.host;
        $('input-port').value = s.port;
        $('input-username').value = s.username || '';
        doLogin();
      });
      const starBtn = el('button', { class: 'sqa-star' }, state.serverFavorites.some(f => f.host === s.host && f.port === s.port) ? '★' : '☆');
      starBtn.addEventListener('click', () => toggleFavorite(s));
      const item = el('div', { class: 'sqa-item' }, [
        el('div', { class: 'sqa-info' }, [
          el('div', { class: 'sqa-host' }, s.host + ':' + s.port),
          el('div', { class: 'sqa-user' }, s.username || ''),
        ]),
        connectBtn,
        starBtn,
      ]);
      list.appendChild(item);
    });
  }

  function toggleFavorite(server) {
    const idx = state.serverFavorites.findIndex(f => f.host === server.host && f.port === server.port);
    if (idx >= 0) state.serverFavorites.splice(idx, 1);
    else state.serverFavorites.push({ host: server.host, port: server.port, username: server.username });
    saveSavedServers();
    renderServerQuickAccess();
  }

  // ============================================================
  // Theme switching
  // ============================================================
  function applyTheme(theme) {
    const root = document.documentElement;
    if (theme === 'auto') {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      root.setAttribute('data-theme', prefersDark ? 'dark' : 'light');
    } else {
      root.setAttribute('data-theme', theme);
    }
  }

  // ============================================================
  // Settings persistence
  // ============================================================
  const SETTINGS_KEY = 'nevo_settings_v1';
  const persistedSettings = loadSettings();

  function loadSettings() { try { return JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}'); } catch (_) { return {}; } }
  function saveSetting(key, value) { persistedSettings[key] = value; try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(persistedSettings)); } catch (_) {} }
  function getSetting(key, fallback) { const v = persistedSettings[key]; return v === undefined ? fallback : v; }

  // 供 media.js 读取输出音量等设置
  window.NevoApp.getSetting = getSetting;

  function applyStoredSettingsToUI() {
    const fpsSel = $('setting-fps');
    if (fpsSel && getSetting('fps', null) !== null) fpsSel.value = String(getSetting('fps', '30'));
    const resSel = $('setting-resolution');
    if (resSel && getSetting('resolution', null) !== null) resSel.value = getSetting('resolution', '1280x720');
    const volSlider = $('setting-output-volume');
    if (volSlider && getSetting('output_volume', null) !== null) volSlider.value = String(getSetting('output_volume', '100'));
    const themeSel = $('setting-theme');
    if (themeSel) themeSel.value = getSetting('theme', 'dark');
    const langSel = $('setting-language');
    if (langSel && window.NevoI18n) langSel.value = window.NevoI18n.getLanguage() || 'zh_CN';
    const modeSel = $('setting-input-mode');
    if (modeSel) modeSel.value = getSetting('input_mode', 'continuous');
    const pttKeyInput = $('setting-ptt-key');
    if (pttKeyInput) pttKeyInput.value = getSetting('ptt_key_label', '') || '';
    const vadSlider = $('setting-vad-sensitivity');
    if (vadSlider && getSetting('vad_sensitivity', null) !== null) vadSlider.value = String(getSetting('vad_sensitivity', '30'));
    // Restore toggles
    document.querySelectorAll('.toggle[data-setting]').forEach(toggle => {
      const name = toggle.dataset.setting;
      const stored = getSetting(name, null);
      if (stored !== null) toggle.classList.toggle('on', !!stored);
    });
    // 默认开启自动检测（若未设置过）
    const autoCheck = document.querySelector('.toggle[data-setting="auto_check_update"]');
    if (autoCheck && getSetting('auto_check_update', null) === null) {
      autoCheck.classList.add('on');
      saveSetting('auto_check_update', true);
    }
  }

  function getVideoConstraints() {
    const resEl = $('setting-resolution');
    const res = (resEl && resEl.value) || getSetting('resolution', '1280x720');
    const fpsEl = $('setting-fps');
    const fps = parseInt((fpsEl && fpsEl.value) || String(getSetting('fps', '30')), 10);
    const [w, h] = res.split('x').map(Number);
    const constraints = { width: { ideal: w || 1280 }, height: { ideal: h || 720 } };
    if (fps) constraints.frameRate = { ideal: fps };
    const savedCamera = getSetting('camera_device', '');
    if (savedCamera) constraints.deviceId = { exact: savedCamera };
    return constraints;
  }

  // ============================================================
  // PTT key capture
  // ============================================================
  function startPTTKeyCapture() {
    state.capturingPTTKey = true;
    const input = $('setting-ptt-key');
    input.value = t('按下按键...');
    input.focus();
  }

  function handlePTTKeyCapture(e) {
    if (!state.capturingPTTKey) return;
    e.preventDefault();
    e.stopPropagation();
    if (e.key === 'Escape') {
      state.capturingPTTKey = false;
      $('setting-ptt-key').value = getSetting('ptt_key_label', '') || '';
      return;
    }
    const keyLabel = e.key === ' ' ? 'Space' : e.key;
    state.pttKey = e.code;
    saveSetting('ptt_key', e.code);
    saveSetting('ptt_key_label', keyLabel);
    $('setting-ptt-key').value = keyLabel;
    state.capturingPTTKey = false;
  }

  // ============================================================
  // Device Management & Settings Testing
  // ============================================================
  const deviceState = {
    micStream: null, micAudioContext: null, micAnalyser: null, micRafId: null, micActive: false,
    cameraStream: null, cameraActive: false,
    selectedInputId: '', selectedOutputId: '', selectedCameraId: '',
  };

  let deviceListenerRegistered = false;

  async function enumerateMediaDevices() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) return;
    let devices = [];
    try { devices = await navigator.mediaDevices.enumerateDevices(); } catch (err) { return; }
    fillDeviceSelect('setting-input-device', devices.filter(d => d.kind === 'audioinput'), t('麦克风'));
    fillDeviceSelect('setting-output-device', devices.filter(d => d.kind === 'audiooutput'), t('扬声器'));
    fillDeviceSelect('setting-camera', devices.filter(d => d.kind === 'videoinput'), t('摄像头'));
    // 只注册一次监听，避免重复调用 enumerateMediaDevices 时堆积监听器
    if (!deviceListenerRegistered) {
      navigator.mediaDevices.addEventListener('devicechange', enumerateMediaDevices);
      deviceListenerRegistered = true;
    }
  }

  function sanitizeDeviceLabel(label) {
    if (!label) return '';
    return label.replace(/\s*\([0-9a-fA-F]{4}:[0-9a-fA-F]{4}\)\s*$/g, '').trim();
  }

  function fillDeviceSelect(selectId, devices, defaultLabel) {
    const sel = $(selectId);
    if (!sel) return;
    const key = selectId === 'setting-input-device' ? 'selectedInputId' : selectId === 'setting-output-device' ? 'selectedOutputId' : 'selectedCameraId';
    const prevValue = sel.value || deviceState[key];
    sel.innerHTML = '';
    const defaultOpt = document.createElement('option');
    defaultOpt.value = '';
    defaultOpt.textContent = `${t('默认')}${defaultLabel}`;
    sel.appendChild(defaultOpt);
    devices.forEach((d, i) => {
      const opt = document.createElement('option');
      opt.value = d.deviceId;
      opt.textContent = sanitizeDeviceLabel(d.label) || `${defaultLabel} ${i + 1}`;
      sel.appendChild(opt);
    });
    if (prevValue && [...sel.options].some(o => o.value === prevValue)) sel.value = prevValue;
  }

  async function startMicTest() {
    const btn = $('btn-test-mic');
    if (deviceState.micActive) { stopMicTest(); return; }
    const constraints = {
      audio: {
        deviceId: deviceState.selectedInputId ? { exact: deviceState.selectedInputId } : undefined,
        echoCancellation: getToggleSetting('echo_cancellation'),
        autoGainControl: getToggleSetting('auto_gain'),
        noiseSuppression: true,
      },
    };
    try { deviceState.micStream = await navigator.mediaDevices.getUserMedia(constraints); }
    catch (err) { toast(t('无法访问麦克风') + ': ' + (err.message || err.name), 'error'); return; }
    deviceState.micAudioContext = new (window.AudioContext || window.webkitAudioContext)();
    const source = deviceState.micAudioContext.createMediaStreamSource(deviceState.micStream);
    deviceState.micAnalyser = deviceState.micAudioContext.createAnalyser();
    deviceState.micAnalyser.fftSize = 256;
    deviceState.micAnalyser.smoothingTimeConstant = 0.6;
    source.connect(deviceState.micAnalyser);
    deviceState.micActive = true;
    btn.textContent = t('停止测试'); btn.classList.add('active');
    const dataArr = new Uint8Array(deviceState.micAnalyser.frequencyBinCount);
    const meterBar = $('mic-meter-bar');
    const tick = () => {
      if (!deviceState.micActive) return;
      deviceState.micAnalyser.getByteFrequencyData(dataArr);
      let sum = 0;
      for (let i = 0; i < dataArr.length; i++) sum += dataArr[i];
      const avg = sum / dataArr.length;
      const pct = Math.min(100, (avg / 128) * 100);
      meterBar.style.width = pct + '%';
      deviceState.micRafId = requestAnimationFrame(tick);
    };
    tick();
  }

  function stopMicTest() {
    deviceState.micActive = false;
    if (deviceState.micRafId) cancelAnimationFrame(deviceState.micRafId);
    deviceState.micRafId = null;
    if (deviceState.micStream) { deviceState.micStream.getTracks().forEach(t => t.stop()); deviceState.micStream = null; }
    if (deviceState.micAudioContext) { try { deviceState.micAudioContext.close(); } catch (_) {} deviceState.micAudioContext = null; }
    deviceState.micAnalyser = null;
    const btn = $('btn-test-mic');
    if (btn) { btn.textContent = t('测试麦克风'); btn.classList.remove('active'); }
    const meterBar = $('mic-meter-bar');
    if (meterBar) meterBar.style.width = '0%';
  }

  function playSpeakerTest() {
    const btn = $('btn-test-speaker');
    const volEl = $('setting-output-volume');
    const volume = parseInt((volEl && volEl.value) || '100', 10) / 100;
    const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    const notes = [{ freq: 440, start: 0, dur: 0.15 }, { freq: 660, start: 0.2, dur: 0.15 }, { freq: 880, start: 0.4, dur: 0.25 }];
    const dest = audioCtx.createGain();
    dest.gain.value = volume;
    dest.connect(audioCtx.destination);
    notes.forEach(n => {
      const osc = audioCtx.createOscillator();
      osc.type = 'sine'; osc.frequency.value = n.freq;
      const gain = audioCtx.createGain();
      gain.gain.setValueAtTime(0, audioCtx.currentTime + n.start);
      gain.gain.linearRampToValueAtTime(0.3, audioCtx.currentTime + n.start + 0.02);
      gain.gain.linearRampToValueAtTime(0, audioCtx.currentTime + n.start + n.dur);
      osc.connect(gain); gain.connect(dest);
      osc.start(audioCtx.currentTime + n.start);
      osc.stop(audioCtx.currentTime + n.start + n.dur);
    });
    btn.classList.add('active'); btn.textContent = t('播放中...');
    setTimeout(() => { btn.classList.remove('active'); btn.textContent = t('测试扬声器'); try { audioCtx.close(); } catch (_) {} }, 800);
  }

  async function toggleCameraTest() {
    if (deviceState.cameraActive) { stopCameraTest(); return; }
    const btn = $('btn-test-camera');
    const previewRow = $('camera-preview-row');
    const video = $('settings-camera-preview');
    const wrapper = video && video.parentElement ? video.parentElement : null;
    const resEl = $('setting-resolution');
    const res = (resEl && resEl.value) || '1280x720';
    const [w, h] = res.split('x').map(Number);
    const constraints = {
      video: {
        deviceId: deviceState.selectedCameraId ? { exact: deviceState.selectedCameraId } : undefined,
        width: { ideal: w }, height: { ideal: h },
      },
      audio: false,
    };
    try { deviceState.cameraStream = await navigator.mediaDevices.getUserMedia(constraints); }
    catch (err) {
      if (wrapper) wrapper.classList.remove('has-video');
      if (previewRow) previewRow.style.display = 'flex';
      const ph = $('camera-preview-placeholder');
      if (ph) ph.textContent = t('无法访问摄像头') + ': ' + (err.message || err.name);
      toast(t('无法访问摄像头') + ': ' + (err.message || err.name), 'error');
      return;
    }
    video.srcObject = deviceState.cameraStream;
    if (wrapper) wrapper.classList.add('has-video');
    previewRow.style.display = 'flex';
    deviceState.cameraActive = true;
    btn.textContent = t('停止测试'); btn.classList.add('active');
    enumerateMediaDevices();
  }

  function stopCameraTest() {
    if (deviceState.cameraStream) { deviceState.cameraStream.getTracks().forEach(t => t.stop()); deviceState.cameraStream = null; }
    deviceState.cameraActive = false;
    const btn = $('btn-test-camera');
    if (btn) { btn.textContent = t('测试摄像头'); btn.classList.remove('active'); }
    const previewRow = $('camera-preview-row');
    if (previewRow) previewRow.style.display = 'none';
  }

  function getToggleSetting(name) {
    const e = document.querySelector(`.toggle[data-setting="${name}"]`);
    return e ? e.classList.contains('on') : false;
  }

  function updateInputModeUI() {
    const modeEl = $('setting-input-mode');
    const mode = (modeEl && modeEl.value) || 'continuous';
    $('ptt-key-row').style.display = mode === 'ptt' ? 'flex' : 'none';
    $('vad-sensitivity-row').style.display = mode === 'vad' ? 'flex' : 'none';
    state.inputMode = mode;
    if (window.NevoMedia) window.NevoMedia.setInputMode(mode);
  }

  // ============================================================
  // Event listeners
  // ============================================================
  function initEventListeners() {
    // Connect form (submit + button click 都触发登录)
    $('connect-form').addEventListener('submit', (e) => { e.preventDefault(); doLogin(); });
    $('btn-connect').addEventListener('click', (e) => { e.preventDefault(); doLogin(); });
    $('link-advanced-settings').addEventListener('click', (e) => { e.preventDefault(); toast(t('高级设置暂未开放，请使用默认参数连接'), 'info'); });
    $('link-help').addEventListener('click', (e) => { e.preventDefault(); toast(t('使用帮助：输入服务器地址与用户名后点击连接即可。语音/视频需加入频道后使用。'), 'info', 5000); });

    // SQA tabs
    document.querySelectorAll('.sqa-tab').forEach(tab => {
      tab.addEventListener('click', () => {
        document.querySelectorAll('.sqa-tab').forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        renderServerQuickAccess();
      });
    });

    // Chat input
    $('chat-input').addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendChat(); }
    });

    // Mute / Deafen
    $('btn-mute').addEventListener('click', toggleMute);
    $('btn-deafen').addEventListener('click', toggleDeafen);

    // Settings navigation
    $('btn-settings').addEventListener('click', () => showPage('page-settings'));
    $('settings-back').addEventListener('click', () => showPage('page-main'));
    $('files-back').addEventListener('click', () => showPage('page-main'));

    // Rail items
    document.querySelectorAll('.rail-item').forEach(item => {
      item.addEventListener('click', () => {
        const action = item.dataset.rail;
        if (action === 'settings') showPage('page-settings');
        else if (action === 'disconnect') { if (confirm(t('确定要断开连接吗？'))) disconnect(); }
        else if (action === 'home') { toast(state.connected ? `${t('已连接服务器，当前用户')}：${state.username}` : t('尚未连接到服务器'), 'info', 2000); }
        else if (action === 'admin') openAdminPanel();
        else if (action === 'files') { showPage('page-files'); loadFileList(); }
      });
    });

    // Voice disconnect
    $('btn-disconnect-voice').addEventListener('click', () => { if (state.inChannel) leaveChannel(); });

    // Header buttons
    $('btn-members').addEventListener('click', () => {
      if (!state.inChannel) { toast(t('请先加入频道'), 'info', 2000); return; }
      if (state.channelUsers.length === 0) { toast(t('当前频道暂无其他成员'), 'info', 2500); return; }
      const list = state.channelUsers.map(u => `• ${u.username}${u.id === state.userId ? ` (${t('我')})` : ''}`).join('\n');
      toast(`${t('频道成员')}（${state.channelUsers.length}）：\n${list}`, 'info', 4500);
    });

    $('btn-search').addEventListener('click', () => {
      const kw = prompt(t('输入关键词搜索聊天记录'));
      if (!kw) return;
      const lc = kw.toLowerCase();
      const msgs = document.querySelectorAll('#messages .msg-text');
      let hits = 0;
      msgs.forEach(m => {
        if (m.textContent.toLowerCase().includes(lc)) {
          m.style.background = 'rgba(245, 166, 35, 0.18)';
          m.style.transition = 'background 0.3s';
          hits++;
          setTimeout(() => { m.style.background = ''; }, 2500);
        }
      });
      toast(hits > 0 ? `${t('找到')} ${hits} ${t('条匹配消息')}` : t('未找到匹配消息'), 'info', 2000);
    });

    // Attachment — trigger file upload
    $('btn-attach').addEventListener('click', () => {
      if (!state.inChannel) { toast(t('请先加入频道'), 'info', 2000); return; }
      showPage('page-files');
      loadFileList();
    });

    // Emoji panel toggle
    $('btn-emoji').addEventListener('click', (e) => {
      e.stopPropagation();
      const panel = $('emoji-panel');
      panel.style.display = panel.style.display === 'none' ? 'block' : 'none';
    });
    // Close emoji panel on outside click
    document.addEventListener('click', (e) => {
      const panel = $('emoji-panel');
      if (panel.style.display === 'block' && !panel.contains(e.target) && e.target.id !== 'btn-emoji') {
        panel.style.display = 'none';
      }
    });

    // Video call controls
    $('vc-mic').addEventListener('click', () => {
      const btn = $('vc-mic');
      btn.classList.toggle('active'); btn.classList.toggle('inactive');
      const isActive = btn.classList.contains('active');
      sendCommand('toggle_mute', { muted: !isActive });
      // Also mute local voice
      if (window.NevoMedia && window.NevoMedia.state) {
        window.NevoMedia.state.voiceActive = isActive && window.NevoMedia.state.voiceActive;
      }
    });

    $('vc-camera').addEventListener('click', () => {
      const btn = $('vc-camera');
      btn.classList.toggle('active'); btn.classList.toggle('inactive');
      const isActive = btn.classList.contains('active');
      if (isActive && state.localStream) state.localStream.getVideoTracks().forEach(t => t.enabled = true);
      else if (state.localStream) state.localStream.getVideoTracks().forEach(t => t.enabled = false);
      // Also toggle video encoding
      if (window.NevoMedia && window.NevoMedia.state && window.NevoMedia.state.videoActive && !isActive) {
        window.NevoMedia.stopVideo();
      } else if (isActive && window.NevoMedia && window.NevoMedia.state && !window.NevoMedia.state.videoActive) {
        const cameraId = getSetting('camera_device', '');
        const res = getSetting('resolution', '1280x720');
        const [w, h] = res.split('x').map(Number);
        const fps = parseInt(getSetting('fps', '30'), 10);
        window.NevoMedia.startVideo(cameraId, w || 640, h || 480, fps, state.localStream);
      }
    });

    $('vc-screen').addEventListener('click', toggleScreenShare);
    $('vc-settings').addEventListener('click', () => showPage('page-settings'));
    $('vc-hangup').addEventListener('click', hangupVideoCall);

    // Incoming call buttons
    $('btn-accept-call').addEventListener('click', acceptVideoCall);
    $('btn-reject-call').addEventListener('click', rejectVideoCall);

    // Admin modal
    $('admin-modal-close').addEventListener('click', closeAdminPanel);
    $('admin-modal-overlay').addEventListener('click', (e) => { if (e.target === $('admin-modal-overlay')) closeAdminPanel(); });
    $('btn-admin-auth').addEventListener('click', doAdminAuth);
    $('admin-password-input').addEventListener('keydown', (e) => { if (e.key === 'Enter') doAdminAuth(); });
    $('btn-admin-create-channel').addEventListener('click', adminCreateChannel);
    $('btn-admin-set-server-name').addEventListener('click', adminSetServerName);

    // File upload
    const uploadZone = $('file-upload-zone');
    const uploadInput = $('file-upload-input');
    uploadZone.addEventListener('click', () => uploadInput.click());
    uploadZone.addEventListener('dragover', (e) => { e.preventDefault(); uploadZone.classList.add('drag-over'); });
    uploadZone.addEventListener('dragleave', () => uploadZone.classList.remove('drag-over'));
    uploadZone.addEventListener('drop', (e) => {
      e.preventDefault();
      uploadZone.classList.remove('drag-over');
      const files = e.dataTransfer.files;
      for (const f of files) uploadFile(f);
    });
    uploadInput.addEventListener('change', () => {
      for (const f of uploadInput.files) uploadFile(f);
      uploadInput.value = '';
    });

    // ---- Online updater UI ----
    const upd = window.updaterAPI;
    const updStateEl = $('upd-status-text');
    const updStatusRow = $('upd-status-row');
    const updProgressRow = $('upd-progress-row');
    const updBar = $('upd-progress-bar');
    const updInfo = $('upd-progress-info');
    const updVersionEl = $('upd-current-version');

    function updSetStatus(text) {
      if (!updStatusRow) return;
      updStatusRow.style.display = 'flex';
      if (updStateEl) updStateEl.textContent = text;
    }
    function updShowProgress(pct, speed, downloaded, total) {
      if (!updProgressRow) return;
      updProgressRow.style.display = 'flex';
      if (updBar) updBar.style.width = (pct || 0) + '%';
      if (updInfo) {
        const speedStr = speed ? (speed / 1024 / 1024).toFixed(1) + ' MB/s' : '';
        updInfo.textContent = `${Math.round(pct || 0)}%  ${speedStr}`;
      }
    }
    function updApplyToUI(data) {
      if (!updVersionEl && upd) {
        upd.getStatus().then((st) => {
          if (updVersionEl && st && st.currentVersion) updVersionEl.textContent = st.currentVersion;
        });
      } else if (updVersionEl && data && data.currentVersion) {
        updVersionEl.textContent = data.currentVersion;
      }
    }

    if (upd) {
      $('btn-check-update').addEventListener('click', async () => {
        $('btn-check-update').disabled = true;
        updSetStatus(t('正在检查更新...'));
        const res = await upd.checkNow();
        $('btn-check-update').disabled = false;
        if (res.ok && res.info) {
          updSetStatus(`${t('发现新版本')} ${res.info.version}（${res.info.mode === 'delta' ? t('增量') : t('全量')}）`);
          const dl = await upd.download();
          if (dl.ok) {
            updSetStatus(t('下载完成，是否立即重启应用？'));
            if (confirm(t('新版本已就绪，是否立即重启应用？'))) {
              await upd.restartToApply();
            } else {
              updSetStatus(t('已暂存，将在下次启动时应用'));
            }
          } else {
            updSetStatus(t('下载失败') + ': ' + (dl.error || ''));
          }
        } else if (res.ok) {
          updSetStatus(t('当前已是最新版本'));
        } else {
          updSetStatus(t('检查更新失败') + ': ' + (res.error || ''));
        }
      });

      upd.onState((data) => {
        if (data.state === 'downloading') updSetStatus(t('正在下载更新...'));
        if (data.state === 'ready') updSetStatus(t('下载完成，等待重启应用'));
        if (data.state === 'error') updSetStatus(t('更新出错'));
        updApplyToUI(data);
      });
      upd.onProgress((data) => updShowProgress(data.percent, data.speed, data.downloaded, data.total));

      $('btn-view-update-log').addEventListener('click', async () => {
        const log = await upd.getLog();
        const lines = (log || []).slice(-50).map((e) =>
          `${e.timestamp} [${e.event}] ver=${e.target_version || e.current_version || ''} src=${e.source || ''} result=${e.result || ''} ${e.error || ''}`
        ).join('\n');
        alert(t('更新日志') + '\n' + (lines || t('暂无日志')));
      });
    }

    // Settings: theme + language
    $('setting-theme').addEventListener('change', () => {
      const theme = $('setting-theme').value;
      saveSetting('theme', theme);
      applyTheme(theme);
    });
    $('setting-language').addEventListener('change', () => {
      const lang = $('setting-language').value;
      if (window.NevoI18n) window.NevoI18n.setLanguage(lang);
      saveSetting('language', lang);
    });

    // Settings: input mode
    $('setting-input-mode').addEventListener('change', () => {
      const mode = $('setting-input-mode').value;
      saveSetting('input_mode', mode);
      updateInputModeUI();
    });
    $('btn-set-ptt-key').addEventListener('click', startPTTKeyCapture);
    $('setting-vad-sensitivity').addEventListener('input', () => {
      saveSetting('vad_sensitivity', $('setting-vad-sensitivity').value);
    });

    // Settings: toggles
    document.querySelectorAll('.toggle[data-setting]').forEach(toggle => {
      toggle.addEventListener('click', () => {
        toggle.classList.toggle('on');
        saveSetting(toggle.dataset.setting, toggle.classList.contains('on'));
        toggle.style.transform = 'scale(0.95)';
        setTimeout(() => { toggle.style.transform = 'scale(1)'; }, 100);
        // Request notification permission if enabling desktop_notify
        if (toggle.dataset.setting === 'desktop_notify' && toggle.classList.contains('on')) {
          if (window.NevoMedia) window.NevoMedia.requestNotificationPermission();
        }
      });
    });

    // Settings: device selects
    $('setting-input-device').addEventListener('change', () => {
      deviceState.selectedInputId = $('setting-input-device').value;
      saveSetting('input_device', deviceState.selectedInputId);
      if (deviceState.micActive) { stopMicTest(); startMicTest(); }
    });
    $('setting-output-device').addEventListener('change', () => {
      deviceState.selectedOutputId = $('setting-output-device').value;
      saveSetting('output_device', deviceState.selectedOutputId);
    });
    $('setting-camera').addEventListener('change', () => {
      deviceState.selectedCameraId = $('setting-camera').value;
      saveSetting('camera_device', deviceState.selectedCameraId);
      if (deviceState.cameraActive) { stopCameraTest(); toggleCameraTest(); }
    });
    $('setting-fps').addEventListener('change', () => saveSetting('fps', $('setting-fps').value));
    $('setting-resolution').addEventListener('change', () => saveSetting('resolution', $('setting-resolution').value));
    $('setting-output-volume').addEventListener('input', () => {
      saveSetting('output_volume', $('setting-output-volume').value);
      // 实时应用输出音量到远端语音
      if (window.NevoMedia && window.NevoMedia.setRemoteVolume) {
        window.NevoMedia.setRemoteVolume($('setting-output-volume').value);
      }
    });

    $('btn-test-mic').addEventListener('click', startMicTest);
    $('btn-test-speaker').addEventListener('click', playSpeakerTest);
    $('btn-test-camera').addEventListener('click', toggleCameraTest);

    // Restore saved device IDs
    const savedInput = getSetting('input_device', '');
    const savedOutput = getSetting('output_device', '');
    const savedCamera = getSetting('camera_device', '');
    if (savedInput) deviceState.selectedInputId = savedInput;
    if (savedOutput) deviceState.selectedOutputId = savedOutput;
    if (savedCamera) deviceState.selectedCameraId = savedCamera;

    // PTT key global listener
    document.addEventListener('keydown', (e) => {
      if (state.capturingPTTKey) { handlePTTKeyCapture(e); return; }
      // PTT activation
      if (state.inputMode === 'ptt' && state.pttKey && e.code === state.pttKey && !e.repeat) {
        e.preventDefault();
        state.pttActive = true;
        if (window.NevoMedia) window.NevoMedia.setPTT(true);
      }
      // Ctrl+M = toggle mute
      if (e.ctrlKey && e.key === 'm' && state.connected) { e.preventDefault(); toggleMute(); }
      // Ctrl+D = toggle deafen
      if (e.ctrlKey && e.key === 'd' && state.connected) { e.preventDefault(); toggleDeafen(); }
    });
    document.addEventListener('keyup', (e) => {
      if (state.inputMode === 'ptt' && state.pttKey && e.code === state.pttKey) {
        state.pttActive = false;
        if (window.NevoMedia) window.NevoMedia.setPTT(false);
      }
    });

    // System theme change listener (for auto mode)
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
      if (getSetting('theme', 'dark') === 'auto') applyTheme('auto');
    });
  }

  // ============================================================
  // Electron frameless title bar
  // ============================================================
  function initElectronTitleBar() {
    if (typeof window.electronAPI === 'undefined') return;
    document.documentElement.classList.add('electron-frame');
    const minimizeBtn = $('tb-minimize');
    const maximizeBtn = $('tb-maximize');
    const closeBtn = $('tb-close');
    if (!minimizeBtn || !maximizeBtn || !closeBtn) return;
    minimizeBtn.addEventListener('click', () => window.electronAPI.minimizeWindow());
    maximizeBtn.addEventListener('click', () => window.electronAPI.maximizeWindow());
    closeBtn.addEventListener('click', () => window.electronAPI.closeWindow());
    const updateMaximizeIcon = (isMaximized) => {
      maximizeBtn.innerHTML = isMaximized
        ? '<svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.2"><path d="M2 4V2h2M8 6v2H6M2 2l6 6"/></svg>'
        : '<svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.2"><rect x="1" y="1" width="8" height="8" rx="1"/></svg>';
      maximizeBtn.setAttribute('aria-label', isMaximized ? '还原' : '最大化');
    };
    window.electronAPI.onMaximizedChange((isMaximized) => updateMaximizeIcon(isMaximized));
  }

  // ============================================================
  // Init
  // ============================================================
  function init() {
    try {
      // Load saved servers
      loadSavedServers();
      renderServerQuickAccess();
      // Apply theme
      applyTheme(getSetting('theme', 'dark'));
      // Apply i18n
      if (window.NevoI18n) window.NevoI18n.applyTranslations();
      // Init emoji panel
      initEmojiPanel();
      // Init event listeners + settings
      initElectronTitleBar();
      initEventListeners();
      applyStoredSettingsToUI();
      updateInputModeUI();
      // Restore PTT key
      const savedPTTKey = getSetting('ptt_key', null);
      if (savedPTTKey) state.pttKey = savedPTTKey;
      enumerateMediaDevices();
      connectWebSocket();
      console.log('[NEVO] Web client initialized');
    } catch (err) {
      console.error('[NEVO] Init error:', err);
      document.body.insertAdjacentHTML('afterbegin',
        `<div style="position:fixed;top:0;left:0;right:0;z-index:99999;padding:12px 20px;background:#c0392b;color:#fff;font-family:monospace;font-size:13px;">Init Error: ${err.message}</div>`);
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
