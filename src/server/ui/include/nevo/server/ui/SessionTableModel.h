#pragma once
/**
 * @file SessionTableModel.h
 * @brief 活跃会话表格模型
 */

#include <QAbstractTableModel>
#include <vector>
#include <memory>

#include "nevo/server/ServerCore.h"

namespace nevo {

class SessionTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColUserId = 0,
        ColUsername,
        ColRemoteAddress,
        ColStatus,
        ColCount
    };

    explicit SessionTableModel(QObject* parent = nullptr);
    ~SessionTableModel() override;

    void updateFromSnapshot(const ServerStatusSnapshot& snapshot);

    /// Emit headerDataChanged so headers pick up new translations
    void invalidateHeaders();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    std::vector<SessionSnapshot> sessions_;
};

} // namespace nevo
