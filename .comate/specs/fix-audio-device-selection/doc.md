# Fix: Audio Input/Output Device Selection Not Working

## Problem
用户无法在音频设置中选择输入/输出设备。设备下拉框为空。

## Root Cause
`ma_context_`（miniaudio 设备枚举的必要条件）仅在 `AudioEngine::initialize()` 中创建，而 `initialize()` 仅在 `ClientCore::connect()` 的第 7 步中调用。在连接服务器之前，`ma_context_` 为 null，`enumerateInputDevices()` / `enumerateOutputDevices()` 返回空列表。

**调用链：**
```
用户打开 Audio Settings → MainWindow::onAudioSettingsAction()
  → client_core_->enumerateInputDevices()
    → AudioEngine::enumerateInputDevices()
      → ma_context_ 为 null → 返回空列表 → 下拉框为空
```

## Secondary Issues
1. **当前设备未高亮**：`refreshInputDevices()`/`refreshOutputDevices()` 仅填充列表，不设置当前选中项为正在使用的设备
2. **设备切换立即生效**：combo box 的 `currentIndexChanged` 信号直接调用 `selectInputDeviceByName()`，用户取消对话框后设备切换不会回滚
3. **TOCTOU 竞态**：`selectInputDeviceByName()` 重新枚举设备而非使用已有的 ID

## Solution

### Fix 1 (Core): Initialize ma_context_ early
在 `AudioEngine` 中分离 `ma_context_` 的初始化：新增 `initContext()` 方法仅初始化 `ma_context_`（不启动设备/编码器/解码器），允许在连接前枚举设备。

- `AudioEngine::initContext()` — 仅初始化 `ma_context_`，不创建设备
- `AudioEngine::initialize()` — 改为检查 `ma_context_` 是否已存在，如已存在则复用
- `ClientCore` 构造后调用 `audio_engine_->initContext()` 使设备枚举立即可用

### Fix 2 (UI): Restore current device selection in combo box
修改 `refreshInputDevices()`/`refreshOutputDevices()` 增加当前设备名参数，填充后设置选中项。

### Fix 3 (UI): Device changes only apply on dialog Accept
将设备切换从实时信号改为在对话框 Accept 时统一应用，与其他设置（音量、VAD等）行为一致。

## Affected Files
| File | Modification |
|------|-------------|
| `src/core/include/nevo/core/audio/AudioEngine.h` | 新增 `initContext()` 方法声明 |
| `src/core/src/audio/AudioEngine.cpp` | 实现 `initContext()`，修改 `initialize()` 复用已有 `ma_context_` |
| `src/client/include/nevo/client/ClientCore.h` | 新增 `initAudioContext()` 方法声明 |
| `src/client/src/ClientCore.cpp` | 构造后调用 `initAudioContext()`；在 `onAudioSettingsAction` 中传递当前设备名 |
| `src/ui/include/nevo/ui/AudioSettingsWidget.h` | 修改 `refreshInputDevices()`/`refreshOutputDevices()` 签名；新增 `selectedInputDevice()`/`selectedOutputDevice()` |
| `src/ui/src/AudioSettingsWidget.cpp` | 实现当前设备高亮；新增获取选中设备方法 |
| `src/ui/src/MainWindow.cpp` | 修改设备切换逻辑为 Accept 时应用；传递当前设备名 |

## Expected Outcome
- 应用启动后即可枚举和选择音频设备（无需先连接服务器）
- 下拉框正确显示当前使用的设备
- 取消对话框不会应用设备更改
