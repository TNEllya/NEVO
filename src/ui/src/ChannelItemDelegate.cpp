/**
 * @file ChannelItemDelegate.cpp
 * @brief ChannelItemDelegate 实现 - 频道列表项自定义渲染
 *
 * 实现当前频道高亮、用户数量徽章和已加入指示器的绘制。
 */

#include "nevo/ui/ChannelItemDelegate.h"
#include "nevo/ui/ChannelTreeModel.h"

#include <QPainter>
#include <QFontMetrics>

namespace nevo {

ChannelItemDelegate::ChannelItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ChannelItemDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    // 先绘制基础样式
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    bool is_current = index.data(IsCurrentChannelRole).toBool();
    int user_count = index.data(UserCountRole).toInt();

    // 绘制背景
    painter->save();

    if (is_current) {
        // 左侧强调条（4px 宽）
        QRect accent_rect(option.rect.x(), option.rect.y(),
                          4, option.rect.height());
        painter->fillRect(accent_rect, QColor(76, 175, 80));

        // 整行半透明背景
        QRect bg_rect(option.rect.x() + 4, option.rect.y(),
                      option.rect.width() - 4, option.rect.height());
        painter->fillRect(bg_rect, QColor(42, 87, 141, 60));
    }

    painter->restore();

    // 绘制标准内容（图标+文字），但留出徽章空间
    QStyleOptionViewItem adjusted = opt;
    if (user_count > 0) {
        // 右侧留出 36px 给徽章
        adjusted.rect.setWidth(adjusted.rect.width() - 36);
    }

    QStyledItemDelegate::paint(painter, adjusted, index);

    // 绘制用户数量徽章
    if (user_count > 0) {
        painter->save();

        QString count_text = QString::number(user_count);
        QFont badge_font = option.font;
        badge_font.setPointSize(badge_font.pointSize() - 1);
        badge_font.setBold(false);
        QFontMetrics fm(badge_font);

        int text_width = fm.horizontalAdvance(count_text);
        int badge_width = text_width + 10;
        int badge_height = fm.height() + 4;
        int badge_x = option.rect.right() - badge_width - 6;
        int badge_y = option.rect.y() + (option.rect.height() - badge_height) / 2;

        // 徽章背景
        QRect badge_rect(badge_x, badge_y, badge_width, badge_height);
        QColor badge_bg = is_current ? QColor(46, 160, 67, 180)
                                     : QColor(80, 90, 110, 150);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badge_bg);
        painter->drawRoundedRect(badge_rect, 4, 4);

        // 徽章文字
        painter->setFont(badge_font);
        painter->setPen(QColor(220, 230, 240));
        painter->drawText(badge_rect, Qt::AlignCenter, count_text);

        painter->restore();
    }

    // 已加入指示：绿色小圆点
    if (is_current) {
        painter->save();
        int dot_size = 6;
        int dot_x = option.rect.x() + 8;
        int dot_y = option.rect.y() + (option.rect.height() - dot_size) / 2;
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(76, 217, 100));
        painter->drawEllipse(dot_x, dot_y, dot_size, dot_size);
        painter->restore();
    }
}

QSize ChannelItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);

    // 为徽章留出空间
    int user_count = index.data(UserCountRole).toInt();
    if (user_count > 0) {
        size.setWidth(size.width() + 36);
    }

    return size;
}

} // namespace nevo
