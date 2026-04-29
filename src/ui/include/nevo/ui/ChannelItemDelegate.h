#pragma once
/**
 * @file ChannelItemDelegate.h
 * @brief 频道列表项委托 - 自定义渲染频道项
 *
 * ChannelItemDelegate 为频道树视图提供自定义渲染：
 *   - 当前频道：左侧蓝色/绿色强调条 + 加粗文字
 *   - 用户数量：右侧小徽章显示频道内用户数
 *   - 已加入指示：绿色圆点
 *
 * 继承 QStyledItemDelegate，仅覆盖 paint() 和 sizeHint()。
 */

#include <QStyledItemDelegate>

namespace nevo {

class ChannelItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ChannelItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

} // namespace nevo
