/**
 * @file ChannelTreeModel.cpp
 * @brief ChannelTreeModel 实现 - 树形频道模型
 *
 * 实现了将扁平 ChannelInfo 列表重建为嵌套树结构的完整逻辑，
 * 以及 QAbstractItemModel 的所有必要接口。
 */

#include "nevo/ui/ChannelTreeModel.h"

#include <QApplication>
#include <QStyle>

#include "nevo/ui/IconProvider.h"

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

ChannelTreeModel::ChannelTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , root_item_(new TreeItem(ChannelId(0), "Root", nullptr))
    , channel_icon_(IconProvider::channelIcon())
    , current_channel_icon_(IconProvider::currentChannelIcon())
    , current_channel_id_(INVALID_CHANNEL_ID)
{
}

ChannelTreeModel::~ChannelTreeModel()
{
    delete root_item_;
}

// ============================================================
// QAbstractItemModel 接口实现
// ============================================================

int ChannelTreeModel::rowCount(const QModelIndex& parent) const
{
    // 获取父节点：有效索引取其 TreeItem，否则取根节点
    TreeItem* parent_item = parent.isValid()
                                ? static_cast<TreeItem*>(parent.internalPointer())
                                : root_item_;

    return parent_item->childCount();
}

int ChannelTreeModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    // 固定 1 列：频道名称
    return 1;
}

QModelIndex ChannelTreeModel::index(int row, int column,
                                     const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    // 获取父节点
    TreeItem* parent_item = parent.isValid()
                                ? static_cast<TreeItem*>(parent.internalPointer())
                                : root_item_;

    // 获取子节点
    TreeItem* child_item = parent_item->child(row);
    if (child_item) {
        return createIndex(row, column, child_item);
    }

    return QModelIndex();
}

QModelIndex ChannelTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }

    // 获取子节点的 TreeItem
    TreeItem* child_item = static_cast<TreeItem*>(child.internalPointer());
    TreeItem* parent_item = child_item->parent();

    // 父节点是根节点时返回无效索引
    if (parent_item == root_item_) {
        return QModelIndex();
    }

    // 创建父节点的 QModelIndex
    return createIndex(parent_item->row(), 0, parent_item);
}

QVariant ChannelTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    TreeItem* item = static_cast<TreeItem*>(index.internalPointer());
    if (!item) {
        return QVariant();
    }

    bool is_current = (item->channelId() == current_channel_id_);

    switch (role) {
        case Qt::DisplayRole: {
            QString name = QString::fromStdString(item->name());
            auto it = user_counts_.find(item->channelId());
            if (it != user_counts_.end() && it->second > 0) {
                return tr("%1 (%2)").arg(name).arg(it->second);
            }
            return name;
        }

        case Qt::DecorationRole:
            return is_current ? current_channel_icon_ : channel_icon_;

        case Qt::ToolTipRole:
            return QString("Channel ID: %1").arg(item->channelId().value);

        case Qt::FontRole:
            if (is_current) {
                QFont font;
                font.setBold(true);
                return font;
            }
            return QVariant();

        case Qt::BackgroundRole:
            if (is_current) {
                return QColor(8, 66, 160, 40);  // MD3 Primary Container 低透明度
            }
            return QVariant();

        case Qt::ForegroundRole:
            if (is_current) {
                return QColor(168, 199, 250);  // MD3 Primary
            }
            return QColor(226, 226, 233);  // MD3 On Surface

        case ChannelIdRole:
            return QVariant::fromValue(item->channelId().value);

        case IsCurrentChannelRole:
            return is_current;

        case UserCountRole: {
            auto it = user_counts_.find(item->channelId());
            return it != user_counts_.end() ? it->second : 0;
        }

        default:
            return QVariant();
    }
}

// ============================================================
// 数据更新
// ============================================================

void ChannelTreeModel::updateFromChannelList(
    const std::vector<ChannelInfo>& channels)
{
    // 通知视图即将重置模型
    beginResetModel();

    // 删除旧的树结构（根节点的所有子节点）
    // 由于 TreeItem 析构会递归删除子节点，只需删除根节点的直接子节点
    // 但更安全的做法是替换根节点
    delete root_item_;
    root_item_ = new TreeItem(ChannelId(0), "Root", nullptr);

    // 更新用户数量映射
    user_counts_.clear();
    for (const auto& ch : channels) {
        if (!ch.user_ids.empty()) {
            user_counts_[ch.channel_id] = static_cast<int>(ch.user_ids.size());
        }
    }

    // 递归构建树结构：从根频道（parent_id == 0）开始
    buildSubtree(root_item_, channels, ChannelId(0));

    // 通知视图模型已重置完成
    endResetModel();

    // 保留当前频道高亮（如果频道仍存在）
    if (current_channel_id_) {
        QModelIndex idx = indexFromChannelId(current_channel_id_);
        if (!idx.isValid()) {
            current_channel_id_ = INVALID_CHANNEL_ID;
        }
    }
}

// ============================================================
// 索引 / ID 转换
// ============================================================

ChannelId ChannelTreeModel::channelIdFromIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return INVALID_CHANNEL_ID;
    }

    TreeItem* item = static_cast<TreeItem*>(index.internalPointer());
    if (!item) {
        return INVALID_CHANNEL_ID;
    }

    return item->channelId();
}

QModelIndex ChannelTreeModel::indexFromChannelId(ChannelId channel_id) const
{
    if (!channel_id) {
        return QModelIndex();
    }

    // 从根节点开始递归搜索
    TreeItem* item = findItemByChannelId(root_item_, channel_id);
    if (!item) {
        return QModelIndex();
    }

    return createIndexFromItem(item);
}

// ============================================================
// 交互槽函数
// ============================================================

void ChannelTreeModel::onDoubleClicked(const QModelIndex& index)
{
    ChannelId channel_id = channelIdFromIndex(index);
    if (channel_id) {
        emit joinChannelRequested(channel_id);
    }
}

// ============================================================
// 内部辅助方法
// ============================================================

void ChannelTreeModel::buildSubtree(TreeItem* parent_item,
                                     const std::vector<ChannelInfo>& channels,
                                     ChannelId parent_id)
{
    for (const auto& channel : channels) {
        // 只处理属于当前父节点的频道
        if (channel.parent_id == parent_id) {
            // 创建子节点
            TreeItem* child_item = new TreeItem(
                channel.channel_id, channel.name, parent_item);
            parent_item->appendChild(child_item);

            // 递归构建该频道的子树
            buildSubtree(child_item, channels, channel.channel_id);
        }
    }
}

ChannelTreeModel::TreeItem* ChannelTreeModel::findItemByChannelId(
    TreeItem* item, ChannelId channel_id) const
{
    if (!item) {
        return nullptr;
    }

    // 检查当前节点
    if (item->channelId() == channel_id) {
        return item;
    }

    // 递归搜索子节点
    for (TreeItem* child : item->children()) {
        TreeItem* found = findItemByChannelId(child, channel_id);
        if (found) {
            return found;
        }
    }

    return nullptr;
}

QModelIndex ChannelTreeModel::createIndexFromItem(TreeItem* item) const
{
    if (!item || item == root_item_) {
        return QModelIndex();
    }

    return createIndex(item->row(), 0, item);
}

// ============================================================
// 新增槽函数
// ============================================================

void ChannelTreeModel::setCurrentChannel(ChannelId channel_id)
{
    ChannelId old_channel = current_channel_id_;
    current_channel_id_ = channel_id;

    // 通知旧频道项更新
    if (old_channel && old_channel != channel_id) {
        QModelIndex old_idx = indexFromChannelId(old_channel);
        if (old_idx.isValid()) {
            emit dataChanged(old_idx, old_idx,
                {Qt::DisplayRole, Qt::DecorationRole, Qt::FontRole,
                 Qt::BackgroundRole, Qt::ForegroundRole, IsCurrentChannelRole});
        }
    }

    // 通知新频道项更新
    QModelIndex new_idx = indexFromChannelId(channel_id);
    if (new_idx.isValid()) {
        emit dataChanged(new_idx, new_idx,
            {Qt::DisplayRole, Qt::DecorationRole, Qt::FontRole,
             Qt::BackgroundRole, Qt::ForegroundRole, IsCurrentChannelRole});
    }
}

void ChannelTreeModel::setChannelUserCount(ChannelId channel_id, int count)
{
    user_counts_[channel_id] = count;

    QModelIndex idx = indexFromChannelId(channel_id);
    if (idx.isValid()) {
        emit dataChanged(idx, idx, {Qt::DisplayRole, UserCountRole});
    }
}

void ChannelTreeModel::clearCurrentChannel()
{
    ChannelId old_channel = current_channel_id_;
    current_channel_id_ = INVALID_CHANNEL_ID;

    if (old_channel) {
        QModelIndex old_idx = indexFromChannelId(old_channel);
        if (old_idx.isValid()) {
            emit dataChanged(old_idx, old_idx,
                {Qt::DisplayRole, Qt::DecorationRole, Qt::FontRole,
                 Qt::BackgroundRole, Qt::ForegroundRole, IsCurrentChannelRole});
        }
    }
}

} // namespace nevo
