/**
 * @file ServerStatusBar.cpp
 * @brief ServerStatusBar 实现 - 服务器状态栏
 */

#include "nevo/server/ui/ServerStatusBar.h"

#include <QHBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QRadialGradient>
#include <QEvent>

namespace nevo {

ServerStatusBar::ServerStatusBar(QWidget* parent)
    : QFrame(parent)
    , status_indicator_(nullptr)
    , server_label_(nullptr)
    , clients_label_(nullptr)
    , packets_label_(nullptr)
    , uptime_label_(nullptr)
    , is_running_(false)
    , server_name_(tr("NEVO Server"))
    , last_snapshot_()
{
    setupUi();
}

ServerStatusBar::~ServerStatusBar() = default;

void ServerStatusBar::setRunning(bool running)
{
    is_running_ = running;
    if (running) {
        status_indicator_->setPixmap(createIndicatorPixmap(QColor(80, 200, 120), 16));
        status_indicator_->setToolTip(tr("Running"));
        server_label_->setStyleSheet(QStringLiteral("color: #80c878; font-weight: bold; font-size: 13px;"));
    } else {
        status_indicator_->setPixmap(createIndicatorPixmap(QColor(220, 80, 80), 16));
        status_indicator_->setToolTip(tr("Stopped"));
        server_label_->setStyleSheet(QStringLiteral("color: #dc5050; font-weight: bold; font-size: 13px;"));
    }
    server_label_->setText(server_name_);
}

void ServerStatusBar::setServerName(const QString& name)
{
    server_name_ = name;
    server_label_->setText(server_name_);
}

void ServerStatusBar::updateSnapshot(const ServerStatusSnapshot& snapshot)
{
    last_snapshot_ = snapshot;

    clients_label_->setText(
        tr("Clients: %1 / %2 auth")
            .arg(snapshot.active_sessions)
            .arg(snapshot.authenticated_users));

    packets_label_->setText(
        tr("Relayed: %1").arg(snapshot.packets_relayed));

    // Uptime formatting
    uint64_t uptime = snapshot.uptime_seconds;
    int hours = static_cast<int>(uptime / 3600);
    int minutes = static_cast<int>((uptime % 3600) / 60);
    int seconds = static_cast<int>(uptime % 60);
    uptime_label_->setText(
        tr("Uptime: %1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));
}

void ServerStatusBar::setupUi()
{
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "ServerStatusBar {"
        "  background-color: #1e2229;"
        "  border: 1px solid #2c3138;"
        "  border-radius: 10px;"
        "}"
    ));

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(16);

    // Status indicator
    status_indicator_ = new QLabel(this);
    status_indicator_->setFixedSize(16, 16);
    status_indicator_->setPixmap(createIndicatorPixmap(QColor(220, 80, 80), 16));
    layout->addWidget(status_indicator_);

    // Server label
    server_label_ = new QLabel(server_name_, this);
    server_label_->setStyleSheet(QStringLiteral("color: #dc5050; font-weight: bold; font-size: 13px;"));
    layout->addWidget(server_label_);

    // Separator
    QFrame* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet(QStringLiteral("color: #2c3138;"));
    layout->addWidget(sep);

    // Clients
    clients_label_ = new QLabel(tr("Clients: 0 / 0 auth"), this);
    clients_label_->setStyleSheet(QStringLiteral("color: #a0a8b8; font-size: 12px;"));
    layout->addWidget(clients_label_);

    // Packets
    packets_label_ = new QLabel(tr("Relayed: 0"), this);
    packets_label_->setStyleSheet(QStringLiteral("color: #a0a8b8; font-size: 12px;"));
    layout->addWidget(packets_label_);

    // Uptime
    uptime_label_ = new QLabel(tr("Uptime: 00:00:00"), this);
    uptime_label_->setStyleSheet(QStringLiteral("color: #a0a8b8; font-size: 12px;"));
    layout->addWidget(uptime_label_);

    layout->addStretch();
}

void ServerStatusBar::retranslateUi()
{
    // Re-apply running/stopped state to update tooltips
    setRunning(is_running_);

    // Re-apply last snapshot to update labels with new translations
    updateSnapshot(last_snapshot_);
}

void ServerStatusBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QFrame::changeEvent(event);
}

QPixmap ServerStatusBar::createIndicatorPixmap(const QColor& color, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Glow
    QRadialGradient glow(size / 2.0, size / 2.0, size / 2.0 + 2);
    QColor glowColor = color;
    glowColor.setAlpha(80);
    glow.setColorAt(0, glowColor);
    glowColor.setAlpha(0);
    glow.setColorAt(1, glowColor);
    painter.setBrush(glow);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, size, size);

    // Main circle
    painter.setBrush(color);
    painter.drawEllipse(2, 2, size - 4, size - 4);

    // Highlight
    QColor hl = color.lighter(150);
    hl.setAlpha(180);
    painter.setBrush(hl);
    painter.drawEllipse(4, 4, (size - 4) / 2, (size - 4) / 2);

    return pixmap;
}

} // namespace nevo
