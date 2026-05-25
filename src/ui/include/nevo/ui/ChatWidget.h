#pragma once
/**
 * @file ChatWidget.h
 * @brief 文字聊天面板 - QWidget 实现
 *
 * ChatWidget 提供频道内文字聊天功能，包含：
 *   - 消息显示区域（QTextBrowser）
 *   - 消息输入栏（QPlainTextEdit，支持多行输入）
 *   - 发送按钮
 *
 * 快捷键：
 *   - Enter: 发送消息
 *   - Shift+Enter: 换行
 *
 * 消息格式：
 *   [HH:mm] 用户名: 消息内容
 *
 * 信号：
 *   - chatMessageSent(text): 用户发送消息时发射
 */

#include <QWidget>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

namespace nevo {

// ============================================================
// ChatWidget - 文字聊天面板
// ============================================================

/**
 * @class ChatWidget
 * @brief 频道内文字聊天面板
 *
 * 显示频道内的聊天消息，并提供消息输入功能。
 * 输入栏支持多行文本，Enter 发送，Shift+Enter 换行。
 */
class ChatWidget : public QWidget {
    Q_OBJECT

public:
    /// 构造函数
    explicit ChatWidget(QWidget* parent = nullptr);

    /**
     * @brief 添加收到的聊天消息
     *
     * @param sender_name 发送者用户名
     * @param text 消息文本
     * @param timestamp 消息时间戳（Unix 毫秒）
     * @param is_self 是否是自己发送的消息
     */
    void addMessage(const QString& sender_name,
                    const QString& text,
                    uint64_t timestamp,
                    bool is_self = false);

    /**
     * @brief 添加系统消息（如用户加入/离开）
     * @param text 系统消息文本
     */
    void addSystemMessage(const QString& text);

    /// 清空所有消息
    void clearMessages();

    /**
     * @brief 设置输入栏是否可用
     * @param enabled true=可输入, false=禁用
     */
    void setInputEnabled(bool enabled);

signals:
    /// 用户发送聊天消息
    void chatMessageSent(const QString& text);

private slots:
    void onSendClicked();

protected:
    /// 事件过滤器：处理 Enter/Shift+Enter 快捷键
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    /// 格式化时间戳为 HH:mm
    static QString formatTime(uint64_t timestamp_ms);

    /// 发送当前输入的消息
    void sendMessage();

    QTextBrowser* message_display_;
    QPlainTextEdit* input_edit_;
    QPushButton* send_button_;
    QLabel* hint_label_;
};

} // namespace nevo
