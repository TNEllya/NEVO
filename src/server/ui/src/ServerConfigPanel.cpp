/**
 * @file ServerConfigPanel.cpp
 * @brief Server configuration panel implementation
 */

#include "nevo/server/ui/ServerConfigPanel.h"
#include "nevo/server/ServerConfig.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QGroupBox>
#include <QEvent>

namespace nevo {

// Common widget styles
static const char* kInputStyle =
    "QLineEdit, QSpinBox, QComboBox {"
    "  background-color: #282c34;"
    "  color: #c5c8d4;"
    "  border: 1px solid #3a3f4b;"
    "  border-radius: 4px;"
    "  padding: 4px 8px;"
    "  font-size: 12px;"
    "}"
    "QLineEdit:focus, QSpinBox:focus, QComboBox:focus {"
    "  border-color: #61afef;"
    "}"
    "QComboBox::drop-down { border: none; }"
    "QComboBox::down-arrow { image: none; border: none; }"
    "QComboBox QAbstractItemView {"
    "  background-color: #282c34;"
    "  color: #c5c8d4;"
    "  selection-background-color: #2d5aa0;"
    "}";

static const char* kLabelStyle = "color: #a0a8b8; font-size: 12px;";
static const char* kRestartBadge = "color: #e0a040; font-size: 10px;";

ServerConfigPanel::ServerConfigPanel(QWidget* parent)
    : QFrame(parent)
    , name_edit_(nullptr)
    , tcp_port_spin_(nullptr)
    , udp_port_spin_(nullptr)
    , network_group_(nullptr)
    , name_label_(nullptr)
    , tcp_label_(nullptr)
    , tcp_restart_label_(nullptr)
    , udp_label_(nullptr)
    , udp_restart_label_(nullptr)
    , max_users_spin_(nullptr)
    , welcome_edit_(nullptr)
    , log_level_combo_(nullptr)
    , threads_spin_(nullptr)
    , adv_group_(nullptr)
    , max_users_label_(nullptr)
    , welcome_label_(nullptr)
    , log_level_label_(nullptr)
    , threads_label_(nullptr)
    , threads_restart_label_(nullptr)
    , apply_btn_(nullptr)
    , save_btn_(nullptr)
    , hint_label_(nullptr)
    , ft_group_(nullptr)
    , ft_title_label_(nullptr)
    , limit_upload_check_(nullptr)
    , limit_upload_desc_(nullptr)
    , limit_download_check_(nullptr)
    , limit_download_desc_(nullptr)
    , max_upload_label_(nullptr)
    , max_upload_spin_(nullptr)
    , max_download_label_(nullptr)
    , max_download_spin_(nullptr)
{
    setupUi();
}

ServerConfigPanel::~ServerConfigPanel() = default;

void ServerConfigPanel::setConfig(const ServerConfig& config) {
    name_edit_->setText(QString::fromStdString(config.server_name));
    tcp_port_spin_->setValue(config.tcp_port);
    udp_port_spin_->setValue(config.udp_port);
    max_users_spin_->setValue(config.max_users);
    welcome_edit_->setText(QString::fromStdString(config.welcome_message));

    int idx = log_level_combo_->findText(QString::fromStdString(config.log_level));
    if (idx >= 0) {
        log_level_combo_->setCurrentIndex(idx);
    }
    threads_spin_->setValue(config.threads);

    if (limit_upload_check_) limit_upload_check_->setChecked(config.file_transfer.limit_upload_speed);
    if (limit_download_check_) limit_download_check_->setChecked(config.file_transfer.limit_download_speed);
    if (max_upload_spin_) max_upload_spin_->setValue(config.file_transfer.max_concurrent_uploads);
    if (max_download_spin_) max_download_spin_->setValue(config.file_transfer.max_concurrent_downloads);
}

ServerConfig ServerConfigPanel::getConfig() const {
    ServerConfig config;
    config.server_name = name_edit_->text().trimmed().toStdString();
    if (config.server_name.empty()) {
        config.server_name = "NEVO Server";
    }
    config.tcp_port = static_cast<uint16_t>(tcp_port_spin_->value());
    config.udp_port = static_cast<uint16_t>(udp_port_spin_->value());
    config.max_users = max_users_spin_->value();
    config.welcome_message = welcome_edit_->text().toStdString();
    config.log_level = log_level_combo_->currentText().toStdString();
    config.threads = threads_spin_->value();

    config.file_transfer.limit_upload_speed = limit_upload_check_ ? limit_upload_check_->isChecked() : false;
    config.file_transfer.limit_download_speed = limit_download_check_ ? limit_download_check_->isChecked() : false;
    config.file_transfer.max_concurrent_uploads = max_upload_spin_ ? max_upload_spin_->value() : 3;
    config.file_transfer.max_concurrent_downloads = max_download_spin_ ? max_download_spin_->value() : 3;

    return config;
}

void ServerConfigPanel::setServerRunning(bool running) {
    // Restart-required fields: ports and threads
    tcp_port_spin_->setEnabled(!running);
    udp_port_spin_->setEnabled(!running);
    threads_spin_->setEnabled(!running);

    if (running) {
        hint_label_->setText(
            tr("Server is running — port and thread changes will take effect after restart"));
        hint_label_->setStyleSheet(QStringLiteral("color: #e0a040; font-size: 11px;"));
    } else {
        hint_label_->setText(QString());
    }
}

void ServerConfigPanel::setupUi() {
    setStyleSheet(QStringLiteral(
        "ServerConfigPanel {"
        "  background-color: #1e2229;"
        "  border: 1px solid #2c3138;"
        "  border-radius: 8px;"
        "}"
    ));

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(16, 12, 16, 12);
    main_layout->setSpacing(8);

    // Title
    QLabel* title = new QLabel(tr("Server Configuration"), this);
    title->setStyleSheet(QStringLiteral("color: #c5c8d4; font-weight: bold; font-size: 13px;"));
    main_layout->addWidget(title);

    // ---- Network group ----
    network_group_ = new QGroupBox(tr("Network"), this);
    network_group_->setStyleSheet(QStringLiteral(
        "QGroupBox { color: #8fa0c0; font-weight: bold; font-size: 12px; border: 1px solid #2c3138; border-radius: 6px; margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
    ));

    QGridLayout* net_grid = new QGridLayout(network_group_);
    net_grid->setHorizontalSpacing(12);
    net_grid->setVerticalSpacing(6);

    int row = 0;

    // Server Name
    name_label_ = new QLabel(tr("Server Name:"), this);
    name_label_->setStyleSheet(kLabelStyle);
    net_grid->addWidget(name_label_, row, 0);
    name_edit_ = new QLineEdit(this);
    name_edit_->setPlaceholderText(tr("Enter server name"));
    name_edit_->setStyleSheet(kInputStyle);
    net_grid->addWidget(name_edit_, row, 1, 1, 3);

    ++row;
    // TCP Port
    tcp_label_ = new QLabel(tr("TCP Port:"), this);
    tcp_label_->setStyleSheet(kLabelStyle);
    net_grid->addWidget(tcp_label_, row, 0);
    tcp_port_spin_ = new QSpinBox(this);
    tcp_port_spin_->setRange(1, 65535);
    tcp_port_spin_->setValue(24430);
    tcp_port_spin_->setStyleSheet(kInputStyle);
    net_grid->addWidget(tcp_port_spin_, row, 1);
    tcp_restart_label_ = new QLabel(tr("* Restart required"), this);
    tcp_restart_label_->setStyleSheet(kRestartBadge);
    net_grid->addWidget(tcp_restart_label_, row, 2);

    ++row;
    // UDP Port
    udp_label_ = new QLabel(tr("UDP Port:"), this);
    udp_label_->setStyleSheet(kLabelStyle);
    net_grid->addWidget(udp_label_, row, 0);
    udp_port_spin_ = new QSpinBox(this);
    udp_port_spin_->setRange(1, 65535);
    udp_port_spin_->setValue(24431);
    udp_port_spin_->setStyleSheet(kInputStyle);
    net_grid->addWidget(udp_port_spin_, row, 1);
    udp_restart_label_ = new QLabel(tr("* Restart required"), this);
    udp_restart_label_->setStyleSheet(kRestartBadge);
    net_grid->addWidget(udp_restart_label_, row, 2);

    main_layout->addWidget(network_group_);

    // ---- Advanced group ----
    adv_group_ = new QGroupBox(tr("Advanced"), this);
    adv_group_->setStyleSheet(QStringLiteral(
        "QGroupBox { color: #8fa0c0; font-weight: bold; font-size: 12px; border: 1px solid #2c3138; border-radius: 6px; margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
    ));

    QGridLayout* adv_grid = new QGridLayout(adv_group_);
    adv_grid->setHorizontalSpacing(12);
    adv_grid->setVerticalSpacing(6);

    row = 0;

    // Max Users
    max_users_label_ = new QLabel(tr("Max Users:"), this);
    max_users_label_->setStyleSheet(kLabelStyle);
    adv_grid->addWidget(max_users_label_, row, 0);
    max_users_spin_ = new QSpinBox(this);
    max_users_spin_->setRange(1, 10000);
    max_users_spin_->setValue(100);
    max_users_spin_->setStyleSheet(kInputStyle);
    adv_grid->addWidget(max_users_spin_, row, 1);

    ++row;
    // Welcome Message
    welcome_label_ = new QLabel(tr("Welcome Message:"), this);
    welcome_label_->setStyleSheet(kLabelStyle);
    adv_grid->addWidget(welcome_label_, row, 0);
    welcome_edit_ = new QLineEdit(this);
    welcome_edit_->setPlaceholderText(tr("Welcome message for new users"));
    welcome_edit_->setStyleSheet(kInputStyle);
    adv_grid->addWidget(welcome_edit_, row, 1, 1, 3);

    ++row;
    // Log Level
    log_level_label_ = new QLabel(tr("Log Level:"), this);
    log_level_label_->setStyleSheet(kLabelStyle);
    adv_grid->addWidget(log_level_label_, row, 0);
    log_level_combo_ = new QComboBox(this);
    log_level_combo_->addItems(QStringList()
        << QStringLiteral("debug") << QStringLiteral("info")
        << QStringLiteral("warn") << QStringLiteral("error"));
    log_level_combo_->setCurrentIndex(1); // info
    log_level_combo_->setStyleSheet(kInputStyle);
    adv_grid->addWidget(log_level_combo_, row, 1);

    ++row;
    // Threads
    threads_label_ = new QLabel(tr("IO Threads:"), this);
    threads_label_->setStyleSheet(kLabelStyle);
    adv_grid->addWidget(threads_label_, row, 0);
    threads_spin_ = new QSpinBox(this);
    threads_spin_->setRange(1, 64);
    threads_spin_->setValue(4);
    threads_spin_->setStyleSheet(kInputStyle);
    adv_grid->addWidget(threads_spin_, row, 1);
    threads_restart_label_ = new QLabel(tr("* Restart required"), this);
    threads_restart_label_->setStyleSheet(kRestartBadge);
    adv_grid->addWidget(threads_restart_label_, row, 2);

    main_layout->addWidget(adv_group_);

    // ---- File Transfer group ----
    ft_group_ = new QGroupBox(tr("Transfer Limits"), this);
    ft_group_->setStyleSheet(QStringLiteral(
        "QGroupBox { color: #8fa0c0; font-weight: bold; font-size: 12px; border: 1px solid #2c3138; border-radius: 6px; margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
    ));

    QVBoxLayout* ft_layout = new QVBoxLayout(ft_group_);
    ft_layout->setSpacing(10);
    ft_layout->setContentsMargins(12, 20, 12, 12);

    // Limit upload speed
    QHBoxLayout* upload_limit_row = new QHBoxLayout();
    upload_limit_row->setSpacing(8);
    limit_upload_check_ = new QCheckBox(tr("Limit Upload Speed"), this);
    limit_upload_check_->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #c5c8d4; font-size: 12px; spacing: 6px; }"
        "QCheckBox::indicator { width: 36px; height: 18px; border-radius: 9px; "
        "  background-color: #3a3f4b; border: none; }"
        "QCheckBox::indicator:checked { background-color: #4a9eff; }"
        "QCheckBox::indicator::handle { width: 14px; height: 14px; border-radius: 7px; "
        "  background-color: #c5c8d4; margin: 2px; }"
        "QCheckBox::indicator:checked::handle { background-color: #ffffff; margin-left: 20px; }"
    ));
    upload_limit_row->addWidget(limit_upload_check_);
    upload_limit_row->addStretch();
    ft_layout->addLayout(upload_limit_row);

    limit_upload_desc_ = new QLabel(tr("Whether to limit bandwidth used for uploads"), this);
    limit_upload_desc_->setStyleSheet(QStringLiteral("color: #6d7078; font-size: 11px; padding-left: 4px;"));
    ft_layout->addWidget(limit_upload_desc_);

    // Limit download speed
    QHBoxLayout* download_limit_row = new QHBoxLayout();
    download_limit_row->setSpacing(8);
    limit_download_check_ = new QCheckBox(tr("Limit Download Speed"), this);
    limit_download_check_->setStyleSheet(limit_upload_check_->styleSheet());
    download_limit_row->addWidget(limit_download_check_);
    download_limit_row->addStretch();
    ft_layout->addLayout(download_limit_row);

    limit_download_desc_ = new QLabel(tr("Whether to limit bandwidth used for downloads"), this);
    limit_download_desc_->setStyleSheet(QStringLiteral("color: #6d7078; font-size: 11px; padding-left: 4px;"));
    ft_layout->addWidget(limit_download_desc_);

    // Concurrent uploads
    QHBoxLayout* upload_concurrent_row = new QHBoxLayout();
    upload_concurrent_row->setSpacing(8);
    max_upload_label_ = new QLabel(tr("Concurrent Uploads"), this);
    max_upload_label_->setStyleSheet(kLabelStyle);
    max_upload_label_->setFixedWidth(140);
    upload_concurrent_row->addWidget(max_upload_label_);
    max_upload_spin_ = new QSpinBox(this);
    max_upload_spin_->setRange(1, 20);
    max_upload_spin_->setValue(3);
    max_upload_spin_->setFixedWidth(80);
    max_upload_spin_->setStyleSheet(kInputStyle);
    upload_concurrent_row->addWidget(max_upload_spin_);
    upload_concurrent_row->addStretch();
    ft_layout->addLayout(upload_concurrent_row);

    QLabel* upload_concurrent_desc = new QLabel(tr("Limit concurrent uploads per server"), this);
    upload_concurrent_desc->setStyleSheet(QStringLiteral("color: #6d7078; font-size: 11px; padding-left: 4px;"));
    ft_layout->addWidget(upload_concurrent_desc);

    // Concurrent downloads
    QHBoxLayout* download_concurrent_row = new QHBoxLayout();
    download_concurrent_row->setSpacing(8);
    max_download_label_ = new QLabel(tr("Concurrent Downloads"), this);
    max_download_label_->setStyleSheet(kLabelStyle);
    max_download_label_->setFixedWidth(140);
    download_concurrent_row->addWidget(max_download_label_);
    max_download_spin_ = new QSpinBox(this);
    max_download_spin_->setRange(1, 20);
    max_download_spin_->setValue(3);
    max_download_spin_->setFixedWidth(80);
    max_download_spin_->setStyleSheet(kInputStyle);
    download_concurrent_row->addWidget(max_download_spin_);
    download_concurrent_row->addStretch();
    ft_layout->addLayout(download_concurrent_row);

    QLabel* download_concurrent_desc = new QLabel(tr("Limit concurrent downloads per server"), this);
    download_concurrent_desc->setStyleSheet(QStringLiteral("color: #6d7078; font-size: 11px; padding-left: 4px;"));
    ft_layout->addWidget(download_concurrent_desc);

    main_layout->addWidget(ft_group_);

    // ---- Bottom row: buttons + hint ----
    QHBoxLayout* bottom_row = new QHBoxLayout();
    bottom_row->setSpacing(12);

    apply_btn_ = new QPushButton(tr("Apply"), this);
    apply_btn_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #2d5aa0;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 6px 20px;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #3a6fc4; }"
        "QPushButton:pressed { background-color: #1e4a8a; }"
    ));
    connect(apply_btn_, &QPushButton::clicked, this, &ServerConfigPanel::applyRequested);
    bottom_row->addWidget(apply_btn_);

    save_btn_ = new QPushButton(tr("Save Config"), this);
    save_btn_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #3a3f4b;"
        "  color: #c5c8d4;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 6px 20px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover { background-color: #4a5060; }"
        "QPushButton:pressed { background-color: #2a2f3b; }"
    ));
    connect(save_btn_, &QPushButton::clicked, this, &ServerConfigPanel::saveRequested);
    bottom_row->addWidget(save_btn_);

    hint_label_ = new QLabel(this);
    hint_label_->setStyleSheet(QStringLiteral("color: #e0a040; font-size: 11px;"));
    bottom_row->addWidget(hint_label_, 1);

    main_layout->addLayout(bottom_row);
}

void ServerConfigPanel::retranslateUi()
{
    // Group box titles
    if (network_group_) network_group_->setTitle(tr("Network"));
    if (adv_group_) adv_group_->setTitle(tr("Advanced"));

    // Labels
    if (name_label_) name_label_->setText(tr("Server Name:"));
    if (tcp_label_) tcp_label_->setText(tr("TCP Port:"));
    if (tcp_restart_label_) tcp_restart_label_->setText(tr("* Restart required"));
    if (udp_label_) udp_label_->setText(tr("UDP Port:"));
    if (udp_restart_label_) udp_restart_label_->setText(tr("* Restart required"));
    if (max_users_label_) max_users_label_->setText(tr("Max Users:"));
    if (welcome_label_) welcome_label_->setText(tr("Welcome Message:"));
    if (log_level_label_) log_level_label_->setText(tr("Log Level:"));
    if (threads_label_) threads_label_->setText(tr("IO Threads:"));
    if (threads_restart_label_) threads_restart_label_->setText(tr("* Restart required"));

    // Placeholder text
    if (name_edit_) name_edit_->setPlaceholderText(tr("Enter server name"));
    if (welcome_edit_) welcome_edit_->setPlaceholderText(tr("Welcome message for new users"));

    // Button text
    if (apply_btn_) apply_btn_->setText(tr("Apply"));
    if (save_btn_) save_btn_->setText(tr("Save Config"));

    // Hint label (if server is running, update the hint text)
    if (hint_label_ && !hint_label_->text().isEmpty()) {
        hint_label_->setText(tr("Server is running — port and thread changes will take effect after restart"));
    }

    // File Transfer group
    if (ft_group_) ft_group_->setTitle(tr("Transfer Limits"));
    if (limit_upload_check_) limit_upload_check_->setText(tr("Limit Upload Speed"));
    if (limit_upload_desc_) limit_upload_desc_->setText(tr("Whether to limit bandwidth used for uploads"));
    if (limit_download_check_) limit_download_check_->setText(tr("Limit Download Speed"));
    if (limit_download_desc_) limit_download_desc_->setText(tr("Whether to limit bandwidth used for downloads"));
    if (max_upload_label_) max_upload_label_->setText(tr("Concurrent Uploads"));
    if (max_download_label_) max_download_label_->setText(tr("Concurrent Downloads"));
}

void ServerConfigPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QFrame::changeEvent(event);
}

} // namespace nevo
