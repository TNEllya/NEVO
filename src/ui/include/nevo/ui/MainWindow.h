#pragma once
/**
 * @file MainWindow.h
 * @brief 主窗口 - QMainWindow 实现
 *
 * MainWindow 是 NEVO VoIP 客户端的主界面，组合了所有 UI 组件：
 *   - 左侧停靠窗口：ChannelTreeModel + QTreeView（频道树）
 *   - 右侧停靠窗口：UserListModel + QListView（用户列表）
 *   - 底部：ConnectionBar（连接状态栏）
 *   - 菜单栏：File（Connect、Disconnect、Quit）、Settings（Audio）、Help
 *
 * MainWindow 持有 ClientCore 实例（或引用），负责：
 *   1. 连接 UI 信号到 ClientCore 方法
 *   2. 将 ClientCore 回调转发到 UI 更新
 *   3. 管理菜单和工具栏动作
 *   4. 弹出对话框（登录、音频设置等）
 *
 * 窗口布局：
 *   +-------------------------------------------------------+
 *   | Menu: File | Settings | Help                          |
 *   +------------------+------------------------------------+
 *   | Channel Tree     | User List                          |
 *   | (QTreeView)      | (QListView)                        |
 *   |                  |                                    |
 *   |  Root            |  Alice [Speaking]                  |
 *   |  ├── General     |  Bob   [Muted]                     |
 *   |  │   ├── Chat    |  Charlie                           |
 *   |  │   └── Gaming  |                                    |
 *   |  └── Music       |                                    |
 *   +------------------+------------------------------------+
 *   | [●] localhost:8080 [Connect] | Latency: 42ms | ...    |
 *   +-------------------------------------------------------+
 *
 * 线程安全：
 *   - 所有 UI 操作在主线程执行
 *   - ClientCore 回调在 io_context 线程中触发，
 *     MainWindow 通过 QMetaObject::invokeMethod 或信号槽
 *     将回调安全地转发到主线程
 */

#include <QMainWindow>
#include <QTreeView>
#include <QListView>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QTimer>
#include <memory>

#include "nevo/ui/ChannelTreeModel.h"
#include "nevo/ui/UserListModel.h"
#include "nevo/ui/ConnectionBar.h"
#include "nevo/ui/AudioSettingsWidget.h"
#include "nevo/ui/ChannelItemDelegate.h"

#ifdef NEVO_HAS_BOOST
#include "nevo/client/ClientCore.h"
#include <boost/asio.hpp>
#endif

namespace nevo {

// ============================================================
// LoginDialog - 登录对话框
// ============================================================

/**
 * @class LoginDialog
 * @brief 登录对话框，输入用户名
 *
 * 简单的模态对话框，包含用户名输入框和确认/取消按钮。
 */
class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QWidget* parent = nullptr);

    /// 获取输入的用户名
    QString username() const;

private:
    QLineEdit* username_edit_;
};

// ============================================================
// OwnerBindDialog - 服主绑定对话框
// ============================================================

/**
 * @class OwnerBindDialog
 * @brief 服主绑定对话框，输入绑定密钥
 *
 * 简单的模态对话框，包含绑定密钥输入框和确认/取消按钮。
 */
class OwnerBindDialog : public QDialog {
    Q_OBJECT

public:
    explicit OwnerBindDialog(QWidget* parent = nullptr);

    /// 获取输入的绑定密钥
    QString bindKey() const;

private:
    QLineEdit* bind_key_edit_;
};

// ============================================================
// MainWindow - 主窗口
// ============================================================

/**
 * @class MainWindow
 * @brief NEVO VoIP 客户端主窗口
 *
 * 组合所有 UI 组件，协调用户交互和 ClientCore 通信。
 *
 * 典型用法：
 * @code
 *   int main(int argc, char* argv[]) {
 *       QApplication app(argc, argv);
 *       boost::asio::io_context io_ctx;
 *       MainWindow window(io_ctx);
 *       window.show();
 *       return app.exec();
 *   }
 * @endcode
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // ============================================================
    // 构造 / 析构
    // ============================================================

    /**
     * @brief 构造函数
     *
     * 创建 ClientCore 实例，初始化所有 UI 组件，
     * 建立信号槽连接。
     *
     * @param io_ctx Boost.Asio I/O 上下文引用
     * @param parent 父 QWidget
     */
    explicit MainWindow(QWidget* parent = nullptr);
#ifdef NEVO_HAS_BOOST
    explicit MainWindow(boost::asio::io_context& io_ctx,
                        QWidget* parent = nullptr);
#endif

    /// 析构函数
    ~MainWindow() override;

    // 禁止拷贝
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

protected:
    /// Handle language change events for dynamic i18n
    void changeEvent(QEvent* event) override;

    /// Handle window close event: ensure clean shutdown before Qt event loop stops
    void closeEvent(QCloseEvent* event) override;

    /// 全局事件过滤器：检测 PTT 按键按下/释放
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:

    // ============================================================
    // 菜单动作槽
    // ============================================================

    /// File -> Connect：弹出登录对话框并连接
    void onConnectAction();

    /// File -> Disconnect：断开连接
    void onDisconnectAction();

    /// File -> Quit：退出应用
    void onQuitAction();

    /// Settings -> Audio：弹出音频设置对话框
    void onAudioSettingsAction();

    /// Help -> About：显示关于对话框
    void onAboutAction();

    /// Settings -> Language: 切换界面语言
    void onLanguageChanged(const QString& lang_code);

    /// Settings -> Bind Owner: 服主绑定
    void onBindOwnerAction();

    // ============================================================
    // UI 交互槽
    // ============================================================

    /// 频道树双击：加入频道
    void onJoinChannelRequested(ChannelId channel_id);

    /// 离开当前频道
    void onLeaveChannelRequested();

    /// 连接栏：请求连接
    void onConnectRequested(const QString& host, uint16_t port);

    /// 连接栏：请求断开
    void onDisconnectRequested();

    /// 音量变更
    void onVolumeChanged(int volume);

    /// 静音切换
    void onMuteToggled(bool checked);

    /// 耳聋切换
    void onDeafenedToggled(bool checked);

    /// 频道树右键菜单
    void onChannelContextMenu(const QPoint& pos);

    /// 用户列表右键菜单
    void onUserContextMenu(const QPoint& pos);

    /// Channel tree selection changed — update join/leave button states
    void onChannelSelectionChanged();

private:

    // ============================================================
    // 内部辅助方法
    // ============================================================

    /// 初始化 UI 布局
    void setupUi();

    /// 创建菜单栏
    void setupMenuBar();

    /// 创建停靠窗口
    void setupDockWidgets();

    /// 建立 ClientCore 回调到 UI 的信号连接
    void setupClientCoreCallbacks();

    /// 保存客户端设置到 QSettings
    void saveSettings();

    /// 执行关闭清理（在 Qt 事件循环仍活跃时调用）
    void performShutdown();

    /// 从 QSettings 恢复客户端设置
    void loadSettings();

    /// 加载模拟频道数据（独立模式下使用）
    void loadMockChannelData();

    /// Retranslate all UI strings (for dynamic language switching)
    void retranslateUi();

    /// 根据当前输入模式（VAD/PTT）和音频状态更新麦克风指示灯
    void updateMicIndicator();

    /**
     * @brief 安全地在主线程更新 UI
     *
     * 从 ClientCore 回调（io_context 线程）安全地
     * 转发 UI 更新到主线程。
     *
     * @param functor 要在主线程执行的函数
     */
    template<typename Functor>
    void postToUiThread(Functor&& functor);

    // ============================================================
    // 数据成员
    // ============================================================

    // --- ClientCore ---
#ifdef NEVO_HAS_BOOST
    std::unique_ptr<ClientCore> client_core_;  ///< 客户端核心
    boost::asio::io_context* io_ctx_ = nullptr; ///< I/O 上下文指针（standalone 路径可能为 nullptr）
    std::unique_ptr<std::thread> io_thread_;   ///< io_context 运行线程
    bool shutdown_initiated_ = false;           ///< 标记是否已发起关闭流程（防止 closeEvent + 析构双重关闭）
#endif

    // --- 模型 ---
    ChannelTreeModel* channel_model_;           ///< 频道树模型
    UserListModel* user_model_;                 ///< 用户列表模型

    // --- 委托 ---
    ChannelItemDelegate* channel_delegate_;     ///< 频道项自定义渲染委托

    // --- 视图 ---
    QTreeView* channel_tree_;                   ///< 频道树视图
    QListView* user_list_;                      ///< 用户列表视图

    // --- 频道操作按钮 ---
    QPushButton* join_channel_btn_;             ///< 加入频道按钮
    QPushButton* leave_channel_btn_;            ///< 离开频道按钮

    // --- 当前频道标签 ---
    QLabel* current_channel_label_;             ///< 用户列表中的当前频道标签

    // --- 停靠窗口 ---
    QDockWidget* channel_dock_;                 ///< 频道树停靠窗口
    QDockWidget* user_dock_;                    ///< 用户列表停靠窗口

    // --- 底部栏 ---
    ConnectionBar* connection_bar_;             ///< 连接状态栏

    // --- 音频设置 ---
    AudioSettingsWidget* audio_settings_;       ///< 音频设置面板

    // --- 顶部工具栏 ---
    QToolBar* top_toolbar_;

    // --- 菜单动作 ---
    QAction* connect_action_;
    QAction* disconnect_action_;
    QAction* mute_action_;
    QAction* deafen_action_;
    QAction* quit_action_;
    QAction* audio_settings_action_;
    QAction* bind_owner_action_;
    QAction* about_action_;

    // --- 语言 ---
    QMenu* language_menu_ = nullptr;            ///< 语言子菜单
    QActionGroup* language_action_group_ = nullptr; ///< 语言选择互斥组
    QByteArray qm_data_;                        ///< 翻译文件数据（需与翻译器同生命周期）

    // --- PTT 按键状态 ---
    QKeySequence ptt_key_;                      ///< 绑定的 PTT 按键序列
    bool ptt_key_pressed_ = false;              ///< PTT 按键物理按下状态

    // --- 麦克风音频检测 ---
    QTimer* mic_level_timer_ = nullptr;         ///< 麦克风音频输入轮询定时器
    static constexpr float kMicLevelThreshold = 0.01f; ///< 音频输入检测阈值
};

} // namespace nevo
