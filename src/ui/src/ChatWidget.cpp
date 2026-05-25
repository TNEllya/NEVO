/**
 * @file ChatWidget.cpp
 * @brief 文字聊天面板实现
 */

#include "nevo/ui/ChatWidget.h"

#include <QScrollBar>
#include <QDateTime>
#include <QKeyEvent>
#include <QFrame>

namespace nevo {

// ============================================================
// ChatWidget Implementation
// ============================================================

ChatWidget::ChatWidget(QWidget* parent)
    : QWidget(parent)
    , message_display_(nullptr)
    , input_edit_(nullptr)
    , send_button_(nullptr)
    , hint_label_(nullptr)
{
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // --- 消息显示区域 ---
    message_display_ = new QTextBrowser(this);
    message_display_->setReadOnly(true);
    message_display_->setOpenExternalLinks(false);
    message_display_->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  background-color: #111318;"
        "  color: #e2e2e9;"
        "  border: none;"
        "  border-bottom: 1px solid #44474f;"
        "  font-size: 13px;"
        "  padding: 8px;"
        "}"));
    main_layout->addWidget(message_display_, 1);

    // --- 发送消息栏 ---
    auto* input_bar = new QWidget(this);
    input_bar->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1d2024; }"));

    auto* input_bar_layout = new QVBoxLayout(input_bar);
    input_bar_layout->setContentsMargins(10, 8, 10, 8);
    input_bar_layout->setSpacing(4);

    // 输入框行
    auto* input_row = new QHBoxLayout();
    input_row->setSpacing(8);

    input_edit_ = new QPlainTextEdit(this);
    input_edit_->setPlaceholderText(tr("Type a message..."));
    input_edit_->setMaximumHeight(80);
    input_edit_->setMinimumHeight(36);
    input_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    input_edit_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background-color: #191c20;"
        "  color: #e2e2e9;"
        "  border: none;"
        "  border-bottom: 2px solid #44474f;"
        "  border-radius: 4px 4px 0 0;"
        "  padding: 8px 12px 4px;"
        "  font-size: 13px;"
        "}"
        "QPlainTextEdit:focus {"
        "  border-bottom: 2px solid #a8c7fa;"
        "}"
        "QPlainTextEdit::placeholder {"
        "  color: #5e6068;"
        "}"));
    // 安装事件过滤器处理 Enter/Shift+Enter
    input_edit_->installEventFilter(this);

    send_button_ = new QPushButton(tr("Send"), this);
    send_button_->setFixedSize(72, 36);
    send_button_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #a8c7fa;"
        "  color: #062e6f;"
        "  border: none;"
        "  border-radius: 20px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  background-color: #d3e3fd;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #8ec7ff;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #1d2024;"
        "  color: #5e6068;"
        "}"));
    connect(send_button_, &QPushButton::clicked,
            this, &ChatWidget::onSendClicked);

    input_row->addWidget(input_edit_, 1);
    input_row->addWidget(send_button_, 0, Qt::AlignBottom);

    input_bar_layout->addLayout(input_row);

    // 提示行
    hint_label_ = new QLabel(tr("Enter to send, Shift+Enter for new line"), this);
    hint_label_->setStyleSheet(QStringLiteral(
        "color: #8e9099; font-size: 11px; padding-left: 4px;"));
    input_bar_layout->addWidget(hint_label_);

    main_layout->addWidget(input_bar);
}

void ChatWidget::addMessage(const QString& sender_name,
                              const QString& text,
                              uint64_t timestamp,
                              bool is_self)
{
    QString time_str = formatTime(timestamp);
    QString escaped_name = sender_name.toHtmlEscaped();
    QString escaped_text = text.toHtmlEscaped();

    // 保留换行
    escaped_text.replace(QStringLiteral("\n"), QStringLiteral("<br>"));

    QString name_color = is_self ? QStringLiteral("#a8c7fa") : QStringLiteral("#8ec7ff");
    QString html = QStringLiteral(
        "<span style=\"color:#8e9099;font-size:11px;\">[%1]</span> "
        "<span style=\"color:%2;font-weight:500;\">%3</span>: "
        "<span style=\"color:#e2e2e9;\">%4</span>")
        .arg(time_str, name_color, escaped_name, escaped_text);

    message_display_->append(html);

    // 自动滚动到底部
    QScrollBar* scroll = message_display_->verticalScrollBar();
    scroll->setValue(scroll->maximum());
}

void ChatWidget::addSystemMessage(const QString& text)
{
    QString escaped = text.toHtmlEscaped();
    QString html = QStringLiteral(
        "<span style=\"color:#8e9099;font-style:italic;font-size:12px;\">%1</span>")
        .arg(escaped);

    message_display_->append(html);

    QScrollBar* scroll = message_display_->verticalScrollBar();
    scroll->setValue(scroll->maximum());
}

void ChatWidget::clearMessages()
{
    message_display_->clear();
}

void ChatWidget::setInputEnabled(bool enabled)
{
    input_edit_->setEnabled(enabled);
    send_button_->setEnabled(enabled);
    if (!enabled) {
        input_edit_->setPlaceholderText(tr("Connect to a channel to chat"));
    } else {
        input_edit_->setPlaceholderText(tr("Type a message..."));
    }
}

bool ChatWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == input_edit_ && event->type() == QEvent::KeyPress) {
        auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) {
            if (key_event->modifiers() & Qt::ShiftModifier) {
                // Shift+Enter: 插入换行，交给 QPlainTextEdit 默认处理
                return QWidget::eventFilter(obj, event);
            } else {
                // Enter: 发送消息
                sendMessage();
                return true;  // 事件已处理，不插入换行
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ChatWidget::onSendClicked()
{
    sendMessage();
}

void ChatWidget::sendMessage()
{
    QString text = input_edit_->toPlainText().trimmed();
    if (text.isEmpty()) return;

    emit chatMessageSent(text);
    input_edit_->clear();
    // 重置输入框高度
    input_edit_->setMaximumHeight(80);
}

QString ChatWidget::formatTime(uint64_t timestamp_ms)
{
    if (timestamp_ms == 0) {
        return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    }
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_ms));
    return dt.toString(QStringLiteral("HH:mm"));
}

} // namespace nevo
