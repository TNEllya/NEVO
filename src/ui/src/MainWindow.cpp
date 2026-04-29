/**
 * @file MainWindow.cpp
 * @brief MainWindow ?? - ???
 *
 * NEVO VoIP ?????? UI ???????
 * ????? ClientCore ????
 */

#include "nevo/ui/MainWindow.h"

#include <QApplication>
#include <QMessageBox>
#include <QFile>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialog>
#include <QThread>
#include <QSettings>
#include <QTranslator>
#include <QActionGroup>
#include <QPointer>
#include <QCloseEvent>

#include "nevo/core/common/Logger.h"
#include "nevo/core/audio/AudioEngine.h"

#ifdef NEVO_HAS_BOOST
#include "nevo/ui/ConnectionBar.h"
namespace {
nevo::ConnectionState toConnectionState(nevo::ClientState s) {
    switch (s) {
        case nevo::ClientState::Disconnected: return nevo::ConnectionState::Disconnected;
        case nevo::ClientState::Connecting: return nevo::ConnectionState::Connecting;
        case nevo::ClientState::Connected: return nevo::ConnectionState::Connected;
        case nevo::ClientState::InChannel: return nevo::ConnectionState::InChannel;
        default: return nevo::ConnectionState::Disconnected;
    }
}
}
#endif

namespace nevo {

// ============================================================
// LoginDialog ??
// ============================================================

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent)
    , username_edit_(nullptr)
{
    setWindowTitle(tr("NEVO - Login"));
    setModal(true);
    setMinimumWidth(360);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(16);

    // Title label
    QLabel* title_label = new QLabel(tr("Connect to Server"), this);
    QFont title_font = title_label->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label->setFont(title_font);
    title_label->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(title_label);

    // Subtitle
    QLabel* subtitle = new QLabel(
        tr("Enter your username to join the voice server"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color: #8b949e; font-size: 12px;"));
    main_layout->addWidget(subtitle);

    main_layout->addSpacing(8);

    // Form layout for inputs
    QFormLayout* form_layout = new QFormLayout();
    form_layout->setSpacing(12);
    form_layout->setLabelAlignment(Qt::AlignLeft);

    username_edit_ = new QLineEdit(this);
    username_edit_->setPlaceholderText(tr("Enter username"));
    username_edit_->setMinimumHeight(32);
    form_layout->addRow(tr("Username:"), username_edit_);

    main_layout->addLayout(form_layout);
    main_layout->addSpacing(8);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
    buttons->button(QDialogButtonBox::Ok)->setMinimumHeight(36);
    buttons->button(QDialogButtonBox::Cancel)->setMinimumHeight(36);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    main_layout->addWidget(buttons);
}

QString LoginDialog::username() const
{
    return username_edit_->text();
}

// ============================================================
// OwnerBindDialog ??
// ============================================================

OwnerBindDialog::OwnerBindDialog(QWidget* parent)
    : QDialog(parent)
    , bind_key_edit_(nullptr)
{
    setWindowTitle(tr("NEVO - Bind Owner"));
    setModal(true);
    setMinimumWidth(400);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(16);

    // Title label
    QLabel* title_label = new QLabel(tr("Bind Server Owner"), this);
    QFont title_font = title_label->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    title_label->setFont(title_font);
    title_label->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(title_label);

    // Subtitle
    QLabel* subtitle = new QLabel(
        tr("Enter the owner bind key displayed on the server console"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color: #8b949e; font-size: 12px;"));
    main_layout->addWidget(subtitle);

    main_layout->addSpacing(8);

    // Form layout for inputs
    QFormLayout* form_layout = new QFormLayout();
    form_layout->setSpacing(12);
    form_layout->setLabelAlignment(Qt::AlignLeft);

    bind_key_edit_ = new QLineEdit(this);
    bind_key_edit_->setPlaceholderText(tr("Enter 64-character bind key"));
    bind_key_edit_->setMinimumHeight(32);
    bind_key_edit_->setMaxLength(64);
    form_layout->addRow(tr("Bind Key:"), bind_key_edit_);

    main_layout->addLayout(form_layout);
    main_layout->addSpacing(8);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Bind"));
    buttons->button(QDialogButtonBox::Ok)->setMinimumHeight(36);
    buttons->button(QDialogButtonBox::Cancel)->setMinimumHeight(36);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    main_layout->addWidget(buttons);
}

QString OwnerBindDialog::bindKey() const
{
    return bind_key_edit_->text();
}

// ============================================================
// MainWindow Implementation
// ============================================================

#ifdef NEVO_HAS_BOOST

MainWindow::MainWindow(boost::asio::io_context& io_ctx, QWidget* parent)
    : QMainWindow(parent)
    , client_core_(std::make_unique<ClientCore>(io_ctx))
    , io_ctx_(&io_ctx)
    , io_thread_(nullptr)
    , channel_model_(nullptr)
    , user_model_(nullptr)
    , channel_delegate_(nullptr)
    , channel_tree_(nullptr)
    , user_list_(nullptr)
    , channel_dock_(nullptr)
    , user_dock_(nullptr)
    , connection_bar_(nullptr)
    , audio_settings_(nullptr)
    , connect_action_(nullptr)
    , disconnect_action_(nullptr)
    , mute_action_(nullptr)
    , deafen_action_(nullptr)
    , quit_action_(nullptr)
    , audio_settings_action_(nullptr)
    , about_action_(nullptr)
{
    setupUi();

    // ???????????????
    mic_level_timer_ = new QTimer(this);
    mic_level_timer_->setInterval(50);  // 50ms ??
    connect(mic_level_timer_, &QTimer::timeout, this, [this]() {
        updateMicIndicator();
    });

    // ?????????????? PTT ??
    qApp->installEventFilter(this);

    io_thread_ = std::make_unique<std::thread>([this]() {
        if (io_ctx_) io_ctx_->run();
    });

    NEVO_LOG_INFO("ui", "MainWindow created (with ClientCore)");
}

#endif // NEVO_HAS_BOOST

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
#ifdef NEVO_HAS_BOOST
    , io_ctx_(nullptr)  // unused in this path
#endif
    , channel_model_(nullptr)
    , user_model_(nullptr)
    , channel_delegate_(nullptr)
    , channel_tree_(nullptr)
    , user_list_(nullptr)
    , channel_dock_(nullptr)
    , user_dock_(nullptr)
    , connection_bar_(nullptr)
    , audio_settings_(nullptr)
    , connect_action_(nullptr)
    , disconnect_action_(nullptr)
    , mute_action_(nullptr)
    , deafen_action_(nullptr)
    , quit_action_(nullptr)
    , audio_settings_action_(nullptr)
    , about_action_(nullptr)
{
    setupUi();
    NEVO_LOG_INFO("ui", "MainWindow created (standalone, no ClientCore)");
}

MainWindow::~MainWindow()
{
    // Remove translator from QCoreApplication before Qt's parent-child
    // mechanism deletes it, to avoid use-after-free during QCoreApplication cleanup.
    QTranslator* translator = qApp->property("nevoTranslator").value<QTranslator*>();
    if (translator) {
        QCoreApplication::removeTranslator(translator);
        // translator has parent=this, will be deleted by Qt's parent-child mechanism
        qApp->setProperty("nevoTranslator", QVariant());
    }
    QByteArray* qm_data = qApp->property("nevoQmData").value<QByteArray*>();
    if (qm_data) {
        delete qm_data;
        qApp->setProperty("nevoQmData", QVariant());
    }

    // Note: actual cleanup is done in closeEvent() which runs while
    // the Qt event loop is still active. The destructor just handles
    // the case where closeEvent was never called (e.g. programmatic destroy).
#ifdef NEVO_HAS_BOOST
    if (!shutdown_initiated_) {
        performShutdown();
    }
#endif
    NEVO_LOG_INFO("ui", "MainWindow destroyed");
}

// ============================================================
// ???? ?? Qt ????????????
// ============================================================

void MainWindow::closeEvent(QCloseEvent* event)
{
#ifdef NEVO_HAS_BOOST
    if (!shutdown_initiated_) {
        performShutdown();
    }
#else
    saveSettings();
#endif

    event->accept();
}

#ifdef NEVO_HAS_BOOST
void MainWindow::performShutdown()
{
    shutdown_initiated_ = true;

    NEVO_LOG_INFO("ui", "Performing shutdown cleanup...");

    // 1. ????????? IO ???????????????? UI
    if (client_core_) {
        client_core_->onStateChanged = nullptr;
        client_core_->onUserJoined = nullptr;
        client_core_->onUserLeft = nullptr;
        client_core_->onUserSpeaking = nullptr;
        client_core_->onServerMessage = nullptr;
        client_core_->onChannelList = nullptr;
        client_core_->onLatencyUpdate = nullptr;
        client_core_->onError = nullptr;
        client_core_->onOwnerBound = nullptr;
        client_core_->onOwnerBindRequired = nullptr;

        // 2. ?????????????????
        //    ??????? UI ?????? ClientCore::disconnect()
        //    ??? network_mgr_->disconnect() ???? strand ?? socket?
        //    ??? IO ??????????????
        client_core_->disconnect();
    }

    // 3. ?? io_context?? io_thread_ ? run() ??
    if (io_ctx_) {
        io_ctx_->stop();
    }

    // 4. ?? IO ????
    //    ? io_context::stop() ??run() ??????
    //    ??????? handler ????????? handler?
    if (io_thread_ && io_thread_->joinable()) {
        io_thread_->join();
    }

    saveSettings();

    NEVO_LOG_INFO("ui", "Shutdown cleanup complete");
}
#endif

// ============================================================
// ????
// ============================================================

void MainWindow::onConnectAction()
{
    LoginDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString username = dialog.username();

    if (username.isEmpty()) {
        QMessageBox::warning(this, tr("Login Error"),
                             tr("Username cannot be empty."));
        return;
    }

#ifdef NEVO_HAS_BOOST
    QString host = connection_bar_->serverAddress();
    uint16_t port = connection_bar_->serverPort();

    boost::asio::co_spawn(*io_ctx_,
        [this, host, port, username]() mutable
        -> boost::asio::awaitable<void> {
            auto result = co_await client_core_->connect(
                host.toStdString(), port,
                username.toStdString());

            if (!result) {
                QPointer<MainWindow> guard(this);
                QMetaObject::invokeMethod(this, [guard, result]() {
                    if (!guard) return;
                    QMessageBox::critical(guard,
                        MainWindow::tr("Connection Error"),
                        QString("Failed to connect: %1")
                            .arg(QString::fromStdString(
                                result.error().message())));
                }, Qt::QueuedConnection);
            }
        },
        boost::asio::detached);
#else
    QMessageBox::information(this, tr("Connect"),
        tr("Client module not available (built without Boost)."));
#endif
}

void MainWindow::onDisconnectAction()
{
#ifdef NEVO_HAS_BOOST
    client_core_->disconnect();
#else
    statusBar()->showMessage(tr("Disconnected"));
#endif
}

void MainWindow::onQuitAction()
{
    close();
}

void MainWindow::onAudioSettingsAction()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Audio Settings"));
    dialog.setMinimumSize(400, 500);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    AudioSettingsWidget* settings = new AudioSettingsWidget(&dialog);
    layout->addWidget(settings);

    // Save original device names for Cancel restore
    std::string original_input_device;
    std::string original_output_device;
#ifdef NEVO_HAS_BOOST
    if (client_core_) {
        original_input_device = client_core_->currentInputDeviceName();
        original_output_device = client_core_->currentOutputDeviceName();
    }
#endif

    // ? MainWindow ??????????????
    if (audio_settings_) {
        settings->setInputVolume(audio_settings_->inputVolume());
        settings->setOutputVolume(audio_settings_->outputVolume());
        settings->setVoiceInputMode(audio_settings_->voiceInputMode());
        settings->setVadSensitivity(audio_settings_->vadSensitivity());
        settings->setNoiseSuppressionEnabled(audio_settings_->noiseSuppressionEnabled());
        settings->setPttKeySequence(ptt_key_);
    }

    // Populate device lists from AudioEngine
#ifdef NEVO_HAS_BOOST
    if (client_core_) {
        QStringList input_devices, output_devices;
        for (const auto& dev : client_core_->enumerateInputDevices()) {
            input_devices << QString::fromStdString(dev.name);
        }
        for (const auto& dev : client_core_->enumerateOutputDevices()) {
            output_devices << QString::fromStdString(dev.name);
        }
        QString current_input = QString::fromStdString(client_core_->currentInputDeviceName());
        QString current_output = QString::fromStdString(client_core_->currentOutputDeviceName());
        settings->refreshInputDevices(input_devices, current_input);
        settings->refreshOutputDevices(output_devices, current_output);
    }
#endif

    // Connect device selection signals for immediate switching
#ifdef NEVO_HAS_BOOST
    connect(settings, &AudioSettingsWidget::inputDeviceChanged,
            this, [this, settings](const QString& device_name) {
        if (client_core_) {
            auto result = client_core_->selectInputDeviceByName(device_name.toStdString());
            if (!result) {
                NEVO_LOG_WARN("ui", "Failed to select input device '{}': {}",
                             device_name.toStdString(), result.error().message());
                // Revert combo box to the actual current device
                QString actual = QString::fromStdString(client_core_->currentInputDeviceName());
                settings->setCurrentInputDevice(actual);
                if (result.error().code() == ResultCode::DeviceInUse) {
                    QMessageBox::warning(settings, QObject::tr("Input Device In Use"),
                        QObject::tr("The selected input device is being used by another application "
                                    "in exclusive mode.\n\n"
                                    "Please close other applications using the microphone, "
                                    "or disable exclusive mode in their audio settings."));
                }
            }
        }
    });

    connect(settings, &AudioSettingsWidget::outputDeviceChanged,
            this, [this, settings](const QString& device_name) {
        if (client_core_) {
            auto result = client_core_->selectOutputDeviceByName(device_name.toStdString());
            if (!result) {
                NEVO_LOG_WARN("ui", "Failed to select output device '{}': {}",
                             device_name.toStdString(), result.error().message());
                // Revert combo box to the actual current device
                QString actual = QString::fromStdString(client_core_->currentOutputDeviceName());
                settings->setCurrentOutputDevice(actual);
                if (result.error().code() == ResultCode::DeviceInUse) {
                    QMessageBox::warning(settings, QObject::tr("Output Device In Use"),
                        QObject::tr("The selected output device is being used by another application "
                                    "in exclusive mode.\n\n"
                                    "Please close other applications using the speaker, "
                                    "or disable exclusive mode in their audio settings."));
                }
            }
        }
    });
#endif

    // Connect device test signals (these are immediate actions, not settings)
#ifdef NEVO_HAS_BOOST
    connect(settings, &AudioSettingsWidget::testOutputRequested,
            this, [this]() {
        if (client_core_) {
            client_core_->playTestTone();
        }
    });

    connect(settings, &AudioSettingsWidget::testInputToggled,
            this, [this, settings](bool active) {
        if (!client_core_) return;
        if (active) {
            client_core_->setMonitorEnabled(true);
        } else {
            client_core_->setMonitorEnabled(false);
        }
    });

    // ??????????
    connect(settings, &AudioSettingsWidget::voiceInputModeChanged,
            this, [this](VoiceInputMode mode) {
        if (!client_core_) return;
        bool is_vad = (mode == VoiceInputMode::Vad);
        client_core_->audioEngine().setVadEnabled(is_vad);
        client_core_->audioEngine().setPttEnabled(!is_vad);
        updateMicIndicator();
    });

    // VAD ???????
    connect(settings, &AudioSettingsWidget::vadSensitivityChanged,
            this, [this](int sensitivity) {
        if (client_core_) {
            client_core_->audioEngine().setVadSensitivity(sensitivity);
        }
    });

    // Provide level polling function (thread-safe: reads atomic, called from UI thread timer)
    settings->setLevelProvider([this]() -> float {
        if (!client_core_) return 0.0f;
        return client_core_->audioEngine().getCurrentInputLevel();
    });
#endif

    QDialogButtonBox* buttons = new QDialogButtonBox(&dialog);
    QPushButton* save_btn = buttons->addButton(tr("Save Settings"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Ok);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    // Lambda: apply settings from the dialog widget to client_core_ and audio_settings_
    auto applySettings = [&]() {
        float input_gain = settings->inputVolume() / 100.0f;
        float output_vol = settings->outputVolume() / 100.0f;
        int vad_sensitivity = settings->vadSensitivity();
        VoiceInputMode input_mode = settings->voiceInputMode();
        bool vad_enabled = (input_mode == VoiceInputMode::Vad);
        bool noise_suppression = settings->noiseSuppressionEnabled();
        QKeySequence ptt_key = settings->pttKeySequence();
        QString selected_input = settings->selectedInputDevice();
        QString selected_output = settings->selectedOutputDevice();

#ifdef NEVO_HAS_BOOST
        if (client_core_) {
            client_core_->audioEngine().setInputGain(input_gain);
            client_core_->audioEngine().setOutputVolume(output_vol);
            client_core_->audioEngine().setVadEnabled(vad_enabled);
            client_core_->audioEngine().setVadSensitivity(vad_sensitivity);
            client_core_->audioEngine().setNoiseSuppressionEnabled(noise_suppression);

            // ?? PTT ????
            ptt_key_ = ptt_key;
            // PTT ??????? PTT
            bool ptt_enabled = (input_mode == VoiceInputMode::Ptt);
            client_core_->audioEngine().setPttEnabled(ptt_enabled && !ptt_key_.isEmpty());

            // Apply device selection (may be redundant if already applied via signal,
            // but ensures consistency)
            auto input_result = client_core_->selectInputDeviceByName(selected_input.toStdString());
            if (!input_result) {
                NEVO_LOG_WARN("ui", "Failed to select input device '{}': {}",
                             selected_input.toStdString(), input_result.error().message());
                if (input_result.error().code() == ResultCode::DeviceInUse) {
                    QMessageBox::warning(this, tr("Input Device In Use"),
                        tr("The selected input device is being used by another application "
                           "in exclusive mode.\n\n"
                           "Please close other applications using the microphone, "
                           "or disable exclusive mode in their audio settings."));
                }
            }
            auto output_result = client_core_->selectOutputDeviceByName(selected_output.toStdString());
            if (!output_result) {
                NEVO_LOG_WARN("ui", "Failed to select output device '{}': {}",
                             selected_output.toStdString(), output_result.error().message());
                if (output_result.error().code() == ResultCode::DeviceInUse) {
                    QMessageBox::warning(this, tr("Output Device In Use"),
                        tr("The selected output device is being used by another application "
                           "in exclusive mode.\n\n"
                           "Please close other applications using the speaker, "
                           "or disable exclusive mode in their audio settings."));
                }
            }
        }
#endif

        // ??? MainWindow ?????????? saveSettings() ???????
        if (audio_settings_) {
            audio_settings_->setInputVolume(settings->inputVolume());
            audio_settings_->setOutputVolume(settings->outputVolume());
            audio_settings_->setVoiceInputMode(input_mode);
            audio_settings_->setVadSensitivity(vad_sensitivity);
            audio_settings_->setNoiseSuppressionEnabled(noise_suppression);
        }
    };

    connect(save_btn, &QPushButton::clicked, &dialog, [&]() {
        applySettings();
        saveSettings();
    });

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        applySettings();
        saveSettings();
        dialog.accept();
    });

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    int dialog_result = dialog.exec();

    // Always clean up monitoring state when dialog closes
#ifdef NEVO_HAS_BOOST
    if (client_core_) {
        client_core_->setMonitorEnabled(false);
    }
#endif

    if (dialog_result == QDialog::Accepted) {
        // Settings already applied and saved via accepted signal handler
        // Log the final values for reference
        float input_gain = settings->inputVolume() / 100.0f;
        float output_vol = settings->outputVolume() / 100.0f;
        int vad_sensitivity = settings->vadSensitivity();
        VoiceInputMode input_mode = settings->voiceInputMode();
        bool noise_suppression = settings->noiseSuppressionEnabled();
        QKeySequence ptt_key = settings->pttKeySequence();
        QString selected_input = settings->selectedInputDevice();
        QString selected_output = settings->selectedOutputDevice();

        NEVO_LOG_INFO("ui", "Audio settings applied: input_gain={}, output_volume={}, "
                     "input_mode={}, vad_sensitivity={}, noise_suppression={}, ptt_key={}, "
                     "input_device={}, output_device={}",
                     input_gain, output_vol, static_cast<int>(input_mode), vad_sensitivity, noise_suppression,
                     ptt_key.toString().toStdString(),
                     selected_input.toStdString(), selected_output.toStdString());
    }
#ifdef NEVO_HAS_BOOST
    else {
        // Cancel: restore original device selections
        if (client_core_) {
            if (!original_input_device.empty()) {
                client_core_->selectInputDeviceByName(original_input_device);
            }
            if (!original_output_device.empty()) {
                client_core_->selectOutputDeviceByName(original_output_device);
            }
        }
    }
#endif
}

void MainWindow::onBindOwnerAction()
{
    if (!client_core_) {
        QMessageBox::warning(this, tr("Not Connected"),
            tr("Please connect to a server first."));
        return;
    }

    OwnerBindDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString bind_key = dialog.bindKey().trimmed();
    if (bind_key.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Key"),
            tr("Bind key cannot be empty."));
        return;
    }

    // Set up callback for bind response
    client_core_->onOwnerBound = [this](bool success, const std::string& message) {
        postToUiThread([this, success, message]() {
            if (success) {
                QMessageBox::information(this, tr("Owner Bound"),
                    QString::fromStdString(message));
            } else {
                QMessageBox::warning(this, tr("Bind Failed"),
                    QString::fromStdString(message));
            }
        });
    };

    client_core_->sendBindOwnerRequest(bind_key.toStdString());
}

void MainWindow::onAboutAction()
{
    QDialog about_dialog(this);
    about_dialog.setWindowTitle(tr("About NEVO"));
    about_dialog.setFixedSize(420, 280);
    about_dialog.setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(&about_dialog);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    QLabel* title = new QLabel(tr("NEVO VoIP Client"), &about_dialog);
    QFont title_font = title->font();
    title_font.setPointSize(18);
    title_font.setBold(true);
    title->setFont(title_font);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    QLabel* desc = new QLabel(
        tr("A low-latency, encrypted VoIP client built with Qt 6, Boost.Asio, and Opus."),
        &about_dialog);
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignCenter);
    layout->addWidget(desc);

    QLabel* version = new QLabel(tr("Version 0.1.0"), &about_dialog);
    version->setAlignment(Qt::AlignCenter);
    layout->addWidget(version);

    layout->addStretch();

    QPushButton* ok_btn = new QPushButton(tr("OK"), &about_dialog);
    ok_btn->setFixedWidth(100);
    connect(ok_btn, &QPushButton::clicked, &about_dialog, &QDialog::accept);

    QHBoxLayout* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();
    btn_layout->addWidget(ok_btn);
    btn_layout->addStretch();
    layout->addLayout(btn_layout);

    about_dialog.exec();
}

// ============================================================
// UI ??
// ============================================================

void MainWindow::onJoinChannelRequested(ChannelId channel_id)
{
#ifdef NEVO_HAS_BOOST
    if (!client_core_->isConnected()) {
        QMessageBox::warning(this,
            tr("Not Connected"),
            tr("Please connect to a server first."));
        return;
    }

    // Guard: already in this channel
    if (client_core_->isInChannel()) {
        auto snapshot = client_core_->getState();
        if (snapshot.current_channel == channel_id) {
            statusBar()->showMessage(
                tr("Already in this channel."), 3000);
            return;
        }
    }

    boost::asio::co_spawn(*io_ctx_,
        [this, channel_id]() -> boost::asio::awaitable<void> {
            auto result = co_await client_core_->joinChannel(channel_id);
            if (!result) {
                QPointer<MainWindow> guard(this);
                QMetaObject::invokeMethod(this, [guard, result]() {
                    if (!guard) return;
                    QMessageBox::warning(guard,
                        MainWindow::tr("Join Channel Error"),
                        QString("Failed to join channel: %1")
                            .arg(QString::fromStdString(
                                result.error().message())));
                }, Qt::QueuedConnection);
            }
        },
        boost::asio::detached);
#else
    (void)channel_id;
    QMessageBox::information(this, tr("Join Channel"),
        tr("Client module not available (built without Boost)."));
#endif
}

void MainWindow::onLeaveChannelRequested()
{
#ifdef NEVO_HAS_BOOST
    if (!client_core_->isInChannel()) {
        statusBar()->showMessage(
            tr("Not in a channel."), 3000);
        return;
    }

    boost::asio::co_spawn(*io_ctx_,
        [this]() -> boost::asio::awaitable<void> {
            auto result = co_await client_core_->leaveChannel();
            if (!result) {
                QPointer<MainWindow> guard(this);
                QMetaObject::invokeMethod(this, [guard, result]() {
                    if (!guard) return;
                    QMessageBox::warning(guard,
                        MainWindow::tr("Leave Channel Error"),
                        QString("Failed to leave channel: %1")
                            .arg(QString::fromStdString(
                                result.error().message())));
                }, Qt::QueuedConnection);
            }
        },
        boost::asio::detached);
#else
    statusBar()->showMessage(
        tr("Client module not available (built without Boost)."), 3000);
#endif
}

void MainWindow::onConnectRequested(const QString& host, uint16_t port)
{
    LoginDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString username = dialog.username();

    if (username.isEmpty()) {
        QMessageBox::warning(this, tr("Login Error"),
                             tr("Username cannot be empty."));
        return;
    }

#ifdef NEVO_HAS_BOOST
    boost::asio::co_spawn(*io_ctx_,
        [this, host, port, username]() mutable
        -> boost::asio::awaitable<void> {
            auto result = co_await client_core_->connect(
                host.toStdString(), port,
                username.toStdString());

            if (!result) {
                QMetaObject::invokeMethod(this, [this, result]() {
                    QMessageBox::critical(this,
                        tr("Connection Error"),
                        QString("Failed to connect: %1")
                            .arg(QString::fromStdString(
                                result.error().message())));
                }, Qt::QueuedConnection);
            }
        },
        boost::asio::detached);
#else
    (void)host;
    (void)port;
    QMessageBox::information(this, tr("Connect"),
        tr("Client module not available (built without Boost)."));
#endif
}

void MainWindow::onDisconnectRequested()
{
#ifdef NEVO_HAS_BOOST
    client_core_->disconnect();
#else
    statusBar()->showMessage(tr("Disconnected"));
#endif
}

void MainWindow::onVolumeChanged(int volume)
{
    float vol = volume / 100.0f;
#ifdef NEVO_HAS_BOOST
    client_core_->audioEngine().setOutputVolume(vol);
#else
    (void)vol;
#endif
}

void MainWindow::onMuteToggled(bool checked)
{
#ifdef NEVO_HAS_BOOST
    client_core_->setMuted(checked);
    updateMicIndicator();
    NEVO_LOG_INFO("ui", "Mute toggled: {}", checked);
#else
    (void)checked;
#endif
}

void MainWindow::onDeafenedToggled(bool checked)
{
#ifdef NEVO_HAS_BOOST
    client_core_->setDeafened(checked);
    updateMicIndicator();
    NEVO_LOG_INFO("ui", "Deafen toggled: {}", checked);
#else
    (void)checked;
#endif
}

// ============================================================
// UI ??
// ============================================================

void MainWindow::setupUi()
{
    setWindowTitle(tr("NEVO VoIP Client"));
    resize(1000, 700);
    setMinimumSize(800, 500);

    channel_model_ = new ChannelTreeModel(this);
    user_model_ = new UserListModel(this);
    channel_delegate_ = new ChannelItemDelegate(this);

    setupDockWidgets();

    connection_bar_ = new ConnectionBar(this);
    setCentralWidget(connection_bar_);

    setupMenuBar();

    statusBar()->showMessage(tr("Ready"));
    statusBar()->setSizeGripEnabled(true);

    audio_settings_ = new AudioSettingsWidget(this);

    connect(channel_model_, &ChannelTreeModel::joinChannelRequested,
            this, &MainWindow::onJoinChannelRequested);
    connect(channel_model_, &ChannelTreeModel::leaveChannelRequested,
            this, &MainWindow::onLeaveChannelRequested);

    connect(connection_bar_, &ConnectionBar::connectRequested,
            this, &MainWindow::onConnectRequested);
    connect(connection_bar_, &ConnectionBar::disconnectRequested,
            this, &MainWindow::onDisconnectRequested);
    connect(connection_bar_, &ConnectionBar::volumeChanged,
            this, &MainWindow::onVolumeChanged);

    connect(channel_tree_, &QTreeView::doubleClicked,
            channel_model_, &ChannelTreeModel::onDoubleClicked);

#ifdef NEVO_HAS_BOOST
    setupClientCoreCallbacks();
#else
    loadMockChannelData();
#endif

    loadSettings();
}

void MainWindow::setupMenuBar()
{
    // File ??
    QMenu* file_menu = menuBar()->addMenu(tr("&File"));

    connect_action_ = file_menu->addAction(
        tr("&Connect..."));
    connect_action_->setShortcut(QKeySequence::New);
    connect(connect_action_, &QAction::triggered,
            this, &MainWindow::onConnectAction);

    disconnect_action_ = file_menu->addAction(
        tr("&Disconnect"));
    disconnect_action_->setShortcut(QKeySequence::Close);
    disconnect_action_->setEnabled(false);
    connect(disconnect_action_, &QAction::triggered,
            this, &MainWindow::onDisconnectAction);

    file_menu->addSeparator();

    // Mute / Deafen / PTT actions
    mute_action_ = file_menu->addAction(tr("M&ute"));
    mute_action_->setCheckable(true);
    mute_action_->setEnabled(false);
    connect(mute_action_, &QAction::toggled,
            this, &MainWindow::onMuteToggled);

    deafen_action_ = file_menu->addAction(
        tr("&Deafen"));
    deafen_action_->setCheckable(true);
    deafen_action_->setEnabled(false);
    connect(deafen_action_, &QAction::toggled,
            this, &MainWindow::onDeafenedToggled);

    file_menu->addSeparator();

    quit_action_ = file_menu->addAction(
        tr("&Quit"));
    quit_action_->setShortcut(QKeySequence::Quit);
    connect(quit_action_, &QAction::triggered,
            this, &MainWindow::onQuitAction);

    // Settings ??
    QMenu* settings_menu = menuBar()->addMenu(
        tr("&Settings"));

    audio_settings_action_ = settings_menu->addAction(
        tr("&Audio..."));
    connect(audio_settings_action_, &QAction::triggered,
            this, &MainWindow::onAudioSettingsAction);

    bind_owner_action_ = settings_menu->addAction(
        tr("Bind &Owner..."));
    bind_owner_action_->setEnabled(false);
    connect(bind_owner_action_, &QAction::triggered,
            this, &MainWindow::onBindOwnerAction);

    // Language sub-menu
    settings_menu->addSeparator();
    language_menu_ = settings_menu->addMenu(tr("&Language"));
    language_action_group_ = new QActionGroup(this);
    language_action_group_->setExclusive(true);

    // Read current language from settings
    QSettings lang_settings(QStringLiteral("NEVO"), QStringLiteral("NevoClient"));
    QString current_lang = lang_settings.value(QStringLiteral("language"), QStringLiteral("en")).toString();

    struct LangEntry { QString code; QString label; };
    LangEntry languages[] = {
        {QStringLiteral("en"),    QStringLiteral("English")},
        {QStringLiteral("zh_CN"), QStringLiteral("????")},
        {QStringLiteral("zh_TW"), QStringLiteral("????")},
    };

    for (const auto& lang : languages) {
        QAction* action = language_menu_->addAction(lang.label);
        action->setData(lang.code);
        action->setCheckable(true);
        action->setChecked(lang.code == current_lang);
        language_action_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, action]() {
            onLanguageChanged(action->data().toString());
        });
    }

    // Help ??
    QMenu* help_menu = menuBar()->addMenu(
        tr("&Help"));

    about_action_ = help_menu->addAction(
        tr("&About NEVO"));
    connect(about_action_, &QAction::triggered,
            this, &MainWindow::onAboutAction);

    // ?????
    top_toolbar_ = new QToolBar(this);
    top_toolbar_->setMovable(false);
    top_toolbar_->setFloatable(false);
    top_toolbar_->setIconSize(QSize(20, 20));
    top_toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    top_toolbar_->addAction(connect_action_);
    top_toolbar_->addAction(disconnect_action_);
    top_toolbar_->addSeparator();
    top_toolbar_->addAction(mute_action_);
    top_toolbar_->addAction(deafen_action_);
    top_toolbar_->addSeparator();
    top_toolbar_->addAction(audio_settings_action_);

    addToolBar(top_toolbar_);
}

void MainWindow::setupDockWidgets()
{
    // --- Channel Dock ---
    channel_dock_ = new QDockWidget(tr("Channels"), this);
    channel_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    channel_dock_->setFeatures(QDockWidget::DockWidgetMovable);

    // Container widget with vertical layout (tree + button row)
    QWidget* channel_container = new QWidget(channel_dock_);
    QVBoxLayout* channel_layout = new QVBoxLayout(channel_container);
    channel_layout->setContentsMargins(0, 0, 0, 0);
    channel_layout->setSpacing(0);

    channel_tree_ = new QTreeView(channel_container);
    channel_tree_->setModel(channel_model_);
    channel_tree_->setItemDelegate(channel_delegate_);
    channel_tree_->setHeaderHidden(true);
    channel_tree_->setAnimated(true);
    channel_tree_->setExpandsOnDoubleClick(true);
    channel_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    channel_tree_->setIndentation(20);
    channel_tree_->setUniformRowHeights(true);

    // Channel selection change -> update join/leave button states
    connect(channel_tree_, &QTreeView::customContextMenuRequested,
            this, &MainWindow::onChannelContextMenu);

    channel_layout->addWidget(channel_tree_, 1);

    // Button row: Join / Leave
    QHBoxLayout* channel_btn_layout = new QHBoxLayout();
    channel_btn_layout->setContentsMargins(8, 6, 8, 6);
    channel_btn_layout->setSpacing(8);

    join_channel_btn_ = new QPushButton(tr("Join Channel"), channel_container);
    join_channel_btn_->setEnabled(false);
    join_channel_btn_->setFixedHeight(32);
    join_channel_btn_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #5c8aff; color: #ffffff; border: none; "
        "border-radius: 6px; font-size: 13px; font-weight: 500; }"
        "QPushButton:hover { background-color: #7aa2ff; }"
        "QPushButton:pressed { background-color: #4a7aee; }"
        "QPushButton:disabled { background-color: #3a4048; color: #808080; }"));

    leave_channel_btn_ = new QPushButton(tr("Leave Channel"), channel_container);
    leave_channel_btn_->setEnabled(false);
    leave_channel_btn_->setFixedHeight(32);
    leave_channel_btn_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #f44336; color: #ffffff; border: none; "
        "border-radius: 6px; font-size: 13px; font-weight: 500; }"
        "QPushButton:hover { background-color: #ef5350; }"
        "QPushButton:pressed { background-color: #d32f2f; }"
        "QPushButton:disabled { background-color: #3a4048; color: #808080; }"));

    channel_btn_layout->addWidget(join_channel_btn_, 1);
    channel_btn_layout->addWidget(leave_channel_btn_, 1);
    channel_layout->addLayout(channel_btn_layout);

    channel_dock_->setWidget(channel_container);
    addDockWidget(Qt::LeftDockWidgetArea, channel_dock_);

    // Connect join/leave buttons
    connect(join_channel_btn_, &QPushButton::clicked, this, [this]() {
        QModelIndex index = channel_tree_->currentIndex();
        if (index.isValid()) {
            ChannelId ch_id = channel_model_->channelIdFromIndex(index);
            if (ch_id) {
                onJoinChannelRequested(ch_id);
            }
        }
    });
    connect(leave_channel_btn_, &QPushButton::clicked,
            this, &MainWindow::onLeaveChannelRequested);

    // Connect selection change to update button states
    connect(channel_tree_, &QTreeView::clicked,
            this, &MainWindow::onChannelSelectionChanged);

    // --- User Dock ---
    user_dock_ = new QDockWidget(tr("Users"), this);
    user_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    user_dock_->setFeatures(QDockWidget::DockWidgetMovable);

    // Container with current channel label + user list
    QWidget* user_container = new QWidget(user_dock_);
    QVBoxLayout* user_layout = new QVBoxLayout(user_container);
    user_layout->setContentsMargins(0, 0, 0, 0);
    user_layout->setSpacing(0);

    current_channel_label_ = new QLabel(tr("Not in a channel"), user_container);
    current_channel_label_->setStyleSheet(QStringLiteral(
        "color: #a0a8b8; padding: 8px 10px; font-size: 12px; "
        "background-color: #1e2227; border-bottom: 1px solid #2c3138;"));
    user_layout->addWidget(current_channel_label_);

    user_list_ = new QListView(user_container);
    user_list_->setModel(user_model_);
    user_list_->setSelectionMode(QAbstractItemView::NoSelection);
    user_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    user_list_->setUniformItemSizes(true);

    connect(user_list_, &QListView::customContextMenuRequested,
            this, &MainWindow::onUserContextMenu);

    user_layout->addWidget(user_list_, 1);

    user_dock_->setWidget(user_container);
    addDockWidget(Qt::RightDockWidgetArea, user_dock_);
}

#ifdef NEVO_HAS_BOOST
void MainWindow::setupClientCoreCallbacks()
{
    client_core_->onStateChanged =
        [this](ClientState new_state, ClientState old_state) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, new_state, old_state]() {
                if (!guard) return;
                guard->connection_bar_->updateConnectionState(toConnectionState(new_state));

                bool connected = (new_state == ClientState::Connected ||
                                  new_state == ClientState::InChannel);
                guard->connect_action_->setEnabled(!connected);
                guard->disconnect_action_->setEnabled(connected);
                guard->mute_action_->setEnabled(connected);
                guard->deafen_action_->setEnabled(connected);
                guard->bind_owner_action_->setEnabled(connected);

                // ?????????????????????????
                if (connected) {
                    guard->updateMicIndicator();
                    guard->mic_level_timer_->start();
                } else {
                    guard->mic_level_timer_->stop();
                    guard->connection_bar_->updateMicStatus(false, false);
                }

                // Update join/leave button states
                guard->leave_channel_btn_->setEnabled(
                    new_state == ClientState::InChannel);
                guard->onChannelSelectionChanged();

                // Update current channel label
                if (new_state == ClientState::InChannel) {
                    auto snapshot = guard->client_core_->getState();
                    guard->channel_model_->setCurrentChannel(snapshot.current_channel);
                    guard->current_channel_label_->setText(
                        MainWindow::tr("Channel: %1")
                            .arg(QString::fromStdString(snapshot.current_channel_name)));
                    guard->current_channel_label_->setStyleSheet(QStringLiteral(
                        "color: #4caf50; padding: 8px 10px; font-size: 12px; font-weight: 500; "
                        "background-color: #1e2227; border-bottom: 1px solid #2c3138;"));
                    guard->statusBar()->showMessage(
                        MainWindow::tr("Joined channel: %1")
                            .arg(QString::fromStdString(snapshot.current_channel_name)));
                } else if (old_state == ClientState::InChannel &&
                           new_state == ClientState::Connected) {
                    guard->channel_model_->clearCurrentChannel();
                    guard->current_channel_label_->setText(MainWindow::tr("Not in a channel"));
                    guard->current_channel_label_->setStyleSheet(QStringLiteral(
                        "color: #a0a8b8; padding: 8px 10px; font-size: 12px; "
                        "background-color: #1e2227; border-bottom: 1px solid #2c3138;"));
                    guard->statusBar()->showMessage(
                        MainWindow::tr("Left channel"));
                } else if (new_state == ClientState::Disconnected) {
                    guard->channel_model_->clearCurrentChannel();
                    guard->current_channel_label_->setText(MainWindow::tr("Not in a channel"));
                    guard->current_channel_label_->setStyleSheet(QStringLiteral(
                        "color: #a0a8b8; padding: 8px 10px; font-size: 12px; "
                        "background-color: #1e2227; border-bottom: 1px solid #2c3138;"));
                    guard->statusBar()->showMessage(
                        MainWindow::tr("State: %1")
                            .arg(QString::fromStdString(clientStateToString(new_state))));
                } else {
                    guard->statusBar()->showMessage(
                        MainWindow::tr("State: %1")
                            .arg(QString::fromStdString(clientStateToString(new_state))));
                }

                NEVO_LOG_INFO("ui", "Client state changed to: {}",
                             clientStateToString(new_state));
            });
        };

    client_core_->onUserJoined =
        [this](const User& user) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, user]() {
                if (!guard) return;
                auto snapshot = guard->client_core_->getState();
                std::vector<UserInfo> user_infos;
                user_infos.reserve(snapshot.channel_users.size());
                for (const auto& u : snapshot.channel_users) {
                    user_infos.emplace_back(u);
                }
                guard->user_model_->updateUserList(user_infos);

                // Update user count for current channel
                guard->channel_model_->setChannelUserCount(
                    snapshot.current_channel,
                    static_cast<int>(snapshot.channel_users.size()));

                guard->statusBar()->showMessage(
                    QString("User joined: %1")
                        .arg(QString::fromStdString(user.username())));
            });
        };

    client_core_->onUserLeft =
        [this](UserId user_id) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, user_id]() {
                if (!guard) return;
                auto snapshot = guard->client_core_->getState();
                std::vector<UserInfo> user_infos;
                user_infos.reserve(snapshot.channel_users.size());
                for (const auto& u : snapshot.channel_users) {
                    user_infos.emplace_back(u);
                }
                guard->user_model_->updateUserList(user_infos);

                // Update user count for current channel
                guard->channel_model_->setChannelUserCount(
                    snapshot.current_channel,
                    static_cast<int>(snapshot.channel_users.size()));

                guard->statusBar()->showMessage(
                    QString("User left: %1").arg(user_id.value));
            });
        };

    client_core_->onUserSpeaking =
        [this](UserId user_id, bool is_speaking) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, user_id, is_speaking]() {
                if (!guard) return;
                guard->user_model_->setSpeakingUser(user_id, is_speaking);
            });
        };

    client_core_->onServerMessage =
        [this](const std::string& message) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, message]() {
                if (!guard) return;
                guard->statusBar()->showMessage(
                    QString::fromStdString(message), 5000);
            });
        };

    client_core_->onChannelList =
        [this](const std::vector<ChannelInfo>& channels) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, channels]() {
                if (!guard) return;
                guard->channel_model_->updateFromChannelList(channels);
            });
        };

    client_core_->onLatencyUpdate =
        [this](int latency_ms) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, latency_ms]() {
                if (!guard) return;
                guard->connection_bar_->updateLatency(latency_ms);

                auto snapshot = guard->client_core_->getState();
                guard->connection_bar_->updateNatType(snapshot.nat_type);
            });
        };

    client_core_->onError =
        [this](ResultCode code, const std::string& message) {
            QPointer<MainWindow> guard(this);
            postToUiThread([guard, code, message]() {
                if (!guard) return;
                QMessageBox::warning(guard,
                    MainWindow::tr("Error"),
                    QString("Error (%1): %2")
                        .arg(static_cast<int>(code))
                        .arg(QString::fromStdString(message)));
            });
        };

    client_core_->onOwnerBindRequired = [this]() {
        QPointer<MainWindow> guard(this);
        postToUiThread([guard]() {
            if (!guard) return;
            guard->onBindOwnerAction();
        });
    };
}
#endif // NEVO_HAS_BOOST

template<typename Functor>
void MainWindow::postToUiThread(Functor&& functor)
{
    QMetaObject::invokeMethod(this, std::forward<Functor>(functor),
                              Qt::QueuedConnection);
}

void MainWindow::onChannelContextMenu(const QPoint& pos)
{
    QModelIndex index = channel_tree_->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu context_menu(this);
    ChannelId channel_id = channel_model_->channelIdFromIndex(index);
    bool is_current = index.data(IsCurrentChannelRole).toBool();

    if (is_current) {
        QAction* leave_action = context_menu.addAction(
            tr("Leave Channel"));
        if (context_menu.exec(channel_tree_->viewport()->mapToGlobal(pos)) == leave_action) {
            onLeaveChannelRequested();
        }
    } else {
        QAction* join_action = context_menu.addAction(
            tr("Join Channel"));
        if (context_menu.exec(channel_tree_->viewport()->mapToGlobal(pos)) == join_action) {
            onJoinChannelRequested(channel_id);
        }
    }
}

void MainWindow::onChannelSelectionChanged()
{
#ifdef NEVO_HAS_BOOST
    if (!client_core_) return;

    QModelIndex index = channel_tree_->currentIndex();
    bool can_join = false;

    if (index.isValid() && client_core_->isConnected()) {
        ChannelId ch_id = channel_model_->channelIdFromIndex(index);
        if (ch_id) {
            // Can join if not already in this channel
            if (client_core_->isInChannel()) {
                auto snapshot = client_core_->getState();
                can_join = (snapshot.current_channel != ch_id);
            } else {
                can_join = true;
            }
        }
    }

    join_channel_btn_->setEnabled(can_join);
    leave_channel_btn_->setEnabled(client_core_->isInChannel());
#else
    join_channel_btn_->setEnabled(false);
    leave_channel_btn_->setEnabled(false);
#endif
}

void MainWindow::onUserContextMenu(const QPoint& pos)
{
    QModelIndex index = user_list_->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu context_menu(this);

    QString username = index.data(Qt::DisplayRole).toString();

    QAction* info_action = context_menu.addAction(
        tr("View Info: %1").arg(username));

    QAction* selected = context_menu.exec(user_list_->viewport()->mapToGlobal(pos));

    if (selected == info_action) {
#ifdef NEVO_HAS_BOOST
        auto snapshot = client_core_->getState();
        for (const auto& user : snapshot.channel_users) {
            if (QString::fromStdString(user.username()) == username) {
                QMessageBox::information(this,
                    tr("User Info"),
                    tr("Username: %1\nID: %2\nMuted: %3\nSpeaking: %4")
                        .arg(username)
                        .arg(user.id().value)
                        .arg(user.isMuted() ? tr("Yes") : tr("No"))
                        .arg(user.isSpeaking() ? tr("Yes") : tr("No")));
                break;
            }
        }
#else
        (void)info_action;
#endif
    }
}

// ============================================================
// ??
// ============================================================

void MainWindow::saveSettings() {
    QSettings settings("NEVO", "NEVOClient");

    // ?????
    if (connection_bar_) {
        settings.setValue("server/address", connection_bar_->serverAddress());
        settings.setValue("server/port", connection_bar_->serverPort());
    }

    // ????
    if (audio_settings_) {
        settings.setValue("audio/inputVolume", audio_settings_->inputVolume());
        settings.setValue("audio/outputVolume", audio_settings_->outputVolume());
        settings.setValue("audio/inputMode", static_cast<int>(audio_settings_->voiceInputMode()));
        settings.setValue("audio/vadSensitivity", audio_settings_->vadSensitivity());
        settings.setValue("audio/noiseSuppression", audio_settings_->noiseSuppressionEnabled());
    }

    // PTT ????
    settings.setValue("audio/pttKey", ptt_key_.toString());

    NEVO_LOG_DEBUG("ui", "Settings saved");
}

void MainWindow::loadSettings() {
    QSettings settings("NEVO", "NEVOClient");

    // ?????
    if (connection_bar_) {
        QString address = settings.value("server/address", QString()).toString();
        int port = settings.value("server/port", 24430).toInt();
        if (!address.isEmpty()) {
            connection_bar_->setServerAddress(address);
        }
        connection_bar_->setServerPort(static_cast<uint16_t>(port));
    }

    // ????
    if (audio_settings_) {
        int inputVol = settings.value("audio/inputVolume", 80).toInt();
        int outputVol = settings.value("audio/outputVolume", 100).toInt();
        // ???????? vadEnabled/pttEnabled???? inputMode
        int inputMode = settings.value("audio/inputMode", -1).toInt();
        if (inputMode < 0) {
            // ??????? vadEnabled/pttEnabled ????
            bool vadEnabled = settings.value("audio/vadEnabled", true).toBool();
            inputMode = vadEnabled ? static_cast<int>(VoiceInputMode::Vad)
                                   : static_cast<int>(VoiceInputMode::Ptt);
        }
        bool noiseSup = settings.value("audio/noiseSuppression", false).toBool();
        int vadSens = settings.value("audio/vadSensitivity", 50).toInt();

        audio_settings_->setInputVolume(inputVol);
        audio_settings_->setOutputVolume(outputVol);
        audio_settings_->setVoiceInputMode(static_cast<VoiceInputMode>(inputMode));
        audio_settings_->setNoiseSuppressionEnabled(noiseSup);
        audio_settings_->setVadSensitivity(vadSens);
    }

    // ?? PTT ??
    QString pttKeyStr = settings.value("audio/pttKey", QString()).toString();
    if (!pttKeyStr.isEmpty()) {
        ptt_key_ = QKeySequence(pttKeyStr);
    }
#ifdef NEVO_HAS_BOOST
    if (client_core_) {
        if (audio_settings_) {
            bool is_vad = (audio_settings_->voiceInputMode() == VoiceInputMode::Vad);
            client_core_->audioEngine().setVadEnabled(is_vad);
            client_core_->audioEngine().setVadSensitivity(audio_settings_->vadSensitivity());
            client_core_->audioEngine().setPttEnabled(!is_vad && !ptt_key_.isEmpty());
            client_core_->audioEngine().setNoiseSuppressionEnabled(audio_settings_->noiseSuppressionEnabled());
        }
    }
#endif

    NEVO_LOG_DEBUG("ui", "Settings loaded");
}

void MainWindow::loadMockChannelData()
{
    std::vector<ChannelInfo> mock_channels = {
        ChannelInfo(ChannelId(1), "Root", ChannelId(0)),
        ChannelInfo(ChannelId(2), "General", ChannelId(1)),
        ChannelInfo(ChannelId(3), "Chat", ChannelId(2)),
        ChannelInfo(ChannelId(4), "Gaming", ChannelId(2)),
        ChannelInfo(ChannelId(5), "Music", ChannelId(1)),
        ChannelInfo(ChannelId(6), "AFK", ChannelId(1)),
        ChannelInfo(ChannelId(7), "Development", ChannelId(1)),
        ChannelInfo(ChannelId(8), "Frontend", ChannelId(7)),
        ChannelInfo(ChannelId(9), "Backend", ChannelId(7)),
    };

    channel_model_->updateFromChannelList(mock_channels);
    channel_tree_->expandAll();

    // Simulate user counts
    channel_model_->setChannelUserCount(ChannelId(3), 5);
    channel_model_->setChannelUserCount(ChannelId(4), 12);
    channel_model_->setChannelUserCount(ChannelId(5), 2);
    channel_model_->setChannelUserCount(ChannelId(8), 3);
    channel_model_->setChannelUserCount(ChannelId(9), 1);

    statusBar()->showMessage(
        tr("Demo mode - Mock channel data loaded"), 5000);
}

// ============================================================
// i18n: retranslateUi & changeEvent
// ============================================================

void MainWindow::retranslateUi()
{
    // Menu actions
    if (connect_action_) connect_action_->setText(tr("&Connect..."));
    if (disconnect_action_) disconnect_action_->setText(tr("&Disconnect"));
    if (mute_action_) mute_action_->setText(tr("M&ute"));
    if (deafen_action_) deafen_action_->setText(tr("&Deafen"));
    if (quit_action_) quit_action_->setText(tr("&Quit"));
    if (audio_settings_action_) audio_settings_action_->setText(tr("&Audio..."));
    if (about_action_) about_action_->setText(tr("&About NEVO"));

    // Menu titles
    QList<QMenu*> menus = menuBar()->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    if (menus.size() >= 3) {
        menus[0]->setTitle(tr("&File"));
        menus[1]->setTitle(tr("&Settings"));
        menus[2]->setTitle(tr("&Help"));
    }

    // Language submenu title
    if (language_menu_) language_menu_->setTitle(tr("&Language"));

    // Dock widgets
    if (channel_dock_) channel_dock_->setWindowTitle(tr("Channels"));
    if (user_dock_) user_dock_->setWindowTitle(tr("Users"));

    // Channel action buttons
    if (join_channel_btn_) join_channel_btn_->setText(tr("Join Channel"));
    if (leave_channel_btn_) leave_channel_btn_->setText(tr("Leave Channel"));

    // Current channel label
#ifdef NEVO_HAS_BOOST
    if (current_channel_label_ && client_core_) {
        if (client_core_->isInChannel()) {
            auto snapshot = client_core_->getState();
            current_channel_label_->setText(
                tr("Channel: %1")
                    .arg(QString::fromStdString(snapshot.current_channel_name)));
        } else {
            current_channel_label_->setText(tr("Not in a channel"));
        }
    }
#else
    if (current_channel_label_) current_channel_label_->setText(tr("Not in a channel"));
#endif

    // Forward to child widgets
    if (connection_bar_) connection_bar_->retranslateUi();
}

void MainWindow::updateMicIndicator()
{
#ifdef NEVO_HAS_BOOST
    if (!client_core_ || !connection_bar_) return;

    bool muted = (mute_action_ && mute_action_->isChecked()) ||
                 (deafen_action_ && deafen_action_->isChecked());
    if (muted) {
        connection_bar_->updateMicStatus(true, false);
        return;
    }

    bool is_vad = true;
    if (audio_settings_) {
        is_vad = (audio_settings_->voiceInputMode() == VoiceInputMode::Vad);
    }

    if (is_vad) {
        // VAD ???????????
        float level = client_core_->audioEngine().getCurrentInputLevel();
        bool audio_active = (level >= kMicLevelThreshold);
        connection_bar_->updateMicStatus(false, audio_active);
    } else {
        // PTT ?????????????????
        if (ptt_key_pressed_) {
            float level = client_core_->audioEngine().getCurrentInputLevel();
            bool audio_active = (level >= kMicLevelThreshold);
            connection_bar_->updateMicStatus(false, audio_active);
        } else {
            // ??? PTT?????
            connection_bar_->updateMicStatus(false, false);
        }
    }
#endif
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
#ifdef NEVO_HAS_BOOST
    if (!ptt_key_.isEmpty() && client_core_) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (!ke->isAutoRepeat()) {
                QKeySequence pressed(ke->key() | ke->modifiers());
                if (pressed == ptt_key_ && !ptt_key_pressed_) {
                    ptt_key_pressed_ = true;
                    client_core_->setPttActive(true);
                    updateMicIndicator();
                }
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (!ke->isAutoRepeat()) {
                QKeySequence released(ke->key() | ke->modifiers());
                if (released == ptt_key_ && ptt_key_pressed_) {
                    ptt_key_pressed_ = false;
                    client_core_->setPttActive(false);
                    updateMicIndicator();
                }
            }
        }
    }
#else
    (void)obj;
    (void)event;
#endif
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onLanguageChanged(const QString& lang_code)
{
    // Save preference
    QSettings settings(QStringLiteral("NEVO"), QStringLiteral("NevoClient"));
    settings.setValue(QStringLiteral("language"), lang_code);

    // Remove and delete old translator
    QTranslator* old = qApp->property("nevoTranslator").value<QTranslator*>();
    QByteArray* old_qm_data = qApp->property("nevoQmData").value<QByteArray*>();
    if (old) {
        QCoreApplication::removeTranslator(old);
        delete old;
    }
    if (old_qm_data) {
        delete old_qm_data;
        qApp->setProperty("nevoQmData", QVariant());
    }

    // Load new translator from Qt resources
    // Construct the explicit resource path and use QFile + load(data, size)
    // to avoid QTranslator::load() locale-based overload which uses uiLanguages()
    // returning BCP47 tags (e.g. "zh-Hans-CN") that don't match our POSIX-named
    // .qm files (e.g. "nevo_client_zh_CN.qm").
    QString qm_path = QStringLiteral(":/i18n/nevo_client_%1.qm").arg(lang_code);
    QTranslator* translator = new QTranslator(this);

    QFile qm_file(qm_path);
    if (qm_file.open(QIODevice::ReadOnly)) {
        qm_data_ = qm_file.readAll();
        if (translator->load(reinterpret_cast<const uchar*>(qm_data_.constData()),
                             qm_data_.size())) {
            qApp->installTranslator(translator);
            qApp->setProperty("nevoTranslator", QVariant::fromValue(translator));
            NEVO_LOG_INFO("ui", "Language switched to: {}", lang_code.toStdString());
        } else {
            delete translator;
            qm_data_.clear();
            NEVO_LOG_WARN("ui", "Failed to parse translation file: {}", lang_code.toStdString());
        }
    } else {
        delete translator;
        NEVO_LOG_WARN("ui", "Failed to open translation file: {}", qm_path.toStdString());
    }
}

} // namespace nevo
