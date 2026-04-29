/**
 * @file SessionTableModel.cpp
 * @brief 会话表格模型实现
 */

#include "nevo/server/ui/SessionTableModel.h"

#include <QIcon>

namespace nevo {

SessionTableModel::SessionTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

SessionTableModel::~SessionTableModel() = default;

void SessionTableModel::updateFromSnapshot(
    const ServerStatusSnapshot& snapshot)
{
    beginResetModel();
    sessions_ = snapshot.sessions;
    endResetModel();
}

int SessionTableModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return static_cast<int>(sessions_.size());
}

int SessionTableModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 4;
}

QVariant SessionTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(sessions_.size())) {
        return QVariant();
    }

    const auto& s = sessions_[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return QString::number(s.user_id);
            case 1: return QString::fromStdString(s.username);
            case 2: return QString::fromStdString(s.remote_address);
            case 3:
                if (s.is_muted) return tr("Muted");
                if (s.is_speaking) return tr("Speaking");
                return tr("Idle");
        }
    } else if (role == Qt::DecorationRole && index.column() == 3) {
        if (s.is_muted) {
            return QIcon::fromTheme(QStringLiteral("audio-volume-muted"));
        } else if (s.is_speaking) {
            return QIcon::fromTheme(QStringLiteral("audio-volume-high"));
        }
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 0) {
            return Qt::AlignCenter;
        }
    }

    return QVariant();
}

QVariant SessionTableModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }

    switch (section) {
        case 0: return tr("User ID");
        case 1: return tr("Username");
        case 2: return tr("Address");
        case 3: return tr("Status");
    }
    return QVariant();
}

void SessionTableModel::invalidateHeaders()
{
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
}

} // namespace nevo::server::ui
