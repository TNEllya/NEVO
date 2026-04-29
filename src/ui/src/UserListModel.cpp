/**
 * @file UserListModel.cpp
 * @brief UserListModel 实现 - 频道用户列表模型
 *
 * 实现了当前频道用户列表的 Qt 模型，支持说话状态指示
 * 和静音/耳聋图标显示。
 */

#include "nevo/ui/UserListModel.h"

#include <QApplication>
#include <QStyle>
#include <QPixmap>
#include <QPainter>

#include "nevo/ui/IconProvider.h"

namespace nevo {

// ============================================================
// 辅助函数：创建纯色圆形图标
// ============================================================

/// 创建一个指定颜色和大小的圆形图标
static QIcon createCircleIcon(const QColor& color, int size = 16)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(1, 1, size - 2, size - 2);

    return QIcon(pixmap);
}

// ============================================================
// 构造 / 析构
// ============================================================

UserListModel::UserListModel(QObject* parent)
    : QAbstractListModel(parent)
    , muted_icon_(nevo::IconProvider::mutedIcon())
    , deafened_icon_(nevo::IconProvider::deafenedIcon())
    , speaking_icon_(nevo::IconProvider::speakingIcon())
{
}

UserListModel::~UserListModel() = default;

// ============================================================
// QAbstractListModel 接口实现
// ============================================================

int UserListModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return static_cast<int>(users_.size());
}

QVariant UserListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    int row = index.row();
    if (row < 0 || row >= static_cast<int>(users_.size())) {
        return QVariant();
    }

    const UserInfo& user = users_[row];

    switch (role) {
        case UsernameRole:
            // 显示用户名，说话时附加指示符
            return QString::fromStdString(user.username);

        case UserIdRole:
            // 返回用户 ID
            return QVariant::fromValue(user.user_id.value);

        case IsMutedRole:
            // 是否静音
            return user.is_muted;

        case IsDeafenedRole:
            // 是否耳聋
            return user.is_deafened;

        case IsSpeakingRole:
            // 是否正在说话
            return user.is_speaking;

        case DisplayIconRole:
            // 组合图标：优先显示耳聋 > 静音 > 说话
            if (user.is_deafened) {
                return deafened_icon_;
            } else if (user.is_muted) {
                return muted_icon_;
            } else if (user.is_speaking) {
                return speaking_icon_;
            }
            return QVariant();

        case Qt::DecorationRole:
            // Decoration role：显示状态图标
            if (user.is_deafened) {
                return deafened_icon_;
            } else if (user.is_muted) {
                return muted_icon_;
            } else if (user.is_speaking) {
                return speaking_icon_;
            }
            return QVariant();

        case Qt::DisplayRole:
            // Display role：显示用户名（+ 说话指示）
            {
                QString display_name = QString::fromStdString(user.username);
                if (user.is_speaking) {
                    display_name += tr(" [Speaking]");
                }
                if (user.is_muted) {
                    display_name += tr(" [Mic Off]");
                }
                if (user.is_deafened) {
                    display_name += tr(" [Muted]");
                }
                return display_name;
            }

        case Qt::ToolTipRole:
            // 工具提示
            {
                QString tip = QString("User: %1\nID: %2")
                    .arg(QString::fromStdString(user.username))
                    .arg(user.user_id.value);
                if (user.is_speaking) {
                    tip += tr("\nStatus: Speaking");
                }
                if (user.is_muted) {
                    tip += tr("\nMicrophone: Muted");
                }
                if (user.is_deafened) {
                    tip += tr("\nAudio: Muted");
                }
                return tip;
            }

        default:
            return QVariant();
    }
}

QHash<int, QByteArray> UserListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[UsernameRole]   = "username";
    roles[UserIdRole]     = "userId";
    roles[IsMutedRole]    = "isMuted";
    roles[IsDeafenedRole] = "isDeafened";
    roles[IsSpeakingRole] = "isSpeaking";
    roles[DisplayIconRole] = "displayIcon";
    return roles;
}

// ============================================================
// 数据更新
// ============================================================

void UserListModel::updateUserList(const std::vector<UserInfo>& users)
{
    // 通知视图即将重置模型
    beginResetModel();

    // 替换用户列表
    users_ = users;

    // 重建 ID 到行号的映射
    user_id_to_row_.clear();
    for (int i = 0; i < static_cast<int>(users_.size()); ++i) {
        user_id_to_row_[users_[i].user_id] = i;
    }

    // 通知视图模型已重置完成
    endResetModel();
}

void UserListModel::setSpeakingUser(UserId user_id, bool speaking)
{
    // 查找用户行号
    int row = findRowByUserId(user_id);
    if (row < 0) {
        return;
    }

    // 更新说话状态
    users_[row].is_speaking = speaking;

    // 通知视图该行数据变化
    QModelIndex changed_index = index(row);
    emit dataChanged(changed_index, changed_index,
                     {IsSpeakingRole, DisplayIconRole,
                      Qt::DisplayRole, Qt::DecorationRole});
}

// ============================================================
// 内部辅助方法
// ============================================================

int UserListModel::findRowByUserId(UserId user_id) const
{
    auto it = user_id_to_row_.find(user_id);
    if (it != user_id_to_row_.end()) {
        return it->second;
    }
    return -1;
}

} // namespace nevo
