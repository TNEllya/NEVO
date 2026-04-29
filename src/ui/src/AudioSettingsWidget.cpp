/**
 * @file AudioSettingsWidget.cpp
 * @brief AudioSettingsWidget 实现 - 音频设置面板
 *
 * 实现了音频配置界面的 UI 布局和交互逻辑，
 * 包括设备选择、音量控制、VAD 灵敏度、PTT 按键绑定、噪声抑制和设备测试。
 */

#include "nevo/ui/AudioSettingsWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QKeyEvent>
#include <QFocusEvent>

#include <cmath>

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

AudioSettingsWidget::AudioSettingsWidget(QWidget* parent)
    : QWidget(parent)
    , input_device_combo_(nullptr)
    , output_device_combo_(nullptr)
    , device_title_label_(nullptr)
    , input_device_label_(nullptr)
    , output_device_label_(nullptr)
    , volume_title_label_(nullptr)
    , mic_gain_title_label_(nullptr)
    , input_gain_label_(nullptr)
    , output_volume_form_label_(nullptr)
    , input_mode_form_label_(nullptr)
    , sensitivity_form_label_(nullptr)
    , ptt_key_form_label_(nullptr)
    , noise_suppression_form_label_(nullptr)
    , input_volume_slider_(nullptr)
    , input_volume_label_(nullptr)
    , output_volume_slider_(nullptr)
    , output_volume_label_(nullptr)
    , input_mode_combo_(nullptr)
    , vad_sensitivity_slider_(nullptr)
    , vad_sensitivity_label_(nullptr)
    , ptt_key_button_(nullptr)
    , ptt_key_()
    , ptt_capturing_(false)
    , noise_suppression_check_(nullptr)
    , input_test_btn_(nullptr)
    , output_test_btn_(nullptr)
    , input_level_bar_(nullptr)
    , level_meter_timer_(nullptr)
    , testing_input_(false)
{
    setupUi();
}

AudioSettingsWidget::~AudioSettingsWidget()
{
    if (level_meter_timer_ && level_meter_timer_->isActive()) {
        level_meter_timer_->stop();
    }
}

// ============================================================
// 设备列表刷新
// ============================================================

void AudioSettingsWidget::refreshInputDevices(const QStringList& devices, const QString& current_device)
{
    input_device_combo_->blockSignals(true);
    input_device_combo_->clear();
    input_device_combo_->addItems(devices);
    if (!current_device.isEmpty()) {
        int idx = devices.indexOf(current_device);
        if (idx >= 0) {
            input_device_combo_->setCurrentIndex(idx);
        }
    }
    input_device_combo_->blockSignals(false);
}

void AudioSettingsWidget::refreshOutputDevices(const QStringList& devices, const QString& current_device)
{
    output_device_combo_->blockSignals(true);
    output_device_combo_->clear();
    output_device_combo_->addItems(devices);
    if (!current_device.isEmpty()) {
        int idx = devices.indexOf(current_device);
        if (idx >= 0) {
            output_device_combo_->setCurrentIndex(idx);
        }
    }
    output_device_combo_->blockSignals(false);
}

// ============================================================
// 设置/获取当前值
// ============================================================

int AudioSettingsWidget::inputVolume() const
{
    return input_volume_slider_->value();
}

void AudioSettingsWidget::setInputVolume(int volume)
{
    input_volume_slider_->setValue(volume);
}

int AudioSettingsWidget::outputVolume() const
{
    return output_volume_slider_->value();
}

void AudioSettingsWidget::setOutputVolume(int volume)
{
    output_volume_slider_->setValue(volume);
}

int AudioSettingsWidget::vadSensitivity() const
{
    return vad_sensitivity_slider_->value();
}

void AudioSettingsWidget::setVadSensitivity(int sensitivity)
{
    vad_sensitivity_slider_->setValue(sensitivity);
}

bool AudioSettingsWidget::noiseSuppressionEnabled() const
{
    return noise_suppression_check_->isChecked();
}

void AudioSettingsWidget::setNoiseSuppressionEnabled(bool enabled)
{
    noise_suppression_check_->setChecked(enabled);
}

bool AudioSettingsWidget::isVadEnabled() const
{
    return input_mode_combo_ && input_mode_combo_->currentIndex() == static_cast<int>(VoiceInputMode::Vad);
}

void AudioSettingsWidget::setVadEnabled(bool enabled)
{
    if (input_mode_combo_) {
        input_mode_combo_->setCurrentIndex(static_cast<int>(enabled ? VoiceInputMode::Vad : VoiceInputMode::Ptt));
    }
}

bool AudioSettingsWidget::isPttEnabled() const
{
    return input_mode_combo_ && input_mode_combo_->currentIndex() == static_cast<int>(VoiceInputMode::Ptt);
}

void AudioSettingsWidget::setPttEnabled(bool enabled)
{
    if (input_mode_combo_) {
        input_mode_combo_->setCurrentIndex(static_cast<int>(enabled ? VoiceInputMode::Ptt : VoiceInputMode::Vad));
    }
}

VoiceInputMode AudioSettingsWidget::voiceInputMode() const
{
    if (!input_mode_combo_) return VoiceInputMode::Vad;
    return static_cast<VoiceInputMode>(input_mode_combo_->currentIndex());
}

void AudioSettingsWidget::setVoiceInputMode(VoiceInputMode mode)
{
    if (input_mode_combo_) {
        input_mode_combo_->setCurrentIndex(static_cast<int>(mode));
    }
}

int AudioSettingsWidget::noiseSuppression() const
{
    return noiseSuppressionEnabled() ? 100 : 0;
}

void AudioSettingsWidget::setNoiseSuppression(int level)
{
    setNoiseSuppressionEnabled(level > 0);
}

QKeySequence AudioSettingsWidget::pttKeySequence() const
{
    return ptt_key_;
}

void AudioSettingsWidget::setPttKeySequence(const QKeySequence& key)
{
    ptt_key_ = key;
    ptt_key_button_->setText(key.isEmpty()
        ? tr("Click to bind")
        : key.toString());
}

QString AudioSettingsWidget::selectedInputDevice() const
{
    return input_device_combo_ ? input_device_combo_->currentText() : QString();
}

QString AudioSettingsWidget::selectedOutputDevice() const
{
    return output_device_combo_ ? output_device_combo_->currentText() : QString();
}

void AudioSettingsWidget::retranslateUi()
{
    // Section titles
    if (device_title_label_) device_title_label_->setText(tr("Audio Devices"));
    if (volume_title_label_) volume_title_label_->setText(tr("Volume"));
    if (mic_gain_title_label_) mic_gain_title_label_->setText(tr("Microphone Gain"));

    // Form labels
    if (input_device_label_) input_device_label_->setText(tr("Input Device:"));
    if (output_device_label_) output_device_label_->setText(tr("Output Device:"));
    if (input_gain_label_) input_gain_label_->setText(tr("Microphone Gain:"));
    if (output_volume_form_label_) output_volume_form_label_->setText(tr("Output Volume:"));
    if (sensitivity_form_label_) sensitivity_form_label_->setText(tr("Sensitivity:"));
    if (input_mode_form_label_) input_mode_form_label_->setText(tr("Input Mode:"));
    if (input_mode_combo_) {
        // 更新下拉框项目文本
        input_mode_combo_->setItemText(static_cast<int>(VoiceInputMode::Vad), tr("Voice Activity Detection"));
        input_mode_combo_->setItemText(static_cast<int>(VoiceInputMode::Ptt), tr("Push-To-Talk"));
    }
    if (ptt_key_form_label_) ptt_key_form_label_->setText(tr("PTT Key:"));
    if (noise_suppression_form_label_) noise_suppression_form_label_->setText(tr("Noise Suppression:"));

    // Buttons and checkboxes
    if (input_test_btn_ && !testing_input_) input_test_btn_->setText(tr("Test Input"));
    if (output_test_btn_) output_test_btn_->setText(tr("Test Output"));
    if (ptt_key_button_ && !ptt_capturing_) {
        ptt_key_button_->setText(ptt_key_.isEmpty()
            ? tr("Click to bind")
            : ptt_key_.toString());
    }
    if (noise_suppression_check_) {
        noise_suppression_check_->setText(tr("Enable Noise Suppression (RNNoise)"));
    }
}

void AudioSettingsWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

// ============================================================
// 键盘事件处理（PTT 按键捕获）
// ============================================================

void AudioSettingsWidget::keyPressEvent(QKeyEvent* event)
{
    if (ptt_capturing_) {
        int key = event->key();
        if (key != Qt::Key_Shift && key != Qt::Key_Control &&
            key != Qt::Key_Alt && key != Qt::Key_Meta) {

            QKeySequence sequence(event->key() | event->modifiers());
            ptt_key_ = sequence;
            ptt_key_button_->setText(sequence.toString());
            ptt_capturing_ = false;
            ptt_key_button_->setStyleSheet(QString());
            releaseKeyboard();

            emit pttKeyChanged(sequence);
        }
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void AudioSettingsWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (ptt_capturing_) {
        event->accept();
        return;
    }

    QWidget::keyReleaseEvent(event);
}

void AudioSettingsWidget::focusOutEvent(QFocusEvent* event)
{
    if (ptt_capturing_) {
        ptt_capturing_ = false;
        ptt_key_button_->setText(ptt_key_.isEmpty()
            ? tr("Click to bind")
            : ptt_key_.toString());
        ptt_key_button_->setStyleSheet(QString());
        releaseKeyboard();
    }
    QWidget::focusOutEvent(event);
}

// ============================================================
// 私有槽函数
// ============================================================

void AudioSettingsWidget::onInputDeviceChanged(int index)
{
    Q_UNUSED(index);
    QString device_name = input_device_combo_->currentText();
    if (!device_name.isEmpty()) {
        emit inputDeviceChanged(device_name);
    }
}

void AudioSettingsWidget::onOutputDeviceChanged(int index)
{
    Q_UNUSED(index);
    QString device_name = output_device_combo_->currentText();
    if (!device_name.isEmpty()) {
        emit outputDeviceChanged(device_name);
    }
}

void AudioSettingsWidget::setCurrentInputDevice(const QString& device_name)
{
    if (device_name.isEmpty()) return;
    int idx = input_device_combo_->findText(device_name);
    if (idx >= 0 && idx != input_device_combo_->currentIndex()) {
        input_device_combo_->blockSignals(true);
        input_device_combo_->setCurrentIndex(idx);
        input_device_combo_->blockSignals(false);
    }
}

void AudioSettingsWidget::setCurrentOutputDevice(const QString& device_name)
{
    if (device_name.isEmpty()) return;
    int idx = output_device_combo_->findText(device_name);
    if (idx >= 0 && idx != output_device_combo_->currentIndex()) {
        output_device_combo_->blockSignals(true);
        output_device_combo_->setCurrentIndex(idx);
        output_device_combo_->blockSignals(false);
    }
}

void AudioSettingsWidget::onPttKeyBindClicked()
{
    if (ptt_capturing_) {
        ptt_capturing_ = false;
        ptt_key_button_->setText(ptt_key_.isEmpty()
            ? tr("Click to bind")
            : ptt_key_.toString());
        ptt_key_button_->setStyleSheet(QString());
        releaseKeyboard();
    } else {
        ptt_capturing_ = true;
        ptt_key_button_->setText(tr("Press a key..."));
        ptt_key_button_->setStyleSheet(
            QStringLiteral("background-color: #ffff99;"));
        grabKeyboard();
    }
}

void AudioSettingsWidget::onTestOutputClicked()
{
    emit testOutputRequested();
    output_test_btn_->setEnabled(false);
    // Re-enable after 1.2 seconds (tone duration + margin)
    QTimer::singleShot(1200, this, [this]() {
        output_test_btn_->setEnabled(true);
    });
}

void AudioSettingsWidget::onTestInputToggled()
{
    testing_input_ = !testing_input_;
    if (testing_input_) {
        input_test_btn_->setText(tr("Stop Test"));
        input_level_bar_->setValue(0);
        input_level_bar_->setVisible(true);
        if (!level_meter_timer_) {
            level_meter_timer_ = new QTimer(this);
            connect(level_meter_timer_, &QTimer::timeout,
                    this, &AudioSettingsWidget::updateLevelMeter);
        }
        level_meter_timer_->start(33);  // ~30fps
        emit testInputToggled(true);
    } else {
        input_test_btn_->setText(tr("Test Input"));
        if (level_meter_timer_) {
            level_meter_timer_->stop();
        }
        input_level_bar_->setValue(0);
        emit testInputToggled(false);
    }
}

void AudioSettingsWidget::updateLevelMeter()
{
    if (level_provider_) {
        float level = level_provider_();
        input_level_bar_->setValue(static_cast<int>(level * 100.0f));
    }
}

void AudioSettingsWidget::setLevelProvider(std::function<float()> provider)
{
    level_provider_ = std::move(provider);
}

// ============================================================
// 内部辅助方法
// ============================================================

void AudioSettingsWidget::setupUi()
{
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(12, 12, 12, 12);
    main_layout->setSpacing(12);

    // ================================================================
    // 设备选择卡片
    // ================================================================
    QFrame* device_card = new QFrame(this);
    device_card->setObjectName(QStringLiteral("settingsCard"));
    device_card->setFrameShape(QFrame::StyledPanel);
    QFormLayout* device_layout = new QFormLayout(device_card);
    device_layout->setContentsMargins(16, 16, 16, 16);
    device_layout->setSpacing(12);

    QLabel* device_title = new QLabel(tr("Audio Devices"), this);
    device_title_label_ = device_title;
    device_title->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 13px; color: #e0e0e0;"));
    device_layout->addRow(device_title);

    // 输入设备 + 测试
    {
        QHBoxLayout* row = new QHBoxLayout();
        input_device_combo_ = new QComboBox(device_card);
        input_device_combo_->setMinimumWidth(200);
        connect(input_device_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &AudioSettingsWidget::onInputDeviceChanged);
        row->addWidget(input_device_combo_, 1);

        input_test_btn_ = new QPushButton(tr("Test Input"), device_card);
        input_test_btn_->setFixedWidth(90);
        input_test_btn_->setCheckable(true);
        connect(input_test_btn_, &QPushButton::clicked,
                this, &AudioSettingsWidget::onTestInputToggled);
        row->addWidget(input_test_btn_);

        input_device_label_ = new QLabel(tr("Input Device:"), this);
        device_layout->addRow(input_device_label_, row);
    }

    // 输入电平表
    {
        input_level_bar_ = new QProgressBar(device_card);
        input_level_bar_->setRange(0, 100);
        input_level_bar_->setValue(0);
        input_level_bar_->setFixedHeight(12);
        input_level_bar_->setTextVisible(false);
        input_level_bar_->setStyleSheet(QStringLiteral(
            "QProgressBar { background-color: #1e2229; border: 1px solid #3a3f4b; border-radius: 3px; }"
            "QProgressBar::chunk { background-color: #4caf50; border-radius: 2px; }"));
        input_level_bar_->setVisible(false);  // Hidden until test starts
        device_layout->addRow(QString(), input_level_bar_);
    }

    // 输出设备 + 测试
    {
        QHBoxLayout* row = new QHBoxLayout();
        output_device_combo_ = new QComboBox(device_card);
        output_device_combo_->setMinimumWidth(200);
        connect(output_device_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &AudioSettingsWidget::onOutputDeviceChanged);
        row->addWidget(output_device_combo_, 1);

        output_test_btn_ = new QPushButton(tr("Test Output"), device_card);
        output_test_btn_->setFixedWidth(90);
        connect(output_test_btn_, &QPushButton::clicked,
                this, &AudioSettingsWidget::onTestOutputClicked);
        row->addWidget(output_test_btn_);

        output_device_label_ = new QLabel(tr("Output Device:"), this);
        device_layout->addRow(output_device_label_, row);
    }

    main_layout->addWidget(device_card);

    // ================================================================
    // 音量控制卡片（仅输出音量）
    // ================================================================
    QFrame* volume_card = new QFrame(this);
    volume_card->setObjectName(QStringLiteral("settingsCard"));
    volume_card->setFrameShape(QFrame::StyledPanel);
    QFormLayout* volume_layout = new QFormLayout(volume_card);
    volume_layout->setContentsMargins(16, 16, 16, 16);
    volume_layout->setSpacing(12);

    QLabel* volume_title = new QLabel(tr("Volume"), this);
    volume_title_label_ = volume_title;
    volume_title->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 13px; color: #e0e0e0;"));
    volume_layout->addRow(volume_title);

    // 输出音量
    {
        QHBoxLayout* row = new QHBoxLayout();
        output_volume_slider_ = new QSlider(Qt::Horizontal, volume_card);
        output_volume_slider_->setRange(0, 100);
        output_volume_slider_->setValue(80);
        output_volume_label_ = new QLabel(QStringLiteral("80%"), volume_card);
        output_volume_label_->setMinimumWidth(40);

        connect(output_volume_slider_, &QSlider::valueChanged,
                this, [this](int value) {
                    output_volume_label_->setText(
                        QStringLiteral("%1%").arg(value));
                    emit outputVolumeChanged(value);
                });

        row->addWidget(output_volume_slider_);
        row->addWidget(output_volume_label_);
        output_volume_form_label_ = new QLabel(tr("Output Volume:"), this);
        volume_layout->addRow(output_volume_form_label_, row);
    }

    main_layout->addWidget(volume_card);

    // ================================================================
    // 麦克风增益卡片
    // ================================================================
    QFrame* mic_gain_card = new QFrame(this);
    mic_gain_card->setObjectName(QStringLiteral("settingsCard"));
    mic_gain_card->setFrameShape(QFrame::StyledPanel);
    QFormLayout* mic_gain_layout = new QFormLayout(mic_gain_card);
    mic_gain_layout->setContentsMargins(16, 16, 16, 16);
    mic_gain_layout->setSpacing(12);

    QLabel* mic_gain_title = new QLabel(tr("Microphone Gain"), this);
    mic_gain_title_label_ = mic_gain_title;
    mic_gain_title->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 13px; color: #e0e0e0;"));
    mic_gain_layout->addRow(mic_gain_title);

    {
        QHBoxLayout* row = new QHBoxLayout();
        input_volume_slider_ = new QSlider(Qt::Horizontal, mic_gain_card);
        input_volume_slider_->setRange(0, 200);
        input_volume_slider_->setValue(100);
        input_volume_label_ = new QLabel(QStringLiteral("0 dB"), mic_gain_card);
        input_volume_label_->setMinimumWidth(50);

        connect(input_volume_slider_, &QSlider::valueChanged,
                this, [this](int value) {
                    QString text;
                    if (value == 0) {
                        text = tr("Mute");
                    } else {
                        float db = 20.0f * std::log10(static_cast<float>(value) / 100.0f);
                        text = QStringLiteral("%1%2 dB")
                            .arg(db >= 0 ? QStringLiteral("+") : QString())
                            .arg(QString::number(db, 'f', 1));
                    }
                    input_volume_label_->setText(text);
                    emit inputVolumeChanged(value);
                });

        row->addWidget(input_volume_slider_);
        row->addWidget(input_volume_label_);
        input_gain_label_ = new QLabel(tr("Microphone Gain:"), this);
        mic_gain_layout->addRow(input_gain_label_, row);
    }

    main_layout->addWidget(mic_gain_card);

    // ================================================================
    // 语音输入方式组
    // ================================================================
    QFrame* input_mode_group = new QFrame(this);
    input_mode_group->setObjectName(QStringLiteral("settings_card"));
    input_mode_group->setStyleSheet(QStringLiteral(
        "#settings_card { background-color: #252b36; border-radius: 10px; padding: 8px; }"
        "#settings_card QLabel { color: #c5c8d4; font-size: 13px; font-weight: 500; }"));
    QFormLayout* input_mode_layout = new QFormLayout(input_mode_group);
    input_mode_layout->setContentsMargins(16, 16, 16, 16);
    input_mode_layout->setSpacing(12);

    // 语音输入方式选择
    {
        input_mode_combo_ = new QComboBox(input_mode_group);
        input_mode_combo_->addItem(tr("Voice Activity Detection"));  // index 0 = Vad
        input_mode_combo_->addItem(tr("Push-To-Talk"));              // index 1 = Ptt
        input_mode_combo_->setCurrentIndex(static_cast<int>(VoiceInputMode::Vad));
        connect(input_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int index) {
                    bool is_vad = (index == static_cast<int>(VoiceInputMode::Vad));
                    // VAD 灵敏度：VAD 模式下启用
                    vad_sensitivity_slider_->setEnabled(is_vad);
                    vad_sensitivity_label_->setEnabled(is_vad);
                    // PTT 按键：PTT 模式下启用
                    ptt_key_button_->setEnabled(!is_vad);
                    emit voiceInputModeChanged(static_cast<VoiceInputMode>(index));
                    emit vadEnabledChanged(is_vad);
                });
        input_mode_form_label_ = new QLabel(tr("Input Mode:"), this);
        input_mode_layout->addRow(input_mode_form_label_, input_mode_combo_);
    }

    // VAD 灵敏度
    {
        QHBoxLayout* row = new QHBoxLayout();
        vad_sensitivity_slider_ = new QSlider(Qt::Horizontal, input_mode_group);
        vad_sensitivity_slider_->setRange(0, 100);
        vad_sensitivity_slider_->setValue(50);
        vad_sensitivity_label_ = new QLabel(QStringLiteral("50%"), input_mode_group);
        vad_sensitivity_label_->setMinimumWidth(40);

        connect(vad_sensitivity_slider_, &QSlider::valueChanged,
                this, [this](int value) {
                    vad_sensitivity_label_->setText(
                        QStringLiteral("%1%").arg(value));
                    emit vadSensitivityChanged(value);
                });

        row->addWidget(vad_sensitivity_slider_);
        row->addWidget(vad_sensitivity_label_);
        sensitivity_form_label_ = new QLabel(tr("Sensitivity:"), this);
        input_mode_layout->addRow(sensitivity_form_label_, row);
    }

    // PTT 按键绑定
    {
        ptt_key_button_ = new QPushButton(tr("Click to bind"), input_mode_group);
        ptt_key_button_->setToolTip(
            tr("Click this button, then press the key "
               "you want to use for Push-To-Talk"));
        ptt_key_button_->setEnabled(false);  // 默认 VAD 模式，PTT 禁用
        connect(ptt_key_button_, &QPushButton::clicked,
                this, &AudioSettingsWidget::onPttKeyBindClicked);
        ptt_key_form_label_ = new QLabel(tr("PTT Key:"), this);
        input_mode_layout->addRow(ptt_key_form_label_, ptt_key_button_);
    }

    main_layout->addWidget(input_mode_group);

    // ================================================================
    // 噪声抑制
    // ================================================================
    QFrame* noise_group = new QFrame(this);
    noise_group->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
    noise_group->setStyleSheet(
        QStringLiteral("QFrame { background-color: #2A2D33; border: 1px solid #3A3D45; "
                       "border-radius: 8px; padding: 12px; }"));
    QFormLayout* noise_layout = new QFormLayout(noise_group);
    noise_layout->setContentsMargins(12, 12, 12, 12);

    noise_suppression_check_ = new QCheckBox(
        tr("Enable Noise Suppression (RNNoise)"),
        noise_group);
    noise_suppression_check_->setChecked(true);
    connect(noise_suppression_check_, &QCheckBox::toggled,
            this, &AudioSettingsWidget::noiseSuppressionToggled);
    noise_suppression_form_label_ = new QLabel(tr("Noise Suppression:"), this);
    noise_layout->addRow(noise_suppression_form_label_, noise_suppression_check_);

    main_layout->addWidget(noise_group);

    main_layout->addStretch();
}

} // namespace nevo
