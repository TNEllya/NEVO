/**
 * @file main.cpp
 * @brief NEVO VoIP 客户端入口点
 */

#include <QApplication>
#include <QSurfaceFormat>
#include <QTranslator>
#include <QSettings>
#include <QLocale>
#include <QFile>

#include "nevo/ui/MainWindow.h"
#include "nevo/ui/ThemeManager.h"

#include "nevo/core/common/Logger.h"

#ifdef NEVO_HAS_BOOST
#include <boost/asio.hpp>
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("NEVO"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("NEVO"));

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    // Load translation based on saved language preference
    QSettings settings(QStringLiteral("NEVO"), QStringLiteral("NevoClient"));
    QString lang = settings.value(QStringLiteral("language")).toString();
    if (lang.isEmpty()) {
        lang = QLocale::system().name();  // e.g. "zh_CN"
    }

    // Heap-allocate so onLanguageChanged can safely delete it later
    QTranslator* translator = new QTranslator();
    // Use QFile + load(data, size) to load from Qt resources directly.
    // The locale-based load() overload uses uiLanguages() which returns
    // BCP47 tags (e.g. "zh-Hans-CN") that don't match our POSIX-named
    // .qm files (e.g. "nevo_client_zh_CN.qm").
    QString qm_path = QStringLiteral(":/i18n/nevo_client_%1.qm").arg(lang);
    QFile qm_file(qm_path);
    if (qm_file.open(QIODevice::ReadOnly)) {
        QByteArray* qm_data = new QByteArray(qm_file.readAll());
        if (translator->load(reinterpret_cast<const uchar*>(qm_data->constData()),
                             qm_data->size())) {
            app.installTranslator(translator);
            app.setProperty("nevoTranslator", QVariant::fromValue(translator));
            app.setProperty("nevoQmData", QVariant::fromValue(qm_data));
            NEVO_LOG_INFO("main", "Loaded translation for language: {}", lang.toStdString());
        } else {
            delete translator;
            delete qm_data;
            NEVO_LOG_INFO("main", "Failed to parse translation for language: {}", lang.toStdString());
        }
    } else {
        delete translator;
        NEVO_LOG_INFO("main", "No translation found for language: {}, using default", lang.toStdString());
    }

    NEVO_LOG_INFO("main", "NEVO VoIP Client starting...");

    nevo::ThemeManager::instance().applyDarkTheme();

#ifdef NEVO_HAS_BOOST
    boost::asio::io_context io_ctx;
    auto work_guard = boost::asio::make_work_guard(io_ctx);

    nevo::MainWindow main_window(io_ctx);
    main_window.show();

    NEVO_LOG_INFO("main", "MainWindow displayed (with ClientCore)");

    int result = app.exec();

    work_guard.reset();

    NEVO_LOG_INFO("main", "NEVO VoIP Client exiting with code {}", result);
    return result;
#else
    nevo::MainWindow main_window;
    main_window.show();

    NEVO_LOG_INFO("main", "MainWindow displayed (standalone)");

    int result = app.exec();

    NEVO_LOG_INFO("main", "NEVO VoIP Client exiting with code {}", result);
    return result;
#endif
}
