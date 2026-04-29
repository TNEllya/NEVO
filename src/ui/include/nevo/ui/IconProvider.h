#pragma once
/**
 * @file IconProvider.h
 * @brief 现代化图标提供器 - QPainter 绘制 SVG 风格图标
 *
 * IconProvider 使用 QPainter 动态绘制扁平化、SVG 风格的现代图标，
 * 替代 Qt 标准图标，确保在深色主题下视觉一致。
 */

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QColor>

namespace nevo {

class IconProvider {
public:
    static QIcon channelIcon(int size = 20);
    static QIcon currentChannelIcon(int size = 20);
    static QIcon speakingIcon(int size = 16);
    static QIcon mutedIcon(int size = 16);
    static QIcon deafenedIcon(int size = 16);
    static QIcon userIcon(int size = 16);

private:
    static QPixmap createPixmap(int size);
};

} // namespace nevo
