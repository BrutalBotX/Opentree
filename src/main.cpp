#include <QApplication>
#include <QDateTime>
#include <QDir>

#include <QCoreApplication>

#include "app/AppController.h"
#include "data/DatabaseManager.h"
#include "services/ConfigService.h"
#include "services/ScanService.h"
#include "services/SnapshotService.h"
#include "integrations/EverythingClient.h"
#include "app/MainWindow.h"
#include "ui/ThemeManager.h"
#include "utils/Logger.h"

namespace {

int runBackgroundSnapshotMode()
{
    QCoreApplication app(__argc, __argv);
    opentree::Logger::initialize();

    opentree::ConfigService configService;
    opentree::DatabaseManager databaseManager;
    if (!databaseManager.initialize()) {
        return 1;
    }

    opentree::EverythingClient everythingClient;
    opentree::ScanService scanService(&configService, &everythingClient);
    opentree::SnapshotService snapshotService(databaseManager.database());

    const QStringList whitelist = configService.snapshotWhitelist();
    if (whitelist.isEmpty()) {
        return 0;
    }

    for (const QString &rootPath : whitelist) {
        scanService.scanPath(rootPath);
        while (scanService.isBusy()) {
            QCoreApplication::processEvents();
        }

        const opentree::ScanResult result = scanService.lastResult();
        if (result.rootPath.isEmpty()) {
            continue;
        }

        QString error;
        snapshotService.createSnapshot(result, configService.snapshotThresholdBytes(), &error);
    }

    return 0;
}

}

int main(int argc, char *argv[])
{
    QApplication::setApplicationName("OpenTree");
    QApplication::setOrganizationName("OpenTree");

    QString startupPath;

    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == "--background-snapshot") {
            return runBackgroundSnapshotMode();
        }
        if (QString::fromLocal8Bit(argv[index]) == "--open-path" && index + 1 < argc) {
            startupPath = QString::fromLocal8Bit(argv[++index]);
        }
    }

    try {
        QApplication app(argc, argv);
        opentree::Logger::initialize();
        opentree::Logger::info("Application startup begin");

        opentree::ConfigService startupConfig;
        const auto startupThemes = opentree::ThemeManager::loadThemes(startupConfig.themesDirectory());
        opentree::ThemeManager::applyTheme(app, startupThemes.value(startupConfig.themeId(), startupThemes.value("dark")));

        opentree::Logger::info("Constructing AppController");
        opentree::AppController controller;

        opentree::Logger::info("Constructing MainWindow");
        opentree::MainWindow window;

        opentree::Logger::info("Attaching MainWindow");
        controller.attachWindow(&window);

        if (!startupPath.isEmpty()) {
            controller.openPath(startupPath);
        }

        opentree::Logger::info("Showing MainWindow");
        window.show();

        const int exitCode = app.exec();
        opentree::Logger::info(QStringLiteral("Application exiting with code %1").arg(exitCode));
        return exitCode;
    } catch (const std::exception &exception) {
        opentree::Logger::error(QStringLiteral("Fatal startup error: %1").arg(exception.what()));
        return 1;
    } catch (...) {
        opentree::Logger::error(QStringLiteral("Unknown startup error."));
        return 1;
    }
}
