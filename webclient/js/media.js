/**
 * NEVO Media Engine — WebCodecs 编解码 + WebSocket 媒体桥接
 * 负责语音/视频的采集、编码、传输、解码、播放
 * 依赖：浏览器 WebCodecs API、AudioContext、canvas
 */
(function (global) {
  'use strict';

  const MediaState = {
    // 语音
    voiceActive: false,
    voiceStream: null,
    audioContext: null,
    audioSource: null,
    audioAnalyser: null,
    audioEncoder: null,
    audioDecoder: null,
    micRafId: null,
    // 远端语音播放
    remoteAudioContext: null,
    remoteMasterGain: null,
    remoteAudioQueue: [],        // 抖动缓冲：待播放的 AudioBuffer
    remoteAudioPlaying: false,
    _nextPlayTime: 0,            // 时钟对齐排程：下一帧的 AudioContext 起始时刻
    // 波形渲染 DOM 缓存（避免每帧 rAF 全量 querySelector）
    _wfCache: { self: null, bars: new Map() },
    // 视频渲染缓存
    _lastCanvasW: 0,
    _lastCanvasH: 0,
    // 视频
    videoActive: false,
    videoStream: null,
    videoEncoder: null,
    videoDecoder: null,
    _videoStreamOwned: true,
    // 屏幕共享
    screenActive: false,
    screenStream: null,
    screenEncoder: null,
    // 解码视频渲染
    remoteVideoCanvas: null,
    remoteVideoCtx: null,
    // 视频大厅：user_id -> { canvas, ctx, decoder, w, h }（多人网格解码目标）
    lobbyTargets: new Map(),
    // PTT
    pttActive: false,
    inputMode: 'continuous', // continuous | ptt | vad
    // 波形数据
    waveformLevels: new Map(), // user_id -> level
    // 本地说话状态上报（VAD）
    speakingSent: false,
    lastSpeakTime: 0,
  };

  const CODEC_CONFIG = {
    audio: {
      codec: 'opus',
      sampleRate: 48000,
      numberOfChannels: 1,
      bitrate: 48000, // 48kbps：语音清晰度与带宽的平衡点（32k 下齿音/电音感明显）
    },
    video: {
      codec: 'avc1.4D0028', // H.264 Main Profile Level 4.0（1080p@30 内可用）
      width: 640,
      height: 480,
      fps: 30,
      bitrate: 1000000,
    },
  };

  // 根据分辨率/帧率动态选择 H.264 codec 配置：
  // - ≤720p@30 用 Baseline 3.1（低延迟、软硬解兼容性最好）
  // - 1080p@30 内用 Main Profile 4.0（修复旧配置 Baseline 3.1 无法编码 1080p 的问题）
  // - 更高规格回退 Main Profile 5.0
  function pickVideoCodec(width, height, fps) {
    const w = width || 640, h = height || 480, f = fps || 30;
    if (w * h <= 1280 * 720 && f <= 30) return 'avc1.42E01F';
    if (w * h <= 1920 * 1080 && f <= 30) return 'avc1.4D0028';
    return 'avc1.4D0033';
  }

  // 本地 VAD 说话检测参数
  // 将 VAD 上报阈值与波形显示阈值对齐，防止"远程已显示说话但本地波形未动"的不同步现象
  const SPEAK_THRESHOLD = 0.08;      // VAD 上报阈值（与 WAVEFORM_THRESHOLD 一致）
  const WAVEFORM_THRESHOLD = 0.08;   // 本地波形显示阈值（高于环境噪音）
  const SPEAK_RELEASE_MS = 600;      // 停止说话判定延迟（毫秒）
  // VAD 输入模式下的 RMS 阈值：RMS 与频谱归一化量纲不同，0.02（约 -34dB）
  // 对应连续模式 0.08 的灵敏度档位，避免环境噪音被误判为说话
  const VAD_RMS_THRESHOLD = 0.02;

  // ---- WS 媒体帧发送 ----

  function sendMediaFrame(type, data, extra = {}) {
    if (!global.NevoApp || !global.NevoApp.state || !global.NevoApp.state.ws) return;
    if (global.NevoApp.state.ws.readyState !== WebSocket.OPEN) return;
    // 将 ArrayBuffer 转为 base64
    const b64 = arrayBufferToBase64(data);
    global.NevoApp.state.ws.send(JSON.stringify({
      action: 'media_frame',
      params: { type, data: b64, ...extra },
      id: 0, // 媒体帧不需要响应
    }));
  }

  // 上报本地说话状态给网关（由网关转发服务器广播给其他客户端）
  function sendSpeakingState(speaking) {
    if (!global.NevoApp || !global.NevoApp.state || !global.NevoApp.state.ws) return;
    if (global.NevoApp.state.ws.readyState !== WebSocket.OPEN) return;
    global.NevoApp.state.ws.send(JSON.stringify({
      action: 'speaking_state',
      params: { speaking },
      id: 0, // 不需要响应
    }));
  }

  function arrayBufferToBase64(buf) {
    const bytes = new Uint8Array(buf);
    let binary = '';
    const chunk = 32768; // 增大分块，减少字符串拼接次数与 GC 压力
    for (let i = 0; i < bytes.length; i += chunk) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    }
    return btoa(binary);
  }

  function base64ToArrayBuffer(b64) {
    const binary = atob(b64);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes.buffer;
  }

  // ============================================================
  // 语音引擎
  // ============================================================

  async function startVoice(inputDeviceId) {
    if (MediaState.voiceActive) return;
    try {
      const constraints = {
        audio: {
          deviceId: inputDeviceId ? { exact: inputDeviceId } : undefined,
          echoCancellation: true,
          autoGainControl: true,
          noiseSuppression: true,
          sampleRate: CODEC_CONFIG.audio.sampleRate,
          channelCount: 1,
        },
      };
      MediaState.voiceStream = await navigator.mediaDevices.getUserMedia(constraints);
      MediaState.audioContext = new (window.AudioContext || window.webkitAudioContext)({
        sampleRate: CODEC_CONFIG.audio.sampleRate,
      });
      MediaState.audioSource = MediaState.audioContext.createMediaStreamSource(MediaState.voiceStream);
      MediaState.audioAnalyser = MediaState.audioContext.createAnalyser();
      MediaState.audioAnalyser.fftSize = 256;
      MediaState.audioAnalyser.smoothingTimeConstant = 0.6;
      MediaState.audioSource.connect(MediaState.audioAnalyser);

      // WebCodecs 音频编码器
      if (typeof AudioEncoder !== 'undefined') {
        MediaState.audioEncoder = new AudioEncoder({
          output: (chunk, metadata) => {
            // 发送编码后的 Opus 帧到网关
            const buf = new ArrayBuffer(chunk.byteLength);
            chunk.copyTo(buf);
            sendMediaFrame('voice', buf);
          },
          error: (e) => console.error('[MEDIA] AudioEncoder error:', e),
        });
        MediaState.audioEncoder.configure({
          codec: CODEC_CONFIG.audio.codec,
          sampleRate: CODEC_CONFIG.audio.sampleRate,
          numberOfChannels: CODEC_CONFIG.audio.numberOfChannels,
          bitrate: CODEC_CONFIG.audio.bitrate,
        });

        // 使用 AudioWorklet 提取 PCM 数据
        await startAudioWorklet();
      }

      // 初始化远端音频解码器
      if (typeof AudioDecoder !== 'undefined') {
        MediaState.audioDecoder = new AudioDecoder({
          output: (frame) => playRemoteAudio(frame),
          error: (e) => console.error('[MEDIA] AudioDecoder error:', e),
        });
        MediaState.audioDecoder.configure({
          codec: CODEC_CONFIG.audio.codec,
          sampleRate: CODEC_CONFIG.audio.sampleRate,
          numberOfChannels: CODEC_CONFIG.audio.numberOfChannels,
        });
      }

      // 远端播放 AudioContext + 主音量节点（应用"输出音量"设置）
      MediaState.remoteAudioContext = new (window.AudioContext || window.webkitAudioContext)({
        sampleRate: CODEC_CONFIG.audio.sampleRate,
      });
      MediaState.remoteMasterGain = MediaState.remoteAudioContext.createGain();
      MediaState.remoteMasterGain.gain.value = 1.0;
      MediaState.remoteMasterGain.connect(MediaState.remoteAudioContext.destination);
      const outVol = global.NevoApp && global.NevoApp.getSetting
        ? Number(global.NevoApp.getSetting('output_volume', 100)) || 100 : 100;
      MediaState.remoteMasterGain.gain.value = Math.min(1, Math.max(0, outVol / 100));

      MediaState.voiceActive = true;

      // 启动波形可视化
      startWaveformMonitor();
      // 波形容器加上 active 类，CSS 动画作为后备
      const selfWf = document.getElementById('my-waveform');
      if (selfWf) selfWf.classList.add('active');

      console.log('[MEDIA] Voice engine started');
    } catch (e) {
      console.error('[MEDIA] Failed to start voice:', e);
      // 清理已创建的 AudioContext/编码器/解码器等部分资源，避免泄漏
      stopVoice();
      throw e;
    }
  }

  async function startAudioWorklet() {
    // 首选 AudioWorklet（128 帧 ≈ 2.7ms 缓冲，实时线程低延迟），
    // 失败时回退 ScriptProcessorNode（2048 帧 ≈ 43ms，兼容老旧环境）。
    // 旧实现的 ScriptProcessor 4096 帧缓冲（≈85ms）会显著拖慢语音往返延迟。
    if (typeof AudioWorkletNode !== 'undefined' && MediaState.audioContext.audioWorklet) {
      try {
        const workletSrc = `
          class NevoPCMProcessor extends AudioWorkletProcessor {
            constructor() {
              super();
              this._pending = new Float32Array(0);
            }
            process(inputs) {
              const input = inputs[0];
              if (input && input.length && input[0].length) {
                const ch = input[0];
                // 累积到 960 样本（20ms @ 48kHz）再投递：
                // 1) 保证 Opus 编码器输出标准 20ms 帧（各 Chromium 版本行为一致，
                //    避免部分版本按输入块长度输出非 20ms 帧导致对端播放断档）；
                // 2) 主线程消息频率从 ~375 次/s 降到 50 次/s，显著降低
                //    编码/发送路径的抖动（语音卡顿的重要来源）。
                const merged = new Float32Array(this._pending.length + ch.length);
                merged.set(this._pending);
                merged.set(ch, this._pending.length);
                this._pending = merged;
                if (this._pending.length >= 960) {
                  const out = this._pending.slice(0, 960);
                  this._pending = this._pending.slice(960);
                  this.port.postMessage(out, [out.buffer]);
                }
              }
              return true;
            }
          }
          registerProcessor('nevo-pcm-processor', NevoPCMProcessor);
        `;
        const blobUrl = URL.createObjectURL(new Blob([workletSrc], { type: 'application/javascript' }));
        await MediaState.audioContext.audioWorklet.addModule(blobUrl);
        URL.revokeObjectURL(blobUrl);
        const workletNode = new AudioWorkletNode(MediaState.audioContext, 'nevo-pcm-processor');
        workletNode.port.onmessage = (ev) => {
          if (!MediaState.voiceActive || !MediaState.audioEncoder || MediaState.audioEncoder.state !== 'configured') return;
          if (MediaState.inputMode === 'ptt' && !MediaState.pttActive) return;
          if (MediaState.inputMode === 'vad' && !checkVADData(ev.data)) return;
          const frame = new AudioData({
            format: 'f32-planar',
            sampleRate: CODEC_CONFIG.audio.sampleRate,
            numberOfFrames: ev.data.length,
            numberOfChannels: 1,
            timestamp: MediaState.audioContext.currentTime * 1e6,
            data: ev.data,
          });
          try { MediaState.audioEncoder.encode(frame); } catch (err) { /* 编码器可能处于错误状态 */ }
          frame.close();
        };
        MediaState.audioSource.connect(workletNode);
        workletNode.connect(MediaState.audioContext.destination);
        MediaState._audioWorkletNode = workletNode;
        return;
      } catch (e) {
        console.warn('[MEDIA] AudioWorklet unavailable, falling back to ScriptProcessor:', e);
      }
    }

    // 后备：ScriptProcessorNode
    const processor = MediaState.audioContext.createScriptProcessor(2048, 1, 1);
    processor.onaudioprocess = (e) => {
      if (!MediaState.voiceActive || !MediaState.audioEncoder || MediaState.audioEncoder.state !== 'configured') return;
      if (MediaState.inputMode === 'ptt' && !MediaState.pttActive) return;
      if (MediaState.inputMode === 'vad' && !checkVAD(e.inputBuffer)) return;

      const inputData = e.inputBuffer.getChannelData(0);
      const frame = new AudioData({
        format: 'f32-planar',
        sampleRate: CODEC_CONFIG.audio.sampleRate,
        numberOfFrames: inputData.length,
        numberOfChannels: 1,
        timestamp: MediaState.audioContext.currentTime * 1e6,
        data: inputData,
      });
      try {
        MediaState.audioEncoder.encode(frame);
      } catch (err) {
        // 编码器可能处于错误状态
      }
      frame.close();
    };
    MediaState.audioSource.connect(processor);
    processor.connect(MediaState.audioContext.destination);
    MediaState._audioProcessor = processor;
  }

  // VAD：基于 RMS 阈值（Float32Array 版本，供 AudioWorklet 使用）
  function checkVADData(data) {
    let sum = 0;
    for (let i = 0; i < data.length; i++) sum += data[i] * data[i];
    const rms = Math.sqrt(sum / data.length);
    return rms > VAD_RMS_THRESHOLD;
  }

  // VAD：基于 RMS 阈值（AudioBuffer 版本，供 ScriptProcessor 使用）
  function checkVAD(inputBuffer) {
    return checkVADData(inputBuffer.getChannelData(0));
  }

  function stopVoice() {
    MediaState.voiceActive = false;
    // 重置本地说话状态，避免其他客户端残留“说话中”指示
    if (MediaState.speakingSent) {
      MediaState.speakingSent = false;
      sendSpeakingState(false);
    }
    if (MediaState.micRafId) cancelAnimationFrame(MediaState.micRafId);
    MediaState.micRafId = null;
    if (MediaState._audioWorkletNode) {
      try {
        MediaState._audioWorkletNode.disconnect();
        MediaState._audioWorkletNode.port.close();
      } catch (_) {}
      MediaState._audioWorkletNode = null;
    }
    if (MediaState._audioProcessor) {
      try { MediaState._audioProcessor.disconnect(); } catch (_) {}
      MediaState._audioProcessor = null;
    }
    if (MediaState.audioEncoder) {
      try { MediaState.audioEncoder.flush(); MediaState.audioEncoder.close(); } catch (_) {}
      MediaState.audioEncoder = null;
    }
    if (MediaState.audioDecoder) {
      try { MediaState.audioDecoder.close(); } catch (_) {}
      MediaState.audioDecoder = null;
    }
    if (MediaState.audioSource) {
      try { MediaState.audioSource.disconnect(); } catch (_) {}
      MediaState.audioSource = null;
    }
    if (MediaState.audioAnalyser) {
      try { MediaState.audioAnalyser.disconnect(); } catch (_) {}
      MediaState.audioAnalyser = null;
    }
    if (MediaState.audioContext) {
      try { MediaState.audioContext.close(); } catch (_) {}
      MediaState.audioContext = null;
    }
    const selfWf = document.getElementById('my-waveform');
    if (selfWf) selfWf.classList.remove('active');
    // 清理远端播放：停止排程、清空抖动缓冲、断开主音量节点
    if (MediaState._remoteDrainTimer) { clearTimeout(MediaState._remoteDrainTimer); MediaState._remoteDrainTimer = null; }
    MediaState.remoteAudioQueue = [];
    MediaState.remoteAudioPlaying = false;
    MediaState._nextPlayTime = 0;
    if (MediaState.remoteMasterGain) {
      try { MediaState.remoteMasterGain.disconnect(); } catch (_) {}
      MediaState.remoteMasterGain = null;
    }
    if (MediaState.remoteAudioContext) {
      try { MediaState.remoteAudioContext.close(); } catch (_) {}
      MediaState.remoteAudioContext = null;
    }
    if (MediaState.voiceStream) {
      MediaState.voiceStream.getTracks().forEach(t => t.stop());
      MediaState.voiceStream = null;
    }
    console.log('[MEDIA] Voice engine stopped');
  }

  // ---- 远端语音播放 ----

  // 语音帧固定 20ms（48kHz / 960 样本）。使用"排程循环 + 抖动缓冲"平滑播放，
  // 替代"每帧立即 createBufferSource().start()"的旧方案：
  //  - 避免网络突发时多个 BufferSource 重叠导致爆音
  //  - 避免帧到达间隙 >8ms 即静音（旧 MIN_REMOTE_FRAME_GAP_MS 会丢帧）
  //  - 大幅减少 AudioBufferSourceNode / AudioBuffer 的创建频率（GC 压力）
  const REMOTE_FRAME_MS = 20;
  const REMOTE_QUEUE_MAX = 12; // 240ms，超出说明网络延迟累积，丢弃最旧帧防止延迟堆积
  const REMOTE_PREFILL = 2;    // 预填 2 帧再开始播放，吸收起始抖动避免一帧一断

  function playRemoteAudio(frame) {
    if (!MediaState.remoteAudioContext) { frame.close(); return; }
    try {
      const buf = MediaState.remoteAudioContext.createBuffer(
        frame.numberOfChannels,
        frame.numberOfFrames,
        frame.sampleRate
      );
      for (let ch = 0; ch < frame.numberOfChannels; ch++) {
        const chData = new Float32Array(frame.numberOfFrames);
        frame.copyTo(chData, { planeIndex: ch });
        buf.copyToChannel(chData, ch);
      }
      frame.close();
      if (MediaState.remoteAudioQueue.length >= REMOTE_QUEUE_MAX) {
        MediaState.remoteAudioQueue.shift();
      }
      MediaState.remoteAudioQueue.push(buf);
      if (!MediaState.remoteAudioPlaying &&
          MediaState.remoteAudioQueue.length >= REMOTE_PREFILL) {
        MediaState.remoteAudioPlaying = true;
        drainRemoteAudio();
      }
    } catch (e) {
      frame.close();
    }
  }

  function drainRemoteAudio() {
    const buf = MediaState.remoteAudioQueue.shift();
    if (!buf || !MediaState.remoteAudioContext) {
      MediaState.remoteAudioPlaying = false;
      return;
    }
    const ctx = MediaState.remoteAudioContext;
    try {
      const src = ctx.createBufferSource();
      src.buffer = buf;
      src.connect(MediaState.remoteMasterGain || ctx.destination);
      // 对齐 AudioContext 时钟排程（start(when) 精确到采样点）：
      // 每帧在上帧结束的精确时刻开始，彻底消除 setTimeout 漂移
      // 造成的周期性断音/卡顿。
      const now = ctx.currentTime;
      if (MediaState._nextPlayTime < now + 0.003) {
        // 落后（欠载重启动/定时器迟到）：钳制到 3ms 后，避免 start 时刻在过去
        MediaState._nextPlayTime = now + 0.003;
      }
      src.start(MediaState._nextPlayTime);
      MediaState._nextPlayTime += buf.duration;
      // 提前 5ms 唤醒：容忍定时器延迟，保证每帧在精确时刻开始
      const delayMs = Math.max(2, (MediaState._nextPlayTime - ctx.currentTime) * 1000 - 5);
      MediaState._remoteDrainTimer = setTimeout(drainRemoteAudio, delayMs);
    } catch (e) {
      // 播放失败时按帧长重试，避免死循环
      MediaState._remoteDrainTimer = setTimeout(
        drainRemoteAudio, Math.max(10, (buf.duration || 0.02) * 1000));
    }
  }

  // 实时调整远端语音输出音量（0-100）
  function setRemoteVolume(v) {
    const g = MediaState.remoteMasterGain;
    if (g) g.gain.value = Math.min(1, Math.max(0, Number(v) / 100));
  }

  // 成员列表重建后清空波形 DOM 缓存，避免悬挂引用
  function resetWaveformCache() {
    MediaState._wfCache = { self: null, bars: new Map() };
  }

  // ---- 接收远端语音帧 ----

  function handleVoiceFrame(data) {
    if (!MediaState.audioDecoder || MediaState.audioDecoder.state !== 'configured') return;
    try {
      const buf = base64ToArrayBuffer(data.data);
      // 空载荷（网关注册/保活包被中继）不能喂给 AudioDecoder：
      // Chrome/Electron 会抛 "EncodingError: Null or empty decoder buffer"，
      // 解码器进入 closed 状态后所有语音帧被永久丢弃（"一段时间后无声音"的根因）。
      if (!buf.byteLength) return;
      const chunk = new EncodedAudioChunk({
        type: 'key',
        timestamp: Date.now() * 1000,
        data: buf,
      });
      MediaState.audioDecoder.decode(chunk);
    } catch (e) {
      // 解码错误，忽略
    }
  }

  // ---- 波形可视化 ----

  function startWaveformMonitor() {
    // 复用频谱缓冲，避免每帧 rAF 重复分配
    let dataBuf = null;
    const tick = () => {
      if (!MediaState.voiceActive || !MediaState.audioAnalyser) return;
      if (!dataBuf) dataBuf = new Uint8Array(MediaState.audioAnalyser.frequencyBinCount);
      MediaState.audioAnalyser.getByteFrequencyData(dataBuf);
      let sum = 0;
      for (let i = 0; i < dataBuf.length; i++) sum += dataBuf[i];
      const level = sum / dataBuf.length / 256;
      // 更新本地波形（使用 app.js 暴露的 state）
      const appState = global.NevoApp && global.NevoApp.state;
      const userId = appState && appState.userId ? String(appState.userId) : 'self';
      // 低于波形阈值时压到基线，避免环境噪音让波形一直假动
      updateWaveformUI(userId, level > WAVEFORM_THRESHOLD ? dataBuf : 0);
      // VAD：检测本地说话并上报网关（供其他客户端显示说话指示器）
      const nowMs = performance.now();
      if (level > SPEAK_THRESHOLD) {
        MediaState.lastSpeakTime = nowMs;
        if (!MediaState.speakingSent) {
          MediaState.speakingSent = true;
          sendSpeakingState(true);
        }
      } else if (MediaState.speakingSent && nowMs - MediaState.lastSpeakTime > SPEAK_RELEASE_MS) {
        MediaState.speakingSent = false;
        sendSpeakingState(false);
      }
      MediaState.micRafId = requestAnimationFrame(tick);
    };
    tick();
  }

  // 复用高度缓冲，避免每帧 rAF 反复分配数组
  const heightsBuf = [2, 2, 2, 2];

  // 将频谱/电平数据写入 out（复用），不再每次返回新数组
  function computeBarHeights(data, out) {
    const barCount = out.length;
    if (typeof data === 'number') {
      // 手动/测试传入电平时保留少量随机，避免死板
      for (let i = 0; i < barCount; i++) {
        const r = 0.85 + Math.random() * 0.3;
        out[i] = Math.max(2, Math.min(16, data * 100 * r));
      }
    } else if (data && data.length) {
      // 真实频谱数据：直接按频段平均值计算，不再加随机抖动，波形随语音真实变化
      const binSize = Math.floor(data.length / barCount) || 1;
      for (let i = 0; i < barCount; i++) {
        let sum = 0, count = 0;
        const start = i * binSize;
        const end = Math.min(start + binSize, data.length);
        for (let j = start; j < end; j++) { sum += data[j]; count++; }
        const avg = count ? sum / count : 0;
        out[i] = Math.max(2, Math.min(16, (avg / 256) * 100));
      }
    } else {
      for (let i = 0; i < barCount; i++) out[i] = 2;
    }
    return out;
  }

  // 容器上的 wf-bar 引用只查询一次（首次），之后复用
  function applyHeights(container, heights) {
    if (!container) return;
    let bars = container._wfBars;
    if (!bars) {
      bars = Array.prototype.slice.call(container.querySelectorAll('.wf-bar'));
      container._wfBars = bars;
    }
    for (let i = 0; i < bars.length && i < heights.length; i++) {
      const h = heights[i];
      if (bars[i]._lastH !== h) { bars[i]._lastH = h; bars[i].style.height = h + 'px'; }
    }
  }

  // 按 userId 缓存容器；成员列表重建后（DOM 断开）自动重新查询
  function getWfContainer(userId) {
    const cache = MediaState._wfCache.bars;
    let container = cache.get(userId);
    if (container === undefined || (container && !container.isConnected)) {
      container = document.querySelector('.voice-user[data-user-id="' + userId + '"] .waveform-bar') || null;
      cache.set(userId, container);
    }
    return container;
  }

  function getSelfWf() {
    const selfWf = MediaState._wfCache.self;
    if (selfWf && selfWf.isConnected) return selfWf;
    MediaState._wfCache.self = document.getElementById('my-waveform');
    return MediaState._wfCache.self;
  }

  function updateWaveformUI(userId, data) {
    const heights = computeBarHeights(data, heightsBuf);
    applyHeights(getWfContainer(userId), heights);
    applyHeights(getSelfWf(), heights);
  }

  // ============================================================
  // 视频引擎
  // ============================================================

  async function startVideo(deviceId, width, height, fps, reuseStream) {
    if (MediaState.videoActive) return;
    try {
      if (reuseStream) {
        // 复用调用方（如 PiP 预览）已打开的本地流，避免重复请求摄像头
        MediaState.videoStream = reuseStream;
        MediaState._videoStreamOwned = false;
      } else {
        const constraints = {
          video: {
            deviceId: deviceId ? { exact: deviceId } : undefined,
            width: { ideal: width || 640 },
            height: { ideal: height || 480 },
            frameRate: { ideal: fps || 30 },
          },
          audio: false,
        };
        MediaState.videoStream = await navigator.mediaDevices.getUserMedia(constraints);
        MediaState._videoStreamOwned = true;
      }
      const vw = width || 640, vh = height || 480, vf = fps || 30;
      const codec = pickVideoCodec(vw, vh, vf);

      // WebCodecs 视频编码器
      if (typeof VideoEncoder !== 'undefined') {
        MediaState.videoEncoder = new VideoEncoder({
          output: (chunk, metadata) => {
            const buf = new ArrayBuffer(chunk.byteLength);
            chunk.copyTo(buf);
            sendMediaFrame('video', buf, {
              width: vw,
              height: vh,
              fps: vf,
              keyframe: chunk.type === 'key',
            });
          },
          error: (e) => console.error('[MEDIA] VideoEncoder error:', e),
        });
        MediaState.videoEncoder.configure({
          codec,
          width: vw,
          height: vh,
          bitrate: CODEC_CONFIG.video.bitrate,
          framerate: vf,
        });

        // 使用 VideoTrackReader 提取帧（或用 requestVideoFrameCallback）
        startVideoFrameLoop();
      }

      // 初始化远端视频解码器
      if (typeof VideoDecoder !== 'undefined') {
        MediaState.videoDecoder = new VideoDecoder({
          output: (frame) => renderRemoteVideoFrame(frame),
          error: (e) => console.error('[MEDIA] VideoDecoder error:', e),
        });
        MediaState.videoDecoder.configure({
          codec,
          width: vw,
          height: vh,
        });
      }

      MediaState.videoActive = true;
      console.log('[MEDIA] Video engine started', { codec, width: vw, height: vh, fps: vf });
    } catch (e) {
      console.error('[MEDIA] Failed to start video:', e);
      throw e;
    }
  }

  function startVideoFrameLoop() {
    const videoTrack = MediaState.videoStream.getVideoTracks()[0];
    if (!videoTrack) return;

    // 使用 MediaStreamTrackProcessor（如果可用）
    if (typeof MediaStreamTrackProcessor !== 'undefined') {
      const processor = new MediaStreamTrackProcessor({ track: videoTrack });
      const reader = processor.readable.getReader();
      const readLoop = async () => {
        while (MediaState.videoActive) {
          const { done, value: frame } = await reader.read();
          if (done) break;
          if (MediaState.videoEncoder && MediaState.videoEncoder.state === 'configured') {
            try {
              // 关键帧间隔控制
              const keyFrame = (MediaState._videoFrameCount || 0) % 30 === 0;
              MediaState.videoEncoder.encode(frame, { keyFrame });
              MediaState._videoFrameCount = (MediaState._videoFrameCount || 0) + 1;
            } catch (e) {
              // 忽略编码错误
            }
          }
          frame.close();
        }
      };
      readLoop();
    } else {
      // 后备：使用 canvas + requestAnimationFrame
      const video = document.createElement('video');
      video.srcObject = MediaState.videoStream;
      video.play();
      const canvas = document.createElement('canvas');
      canvas.width = 640;
      canvas.height = 480;
      const ctx = canvas.getContext('2d', { alpha: false });
      let frameCount = 0;
      const captureLoop = () => {
        if (!MediaState.videoActive) return;
        ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
        if (MediaState.videoEncoder && MediaState.videoEncoder.state === 'configured') {
          const frame = new VideoFrame(canvas, {
            timestamp: performance.now() * 1000,
          });
          try {
            const keyFrame = frameCount % 30 === 0;
            MediaState.videoEncoder.encode(frame, { keyFrame });
            frameCount++;
          } catch (e) {}
          frame.close();
        }
        requestAnimationFrame(captureLoop);
      };
      captureLoop();
    }
  }

  function stopVideo() {
    MediaState.videoActive = false;
    MediaState._videoFrameCount = 0;
    if (MediaState.videoEncoder) {
      try { MediaState.videoEncoder.flush(); MediaState.videoEncoder.close(); } catch (_) {}
      MediaState.videoEncoder = null;
    }
    if (MediaState.videoDecoder) {
      try { MediaState.videoDecoder.close(); } catch (_) {}
      MediaState.videoDecoder = null;
    }
    // 仅停止本次引擎自己打开的流；外部传入的复用流（如 PiP 预览）由调用方管理
    if (MediaState.videoStream) {
      if (MediaState._videoStreamOwned) MediaState.videoStream.getTracks().forEach(t => t.stop());
      MediaState.videoStream = null;
      MediaState._videoStreamOwned = true;
    }
    console.log('[MEDIA] Video engine stopped');
  }

  // ---- 远端视频渲染 ----

  function setRemoteVideoCanvas(canvas) {
    MediaState.remoteVideoCanvas = canvas;
    MediaState.remoteVideoCtx = canvas.getContext('2d', { alpha: false });
  }

  function renderRemoteVideoFrame(frame) {
    if (!MediaState.remoteVideoCanvas || !MediaState.remoteVideoCtx) {
      frame.close();
      return;
    }
    // 仅当分辨率变化时更新 canvas 尺寸；每帧重设会清空画布并触发重新分配，导致闪烁与开销
    if (MediaState._lastCanvasW !== frame.codedWidth || MediaState._lastCanvasH !== frame.codedHeight) {
      MediaState.remoteVideoCanvas.width = frame.codedWidth;
      MediaState.remoteVideoCanvas.height = frame.codedHeight;
      MediaState._lastCanvasW = frame.codedWidth;
      MediaState._lastCanvasH = frame.codedHeight;
    }
    MediaState.remoteVideoCtx.drawImage(frame, 0, 0);
    frame.close();
  }

  // ---- 视频大厅（多人）解码目标管理 ----

  // 大厅帧超时检测：某成员停止发送视频（关闭摄像头/掉线）后恢复头像占位
  let lobbyStaleTimer = null;

  function startLobbyStaleCheck() {
    if (lobbyStaleTimer) return;
    lobbyStaleTimer = setInterval(() => {
      const now = Date.now();
      for (const [, target] of MediaState.lobbyTargets) {
        if (target.canvas && target.canvas.style.display !== 'none' && now - (target.lastFrameTime || 0) > 2000) {
          target.canvas.style.display = 'none';
          const ph = target.canvas.parentElement && target.canvas.parentElement.querySelector('.lobby-tile-placeholder');
          if (ph) ph.style.display = 'flex';
        }
      }
    }, 1000);
  }

  function stopLobbyStaleCheck() {
    if (lobbyStaleTimer) { clearInterval(lobbyStaleTimer); lobbyStaleTimer = null; }
  }

  // 为某个频道成员注册解码目标 canvas；grid 重建时先 clear 再逐个注册
  function setLobbyVideoTarget(userId, canvas) {
    const ctx = canvas.getContext('2d', { alpha: false });
    let target = MediaState.lobbyTargets.get(userId);
    if (target) {
      target.canvas = canvas;
      target.ctx = ctx;
    } else {
      target = { userId, canvas, ctx, decoder: null, w: 0, h: 0, lastFrameTime: 0 };
      MediaState.lobbyTargets.set(userId, target);
    }
    startLobbyStaleCheck();
  }

  function removeLobbyVideoTarget(userId) {
    const target = MediaState.lobbyTargets.get(userId);
    if (!target) return;
    if (target.decoder) { try { target.decoder.close(); } catch (_) {} }
    MediaState.lobbyTargets.delete(userId);
    if (MediaState.lobbyTargets.size === 0) stopLobbyStaleCheck();
  }

  function clearLobbyVideoTargets() {
    for (const [, target] of MediaState.lobbyTargets) {
      if (target.decoder) { try { target.decoder.close(); } catch (_) {} }
    }
    MediaState.lobbyTargets.clear();
    stopLobbyStaleCheck();
  }

  // 解码输出 → 画到指定成员的 canvas
  function renderLobbyFrame(frame, target) {
    if (!target || !target.canvas || !target.ctx) {
      frame.close();
      return;
    }
    // 首次收到画面：显示 canvas、隐藏头像占位
    if (target.canvas.style.display === 'none') {
      target.canvas.style.display = 'block';
      const ph = target.canvas.parentElement && target.canvas.parentElement.querySelector('.lobby-tile-placeholder');
      if (ph) ph.style.display = 'none';
    }
    if (target.w !== frame.codedWidth || target.h !== frame.codedHeight) {
      target.canvas.width = frame.codedWidth;
      target.canvas.height = frame.codedHeight;
      target.w = frame.codedWidth;
      target.h = frame.codedHeight;
    }
    target.ctx.drawImage(frame, 0, 0);
    frame.close();
  }

  // ---- 接收远端视频帧 ----

  function handleVideoFrame(data) {
    if (!data || !data.data) return;
    const buf = base64ToArrayBuffer(data.data);
    // 与语音一致：空载荷直接跳过，避免解码器进入错误/关闭状态
    if (!buf.byteLength) return;

    // 视频大厅模式：按 sender_id 路由到各成员的解码器
    if (MediaState.lobbyTargets.size > 0) {
      const target = MediaState.lobbyTargets.get(Number(data.sender_id) || 0);
      if (!target) return; // 非大厅目标成员（如不在大厅的其他频道成员）的帧直接丢弃
      try {
        // 惰性创建解码器：首次收到帧时按发送方分辨率/帧率推断 codec（与发送端同一规则）
        if (!target.decoder) {
          const codec = pickVideoCodec(data.width || 640, data.height || 480, data.fps || 30);
          target.decoder = new VideoDecoder({
            output: (frame) => renderLobbyFrame(frame, target),
            error: (e) => console.error('[MEDIA] Lobby VideoDecoder error:', e),
          });
          target.decoder.configure({
            codec,
            width: data.width || 640,
            height: data.height || 480,
          });
        }
        if (target.decoder.state !== 'configured') return;
        const chunk = new EncodedVideoChunk({
          type: data.keyframe ? 'key' : 'delta',
          timestamp: Date.now() * 1000,
          data: buf,
        });
        target.decoder.decode(chunk);
      } catch (e) {
        // 解码错误，忽略
      }
      return;
    }

    // 原有 1v1 通话路径
    if (!MediaState.videoDecoder || MediaState.videoDecoder.state !== 'configured') return;
    try {
      const chunk = new EncodedVideoChunk({
        type: data.keyframe ? 'key' : 'delta',
        timestamp: Date.now() * 1000,
        data: buf,
      });
      MediaState.videoDecoder.decode(chunk);
    } catch (e) {
      // 解码错误，忽略
    }
  }

  // ============================================================
  // 屏幕共享
  // ============================================================

  async function startScreenShare() {
    if (MediaState.screenActive) return;
    try {
      MediaState.screenStream = await navigator.mediaDevices.getDisplayMedia({
        video: { width: { ideal: 1920 }, height: { ideal: 1080 }, frameRate: { ideal: 15 } },
        audio: false,
      });

      if (typeof VideoEncoder !== 'undefined') {
        const sw = 1920, sh = 1080, sf = 15;
        MediaState.screenEncoder = new VideoEncoder({
          output: (chunk, metadata) => {
            const buf = new ArrayBuffer(chunk.byteLength);
            chunk.copyTo(buf);
            sendMediaFrame('video', buf, {
              width: sw,
              height: sh,
              fps: sf,
              keyframe: chunk.type === 'key',
            });
          },
          error: (e) => console.error('[MEDIA] Screen encoder error:', e),
        });
        MediaState.screenEncoder.configure({
          codec: pickVideoCodec(sw, sh, sf),
          width: sw,
          height: sh,
          bitrate: 2000000,
          framerate: sf,
        });

        const videoTrack = MediaState.screenStream.getVideoTracks()[0];
        if (typeof MediaStreamTrackProcessor !== 'undefined') {
          const processor = new MediaStreamTrackProcessor({ track: videoTrack });
          const reader = processor.readable.getReader();
          const readLoop = async () => {
            while (MediaState.screenActive) {
              const { done, value: frame } = await reader.read();
              if (done) break;
              if (MediaState.screenEncoder && MediaState.screenEncoder.state === 'configured') {
                try {
                  const keyFrame = (MediaState._screenFrameCount || 0) % 30 === 0;
                  MediaState.screenEncoder.encode(frame, { keyFrame });
                  MediaState._screenFrameCount = (MediaState._screenFrameCount || 0) + 1;
                } catch (e) {}
              }
              frame.close();
            }
          };
          readLoop();
        }
      }

      // 用户点击浏览器原生"停止共享"按钮时
      MediaState.screenStream.getVideoTracks()[0].addEventListener('ended', () => {
        stopScreenShare();
      });

      MediaState.screenActive = true;
      console.log('[MEDIA] Screen share started');
    } catch (e) {
      console.error('[MEDIA] Failed to start screen share:', e);
      throw e;
    }
  }

  function stopScreenShare() {
    MediaState.screenActive = false;
    MediaState._screenFrameCount = 0;
    if (MediaState.screenEncoder) {
      try { MediaState.screenEncoder.flush(); MediaState.screenEncoder.close(); } catch (_) {}
      MediaState.screenEncoder = null;
    }
    if (MediaState.screenStream) {
      MediaState.screenStream.getTracks().forEach(t => t.stop());
      MediaState.screenStream = null;
    }
    console.log('[MEDIA] Screen share stopped');
  }

  // ============================================================
  // PTT / 输入模式
  // ============================================================

  function setInputMode(mode) {
    MediaState.inputMode = mode;
  }

  function setPTT(active) {
    MediaState.pttActive = active;
  }

  // ============================================================
  // 提示音
  // ============================================================

  const SOUNDS = {
    connect: [523, 659, 784], // C-E-G 大三和弦
    disconnect: [784, 659, 523], // 反向
    message: [880], // 高音 A5
    call: [659, 523, 659, 523], // 来电铃
  };

  // 复用一个惰性创建的提示音 AudioContext，避免每次提示音 new/close 的开销
  let soundCtx = null;

  function playSound(name, volume = 0.3) {
    const notes = SOUNDS[name];
    if (!notes) return;
    if (!soundCtx || soundCtx.state === 'closed') {
      soundCtx = new (window.AudioContext || window.webkitAudioContext)();
    }
    const ctx = soundCtx;
    if (ctx.state === 'suspended') ctx.resume();
    const dest = ctx.createGain();
    dest.gain.value = volume;
    dest.connect(ctx.destination);
    notes.forEach((freq, i) => {
      const osc = ctx.createOscillator();
      osc.type = 'sine';
      osc.frequency.value = freq;
      const gain = ctx.createGain();
      const t = ctx.currentTime + i * 0.12;
      gain.gain.setValueAtTime(0, t);
      gain.gain.linearRampToValueAtTime(0.5, t + 0.02);
      gain.gain.linearRampToValueAtTime(0, t + 0.1);
      osc.connect(gain);
      gain.connect(dest);
      osc.start(t);
      osc.stop(t + 0.1);
    });
  }

  // ============================================================
  // 桌面通知
  // ============================================================

  function showNotification(title, body) {
    if (!('Notification' in window)) return;
    if (Notification.permission === 'granted') {
      new Notification(title, { body, icon: '/favicon.ico' });
    } else if (Notification.permission !== 'denied') {
      Notification.requestPermission().then(p => {
        if (p === 'granted') new Notification(title, { body });
      });
    }
  }

  function requestNotificationPermission() {
    if ('Notification' in window && Notification.permission === 'default') {
      Notification.requestPermission();
    }
  }

  // ============================================================
  // 导出
  // ============================================================

  global.NevoMedia = {
    state: MediaState,
    startVoice,
    stopVoice,
    handleVoiceFrame,
    startVideo,
    stopVideo,
    handleVideoFrame,
    setRemoteVideoCanvas,
    setLobbyVideoTarget,
    removeLobbyVideoTarget,
    clearLobbyVideoTargets,
    startScreenShare,
    stopScreenShare,
    setInputMode,
    setPTT,
    playSound,
    showNotification,
    requestNotificationPermission,
    updateWaveformUI,
    setRemoteVolume,
    resetWaveformCache,
    _startWaveformMonitor: startWaveformMonitor,
    CODEC_CONFIG,
  };
})(window);
