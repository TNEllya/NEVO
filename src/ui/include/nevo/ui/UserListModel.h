#pragma once
/**
 * @file UserListModel.h
 * @brief 频道用户列表模型 - QAbstractListModel 实现
 *
 * UserListModel 为 NEVO VoIP 客户端提供当前频道用户列表的 Qt 模型层。
 * 它展示当前频道中所有在线用户，并支持说话状态指示和静音/耳聋图标。
 *
 * 核心功能：
 *   1. updateUserList(): 从服务器数据更新用户列表
 *   2. setSpeakingUser(): 标记/取消标记正在说话的用户
 *   3. Display role: 用户名 + 说话指示 + 静音/耳聋图标
 *   4. QML 兼容：通过 roleNames() 暴露自定义角色
 *
 * 数据来源：
 *   用户列表由 ClientCore::getState().channel_users 提供，
 *   当用户加入/离开频道时由 UI 层调用 updateUserList() 刷新。
 *
 * 线程安全：
 *   - 所有模型操作应在主线程（GUI 线程）执行
 *   - updateUserList() 内部调用 beginResetModel/endResetModel
 */

#include <QAbstractListModel>
#include <QIcon>
#include <unordered_map>
#include <vector>

#include "nevo/core/common/Types.h"
#include "nevo/core/model/User.h"

namespace nevo {

// ============================================================
// UserInfo - 用户列表显示数据
// ============================================================

/// 用户显示信息结构体，用于 UserListModel 内部存储
struct UserInfo {
    UserId user_id;             ///< 用户唯一标识
    std::string username;       ///< 用户名
    bool is_muted = false;      ///< 是否静音
    bool is_deafened = false;   ///< 是否耳聋
    bool is_speaking = false;   ///< 是否正在说话

    /// 默认构造
    UserInfo() = default;

    /// 从 User 对象构造
    explicit UserInfo(const User& user)
        : user_id(user.id())
        , username(user.username())
        , is_muted(user.isMuted())
        , is_deafened(user.isDeafened())
        , is_speaking(user.isSpeaking()) {}
};

// ============================================================
// UserListModel - 频道用户列表模型
// ============================================================

/**
 * @class UserListModel
 * @brief QAbstractListModel 实现，用于当前频道用户列表显示
 *
 * 展示当前频道中所有在线用户，支持：
 *   - 用户名显示
 *   - 说话状态指示（绿色圆点）
 *   - 静音图标（红色麦克风斜杠）
 *   - 耳聋图标（红色耳机斜杠）
 *   - QML 兼容的自定义角色名
 *
 * 典型用法：
 * @code
 *   UserListModel model;
 *   model.updateUserList(user_list);
 *
 *   QListView* list = new QListView;
 *   list->setModel(&model);
 *
 *   // 说话状态更新
 *   model.setSpeakingUser(user_id, true);
 * @endcode
 */
class UserListModel : public QAbstractListModel {
    Q_OBJECT

public:
    // ============================================================
    // 自定义角色枚举（供 QML 使用）
    // ============================================================

    /// 数据角色枚举
    enum UserRoles {
        UsernameRole = Qt::UserRole,        ///< 用户名
        UserIdRole,                         ///< 用户 ID（uint64_t）
        IsMutedRole,                        ///< 是否静音
        IsDeafenedRole,                     ///< 是否耳聋
        IsSpeakingRole,                     ///< 是否正在说话
        DisplayIconRole,                    ///< 组合显示图标
    };

    // ============================================================
    // 构造 / 析构
    // ============================================================

    /// 构造函数
    /// @param parent 父 QObject
    explicit UserListModel(QObject* parent = nullptr);

    /// 析构函数
    ~UserListModel() override;

    // 禁止拷贝
    UserListModel(const UserListModel&) = delete;
    UserListModel& operator=(const UserListModel&) = delete;

    // ============================================================
    // QAbstractListModel 接口实现
    // ============================================================

    /// 获取行数（用户数量）
    /// @param parent 父索引（ListModel 不使用）
    /// @return 用户数量
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /// 获取数据
    /// @param index 模型索引
    /// @param role  数据角色
    /// @return 对应的数据
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;

    /// 获取角色名映射（QML 兼容）
    /// @return 角色名到角色 ID 的映射
    QHash<int, QByteArray> roleNames() const override;

    // ============================================================
    // 数据更新
    // ============================================================

    /**
     * @brief 从用户信息列表更新模型
     *
     * 替换整个用户列表并通知视图刷新。
     *
     * @param users 新的用户信息列表
     */
    void updateUserList(const std::vector<UserInfo>& users);

    /**
     * @brief 设置指定用户的说话状态
     *
     * 只更新单个用户的数据，避免全模型刷新。
     *
     * @param user_id  用户 ID
     * @param speaking true 表示正在说话，false 表示停止说话
     */
    void setSpeakingUser(UserId user_id, bool speaking);

private:
    // ============================================================
    // 内部辅助方法
    // ============================================================

    /**
     * @brief 根据用户 ID 查找行号
     *
     * @param user_id 目标用户 ID
     * @return 行号，未找到返回 -1
     */
    int findRowByUserId(UserId user_id) const;

    // ============================================================
    // 数据成员
    // ============================================================

    /// 用户信息列表（按行号索引）
    std::vector<UserInfo> users_;

    /// 用户 ID 到行号的映射（加速查找）
    std::unordered_map<UserId, int> user_id_to_row_;

    /// 静音图标
    QIcon muted_icon_;

    /// 耳聋图标
    QIcon deafened_icon_;

    /// 说话指示图标（绿色圆点）
    QIcon speaking_icon_;
};

} // namespace nevo
