#pragma once
/**
 * @file AudioSettingsWidget.h
 * @brief 音频设置面板 - QWidget 实现
 *
 * AudioSettingsWidget 为 NEVO VoIP 客户端提供音频配置界面，
 * 包括输入/输出设备选择、VAD 灵敏度、PTT 按键绑定、
 * 噪声抑制开关和输入/输出音量控制。
 *
 * 布局结构（垂直布局）：
 *   [输入设备选择]
 *   [输出设备选择]
 *   [输入音量滑块]
 *   [输出音量滑块]
 *   [VAD 灵敏度滑块]
 *   [PTT 按键绑定按钮]
 *   [噪声抑制开关]
 *
 * 信号：
 *   - inputDeviceChanged(): 输入设备变更
 *   - outputDeviceChanged(): 输出设备变更
 *   - inputVolumeChanged(): 输入音量变更
 *   - outputVolumeChanged(): 输出音量变更
 *   - vadSensitivityChanged(): VAD 灵敏度变更
 *   - pttKeyChanged(): PTT 按键变更
 *   - noiseSuppressionToggled(): 噪声抑制开关变更
 *
 * 线程安全：
 *   - 所有操作应在主线程执行
 */

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QKeySequence>
#include <QString>
#include <QTimer>

#include <functional>

namespace nevo {

// ============================================================
// VoiceInputMode - 语音输入方式
// ============================================================

/// 语音输入方式枚举
enum class VoiceInputMode {
    Vad = 0,   ///< 语音活动检测（自动）
    Ptt = 1    ///< 按键说话（手动）
};

// ============================================================
// AudioSettingsWidget - 音频设置面板
// ============================================================

/**
 * @class AudioSettingsWidget
 * @brief 音频配置界面
 *
 * 提供输入/输出设备选择、音量控制、VAD 灵敏度调节、
 * PTT 按键绑定和噪声抑制开关。
 *
 * 典型用法：
 * @code
 *   AudioSettingsWidget* settings = new AudioSettingsWidget;
 *
 *   // 连接信号到 AudioEngine
 *   connect(settings, &AudioSettingsWidget::inputVolumeChanged,
 *           [](int vol) { audioEngine.setInputGain(vol / 100.0f); });
 *   connect(settings, &AudioSettingsWidget::outputVolumeChanged,
 *           [](int vol) { audioEngine.setOutputVolume(vol / 100.0f); });
 *
 *   // 刷新设备列表
 *   settings->refreshInputDevices(device_list);
 *   settings->refreshOutputDevices(device_list);
 * @endcode
 */
class AudioSettingsWidget : public QWidget {
    Q_OBJECT

public:
    // ============================================================
    // 构造 / 析构
    // ============================================================

    /// 构造函数
    /// @param parent 父 QWidget
    explicit AudioSettingsWidget(QWidget* parent = nullptr);

    /// 析构函数
    ~AudioSettingsWidget() override;

    // 禁止拷贝
    AudioSettingsWidget(const AudioSettingsWidget&) = delete;
    AudioSettingsWidget& operator=(const AudioSettingsWidget&) = delete;

    // ============================================================
    // 设备列表刷新
    // ============================================================

    /**
     * @brief 刷新输入设备列表
     *
     * 清空当前列表并填入新的设备名称。
     *
     * @param devices 输入设备名称列表
     * @param current_device 当前正在使用的设备名称（空字符串表示默认）
     */
    void refreshInputDevices(const QStringList& devices, const QString& current_device = QString());

    /**
     * @brief 刷新输出设备列表
     *
     * @param devices 输出设备名称列表
     * @param current_device 当前正在使用的设备名称（空字符串表示默认）
     */
    void refreshOutputDevices(const QStringList& devices, const QString& current_device = QString());

    // ============================================================
    // 设置/获取当前值
    // ============================================================

    /// 获取当前输入音量（0~100）
    int inputVolume() const;

    /// 设置输入音量（0~100）
    void setInputVolume(int volume);

    /// 获取当前输出音量（0~100）
    int outputVolume() const;

    /// 设置输出音量（0~100）
    void setOutputVolume(int volume);

    /// 获取 VAD 灵敏度（0~100）
    int vadSensitivity() const;

    /// 设置 VAD 灵敏度（0~100）
    void setVadSensitivity(int sensitivity);

    /// 获取是否启用 VAD
    bool isVadEnabled() const;

    /// 设置 VAD 启用状态
    void setVadEnabled(bool enabled);

    /// 获取是否启用 PTT
    bool isPttEnabled() const;

    /// 设置 PTT 启用状态
    void setPttEnabled(bool enabled);

    /// 获取语音输入方式
    VoiceInputMode voiceInputMode() const;

    /// 设置语音输入方式
    void setVoiceInputMode(VoiceInputMode mode);

    /// 获取噪声抑制级别（0~100）
    int noiseSuppression() const;

    /// 设置噪声抑制级别（0~100）
    void setNoiseSuppression(int level);

    /// 获取是否启用噪声抑制
    bool noiseSuppressionEnabled() const;

    /// 设置噪声抑制启用状态
    void setNoiseSuppressionEnabled(bool enabled);

    /// 获取 PTT 按键序列
    QKeySequence pttKeySequence() const;

    /// 设置 PTT 按键序列
    void setPttKeySequence(const QKeySequence& key);

    /// 获取当前选中的输入设备名称
    QString selectedInputDevice() const;

    /// 获取当前选中的输出设备名称
    QString selectedOutputDevice() const;

    /// 设置输入设备下拉框当前选中项（不触发信号）
    /// 用于设备切换失败时将 UI 恢复为后端实际使用的设备
    void setCurrentInputDevice(const QString& device_name);

    /// 设置输出设备下拉框当前选中项（不触发信号）
    /// 用于设备切换失败时将 UI 恢复为后端实际使用的设备
    void setCurrentOutputDevice(const QString& device_name);

    /// Retranslate UI strings (for i18n language switching)
    void retranslateUi();

    /// Access the input level progress bar (for external level updates)
    QProgressBar* inputLevelBar() const { return input_level_bar_; }

    /// Set a level provider function for the input level meter
    /// The provider is called from updateLevelMeter() on the UI thread
    void setLevelProvider(std::function<float()> provider);

signals:

    /// 输入设备变更
    /// @param device_name 新选择的输入设备名称
    void inputDeviceChanged(const QString& device_name);

    /// 输出设备变更
    /// @param device_name 新选择的输出设备名称
    void outputDeviceChanged(const QString& device_name);

    /// 输入音量变更
    /// @param volume 新音量（0~100）
    void inputVolumeChanged(int volume);

    /// 输出音量变更
    /// @param volume 新音量（0~100）
    void outputVolumeChanged(int volume);

    /// VAD 灵敏度变更
    /// @param sensitivity 新灵敏度（0~100）
    void vadSensitivityChanged(int sensitivity);

    /// VAD 启用状态变更
    /// @param enabled 是否启用
    void vadEnabledChanged(bool enabled);

    /// 语音输入方式变更
    /// @param mode 新的输入方式
    void voiceInputModeChanged(VoiceInputMode mode);

    /// PTT 按键变更
    /// @param key 新的按键序列
    void pttKeyChanged(const QKeySequence& key);

    /// 噪声抑制开关变更
    /// @param enabled 是否启用
    void noiseSuppressionToggled(bool enabled);

    /// 请求播放测试音
    void testOutputRequested();

    /// 请求开始/停止输入测试
    /// @param active true=开始测试, false=停止测试
    void testInputToggled(bool active);

protected:

    /**
     * @brief 键盘事件处理（用于 PTT 按键绑定捕获）
     *
     * 当 PTT 按键绑定处于捕获模式时，捕获按键并退出捕获模式。
     *
     * @param event 键盘事件
     */
    void keyPressEvent(QKeyEvent* event) override;

    /**
     * @brief 键盘释放事件处理
     * @param event 键盘事件
     */
    void keyReleaseEvent(QKeyEvent* event) override;

    /**
     * @brief 焦点丢失事件处理
     *
     * 当 PTT 按键捕获模式下丢失焦点时，重置捕获状态。
     */
    void focusOutEvent(QFocusEvent* event) override;

    /// Handle language change events for dynamic i18n
    void changeEvent(QEvent* event) override;

private slots:

    /// 输入设备选择变更
    void onInputDeviceChanged(int index);

    /// 输出设备选择变更
    void onOutputDeviceChanged(int index);

    /// PTT 按键绑定按钮点击
    void onPttKeyBindClicked();

    /// Test output button clicked
    void onTestOutputClicked();

    /// Test input button toggled
    void onTestInputToggled();

    /// Level meter timer tick
    void updateLevelMeter();

private:

    // ============================================================
    // 内部辅助方法
    // ============================================================

    /// 初始化 UI 布局
    void setupUi();

    // ============================================================
    // 数据成员
    // ============================================================

    // --- 设备选择 ---
    QComboBox* input_device_combo_;    ///< 输入设备选择下拉框
    QComboBox* output_device_combo_;   ///< 输出设备选择下拉框

    // --- 表单标签（用于 retranslateUi） ---
    QLabel* device_title_label_;       ///< "Audio Devices" 标题
    QLabel* input_device_label_;       ///< "Input Device:" 标签
    QLabel* output_device_label_;      ///< "Output Device:" 标签
    QLabel* volume_title_label_;       ///< "Volume" 标题
    QLabel* mic_gain_title_label_;     ///< "Microphone Gain" 标题
    QLabel* input_gain_label_;         ///< "Microphone Gain:" 标签
    QLabel* output_volume_form_label_; ///< "Output Volume:" 标签
    QLabel* input_mode_form_label_;    ///< "Input Mode:" 标签
    QLabel* sensitivity_form_label_;   ///< "Sensitivity:" 标签
    QLabel* ptt_key_form_label_;       ///< "PTT Key:" 标签
    QLabel* noise_suppression_form_label_; ///< "Noise Suppression:" 标签

    // --- 音量控制 ---
    QSlider* input_volume_slider_;     ///< 输入音量滑块
    QLabel* input_volume_label_;       ///< 输入音量数值标签
    QSlider* output_volume_slider_;    ///< 输出音量滑块
    QLabel* output_volume_label_;      ///< 输出音量数值标签

    // --- 语音输入方式 ---
    QComboBox* input_mode_combo_;      ///< 语音输入方式选择 (VAD/PTT)
    QSlider* vad_sensitivity_slider_;  ///< VAD 灵敏度滑块
    QLabel* vad_sensitivity_label_;    ///< VAD 灵敏度数值标签

    // --- PTT ---
    QPushButton* ptt_key_button_;      ///< PTT 按键绑定按钮
    QKeySequence ptt_key_;             ///< 当前 PTT 按键序列
    bool ptt_capturing_ = false;       ///< 是否正在捕获按键

    // --- 噪声抑制 ---
    QCheckBox* noise_suppression_check_;  ///< 噪声抑制开关

    // --- 设备测试 ---
    QPushButton* input_test_btn_;       ///< 输入测试按钮（开始/停止）
    QPushButton* output_test_btn_;      ///< 输出测试按钮
    QProgressBar* input_level_bar_;     ///< 输入电平表
    QTimer* level_meter_timer_;         ///< 电平表刷新定时器
    bool testing_input_ = false;        ///< 是否正在测试输入
    std::function<float()> level_provider_; ///< 输入电平提供函数
};

} // namespace nevo
