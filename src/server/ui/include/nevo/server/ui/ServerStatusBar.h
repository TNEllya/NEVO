#pragma once
/**
 * @file ServerStatusBar.h
 * @brief Server status bar widget
 */

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>

#include "nevo/server/ServerCore.h"

namespace nevo {

class ServerStatusBar : public QFrame {
    Q_OBJECT

public:
    explicit ServerStatusBar(QWidget* parent = nullptr);
    ~ServerStatusBar() override;

    void setRunning(bool running);
    void setServerName(const QString& name);
    void updateSnapshot(const ServerStatusSnapshot& snapshot);

    /// Retranslate UI strings for dynamic language switching
    void retranslateUi();

signals:
    void startRequested();
    void stopRequested();

private:
    void setupUi();
    QPixmap createIndicatorPixmap(const QColor& color, int size);

protected:
    void changeEvent(QEvent* event) override;

    QLabel* status_indicator_;
    QLabel* server_label_;
    QLabel* clients_label_;
    QLabel* packets_label_;
    QLabel* uptime_label_;
    bool is_running_ = false;
    QString server_name_;
    ServerStatusSnapshot last_snapshot_;
};

} // namespace nevo
