/**
 * @file ServerMainWindow.cpp
 * @brief Server GUI main window implementation
 */

#include "nevo/server/ui/ServerMainWindow.h"

#include <QVBoxLayout>
#include <QSplitter>
#include <QTimer>
#include <QMessageBox>
#include <QCloseEvent>
#include <QTableView>
#include <QStatusBar>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTranslator>
#include <QApplication>
#include <QFile>

#include "nevo/server/ui/ServerConfigPanel.h"
#include "nevo/server/ui/ServerStatusBar.h"
#include "nevo/server/ui/ServerLogView.h"
#include "nevo/server/ui/ChannelPanel.h"
#include "nevo/ui/ThemeManager.h"
#include "nevo/core/common/Logger.h"

namespace nevo {

// ============================================================
// Construction / Destruction
// ============================================================

ServerMainWindow::ServerMainWindow(const ServerConfig& config,
                                   const std::string& config_path,
                                   QWidget* parent)
    : QMainWindow(parent)
    , io_ctx_(std::make_unique<boost::asio::io_context>())
    , server_core_(std::make_unique<ServerCore>(*io_ctx_, config.tcp_port, config.udp_port))
    , config_(config)
    , config_path_(config_path)
    , running_(false)
    , refresh_timer_(nullptr)
    , config_panel_(nullptr)
    , session_model_(nullptr)
    , session_table_(nullptr)
    , status_bar_(nullptr)
    , log_view_(nullptr)
    , channel_panel_(nullptr)
    , status_label_(nullptr)
    , start_action_(nullptr)
    , stop_action_(nullptr)
    , disconnect_all_action_(nullptr)
    , about_action_(nullptr)
    , quit_action_(nullptr)
{
    setupUi();
    setupCallbacks();

    NEVO_LOG_INFO("server_gui", "ServerMainWindow created (tcp={}, udp={})", config_.tcp_port, config_.udp_port);
}

ServerMainWindow::~ServerMainWindow() {
    if (running_) {
        stopServer();
    }

    // Remove and delete any translator that was installed by onLanguageChanged.
    // This MUST happen before the base class destructor destroys child QObjects,
    // because QCoreApplication still holds a raw pointer to the translator.
    QTranslator* translator = qApp->property("nevoServerTranslator").value<QTranslator*>();
    if (translator) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
        qApp->setProperty("nevoServerTranslator", QVariant());
    }
    // Also clean up any QByteArray* that main() stored
    QByteArray* old_qm_data = qApp->property("nevoServerQmData").value<QByteArray*>();
    if (old_qm_data) {
        delete old_qm_data;
        qApp->setProperty("nevoServerQmData", QVariant());
    }

    for (auto& t : io_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }

    NEVO_LOG_INFO("server_gui", "ServerMainWindow destroyed");
}

// ============================================================
// Public slots
// ============================================================

void ServerMainWindow::onStartServer() {
    if (running_) {
        return;
    }

    auto init_result = server_core_->initialize(config_.db_path);
    if (!init_result) {
        QMessageBox::critical(this, tr("Initialization Error"),
            QString::fromStdString(init_result.error().message()));
        return;
    }

    server_core_->start();
    running_ = true;

    start_action_->setEnabled(false);
    stop_action_->setEnabled(true);
    disconnect_all_action_->setEnabled(true);
    statusBar()->showMessage(tr("Server running"));

    // Launch io_context worker threads
    uint32_t thread_count = static_cast<uint32_t>(config_.threads);
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) thread_count = 4;
    }
    io_threads_.reserve(thread_count);
    for (uint32_t i = 0; i < thread_count; ++i) {
        io_threads_.emplace_back([this]() {
            try {
                io_ctx_->run();
            } catch (const std::exception& e) {
                NEVO_LOG_ERROR("server_gui", "IO thread exception: {}", e.what());
            }
        });
    }

    status_bar_->setRunning(true);
    config_panel_->setServerRunning(true);

    // Provide server core to channel panel for operations
    if (channel_panel_) {
        channel_panel_->setServerCore(server_core_.get());
    }

    NEVO_LOG_INFO("server_gui", "Server started from GUI");
}

void ServerMainWindow::onStopServer() {
    stopServer();
}

void ServerMainWindow::onDisconnectAll() {
    if (running_ && server_core_) {
        server_core_->shutdown();
        running_ = false;
    }
}

void ServerMainWindow::onQuitAction() {
    close();
}

void ServerMainWindow::onRefreshSnapshot() {
    if (!running_ || !server_core_) {
        return;
    }

    auto snapshot = server_core_->getStatusSnapshot();

    // Update models
    session_model_->updateFromSnapshot(snapshot);

    // Update channel panel
    if (channel_panel_) {
        channel_panel_->updateFromSnapshot(snapshot);
    }

    // Update status bar
    status_bar_->updateSnapshot(snapshot);

    // Update window title
    updateWindowTitle();
}

void ServerMainWindow::onLogMessage(const QString& msg) {
    if (log_view_) {
        log_view_->appendLog(msg);
    }
}

void ServerMainWindow::onAboutAction() {
    QMessageBox::about(this,
        tr("About NEVO Server"),
        tr("NEVO VoIP Server\n"
           "A low-latency, encrypted VoIP server.\n"
           "Built with Qt 6, Boost.Asio, and SQLite3."));
}

// ============================================================
// Events
// ============================================================

void ServerMainWindow::closeEvent(QCloseEvent* event) {
    if (running_) {
        auto reply = QMessageBox::question(this,
            tr("Confirm Exit"),
            tr("Server is still running. Stop server and exit?"),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }

        stopServer();
    }

    event->accept();
}

// ============================================================
// Internal methods
// ============================================================

void ServerMainWindow::setupUi() {
    setWindowTitle(QString::fromStdString(config_.server_name));
    resize(1200, 800);
    setMinimumSize(900, 600);

    // Central widget: vertical layout with config panel on top and splitter below
    QWidget* central_widget = new QWidget(this);
    setCentralWidget(central_widget);

    QVBoxLayout* main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // --- Top: Configuration panel ---
    config_panel_ = new ServerConfigPanel(this);
    config_panel_->setConfig(config_);
    connect(config_panel_, &ServerConfigPanel::applyRequested,
            this, &ServerMainWindow::onApplyConfig);
    connect(config_panel_, &ServerConfigPanel::saveRequested,
            this, &ServerMainWindow::onSaveConfig);
    main_layout->addWidget(config_panel_);

    // --- Bottom: horizontal splitter ---
    QSplitter* central_splitter = new QSplitter(Qt::Horizontal, this);

    // --- Left: session table ---
    session_model_ = new SessionTableModel(this);
    session_table_ = new QTableView(this);
    session_table_->setModel(session_model_);
    session_table_->setSelectionMode(QAbstractItemView::NoSelection);
    session_table_->setAlternatingRowColors(true);
    session_table_->verticalHeader()->setVisible(false);
    session_table_->horizontalHeader()->setStretchLastSection(true);
    session_table_->setColumnWidth(0, 80);   // User ID
    session_table_->setColumnWidth(1, 140);  // Username
    session_table_->setColumnWidth(2, 160);  // Address
    session_table_->setColumnWidth(3, 100);  // Status
    session_table_->setStyleSheet(
        QStringLiteral("QTableView { border: none; gridline-color: transparent; }"
                       "QTableView::item { padding: 6px; }"));

    QWidget* session_panel = new QWidget(this);
    QVBoxLayout* session_layout = new QVBoxLayout(session_panel);
    session_layout->setContentsMargins(0, 0, 0, 0);
    session_layout->addWidget(session_table_);
    central_splitter->addWidget(session_panel);

    // --- Right: vertical splitter with channel panel and log view ---
    QSplitter* right_splitter = new QSplitter(Qt::Vertical, this);

    // Channel management panel (top-right)
    channel_panel_ = new ChannelPanel(this);
    channel_panel_->setMinimumHeight(120);
    connect(channel_panel_, &ChannelPanel::channelOperationCompleted,
            this, [this](const QString& msg) {
        statusBar()->showMessage(msg);
    });
    right_splitter->addWidget(channel_panel_);

    // Log view (bottom-right)
    log_view_ = new ServerLogView(this);
    log_view_->setMinimumHeight(100);
    right_splitter->addWidget(log_view_);

    // Set initial proportions for right splitter (60% channels, 40% log)
    right_splitter->setSizes(QList<int>() << 300 << 200);

    central_splitter->addWidget(right_splitter);

    main_layout->addWidget(central_splitter, 1);

    // Splitter proportions
    central_splitter->setSizes(QList<int>() << 700 << 500);

    // Bottom status bar widget
    status_bar_ = new ServerStatusBar(this);
    statusBar()->addPermanentWidget(status_bar_, 1);

    // Menu bar
    setupMenuBar();

    // Periodic refresh timer (every 1 second)
    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &ServerMainWindow::onRefreshSnapshot);
    refresh_timer_->start(1000);
}

void ServerMainWindow::setupMenuBar() {
    // Server menu
    server_menu_ = menuBar()->addMenu(tr("&Server"));

    start_action_ = server_menu_->addAction(tr("&Start"));
    start_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    connect(start_action_, &QAction::triggered, this, &ServerMainWindow::onStartServer);

    stop_action_ = server_menu_->addAction(tr("S&top"));
    stop_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    stop_action_->setEnabled(false);
    connect(stop_action_, &QAction::triggered, this, &ServerMainWindow::onStopServer);

    disconnect_all_action_ = server_menu_->addAction(
        tr("Disconnect &All"));
    connect(disconnect_all_action_, &QAction::triggered,
            this, &ServerMainWindow::onDisconnectAll);
    disconnect_all_action_->setEnabled(false);

    server_menu_->addSeparator();

    quit_action_ = server_menu_->addAction(tr("&Quit"));
    quit_action_->setShortcut(QKeySequence::Quit);
    connect(quit_action_, &QAction::triggered, this, &QWidget::close);

    // Settings menu
    settings_menu_ = menuBar()->addMenu(tr("&Settings"));

    // Language sub-menu
    language_menu_ = settings_menu_->addMenu(tr("&Language"));
    language_action_group_ = new QActionGroup(this);
    language_action_group_->setExclusive(true);

    QSettings lang_settings(QStringLiteral("NEVO"), QStringLiteral("NevoServer"));
    QString current_lang = lang_settings.value(QStringLiteral("language"), QStringLiteral("en")).toString();

    struct LangEntry { QString code; QString label; };
    LangEntry languages[] = {
        {QStringLiteral("en"),    QStringLiteral("English")},
        {QStringLiteral("zh_CN"), QStringLiteral("????")},
        {QStringLiteral("zh_TW"), QStringLiteral("繝體中文")},
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

    // Help menu
    help_menu_ = menuBar()->addMenu(tr("&Help"));

    about_action_ = help_menu_->addAction(tr("&About"));
    connect(about_action_, &QAction::triggered, this, &ServerMainWindow::onAboutAction);
}

void ServerMainWindow::setupCallbacks() {
    server_core_->onServerStateChanged = [this](bool running) {
        QMetaObject::invokeMethod(this, [this, running]() {
            status_bar_->setRunning(running);
        }, Qt::QueuedConnection);
    };

    server_core_->onLogMessage = [this](const std::string& level, const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, level, msg]() {
            QString prefix = QString::fromStdString(level);
            prefix = prefix.toUpper();
            QString text = QStringLiteral("[%1] %2").arg(prefix, QString::fromStdString(msg));
            onLogMessage(text);
        }, Qt::QueuedConnection);
    };

    server_core_->onOwnerBound = [this](UserId user_id, const std::string& username) {
        QMetaObject::invokeMethod(this, [this, user_id, username]() {
            QMessageBox::information(this,
                tr("Owner Bound"),
                tr("User %1 (ID: %2) has successfully bound as server owner.")
                    .arg(QString::fromStdString(username))
                    .arg(static_cast<qulonglong>(user_id.value)));
        }, Qt::QueuedConnection);
    };
}

void ServerMainWindow::stopServer() {
    if (!running_) {
        return;
    }

    NEVO_LOG_INFO("server_gui", "Stopping server from GUI...");

    server_core_->shutdown();

    if (io_ctx_) {
        io_ctx_->stop();
    }

    for (auto& t : io_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    io_threads_.clear();

    running_ = false;
    start_action_->setEnabled(true);
    stop_action_->setEnabled(false);
    disconnect_all_action_->setEnabled(false);

    status_bar_->setRunning(false);
    config_panel_->setServerRunning(false);
    statusBar()->showMessage(tr("Server stopped"));
    setWindowTitle(QString::fromStdString(config_.server_name));

    NEVO_LOG_INFO("server_gui", "Server stopped");
}

void ServerMainWindow::onApplyConfig() {
    auto new_config = config_panel_->getConfig();

    // Validate the new config
    auto vr = new_config.validate();
    if (!vr) {
        QMessageBox::warning(this, tr("Invalid Configuration"),
            QString::fromStdString(vr.error().message()));
        return;
    }

    bool ports_changed = (new_config.tcp_port != config_.tcp_port ||
                          new_config.udp_port != config_.udp_port);
    bool name_changed = (new_config.server_name != config_.server_name);
    bool restart_fields_changed = (new_config.threads != config_.threads);

    // Apply hot-applicable changes to running server immediately
    if (running_ && server_core_) {
        if (new_config.max_users != config_.max_users) {
            server_core_->setMaxUsers(new_config.max_users);
        }
        if (new_config.welcome_message != config_.welcome_message) {
            server_core_->setWelcomeMessage(new_config.welcome_message);
        }
        if (new_config.log_level != config_.log_level) {
            server_core_->setLogLevel(new_config.log_level);
        }
    }

    config_ = new_config;

    if (name_changed) {
        updateWindowTitle();
        status_bar_->setServerName(QString::fromStdString(config_.server_name));
    }

    if (ports_changed && running_) {
        NEVO_LOG_INFO("server_gui", "Port change detected (TCP:{}, UDP:{}), restarting server...",
                      config_.tcp_port, config_.udp_port);
        recreateServerCore();
    } else if ((ports_changed || restart_fields_changed) && !running_) {
        statusBar()->showMessage(
            tr("Configuration updated �?some changes will apply on next start"));
    } else {
        statusBar()->showMessage(tr("Configuration applied"));
    }
}

void ServerMainWindow::onSaveConfig() {
    auto panel_config = config_panel_->getConfig();

    // Validate before saving
    auto vr = panel_config.validate();
    if (!vr) {
        QMessageBox::warning(this, tr("Invalid Configuration"),
            QString::fromStdString(vr.error().message()));
        return;
    }

    config_ = panel_config;

    if (config_path_.empty()) {
        config_path_ = "server_config.json";
    }

    if (config_.saveToFile(config_path_)) {
        statusBar()->showMessage(
            tr("Configuration saved to %1").arg(QString::fromStdString(config_path_)));
        NEVO_LOG_INFO("server_gui", "Config saved to {}", config_path_);
    } else {
        QMessageBox::warning(this, tr("Save Error"),
            tr("Failed to save configuration to %1")
                .arg(QString::fromStdString(config_path_)));
        NEVO_LOG_ERROR("server_gui", "Failed to save config to {}", config_path_);
    }
}

void ServerMainWindow::recreateServerCore() {
    bool was_running = running_;
    if (was_running) {
        stopServer();
    }

    // Create a fresh io_context (the old one was stopped)
    io_ctx_ = std::make_unique<boost::asio::io_context>();

    // Recreate ServerCore with updated ports
    server_core_ = std::make_unique<ServerCore>(*io_ctx_, config_.tcp_port, config_.udp_port);
    setupCallbacks();

    NEVO_LOG_INFO("server_gui", "ServerCore recreated with TCP:{} UDP:{}",
                  config_.tcp_port, config_.udp_port);

    if (was_running) {
        onStartServer();
    }
}

void ServerMainWindow::updateWindowTitle() {
    if (running_ && server_core_) {
        auto snapshot = server_core_->getStatusSnapshot();
        setWindowTitle(tr("%1 - %2 clients")
            .arg(QString::fromStdString(config_.server_name))
            .arg(static_cast<int>(snapshot.active_sessions)));
    } else {
        setWindowTitle(QString::fromStdString(config_.server_name));
    }
}

void ServerMainWindow::retranslateUi()
{
    // Menu titles
    if (server_menu_) server_menu_->setTitle(tr("&Server"));
    if (settings_menu_) settings_menu_->setTitle(tr("&Settings"));
    if (help_menu_) help_menu_->setTitle(tr("&Help"));
    if (language_menu_) language_menu_->setTitle(tr("&Language"));

    // Menu actions
    if (start_action_) start_action_->setText(tr("&Start"));
    if (stop_action_) stop_action_->setText(tr("S&top"));
    if (disconnect_all_action_) disconnect_all_action_->setText(tr("Disconnect &All"));
    if (about_action_) about_action_->setText(tr("&About"));
    if (quit_action_) quit_action_->setText(tr("&Quit"));

    // Propagate to child widgets
    if (config_panel_) config_panel_->retranslateUi();
    if (session_model_) session_model_->invalidateHeaders();
    if (status_bar_) status_bar_->retranslateUi();
    if (channel_panel_) channel_panel_->retranslateUi();
}

void ServerMainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void ServerMainWindow::onLanguageChanged(const QString& lang_code)
{
    // Save preference
    QSettings settings(QStringLiteral("NEVO"), QStringLiteral("NevoServer"));
    settings.setValue(QStringLiteral("language"), lang_code);

    // Remove and delete the old translator that was installed by a previous
    // call to this function (or by main() via qApp property).
    QTranslator* old = qApp->property("nevoServerTranslator").value<QTranslator*>();
    if (old) {
        QCoreApplication::removeTranslator(old);
        delete old;
        qApp->setProperty("nevoServerTranslator", QVariant());
    }

    // Also clean up the QByteArray* that main() may have stored for the
    // initial translator's backing data.  After the first language switch,
    // we use the member variable qm_data_ instead, so the heap-allocated
    // QByteArray from main() is no longer needed.
    QByteArray* old_qm_data = qApp->property("nevoServerQmData").value<QByteArray*>();
    if (old_qm_data) {
        delete old_qm_data;
        qApp->setProperty("nevoServerQmData", QVariant());
    }

    // Load new translator from Qt resources.
    // NOTE: Do NOT pass `this` as the QTranslator parent. QCoreApplication::installTranslator()
    // does not take ownership; if the translator's parent is a QWidget that gets destroyed before
    // the QCoreApplication, the dangling pointer causes a use-after-free.
    // Instead, give no parent and manage lifetime via qApp property + explicit delete.
    QTranslator* translator = new QTranslator();  // no parent �?we manage lifetime ourselves

    QString qm_path = QStringLiteral(":/i18n/nevo_server_%1.qm").arg(lang_code);
    QFile qm_file(qm_path);
    if (qm_file.open(QIODevice::ReadOnly)) {
        qm_data_ = qm_file.readAll();
        if (translator->load(reinterpret_cast<const uchar*>(qm_data_.constData()),
                             static_cast<int>(qm_data_.size()))) {
            qApp->installTranslator(translator);
            qApp->setProperty("nevoServerTranslator", QVariant::fromValue(translator));
            NEVO_LOG_INFO("server_gui", "Language switched to: {}", lang_code.toStdString());
        } else {
            delete translator;
            qm_data_.clear();
            NEVO_LOG_WARN("server_gui", "Failed to parse translation file: {}", lang_code.toStdString());
        }
    } else {
        delete translator;
        NEVO_LOG_WARN("server_gui", "Failed to open translation file: {}", qm_path.toStdString());
    }
}

} // namespace nevo
