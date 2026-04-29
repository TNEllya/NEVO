/**
 * @file ServerLogView.cpp
 * @brief ServerLogView implementation - server log view
 */

#include "nevo/server/ui/ServerLogView.h"

#include <QVBoxLayout>
#include <QScrollBar>
#include <QDateTime>
#include <QKeyEvent>

namespace nevo {

ServerLogView::ServerLogView(QWidget* parent)
    : QWidget(parent)
    , log_edit_(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    log_edit_ = new QPlainTextEdit(this);
    log_edit_->setReadOnly(true);
    log_edit_->setMaximumBlockCount(2000);
    log_edit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    log_edit_->setFont(QFont(QStringLiteral("Consolas"), 9));
    log_edit_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background-color: #1a1d23;"
        "  color: #c5c8d4;"
        "  border: none;"
        "  padding: 8px;"
        "}"
    ));

    layout->addWidget(log_edit_);
}

ServerLogView::~ServerLogView() = default;

void ServerLogView::appendLog(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    QString formatted = QStringLiteral("[%1] %2").arg(timestamp, message);

    log_edit_->appendPlainText(formatted);

    // Auto-scroll to bottom
    QScrollBar* sb = log_edit_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ServerLogView::clearLog()
{
    log_edit_->clear();
}

void ServerLogView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        clearLog();
    } else {
        QWidget::keyPressEvent(event);
    }
}

} // namespace nevo
