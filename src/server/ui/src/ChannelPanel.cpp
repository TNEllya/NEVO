/**
 * @file ChannelPanel.cpp
 * @brief Channel management panel implementation
 */

#include "nevo/server/ui/ChannelPanel.h"
#include "nevo/server/ChannelManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardItem>
#include <QEvent>

#include <unordered_map>

namespace nevo {

// ============================================================
// Construction / Destruction
// ============================================================

ChannelPanel::ChannelPanel(QWidget* parent)
    : QFrame(parent)
{
    setupUi();
}

ChannelPanel::~ChannelPanel() = default;

// ============================================================
// Public methods
// ============================================================

void ChannelPanel::updateFromSnapshot(const ServerStatusSnapshot& snapshot) {
    cached_channels_ = snapshot.channels;
    rebuildTree(cached_channels_);
}

void ChannelPanel::setServerCore(ServerCore* core) {
    server_core_ = core;
}

// ============================================================
// Private slots
// ============================================================

void ChannelPanel::onAddChannel() {
    if (!server_core_) {
        QMessageBox::warning(this, tr("Error"), tr("Server is not running"));
        return;
    }

    // Determine parent channel
    uint64_t parent_id = selectedChannelId();
    if (parent_id == 0) {
        parent_id = root_channel_id_;
    }

    bool ok = false;
    QString name = QInputDialog::getText(this,
        tr("Add Channel"),
        tr("Channel name:"),
        QLineEdit::Normal,
        QString(),
        &ok);

    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    auto channel_mgr = server_core_->channelManager();
    if (!channel_mgr) {
        QMessageBox::warning(this, tr("Error"), tr("Channel manager not available"));
        return;
    }

    auto result = channel_mgr->createChannel(ChannelId(parent_id),
                                              name.trimmed().toStdString(),
                                              UserId(0));
    if (result) {
        // Notify all connected clients about the channel change
        server_core_->broadcastChannelListUpdate();
        emit channelOperationCompleted(
            tr("Channel '%1' created").arg(name.trimmed()));
    } else {
        QMessageBox::warning(this, tr("Error"),
            QString::fromStdString(result.error().message()));
    }
}

void ChannelPanel::onDeleteChannel() {
    if (!server_core_) {
        QMessageBox::warning(this, tr("Error"), tr("Server is not running"));
        return;
    }

    uint64_t channel_id = selectedChannelId();
    if (channel_id == 0) {
        QMessageBox::information(this, tr("Info"), tr("Please select a channel to delete"));
        return;
    }

    if (isSpecialChannel(channel_id)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Cannot delete the Root or Lobby channel"));
        return;
    }

    // Find channel name for confirmation
    QString channel_name;
    QStandardItem* item = findItemByChannelId(channel_id);
    if (item) {
        channel_name = item->text();
    }

    auto reply = QMessageBox::question(this,
        tr("Delete Channel"),
        tr("Are you sure you want to delete channel '%1'?\n"
           "All sub-channels will also be deleted.\n"
           "Users in these channels will be moved to Lobby.")
            .arg(channel_name),
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    auto channel_mgr = server_core_->channelManager();
    if (!channel_mgr) {
        QMessageBox::warning(this, tr("Error"), tr("Channel manager not available"));
        return;
    }

    auto result = channel_mgr->deleteChannel(ChannelId(channel_id));
    if (result) {
        // Notify all connected clients about the channel change
        server_core_->broadcastChannelListUpdate();
        emit channelOperationCompleted(
            tr("Channel '%1' deleted").arg(channel_name));
    } else {
        QMessageBox::warning(this, tr("Error"),
            QString::fromStdString(result.error().message()));
    }
}

void ChannelPanel::onRenameChannel() {
    if (!server_core_) {
        QMessageBox::warning(this, tr("Error"), tr("Server is not running"));
        return;
    }

    uint64_t channel_id = selectedChannelId();
    if (channel_id == 0) {
        QMessageBox::information(this, tr("Info"), tr("Please select a channel to rename"));
        return;
    }

    if (channel_id == root_channel_id_) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot rename the Root channel"));
        return;
    }

    // Find current name
    QString old_name;
    QStandardItem* item = findItemByChannelId(channel_id);
    if (item) {
        old_name = item->text();
    }

    bool ok = false;
    QString new_name = QInputDialog::getText(this,
        tr("Rename Channel"),
        tr("New channel name:"),
        QLineEdit::Normal,
        old_name,
        &ok);

    if (!ok || new_name.trimmed().isEmpty() || new_name.trimmed() == old_name) {
        return;
    }

    auto channel_mgr = server_core_->channelManager();
    if (!channel_mgr) {
        QMessageBox::warning(this, tr("Error"), tr("Channel manager not available"));
        return;
    }

    auto result = channel_mgr->renameChannel(ChannelId(channel_id),
                                              new_name.trimmed().toStdString());
    if (result) {
        // Notify all connected clients about the channel change
        server_core_->broadcastChannelListUpdate();
        emit channelOperationCompleted(
            tr("Channel renamed from '%1' to '%2'").arg(old_name, new_name.trimmed()));
    } else {
        QMessageBox::warning(this, tr("Error"),
            QString::fromStdString(result.error().message()));
    }
}

void ChannelPanel::onSelectionChanged(const QItemSelection& selected,
                                       const QItemSelection& /*deselected*/) {
    bool has_selection = !selected.isEmpty();
    uint64_t channel_id = selectedChannelId();

    // Can delete if selected and not a special channel
    delete_btn_->setEnabled(has_selection && !isSpecialChannel(channel_id));
    // Can rename if selected and not root
    rename_btn_->setEnabled(has_selection && channel_id != root_channel_id_);
}

// ============================================================
// Internal methods
// ============================================================

void ChannelPanel::setupUi() {
    setStyleSheet(QStringLiteral(
        "ChannelPanel {"
        "  background-color: #1e2229;"
        "  border: 1px solid #2c3138;"
        "  border-radius: 8px;"
        "}"
    ));

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(12, 10, 12, 10);
    main_layout->setSpacing(8);

    // Title
    title_label_ = new QLabel(tr("Channels"), this);
    title_label_->setStyleSheet(QStringLiteral(
        "color: #c5c8d4; font-weight: bold; font-size: 13px;"));
    main_layout->addWidget(title_label_);

    // Channel tree
    tree_model_ = new QStandardItemModel(this);
    tree_model_->setHorizontalHeaderLabels(QStringList() << tr("Channel"));

    channel_tree_ = new QTreeView(this);
    channel_tree_->setModel(tree_model_);
    channel_tree_->setHeaderHidden(true);
    channel_tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channel_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    channel_tree_->setExpandsOnDoubleClick(true);
    channel_tree_->setAnimated(true);
    channel_tree_->setIndentation(16);
    channel_tree_->setStyleSheet(QStringLiteral(
        "QTreeView {"
        "  background-color: #282c34;"
        "  color: #c5c8d4;"
        "  border: 1px solid #2c3138;"
        "  border-radius: 4px;"
        "  font-size: 12px;"
        "  padding: 4px;"
        "}"
        "QTreeView::item {"
        "  padding: 4px 2px;"
        "  border: none;"
        "}"
        "QTreeView::item:selected {"
        "  background-color: #2d5aa0;"
        "  color: #ffffff;"
        "}"
        "QTreeView::item:hover {"
        "  background-color: #333842;"
        "}"
    ));

    connect(channel_tree_->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &ChannelPanel::onSelectionChanged);

    main_layout->addWidget(channel_tree_, 1);

    // Button row
    QHBoxLayout* btn_layout = new QHBoxLayout();
    btn_layout->setSpacing(8);

    add_btn_ = new QPushButton(tr("Add"), this);
    add_btn_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #2d5aa0;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 5px 14px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #3a6fc4; }"
        "QPushButton:pressed { background-color: #1e4a8a; }"
        "QPushButton:disabled { background-color: #2a2f3b; color: #5a6070; }"
    ));
    connect(add_btn_, &QPushButton::clicked, this, &ChannelPanel::onAddChannel);
    btn_layout->addWidget(add_btn_);

    delete_btn_ = new QPushButton(tr("Delete"), this);
    delete_btn_->setEnabled(false);
    delete_btn_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #8c3030;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 5px 14px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #a04040; }"
        "QPushButton:pressed { background-color: #702020; }"
        "QPushButton:disabled { background-color: #2a2f3b; color: #5a6070; }"
    ));
    connect(delete_btn_, &QPushButton::clicked, this, &ChannelPanel::onDeleteChannel);
    btn_layout->addWidget(delete_btn_);

    rename_btn_ = new QPushButton(tr("Rename"), this);
    rename_btn_->setEnabled(false);
    rename_btn_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #3a3f4b;"
        "  color: #c5c8d4;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 5px 14px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover { background-color: #4a5060; }"
        "QPushButton:pressed { background-color: #2a2f3b; }"
        "QPushButton:disabled { background-color: #2a2f3b; color: #5a6070; }"
    ));
    connect(rename_btn_, &QPushButton::clicked, this, &ChannelPanel::onRenameChannel);
    btn_layout->addWidget(rename_btn_);

    btn_layout->addStretch();

    main_layout->addLayout(btn_layout);
}

void ChannelPanel::rebuildTree(const std::vector<ChannelSnapshot>& channels) {
    tree_model_->clear();

    if (channels.empty()) {
        return;
    }

    // Build a map from channel_id to QStandardItem for parent lookup
    std::unordered_map<uint64_t, QStandardItem*> item_map;

    // First pass: create items and identify root/default
    for (const auto& ch : channels) {
        auto* item = new QStandardItem(QString::fromStdString(ch.channel_name));
        item->setData(ch.channel_id, Qt::UserRole);
        item->setEditable(false);

        // Style special channels
        if (ch.parent_id == 0) {
            root_channel_id_ = ch.channel_id;
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }

        item_map[ch.channel_id] = item;
    }

    // Identify default channel (Lobby)
    for (const auto& ch : channels) {
        if (ch.channel_name == "Lobby") {
            default_channel_id_ = ch.channel_id;
            break;
        }
    }

    // Second pass: build hierarchy
    for (const auto& ch : channels) {
        auto it = item_map.find(ch.channel_id);
        if (it == item_map.end()) continue;

        QStandardItem* item = it->second;

        if (ch.parent_id == 0) {
            // Root channel
            tree_model_->appendRow(item);
        } else {
            auto parent_it = item_map.find(ch.parent_id);
            if (parent_it != item_map.end()) {
                parent_it->second->appendRow(item);
            } else {
                // Parent not found, add to root
                tree_model_->appendRow(item);
            }
        }
    }

    // Expand all items
    channel_tree_->expandAll();
}

QStandardItem* ChannelPanel::findItemByChannelId(uint64_t channel_id) const {
    for (int r = 0; r < tree_model_->rowCount(); ++r) {
        QStandardItem* found = findItemRecursive(tree_model_->item(r), channel_id);
        if (found) return found;
    }
    return nullptr;
}

QStandardItem* ChannelPanel::findItemRecursive(QStandardItem* parent, uint64_t channel_id) const {
    if (!parent) return nullptr;
    if (parent->data(Qt::UserRole).toULongLong() == channel_id) return parent;

    for (int r = 0; r < parent->rowCount(); ++r) {
        QStandardItem* found = findItemRecursive(parent->child(r), channel_id);
        if (found) return found;
    }
    return nullptr;
}

uint64_t ChannelPanel::selectedChannelId() const {
    QModelIndexList selected = channel_tree_->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return 0;
    return tree_model_->data(selected.first(), Qt::UserRole).toULongLong();
}

bool ChannelPanel::isSpecialChannel(uint64_t channel_id) const {
    return channel_id == root_channel_id_ || channel_id == default_channel_id_;
}

void ChannelPanel::retranslateUi() {
    if (title_label_) title_label_->setText(tr("Channels"));
    if (add_btn_) add_btn_->setText(tr("Add"));
    if (delete_btn_) delete_btn_->setText(tr("Delete"));
    if (rename_btn_) rename_btn_->setText(tr("Rename"));
    if (tree_model_) {
        tree_model_->setHorizontalHeaderLabels(QStringList() << tr("Channel"));
    }
}

void ChannelPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QFrame::changeEvent(event);
}

} // namespace nevo
