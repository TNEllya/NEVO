#pragma once
/**
 * @file ChannelTreeModel.h
 * @brief 树状频道模型 - QAbstractItemModel 实现
 *
 * ChannelTreeModel 为 NEVO VoIP 客户端提供树形频道列表的 Qt 模型层。
 * 它将服务器下发的扁平 ChannelInfo 列表重建为嵌套树结构，
 * 供 QTreeView 展示和交互。
 *
 * 核心功能：
 *   1. updateFromChannelList(): 从服务器数据重建整棵树
 *   2. Display role: 显示频道名称
 *   3. Decoration role: 显示频道图标
 *   4. 双击信号：通知 UI 层用户希望加入某个频道
 *
 * 内部数据结构：
 *   TreeItem 构成的 N 叉树，每个 TreeItem 持有 channel_id、名称、
 *   子节点列表和父节点指针。TreeItem 的生命周期由模型管理。
 *
 * 线程安全：
 *   - 所有模型操作应在主线程（GUI 线程）执行
 *   - updateFromChannelList() 在内部调用 beginResetModel/endResetModel
 */

#include <QAbstractItemModel>
#include <QIcon>
#include <memory>
#include <unordered_map>
#include <vector>

#include "nevo/core/common/Types.h"
#include "nevo/core/model/Channel.h"

namespace nevo {

// ============================================================
// Custom Data Roles
// ============================================================

/// 自定义数据角色：频道 ID
constexpr int ChannelIdRole = Qt::UserRole + 1;
/// 自定义数据角色：是否为当前频道
constexpr int IsCurrentChannelRole = Qt::UserRole + 2;
/// 自定义数据角色：频道用户数
constexpr int UserCountRole = Qt::UserRole + 3;

// ============================================================
// ChannelTreeModel - 树形频道模型
// ============================================================

/**
 * @class ChannelTreeModel
 * @brief QAbstractItemModel 实现，用于树状频道显示
 *
 * 将 ChannelInfo 扁平列表重建为嵌套树结构，
 * 供 QTreeView 展示。支持双击加入频道。
 *
 * 典型用法：
 * @code
 *   ChannelTreeModel model;
 *   model.updateFromChannelList(channel_list);
 *
 *   QTreeView* tree = new QTreeView;
 *   tree->setModel(&model);
 *
 *   // 双击加入频道
 *   connect(tree, &QTreeView::doubleClicked,
 *           &model, &ChannelTreeModel::onDoubleClicked);
 *   connect(&model, &ChannelTreeModel::joinChannelRequested,
 *           [](ChannelId id) { clientCore.joinChannel(id); });
 * @endcode
 */
class ChannelTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    // ============================================================
    // TreeItem - 内部树节点
    // ============================================================

    /**
     * @class TreeItem
     * @brief 内部树节点，持有频道信息和子节点列表
     *
     * TreeItem 不继承 QObject，纯粹的数据节点。
     * 生命周期由 ChannelTreeModel 管理。
     */
    class TreeItem {
    public:
        /// 构造根节点
        explicit TreeItem(ChannelId channel_id = ChannelId(0),
                          const std::string& name = "",
                          TreeItem* parent = nullptr)
            : channel_id_(channel_id), name_(name), parent_(parent) {}

        /// 析构时自动删除所有子节点
        ~TreeItem() { qDeleteAll(children_); }

        // 禁止拷贝
        TreeItem(const TreeItem&) = delete;
        TreeItem& operator=(const TreeItem&) = delete;

        // --- 访问器 ---

        /// 获取频道 ID
        ChannelId channelId() const { return channel_id_; }

        /// 获取频道名称
        const std::string& name() const { return name_; }

        /// 获取父节点
        TreeItem* parent() const { return parent_; }

        /// 获取子节点列表
        const QList<TreeItem*>& children() const { return children_; }

        /// 获取子节点数量
        int childCount() const { return children_.size(); }

        /// 获取指定索引的子节点
        TreeItem* child(int index) const {
            return (index >= 0 && index < children_.size())
                       ? children_.at(index) : nullptr;
        }

        /// 获取本节点在父节点子列表中的行号
        int row() const {
            if (parent_) {
                return parent_->children_.indexOf(
                    const_cast<TreeItem*>(this));
            }
            return 0;
        }

        // --- 修改方法 ---

        /// 添加子节点
        void appendChild(TreeItem* child) {
            children_.append(child);
            child->parent_ = this;
        }

        /// 设置频道名称
        void setName(const std::string& name) { name_ = name; }

    private:
        ChannelId channel_id_;       ///< 频道 ID
        std::string name_;           ///< 频道名称
        TreeItem* parent_;           ///< 父节点指针（非拥有）
        QList<TreeItem*> children_;  ///< 子节点列表（拥有所有权）
    };

    // ============================================================
    // 构造 / 析构
    // ============================================================

    /// 构造函数
    /// @param parent 父 QObject
    explicit ChannelTreeModel(QObject* parent = nullptr);

    /// 析构函数：删除根节点及其所有子节点
    ~ChannelTreeModel() override;

    // 禁止拷贝
    ChannelTreeModel(const ChannelTreeModel&) = delete;
    ChannelTreeModel& operator=(const ChannelTreeModel&) = delete;

    // ============================================================
    // QAbstractItemModel 接口实现
    // ============================================================

    /// 获取指定父节点下的行数
    /// @param parent 父节点索引
    /// @return 子节点数量
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /// 获取列数（固定为 1 列：频道名称）
    /// @param parent 父节点索引
    /// @return 列数
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /// 获取模型索引
    /// @param row    行号
    /// @param column 列号
    /// @param parent 父节点索引
    /// @return 对应的 QModelIndex
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;

    /// 获取父节点索引
    /// @param child 子节点索引
    /// @return 父节点的 QModelIndex
    QModelIndex parent(const QModelIndex& child) const override;

    /// 获取数据（Display role 显示名称，Decoration role 显示图标）
    /// @param index 模型索引
    /// @param role  数据角色
    /// @return 对应的数据
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;

    // ============================================================
    // 数据更新
    // ============================================================

    /**
     * @brief 从服务器频道列表重建树结构
     *
     * 接收扁平的 ChannelInfo 列表，根据 parent_id 重建为嵌套树。
     * 调用 beginResetModel/endResetModel 通知视图刷新。
     *
     * @param channels 服务器下发的频道信息列表
     */
    void updateFromChannelList(const std::vector<ChannelInfo>& channels);

    // ============================================================
    // 索引 / ID 转换
    // ============================================================

    /**
     * @brief 从 QModelIndex 获取频道 ID
     *
     * @param index 模型索引
     * @return 频道 ID，无效索引返回 INVALID_CHANNEL_ID
     */
    ChannelId channelIdFromIndex(const QModelIndex& index) const;

    /**
     * @brief 从频道 ID 获取 QModelIndex
     *
     * 递归搜索整棵树查找匹配的频道 ID。
     *
     * @param channel_id 频道 ID
     * @return 对应的 QModelIndex，未找到返回无效索引
     */
    QModelIndex indexFromChannelId(ChannelId channel_id) const;

    // ============================================================
    // 交互信号
    // ============================================================

signals:

    /**
     * @brief 用户双击频道时发射
     *
     * UI 层可连接此信号，调用 ClientCore::joinChannel()。
     *
     * @param channel_id 用户双击的频道 ID
     */
    void joinChannelRequested(ChannelId channel_id);

    /**
     * @brief 用户请求离开当前频道时发射
     *
     * UI 层可连接此信号，调用 ClientCore::leaveChannel()。
     */
    void leaveChannelRequested();

public slots:

    /**
     * @brief 处理视图的双击事件
     *
     * 将 QModelIndex 转换为频道 ID 并发射 joinChannelRequested 信号。
     *
     * @param index 双击的模型索引
     */
    void onDoubleClicked(const QModelIndex& index);

    /**
     * @brief 设置当前已加入的频道
     *
     * 更新当前频道 ID，触发对应项的 dataChanged 以更新显示。
     *
     * @param channel_id 当前频道 ID
     */
    void setCurrentChannel(ChannelId channel_id);

    /**
     * @brief 设置指定频道的用户数
     *
     * @param channel_id 频道 ID
     * @param count      用户数量
     */
    void setChannelUserCount(ChannelId channel_id, int count);

    /**
     * @brief 清除当前频道状态
     *
     * 离开频道或断开连接时调用。
     */
    void clearCurrentChannel();

private:
    // ============================================================
    // 内部辅助方法
    // ============================================================

    /**
     * @brief 递归构建子树
     *
     * 根据 parent_id 将扁平列表中的频道挂载到对应的父节点下。
     *
     * @param parent_item 父 TreeItem 节点
     * @param channels    所有频道信息列表
     * @param parent_id   当前子树的父频道 ID
     */
    void buildSubtree(TreeItem* parent_item,
                      const std::vector<ChannelInfo>& channels,
                      ChannelId parent_id);

    /**
     * @brief 递归搜索频道 ID 对应的 TreeItem
     *
     * @param item        起始搜索节点
     * @param channel_id  目标频道 ID
     * @return 匹配的 TreeItem 指针，未找到返回 nullptr
     */
    TreeItem* findItemByChannelId(TreeItem* item,
                                  ChannelId channel_id) const;

    /**
     * @brief 从 TreeItem 创建 QModelIndex
     *
     * @param item TreeItem 指针
     * @return 对应的 QModelIndex
     */
    QModelIndex createIndexFromItem(TreeItem* item) const;

    // ============================================================
    // 数据成员
    // ============================================================

    /// 根节点（虚拟根，不对应任何频道，仅作为树根容器）
    TreeItem* root_item_;

    /// 频道图标
    QIcon channel_icon_;

    /// 当前已加入频道的图标
    QIcon current_channel_icon_;

    /// 当前已加入的频道 ID
    ChannelId current_channel_id_;

    /// 每个频道的用户数量
    std::unordered_map<ChannelId, int> user_counts_;
};

} // namespace nevo
