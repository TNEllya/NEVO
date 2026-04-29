#pragma once
/**
 * @file ConnectionBar.h
 * @brief 连接状态栏 - QWidget 实现
 *
 * ConnectionBar 是 NEVO VoIP 客户端的底部连接状态栏，
 * 显示服务器地址输入、连接/断开按钮、连接状态指示灯、
 * 延迟信息、NAT 类型和音量控制。
 *
 * 布局结构：
 *   [状态灯] [服务器地址输入] [连接/断开按钮] | [延迟] [NAT类型] | [音量滑块]
 *
 * 状态指示灯颜色：
 *   - 绿色：已连接 (Connected / InChannel)
 *   - 黄色：连接中 (Connecting)
 *   - 红色：已断开 (Disconnected)
 *
 * 信号连接：
 *   - connectRequested(): 用户点击连接按钮时发射
 *   - disconnectRequested(): 用户点击断开按钮时发射
 *   - volumeChanged(): 用户拖动音量滑块时发射
 *
 * 线程安全：
 *   - 所有操作应在主线程执行
 *   - 通过 QMetaObject::invokeMethod 可从其他线程更新状态
 */

#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QString>
#include <QGraphicsDropShadowEffect>

#include "nevo/core/common/Types.h"

namespace nevo {

// Connection state for UI display (independent of ClientCore)
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    InChannel
};

// 前向声明
enum class NatType;

// ============================================================
// ConnectionBar - 连接状态栏
// ============================================================

/**
 * @class ConnectionBar
 * @brief 连接状态栏，显示连接信息和控制
 *
 * 提供服务器地址输入、连接控制按钮、状态指示、
 * 延迟/NAT 信息显示和音量调节功能。
 *
 * 典型用法：
 * @code
 *   ConnectionBar* bar = new ConnectionBar;
 *
 *   // 连接信号
 *   connect(bar, &ConnectionBar::connectRequested,
 *           [](const QString& host, uint16_t port) {
 *               clientCore.connect(host, port, ...);
 *           });
 *   connect(bar, &ConnectionBar::disconnectRequested,
 *           []() { clientCore.disconnect(); });
 *
 *   // 更新状态
     *   bar->updateConnectionState(ConnectionState::Connected);
 *   bar->updateLatency(42);
 *   bar->updateNatType(NatType::FullCone);
 * @endcode
 */
class ConnectionBar : public QWidget {
    Q_OBJECT

public:
    // ============================================================
    // 构造 / 析构
    // ============================================================

    /// 构造函数
    /// @param parent 父 QWidget
    explicit ConnectionBar(QWidget* parent = nullptr);

    /// 析构函数
    ~ConnectionBar() override;

    // 禁止拷贝
    ConnectionBar(const ConnectionBar&) = delete;
    ConnectionBar& operator=(const ConnectionBar&) = delete;

    // --- Server address accessors ---
    QString serverAddress() const;
    uint16_t serverPort() const;
    void setServerAddress(const QString& address);
    void setServerPort(uint16_t port);

    // --- State update methods ---
    void updateConnectionState(ConnectionState state);
    void updateLatency(int latency_ms);
    void updateNatType(NatType nat_type);
    void updateMicStatus(bool muted, bool audio_active);
    int volume() const;
    void setVolume(int vol);

    /// Retranslate all UI strings (for dynamic language switching)
    void retranslateUi();

signals:
    void connectRequested(const QString& host, uint16_t port);
    void disconnectRequested();
    void volumeChanged(int value);

protected:
    /// Handle language change events for dynamic i18n
    void changeEvent(QEvent* event) override;

private slots:

    /**
     * @brief 连接/断开按钮点击处理
     *
     * 根据当前连接状态决定发射 connectRequested 或 disconnectRequested。
     */
    void onConnectButtonClicked();

private:
    // ============================================================
    // 内部辅助方法
    // ============================================================

    /**
     * @brief 初始化 UI 布局
     */
    void setupUi();

    /**
     * @brief 更新状态指示灯颜色
     * @param color 指示灯颜色
     */
    void setStatusIndicatorColor(const QColor& color);

    // ============================================================
    // 数据成员
    // ============================================================

    // --- 连接状态 ---
    bool is_connected_ = false;  ///< 当前是否已连接

    // --- UI 控件 ---
    QLabel* status_indicator_;    ///< 状态指示灯（彩色圆点）
    QLineEdit* server_edit_;      ///< 服务器地址输入框
    QPushButton* connect_btn_;    ///< 连接/断开按钮
    QLabel* latency_label_;       ///< 延迟显示标签
    QLabel* nat_type_label_;      ///< NAT 类型显示标签
    QSlider* volume_slider_;      ///< 音量滑块
    QLabel* volume_label_;        ///< 音量值标签
    QFrame* mic_status_bar_;      ///< 麦克风状态灯条
    bool mic_muted_ = false;      ///< 麦克风静音状态
    bool mic_audio_active_ = false; ///< 麦克风是否检测到音频输入
};

} // namespace nevo
