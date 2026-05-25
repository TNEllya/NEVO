#pragma once
/**
 * @file ServerConfigPanel.h
 * @brief Server configuration panel widget
 *
 * Provides input controls for server name, TCP port, UDP port,
 * max users, welcome message, log level, threads,
 * and buttons to apply or save the configuration.
 */

#include <QFrame>
#include <QCheckBox>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QGroupBox;

namespace nevo {

struct ServerConfig;

class ServerConfigPanel : public QFrame {
    Q_OBJECT

public:
    explicit ServerConfigPanel(QWidget* parent = nullptr);
    ~ServerConfigPanel() override;

    /// Populate fields from a ServerConfig struct
    void setConfig(const ServerConfig& config);

    /// Read current field values into a ServerConfig struct
    ServerConfig getConfig() const;

    /// When server is running, disable restart-required fields
    void setServerRunning(bool running);

    /// Retranslate UI strings for dynamic language switching
    void retranslateUi();

signals:
    void applyRequested();
    void saveRequested();

private:
    void setupUi();

    // Network group
    QLineEdit* name_edit_;
    QSpinBox* tcp_port_spin_;
    QSpinBox* udp_port_spin_;
    QGroupBox* network_group_;
    QLabel* name_label_;
    QLabel* tcp_label_;
    QLabel* tcp_restart_label_;
    QLabel* udp_label_;
    QLabel* udp_restart_label_;

    // Advanced group
    QSpinBox* max_users_spin_;
    QLineEdit* welcome_edit_;
    QComboBox* log_level_combo_;
    QSpinBox* threads_spin_;
    QGroupBox* adv_group_;
    QLabel* max_users_label_;
    QLabel* welcome_label_;
    QLabel* log_level_label_;
    QLabel* threads_label_;
    QLabel* threads_restart_label_;

    // Buttons & status
    QPushButton* apply_btn_;
    QPushButton* save_btn_;
    QLabel* hint_label_;

    // File Transfer group
    QGroupBox* ft_group_;
    QLabel* ft_title_label_;
    QCheckBox* limit_upload_check_;
    QLabel* limit_upload_desc_;
    QCheckBox* limit_download_check_;
    QLabel* limit_download_desc_;
    QLabel* max_upload_label_;
    QSpinBox* max_upload_spin_;
    QLabel* max_download_label_;
    QSpinBox* max_download_spin_;

protected:
    void changeEvent(QEvent* event) override;
};

} // namespace nevo
