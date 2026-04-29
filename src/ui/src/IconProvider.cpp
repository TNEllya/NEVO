/**
 * @file IconProvider.cpp
 * @brief IconProvider 实现 - 现代化 SVG 风格图标绘制
 */

#include "nevo/ui/IconProvider.h"

#include <QPainter>
#include <QPixmap>
#include <QPainterPath>

namespace nevo {

QPixmap IconProvider::createPixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

QIcon IconProvider::channelIcon(int size)
{
    QPixmap pixmap = createPixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    qreal scale = size / 24.0;
    painter.scale(scale, scale);

    QPainterPath path;
    path.moveTo(2, 5);
    path.lineTo(10, 5);
    path.lineTo(12, 3);
    path.lineTo(22, 3);
    path.lineTo(22, 19);
    path.lineTo(2, 19);
    path.closeSubpath();

    painter.setPen(QPen(QColor(139, 195, 247), 1.5));
    painter.setBrush(QColor(66, 133, 244));
    painter.drawPath(path);

    return QIcon(pixmap);
}

QIcon IconProvider::currentChannelIcon(int size)
{
    QPixmap pixmap = createPixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    qreal scale = size / 24.0;
    painter.scale(scale, scale);

    QPainterPath path;
    path.moveTo(2, 5);
    path.lineTo(10, 5);
    path.lineTo(12, 3);
    path.lineTo(22, 3);
    path.lineTo(22, 19);
    path.lineTo(2, 19);
    path.closeSubpath();

    painter.setPen(QPen(QColor(100, 230, 150), 1.5));
    painter.setBrush(QColor(46, 160, 67));
    painter.drawPath(path);

    // Small green dot indicator
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(76, 217, 100));
    painter.drawEllipse(17, 2, 5, 5);

    return QIcon(pixmap);
}

QIcon IconProvider::speakingIcon(int size)
{
    QPixmap pixmap = createPixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    int padding = 2;
    int drawSize = size - 2 * padding;
    int center = size / 2;

    // Glow
    QRadialGradient glow(center, center, drawSize / 2.0 + 2);
    glow.setColorAt(0, QColor(100, 230, 150, 120));
    glow.setColorAt(1, QColor(100, 230, 150, 0));
    painter.setBrush(glow);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(padding - 2, padding - 2, drawSize + 4, drawSize + 4);

    // Green circle
    painter.setBrush(QColor(76, 217, 100));
    painter.drawEllipse(padding, padding, drawSize, drawSize);

    return QIcon(pixmap);
}

QIcon IconProvider::mutedIcon(int size)
{
    QPixmap pixmap = createPixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    qreal scale = size / 24.0;
    painter.scale(scale, scale);

    painter.setPen(QPen(QColor(255, 95, 86), 2));
    painter.setBrush(Qt::NoBrush);

    // Microphone body
    QPainterPath micPath;
    micPath.moveTo(8, 6);
    micPath.lineTo(8, 12);
    micPath.arcTo(6, 10, 4, 4, 180, 180);
    micPath.lineTo(12, 14);
    micPath.arcTo(10, 10, 4, 4, 0, 180);
    micPath.lineTo(14, 6);
    painter.drawPath(micPath);

    // Slash
    painter.drawLine(5, 5, 19, 19);

    return QIcon(pixmap);
}

QIcon IconProvider::deafenedIcon(int size)
{
    QPixmap pixmap = createPixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    qreal scale = size / 24.0;
    painter.scale(scale, scale);

    painter.setPen(QPen(QColor(255, 95, 86), 2));
    painter.setBrush(Qt::NoBrush);

    // Headband
    painter.drawArc(6, 4, 12, 10, 0, 180 * 16);
    // Ear cups
    painter.drawRect(5, 10, 4, 6);
    painter.drawRect(15, 10, 4, 6);
    // Slash
    painter.drawLine(5, 5, 19, 19);

    return QIcon(pixmap);
}

QIcon IconProvider::userIcon(int size)
{
    QPixmap pixmap = createPixmap(size);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    int padding = 2;
    int drawSize = size - 2 * padding;

    // Circle background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(80, 85, 95));
    painter.drawEllipse(padding, padding, drawSize, drawSize);

    // User silhouette
    painter.setBrush(QColor(170, 175, 185));
    int headRadius = drawSize / 4;
    int centerX = size / 2;
    int headY = padding + drawSize / 3;
    painter.drawEllipse(centerX - headRadius, headY - headRadius,
                        headRadius * 2, headRadius * 2);

    // Shoulders
    QPainterPath shoulderPath;
    shoulderPath.moveTo(centerX - drawSize / 2 + 4, size - padding - 2);
    shoulderPath.quadTo(centerX, headY + headRadius + 2,
                        centerX + drawSize / 2 - 4, size - padding - 2);
    painter.drawPath(shoulderPath);

    return QIcon(pixmap);
}

} // namespace nevo
