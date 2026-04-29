#pragma once
/**
 * @file ServerMainWindow.h
 * @brief Server GUI main window
 *
 * Combines all monitoring widgets and provides server control UI.
 */

#include <QMainWindow>
#include <QTimer>
#include <QActionGroup>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "nevo/server/ServerCore.h"
#include "nevo/server/ServerConfig.h"
#include "nevo/server/ui/SessionTableModel.h"

class QTableView;
class QLabel;
class QAction;
class QCloseEvent;
class QMenu;

namespace nevo {

class ServerStatusBar;
class ServerLogView;
class ServerConfigPanel;
class ChannelPanel;

/**
 * @class ServerMainWindow
 * @brief Main window for the NEVO Server GUI
 */
class ServerMainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param config      Server configuration
     * @param config_path Path to config file (for saving), may be empty
     * @param parent       Parent widget
     */
    explicit ServerMainWindow(const ServerConfig& config,
                              const std::string& config_path = "",
                              QWidget* parent = nullptr);

    /// Destructor
    ~ServerMainWindow() override;

    // Disable copy
    ServerMainWindow(const ServerMainWindow&) = delete;
    ServerMainWindow& operator=(const ServerMainWindow&) = delete;

private slots:
    /// Periodic status refresh
    void onRefreshSnapshot();

    /// Menu: Start server
    void onStartServer();

    /// Menu: Stop server
    void onStopServer();

    /// Menu: Disconnect all clients
    void onDisconnectAll();

    /// Menu: About
    void onAboutAction();

    /// Menu: Quit
    void onQuitAction();

    /// Log message handler
    void onLogMessage(const QString& msg);

    /// Apply configuration from the config panel
    void onApplyConfig();

    /// Save configuration to file
    void onSaveConfig();

    /// Language switching
    void onLanguageChanged(const QString& lang_code);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setupUi();
    void setupMenuBar();
    void setupCallbacks();
    void stopServer();
    void recreateServerCore();
    void updateWindowTitle();
    void retranslateUi();

    /// IO context (owned here)
    std::unique_ptr<boost::asio::io_context> io_ctx_;

    /// Server core instance
    std::unique_ptr<ServerCore> server_core_;

    /// Current server configuration
    ServerConfig config_;

    /// Path to config file (empty if not loaded from file)
    std::string config_path_;

    /// Whether server is running
    bool running_ = false;

    /// IO worker threads
    std::vector<std::thread> io_threads_;

    /// Refresh timer
    QTimer* refresh_timer_;

    /// Configuration panel
    ServerConfigPanel* config_panel_;

    /// Session table model
    SessionTableModel* session_model_;

    /// Session table view
    QTableView* session_table_;

    /// Status bar widget
    ServerStatusBar* status_bar_;

    /// Log view
    ServerLogView* log_view_;

    /// Channel management panel
    ChannelPanel* channel_panel_;

    /// Status label
    QLabel* status_label_;

    /// Menu actions
    QAction* start_action_;
    QAction* stop_action_;
    QAction* disconnect_all_action_;
    QAction* about_action_;
    QAction* quit_action_;

    // --- Menus ---
    QMenu* server_menu_ = nullptr;
    QMenu* settings_menu_ = nullptr;
    QMenu* help_menu_ = nullptr;
    QMenu* language_menu_ = nullptr;
    QActionGroup* language_action_group_ = nullptr;
    QByteArray qm_data_;
};

} // namespace nevo
