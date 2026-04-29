/**
 * @file ConnectionBar.cpp
 * @brief ConnectionBar 实现 - 连接状态栏
 *
 * 实现了连接状态栏的 UI 布局和交互逻辑，
 * 包括服务器地址输入、连接控制、状态指示和音量调节。
 */

#include "nevo/ui/ConnectionBar.h"

#include <QHBoxLayout>
#include <QPixmap>
#include <QPainter>
#include <QIntValidator>
#include <QEvent>

#ifdef NEVO_HAS_BOOST
#include "nevo/client/ClientCore.h"
#include "nevo/network/NatTraversal.h"
#endif

namespace nevo {

// Helper to convert ClientState to ConnectionState when available
#ifdef NEVO_HAS_BOOST
static ConnectionState toConnectionState(ClientState s) {
    switch (s) {
        case ClientState::Disconnected: return ConnectionState::Disconnected;
        case ClientState::Connecting: return ConnectionState::Connecting;
        case ClientState::Connected: return ConnectionState::Connected;
        case ClientState::InChannel: return ConnectionState::InChannel;
        default: return ConnectionState::Disconnected;
    }
}
#endif

// ============================================================
// 辅助函数：创建圆形指示灯图标
// ============================================================

/// 创建指定颜色的圆形指示灯 QPixmap，支持发光效果
static QPixmap createIndicatorPixmap(const QColor& color, int size = 20)
{
    // 为发光效果增加额外画布空间
    int glow_padding = 4;
    int canvas_size = size + glow_padding * 2;
    QPixmap pixmap(canvas_size, canvas_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // 绘制外发光
    QColor glow_color = color;
    glow_color.setAlpha(80);
    for (int i = 3; i >= 0; --i) {
        glow_color.setAlpha(60 - i * 15);
        painter.setBrush(glow_color);
        painter.setPen(Qt::NoPen);
        int glow_size = size + i * 3;
        int offset = (canvas_size - glow_size) / 2;
        painter.drawEllipse(offset, offset, glow_size, glow_size);
    }

    // 绘制主圆
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    int main_offset = (canvas_size - size) / 2;
    painter.drawEllipse(main_offset, main_offset, size, size);

    // 绘制高光（更亮的半圆，营造立体感）
    QColor highlight = color.lighter(160);
    highlight.setAlpha(180);
    painter.setBrush(highlight);
    int highlight_size = size / 2;
    int highlight_offset = main_offset + 2;
    painter.drawEllipse(highlight_offset, highlight_offset,
                        highlight_size, highlight_size);

    return pixmap;
}

// ============================================================
// 构造 / 析构
// ============================================================

ConnectionBar::ConnectionBar(QWidget* parent)
    : QWidget(parent)
    , status_indicator_(nullptr)
    , server_edit_(nullptr)
    , connect_btn_(nullptr)
    , latency_label_(nullptr)
    , nat_type_label_(nullptr)
    , volume_slider_(nullptr)
    , volume_label_(nullptr)
{
    setupUi();
}

ConnectionBar::~ConnectionBar() = default;

// ============================================================
// 状态更新
// ============================================================

void ConnectionBar::updateConnectionState(ConnectionState state)
{
    switch (state) {
        case ConnectionState::Disconnected:
            // 红灯，按钮显示 "Connect"
            setStatusIndicatorColor(QColor(220, 50, 50));
            connect_btn_->setText(tr("Connect"));
            is_connected_ = false;
            server_edit_->setEnabled(true);
            break;

        case ConnectionState::Connecting:
            // 黄灯，按钮显示 "Cancel"
            setStatusIndicatorColor(QColor(220, 200, 50));
            connect_btn_->setText(tr("Cancel"));
            is_connected_ = false;
            server_edit_->setEnabled(false);
            break;

        case ConnectionState::Connected:
        case ConnectionState::InChannel:
            // 绿灯，按钮显示 "Disconnect"
            setStatusIndicatorColor(QColor(50, 200, 50));
            connect_btn_->setText(tr("Disconnect"));
            is_connected_ = true;
            server_edit_->setEnabled(false);
            break;
    }
}

void ConnectionBar::updateLatency(int latency_ms)
{
    if (latency_ms < 0) {
        latency_label_->setText(tr("Latency: --"));
    } else {
        latency_label_->setText(
            tr("Latency: %1ms").arg(latency_ms));
    }
}

void ConnectionBar::updateNatType(NatType nat_type)
{
#ifdef NEVO_HAS_BOOST
    const char* nat_str = natTypeToString(nat_type);
    nat_type_label_->setText(
        tr("NAT: %1").arg(QString::fromUtf8(nat_str)));
#else
    (void)nat_type;
    nat_type_label_->setText(tr("NAT: N/A"));
#endif
}

void ConnectionBar::updateMicStatus(bool muted, bool audio_active)
{
    mic_muted_ = muted;
    mic_audio_active_ = audio_active;
    if (muted) {
        // 麦克风闭麦：红色
        mic_status_bar_->setStyleSheet(QStringLiteral(
            "QFrame { background-color: #f44336; border-radius: 3px; }"
        ));
        mic_status_bar_->setToolTip(tr("Microphone: Muted"));
    } else if (audio_active) {
        // 麦克风开启且检测到音频输入：蓝色
        mic_status_bar_->setStyleSheet(QStringLiteral(
            "QFrame { background-color: #5c8aff; border-radius: 3px; }"
        ));
        mic_status_bar_->setToolTip(tr("Microphone: Active"));
    } else {
        // 麦克风开启但未检测到音频输入：黄色
        mic_status_bar_->setStyleSheet(QStringLiteral(
            "QFrame { background-color: #ffc107; border-radius: 3px; }"
        ));
        mic_status_bar_->setToolTip(tr("Microphone: Idle"));
    }
}

QString ConnectionBar::serverAddress() const
{
    QString text = server_edit_->text().trimmed();

    // 分离 host 和 port（格式：host:port）
    int colon_pos = text.lastIndexOf(':');
    if (colon_pos > 0) {
        return text.left(colon_pos);
    }
    return text;
}

uint16_t ConnectionBar::serverPort() const
{
    QString text = server_edit_->text().trimmed();

    // 分离 host 和 port（格式：host:port）
    int colon_pos = text.lastIndexOf(':');
    if (colon_pos > 0) {
        bool ok = false;
        uint16_t port = static_cast<uint16_t>(
            text.mid(colon_pos + 1).toUShort(&ok));
        return ok ? port : 8080;  // 默认端口 8080
    }
    return 8080;  // 默认端口
}

void ConnectionBar::setServerAddress(const QString& address)
{
    server_edit_->setText(address);
}

void ConnectionBar::setServerPort(uint16_t port)
{
    QString text = server_edit_->text().trimmed();
    int colon_pos = text.lastIndexOf(':');
    QString host = (colon_pos > 0) ? text.left(colon_pos) : text;
    server_edit_->setText(host + QStringLiteral(":") + QString::number(port));
}

int ConnectionBar::volume() const
{
    return volume_slider_->value();
}

void ConnectionBar::setVolume(int vol)
{
    volume_slider_->setValue(vol);
}

// ============================================================
// 私有槽函数
// ============================================================

void ConnectionBar::onConnectButtonClicked()
{
    if (is_connected_) {
        // 已连接状态，请求断开
        emit disconnectRequested();
    } else {
        // 未连接状态，请求连接
        QString host = serverAddress();
        uint16_t port = serverPort();
        if (!host.isEmpty()) {
            emit connectRequested(host, port);
        }
    }
}

// ============================================================
// 内部辅助方法
// ============================================================

void ConnectionBar::setupUi()
{
    // 卡片式背景与圆角
    setStyleSheet(QStringLiteral(
        "ConnectionBar {"
        "  background-color: #1e2227;"
        "  border: 1px solid #2c3138;"
        "  border-radius: 8px;"
        "}"
    ));

    // 主水平布局
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(16);

    // --- 状态指示灯 ---
    status_indicator_ = new QLabel(this);
    setStatusIndicatorColor(QColor(220, 50, 50));  // 初始红色（未连接）
    status_indicator_->setFixedSize(20, 20);
    status_indicator_->setToolTip(tr("Disconnected"));
    layout->addWidget(status_indicator_);

    // --- 服务器地址输入 ---
    server_edit_ = new QLineEdit(this);
    server_edit_->setPlaceholderText(
        tr("Server address (host:port)"));
    server_edit_->setMinimumWidth(220);
    server_edit_->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background-color: #16191d;"
        "  border: 1px solid #2c3138;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  color: #f0f0f0;"
        "  font-size: 13px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #5c8aff;"
        "}"
    ));
    layout->addWidget(server_edit_);

    // --- 连接/断开按钮 ---
    connect_btn_ = new QPushButton(tr("Connect"), this);
    connect_btn_->setFixedWidth(110);
    connect_btn_->setCursor(Qt::PointingHandCursor);
    connect_btn_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #5c8aff;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 8px 16px;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #7aa2ff;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #4a7aee;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #3a4048;"
        "  color: #808080;"
        "}"
    ));
    connect(connect_btn_, &QPushButton::clicked,
            this, &ConnectionBar::onConnectButtonClicked);
    layout->addWidget(connect_btn_);

    // --- 分隔线 ---
    QFrame* separator1 = new QFrame(this);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setStyleSheet(QStringLiteral("color: #2c3138;"));
    layout->addWidget(separator1);

    // --- 延迟显示 ---
    latency_label_ = new QLabel(tr("Latency: --"), this);
    latency_label_->setMinimumWidth(110);
    latency_label_->setStyleSheet(QStringLiteral(
        "color: #a0a8b8; font-size: 12px;"
    ));
    layout->addWidget(latency_label_);

    // --- NAT 类型显示 ---
    nat_type_label_ = new QLabel(tr("NAT: --"), this);
    nat_type_label_->setMinimumWidth(130);
    nat_type_label_->setStyleSheet(QStringLiteral(
        "color: #a0a8b8; font-size: 12px;"
    ));
    layout->addWidget(nat_type_label_);

    // --- 分隔线 ---
    QFrame* separator2 = new QFrame(this);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setStyleSheet(QStringLiteral("color: #2c3138;"));
    layout->addWidget(separator2);

    // --- 音量标签 ---
    volume_label_ = new QLabel(tr("Vol:"), this);
    volume_label_->setStyleSheet(QStringLiteral(
        "color: #a0a8b8; font-size: 12px;"
    ));
    layout->addWidget(volume_label_);

    // --- 音量滑块 ---
    volume_slider_ = new QSlider(Qt::Horizontal, this);
    volume_slider_->setRange(0, 100);
    volume_slider_->setValue(80);
    volume_slider_->setMinimumWidth(120);
    volume_slider_->setMaximumWidth(200);
    volume_slider_->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal {"
        "  height: 4px;"
        "  background: #2c3138;"
        "  border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #5c8aff;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 14px;"
        "  height: 14px;"
        "  margin: -5px 0;"
        "  background: #ffffff;"
        "  border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "  background: #e0e0e0;"
        "}"
    ));
    connect(volume_slider_, &QSlider::valueChanged,
            this, &ConnectionBar::volumeChanged);
    layout->addWidget(volume_slider_);

    // --- 音量数值 ---
    QLabel* volume_value = new QLabel(
        tr("%1%").arg(volume_slider_->value()), this);
    volume_value->setStyleSheet(QStringLiteral(
        "color: #a0a8b8; font-size: 12px; min-width: 32px;"
    ));
    connect(volume_slider_, &QSlider::valueChanged,
            volume_value, [volume_value](int value) {
                volume_value->setText(tr("%1%").arg(value));
            });
    layout->addWidget(volume_value);

    // --- 麦克风状态灯条 ---
    mic_status_bar_ = new QFrame(this);
    mic_status_bar_->setFixedHeight(6);
    mic_status_bar_->setMinimumWidth(80);
    mic_status_bar_->setStyleSheet(QStringLiteral(
        "QFrame { background-color: #ffc107; border-radius: 3px; }"
    ));
    mic_status_bar_->setToolTip(tr("Microphone: Idle"));
    layout->addWidget(mic_status_bar_);

    // 弹性空间
    layout->addStretch();
}

void ConnectionBar::setStatusIndicatorColor(const QColor& color)
{
    if (!status_indicator_) {
        return;
    }

    status_indicator_->setPixmap(createIndicatorPixmap(color, 20));

    if (color == QColor(220, 50, 50)) {
        status_indicator_->setToolTip(tr("Disconnected"));
        status_indicator_->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));
    } else if (color == QColor(220, 200, 50)) {
        status_indicator_->setToolTip(tr("Connecting"));
        status_indicator_->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));
    } else if (color == QColor(50, 200, 50)) {
        status_indicator_->setToolTip(tr("Connected"));
        status_indicator_->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));
    }
}

// ============================================================
// i18n: retranslateUi & changeEvent
// ============================================================

void ConnectionBar::retranslateUi()
{
    if (server_edit_) server_edit_->setPlaceholderText(tr("Server address (host:port)"));
    // Button text is state-dependent — re-apply based on current state
    if (connect_btn_) {
        if (is_connected_) {
            connect_btn_->setText(tr("Disconnect"));
        } else {
            connect_btn_->setText(tr("Connect"));
        }
    }
    if (volume_label_) volume_label_->setText(tr("Vol:"));
    if (latency_label_) latency_label_->setText(tr("Latency: --"));
    if (nat_type_label_) nat_type_label_->setText(tr("NAT: --"));
    if (mic_status_bar_) {
        if (mic_muted_)
            mic_status_bar_->setToolTip(tr("Microphone: Muted"));
        else if (mic_audio_active_)
            mic_status_bar_->setToolTip(tr("Microphone: Active"));
        else
            mic_status_bar_->setToolTip(tr("Microphone: Idle"));
    }
}

void ConnectionBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

} // namespace nevo
