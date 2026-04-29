#pragma once
/**
 * @file ChannelPanel.h
 * @brief Channel management panel for server GUI
 *
 * Provides a tree view of channels with add, delete, and rename functionality.
 */

#include <QFrame>
#include <QTreeView>
#include <QStandardItemModel>
#include <QItemSelection>

#include "nevo/server/ServerCore.h"

class QPushButton;
class QLabel;
class QLineEdit;
class QInputDialog;

namespace nevo {

class ChannelManager;

class ChannelPanel : public QFrame {
    Q_OBJECT

public:
    explicit ChannelPanel(QWidget* parent = nullptr);
    ~ChannelPanel() override;

    /// Update the channel tree from a status snapshot
    void updateFromSnapshot(const ServerStatusSnapshot& snapshot);

    /// Set the server core for channel operations
    void setServerCore(ServerCore* core);

    /// Retranslate UI strings for dynamic language switching
    void retranslateUi();

signals:
    /// Emitted when a channel operation is performed (for log/notification)
    void channelOperationCompleted(const QString& message);

private slots:
    void onAddChannel();
    void onDeleteChannel();
    void onRenameChannel();
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
    void setupUi();
    void rebuildTree(const std::vector<ChannelSnapshot>& channels);
    QStandardItem* findItemByChannelId(uint64_t channel_id) const;
    QStandardItem* findItemRecursive(QStandardItem* parent, uint64_t channel_id) const;
    uint64_t selectedChannelId() const;
    bool isSpecialChannel(uint64_t channel_id) const;

    QTreeView* channel_tree_ = nullptr;
    QStandardItemModel* tree_model_ = nullptr;
    QPushButton* add_btn_ = nullptr;
    QPushButton* delete_btn_ = nullptr;
    QPushButton* rename_btn_ = nullptr;
    QLabel* title_label_ = nullptr;

    /// Cached snapshot for reference
    std::vector<ChannelSnapshot> cached_channels_;

    /// Server core pointer (not owned)
    ServerCore* server_core_ = nullptr;

    /// Special channel IDs (root and default) that cannot be deleted
    uint64_t root_channel_id_ = 0;
    uint64_t default_channel_id_ = 0;

protected:
    void changeEvent(QEvent* event) override;
};

} // namespace nevo
