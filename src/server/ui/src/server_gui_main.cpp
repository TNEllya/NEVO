/**
 * @file server_gui_main.cpp
 * @brief NEVO VoIP Server GUI Entry Point
 *
 * Provides a server startup entry point with a modern management interface.
 * Retains command-line parameter support, migrating original console main logic
 * to run within the Qt event loop.
 */

#include <QApplication>
#include <QTranslator>
#include <QSettings>
#include <QLocale>
#include <QFile>

#include "nevo/server/ui/ServerMainWindow.h"
#include "nevo/server/ServerConfig.h"
#include "nevo/ui/ThemeManager.h"
#include "nevo/core/common/Logger.h"

#include <boost/asio.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

spdlog::level::level_enum parseLogLevel(const std::string& level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info")  return spdlog::level::info;
    if (level == "warn")  return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    return spdlog::level::info;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("NEVO Server"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("NEVO"));

    // Load translation based on saved language preference
    QSettings settings(QStringLiteral("NEVO"), QStringLiteral("NevoServer"));
    QString lang = settings.value(QStringLiteral("language")).toString();
    if (lang.isEmpty()) {
        lang = QLocale::system().name();
    }

    // The translator and its backing data must outlive the QCoreApplication
    // because QTranslator::load(const uchar*, int) does NOT copy the data.
    // We heap-allocate both and store them as qApp properties so that
    // ServerMainWindow::onLanguageChanged() can find and remove them later.
    QTranslator* translator = new QTranslator();  // no parent — lifetime via qApp property
    QByteArray* qm_data = new QByteArray();
    QString qm_path = QStringLiteral(":/i18n/nevo_server_%1.qm").arg(lang);
    QFile qm_file(qm_path);
    if (qm_file.open(QIODevice::ReadOnly)) {
        *qm_data = qm_file.readAll();
        if (translator->load(reinterpret_cast<const uchar*>(qm_data->constData()),
                             static_cast<int>(qm_data->size()))) {
            app.installTranslator(translator);
            // Store both in qApp properties so onLanguageChanged can remove them
            qApp->setProperty("nevoServerTranslator", QVariant::fromValue(translator));
            qApp->setProperty("nevoServerQmData", QVariant::fromValue(qm_data));
            NEVO_LOG_INFO("server", "Loaded translation for language: {}", lang.toStdString());
        } else {
            delete translator;
            delete qm_data;
            NEVO_LOG_WARN("server", "Failed to parse translation for language: {}", lang.toStdString());
        }
    } else {
        delete translator;
        delete qm_data;
        NEVO_LOG_WARN("server", "No translation found for language: {}, using default", lang.toStdString());
    }

    // Apply dark theme
    nevo::ThemeManager::instance().applyDarkTheme();

    // Load server-specific stylesheet overrides
    nevo::ThemeManager::instance().loadAndApplyStyleSheet(
        QStringLiteral("themes/server_dark_theme.qss"));

    // Parse configuration from CLI and optional config file
    nevo::ServerConfig config = nevo::ServerConfig::fromArgs(argc, argv);

    // Extract config file path for save support
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // Initialize logging
    nevo::LoggerManager::instance().initialize(
        "nevo_server.log",
        parseLogLevel(config.log_level)
    );

    NEVO_LOG_INFO("server", "========================================");
    NEVO_LOG_INFO("server", "NEVO VoIP Server GUI Starting");
    NEVO_LOG_INFO("server", "========================================");

    // Create main window with unified config
    nevo::ServerMainWindow window(config, config_path);
    window.show();

    NEVO_LOG_INFO("server", "Server GUI displayed");

    int result = app.exec();

    // Clean up the initial translator before QCoreApplication is destroyed.
    // If onLanguageChanged() already removed it, the property will be null.
    QTranslator* cleanup_translator = qApp->property("nevoServerTranslator").value<QTranslator*>();
    QByteArray* cleanup_qm_data = qApp->property("nevoServerQmData").value<QByteArray*>();
    if (cleanup_translator) {
        QCoreApplication::removeTranslator(cleanup_translator);
        delete cleanup_translator;
    }
    delete cleanup_qm_data;

    NEVO_LOG_INFO("server", "NEVO VoIP Server GUI exiting with code {}", result);
    return result;
}
