/**
 * @file ThemeManager.cpp
 * @brief ThemeManager 实现 - 全局主题管理器
 */

#include "nevo/ui/ThemeManager.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QFontDatabase>
#include <QDebug>

namespace nevo {

ThemeManager& ThemeManager::instance()
{
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
}

ThemeManager::~ThemeManager() = default;

void ThemeManager::applyDarkTheme()
{
    // Set modern application font
    QFont font = QApplication::font();
    font.setPointSize(10);
    font.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(font);

    // Apply dark palette as fallback/base
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(30, 30, 46));
    palette.setColor(QPalette::WindowText, QColor(205, 214, 244));
    palette.setColor(QPalette::Base, QColor(49, 50, 68));
    palette.setColor(QPalette::AlternateBase, QColor(69, 71, 90));
    palette.setColor(QPalette::ToolTipBase, QColor(49, 50, 68));
    palette.setColor(QPalette::ToolTipText, QColor(205, 214, 244));
    palette.setColor(QPalette::Text, QColor(205, 214, 244));
    palette.setColor(QPalette::Button, QColor(69, 71, 90));
    palette.setColor(QPalette::ButtonText, QColor(205, 214, 244));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, QColor(137, 180, 250));
    palette.setColor(QPalette::HighlightedText, QColor(30, 30, 46));
    palette.setColor(QPalette::Link, QColor(137, 180, 250));
    palette.setColor(QPalette::LinkVisited, QColor(180, 190, 254));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(108, 112, 134));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(108, 112, 134));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(108, 112, 134));

    qApp->setPalette(palette);

    // Load and apply QSS stylesheet
    if (!loadAndApplyStyleSheet(QStringLiteral("themes/dark_theme.qss"))) {
        qWarning() << "Failed to load dark_theme.qss, using palette only";
    }
}

bool ThemeManager::loadAndApplyStyleSheet(const QString& qss_path)
{
    QStringList searchPaths;
    searchPaths << qss_path;
    searchPaths << QApplication::applicationDirPath() + "/" + qss_path;
    searchPaths << QApplication::applicationDirPath() + "/../" + qss_path;
    searchPaths << QApplication::applicationDirPath() + "/../../" + qss_path;

    for (const QString& path : searchPaths) {
        QFile file(path);
        if (file.exists() && file.open(QFile::ReadOnly | QFile::Text)) {
            QString styleSheet = QString::fromUtf8(file.readAll());
            qApp->setStyleSheet(styleSheet);
            qDebug() << "Loaded stylesheet:" << path;
            return true;
        }
    }

    qWarning() << "Could not find stylesheet:" << qss_path;
    return false;
}

QColor ThemeManager::backgroundColor()
{
    return QColor(30, 30, 46);
}

QColor ThemeManager::surfaceColor()
{
    return QColor(49, 50, 68);
}

QColor ThemeManager::accentColor()
{
    return QColor(137, 180, 250);
}

QColor ThemeManager::successColor()
{
    return QColor(166, 227, 161);
}

QColor ThemeManager::warningColor()
{
    return QColor(249, 226, 175);
}

QColor ThemeManager::errorColor()
{
    return QColor(243, 139, 168);
}

QColor ThemeManager::textColor()
{
    return QColor(205, 214, 244);
}

QColor ThemeManager::textSecondaryColor()
{
    return QColor(166, 173, 200);
}

QColor ThemeManager::borderColor()
{
    return QColor(69, 71, 90);
}

} // namespace nevo
