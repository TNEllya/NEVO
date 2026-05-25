/**
 * @file ChannelItemDelegate.cpp
 * @brief ChannelItemDelegate 实现 - 频道列表项自定义渲染 (Material Design 3)
 *
 * 实现当前频道高亮、用户数量徽章和已加入指示器的绘制。
 */

#include "nevo/ui/ChannelItemDelegate.h"
#include "nevo/ui/ChannelTreeModel.h"

#include <QPainter>
#include <QFontMetrics>

namespace nevo {

// MD3 color tokens
static constexpr auto kPrimary          = QColor(168, 199, 250);  // #a8c7fa
static constexpr auto kOnPrimary        = QColor(6, 46, 111);     // #062e6f
static constexpr auto kPrimaryContainer = QColor(8, 66, 160);     // #0842a0
static constexpr auto kSurfaceContainerHigh = QColor(39, 42, 47); // #272a2f
static constexpr auto kOutline          = QColor(142, 144, 153);  // #8e9099
static constexpr auto kOnSurface        = QColor(226, 226, 233);  // #e2e2e9
static constexpr auto kSurfaceVariant   = QColor(68, 71, 79);     // #44474f

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
        // MD3: 左侧强调条（4px）使用 Primary Container
        QRect accent_rect(option.rect.x(), option.rect.y(),
                          4, option.rect.height());
        painter->fillRect(accent_rect, kPrimary);

        // MD3: 整行使用 Primary Container 低透明度
        QRect bg_rect(option.rect.x() + 4, option.rect.y(),
                      option.rect.width() - 4, option.rect.height());
        painter->fillRect(bg_rect, QColor(8, 66, 160, 40));
    }

    painter->restore();

    // 绘制标准内容（图标+文字），但留出徽章空间
    QStyleOptionViewItem adjusted = opt;
    if (user_count > 0) {
        // 右侧留出 40px 给徽章
        adjusted.rect.setWidth(adjusted.rect.width() - 40);
    }

    QStyledItemDelegate::paint(painter, adjusted, index);

    // 绘制用户数量徽章 (MD3 style — rounded pill)
    if (user_count > 0) {
        painter->save();

        QString count_text = QString::number(user_count);
        QFont badge_font = option.font;
        badge_font.setPointSize(badge_font.pointSize() - 1);
        badge_font.setBold(false);
        QFontMetrics fm(badge_font);

        int text_width = fm.horizontalAdvance(count_text);
        int badge_width = text_width + 14;
        int badge_height = fm.height() + 6;
        int badge_x = option.rect.right() - badge_width - 8;
        int badge_y = option.rect.y() + (option.rect.height() - badge_height) / 2;

        // 徽章背景
        QRect badge_rect(badge_x, badge_y, badge_width, badge_height);
        QColor badge_bg = is_current ? QColor(8, 66, 160, 140)
                                     : QColor(68, 71, 79, 160);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badge_bg);
        painter->drawRoundedRect(badge_rect, badge_height / 2, badge_height / 2);

        // 徽章文字
        painter->setFont(badge_font);
        painter->setPen(kOnSurface);
        painter->drawText(badge_rect, Qt::AlignCenter, count_text);

        painter->restore();
    }

    // 已加入指示：MD3 Primary 色圆点
    if (is_current) {
        painter->save();
        int dot_size = 8;
        int dot_x = option.rect.x() + 8;
        int dot_y = option.rect.y() + (option.rect.height() - dot_size) / 2;
        painter->setPen(Qt::NoPen);
        painter->setBrush(kPrimary);
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
        size.setWidth(size.width() + 40);
    }

    return size;
}

} // namespace nevo
