#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <QCoreApplication>
#include <QEventLoop>
#include <QHash>

#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "app/AppController.h"
#include "data/DatabaseManager.h"
#include "services/ConfigService.h"
#include "services/DedupService.h"
#include "services/ScanService.h"
#include "services/ReportService.h"
#include "services/SnapshotService.h"
#include "services/VirtualTrashService.h"
#include "integrations/EverythingClient.h"
#include "app/MainWindow.h"
#include "ui/ChartPanel.h"
#include "ui/DetailsTablePanel.h"
#include "ui/DuplicatesPanel.h"
#include "ui/ThemeManager.h"
#include "ui/TreePanel.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"
#include "utils/SizeFormatter.h"

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

#ifdef _WIN32
void attachParentConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
#ifdef _MSC_VER
        FILE *stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
#else
        (void)freopen("CONOUT$", "w", stdout);
        (void)freopen("CONOUT$", "w", stderr);
#endif
    }
}
#endif

qint64 totalBytes(const opentree::ScanResult &result)
{
    qint64 total = 0;
    for (const opentree::FileEntry &file : result.files) {
        total += file.size;
    }
    return total;
}

QString describeResult(const opentree::ScanResult &result)
{
    return QStringLiteral("%1 files, %2 folders, %3 bytes")
        .arg(result.files.size())
        .arg(result.folders.size())
        .arg(totalBytes(result));
}

bool containsAny(const QString &path, const QStringList &patterns)
{
    for (const QString &pattern : patterns) {
        if (path.contains(pattern, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// Headless verification path: runs the Everything query and a filesystem scan for the
// same root and prints both so the two engines can be compared without opening the GUI.
// Usage: OpenTree.exe --test-scan <path>
int runScanTestMode(const QString &path)
{
    QCoreApplication app(__argc, __argv);
    opentree::Logger::initialize();
#ifdef _WIN32
    attachParentConsole();
#endif

    const QString normalizedPath = opentree::PathUtils::normalizePath(path);

    QStringList report;
    const auto record = [&report](const QString &line) {
        report << line;
        qInfo().noquote() << line;
    };

    opentree::ConfigService configService;
    const QStringList excluded = configService.excludedPatterns();

    opentree::EverythingClient client;
    record(QStringLiteral("Everything SDK library: %1").arg(client.libraryPath()));

    QString error;
    const QString executablePath = configService.resolvedEverythingExecutablePath();
    bool ready = client.testConnection(&error);
    if (!ready && !executablePath.isEmpty()) {
        record(QStringLiteral("Everything not reachable (%1), starting %2").arg(error, executablePath));
        ready = client.ensureEverythingRunning(executablePath, &error);
    }

    opentree::ScanResult everythingResult;
    if (ready) {
        QVector<opentree::FileEntry> files;
        QVector<opentree::FolderEntry> folders;
        QElapsedTimer timer;
        timer.start();
        if (client.queryRoot(normalizedPath, &files, &folders, &error)) {
            QVector<opentree::FileEntry> filteredFiles;
            filteredFiles.reserve(files.size());
            for (const opentree::FileEntry &file : files) {
                if (!containsAny(file.path, excluded)) {
                    filteredFiles.push_back(file);
                }
            }

            QVector<opentree::FolderEntry> filteredFolders;
            filteredFolders.reserve(folders.size());
            for (const opentree::FolderEntry &folder : folders) {
                if (!containsAny(folder.path, excluded)) {
                    filteredFolders.push_back(folder);
                }
            }

            everythingResult = opentree::ScanService::buildTreeResult(normalizedPath, filteredFolders, filteredFiles);
            everythingResult.usedEverything = true;
            record(QStringLiteral("Everything query: %1 ms -> %2")
                       .arg(timer.elapsed())
                       .arg(describeResult(everythingResult)));
        } else {
            record(QStringLiteral("Everything query FAILED: %1").arg(error));
        }
    } else {
        record(QStringLiteral("Everything unavailable: %1").arg(error));
    }

    QElapsedTimer filesystemTimer;
    filesystemTimer.start();
    const opentree::ScanResult filesystemResult = opentree::ScanService::performFilesystemScan(normalizedPath, excluded);
    record(QStringLiteral("Filesystem scan: %1 ms -> %2")
               .arg(filesystemTimer.elapsed())
               .arg(describeResult(filesystemResult)));

    int exitCode = 2;
    if (everythingResult.usedEverything) {
        const bool filesMatch = everythingResult.files.size() == filesystemResult.files.size();
        const bool bytesMatch = totalBytes(everythingResult) == totalBytes(filesystemResult);
        record(QStringLiteral("Compare: files %1, bytes %2, usedEverything=true")
                   .arg(filesMatch ? QStringLiteral("MATCH") : QStringLiteral("DIFFER"))
                   .arg(bytesMatch ? QStringLiteral("MATCH") : QStringLiteral("DIFFER")));
        exitCode = (filesMatch && bytesMatch) ? 0 : 1;
    } else {
        record(QStringLiteral("Compare: Everything was not used (filesystem fallback)"));
    }

    const QString reportPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("opentree-test-scan.txt"));
    QFile reportFile(reportPath);
    if (reportFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&reportFile);
        for (const QString &line : report) {
            stream << line << '\n';
        }
    }
    qInfo().noquote() << QStringLiteral("Report written to %1").arg(reportPath);

    return exitCode;
}

// Synthetic scan result shared by the offscreen preview modes.
opentree::ScanResult buildPreviewScanResult(const QString &root)
{
    opentree::ScanResult result;
    result.rootPath = root;

    struct DemoItem {
        const char *name;
        int mb;
    };

    const DemoItem folders[] = {
        {"Windows", 240},
        {"Program Files", 180},
        {"Users", 150},
        {"ProgramData", 110},
        {"Games", 90},
        {"ThisIsAnExtremelyLongFolderNameThatNeedsEliding", 70},
        {"Downloads", 55},
        {"Videos", 40},
        {"Documents", 30},
        {"Pictures", 22},
        {"Music", 15},
        {"Temp", 10},
        {"Logs", 6},
        {"Cache", 4},
        {"Misc", 2},
    };
    const DemoItem files[] = {
        {"huge_backup_image.iso", 60},
        {"video_render_final_final.mp4", 35},
        {"dataset.sqlite", 20},
        {"archive.zip", 12},
        {"notes.txt", 3},
    };

    qint64 totalMB = 0;
    for (const DemoItem &item : folders) {
        totalMB += item.mb;
    }
    for (const DemoItem &item : files) {
        totalMB += item.mb;
    }

    opentree::TreeEntry rootEntry;
    rootEntry.kind = opentree::TreeEntryKind::Folder;
    rootEntry.name = QStringLiteral("ChartDemo");
    rootEntry.path = root;
    rootEntry.size = totalMB * 1024LL * 1024LL;
    result.treeEntries.push_back(rootEntry);

    for (const DemoItem &item : folders) {
        opentree::TreeEntry entry;
        entry.kind = opentree::TreeEntryKind::Folder;
        entry.name = QString::fromLatin1(item.name);
        entry.path = root + QLatin1Char('/') + entry.name;
        entry.parentPath = root;
        entry.size = qint64(item.mb) * 1024LL * 1024LL;
        entry.parentSize = rootEntry.size;
        entry.fileCount = 1 + (qint64(item.mb) % 7);
        entry.folderCount = (qint64(item.mb) % 5);
        result.treeEntries.push_back(entry);
    }

    // Parent sizes for the nested items below (mirrors what buildTreeResult computes).
    QHash<QString, qint64> sizeByPath;
    for (const opentree::TreeEntry &entry : result.treeEntries) {
        sizeByPath.insert(entry.path, entry.size);
    }

    // Nested folders so depth 2/3 treemap rendering can be previewed.
    struct NestedItem {
        const char *parent;
        const char *name;
        int mb;
    };
    const NestedItem nested[] = {
        {"Windows", "System32", 120},
        {"Windows", "SysWOW64", 70},
        {"Windows", "WinSxS", 50},
        {"Program Files", "Microsoft", 80},
        {"Program Files", "Google", 50},
        {"Program Files", "Common Files", 30},
        {"Program Files", "Other Vendor", 20},
        {"Users", "Alice", 80},
        {"Users", "Public", 40},
        {"Users", "Default", 30},
    };
    for (const NestedItem &item : nested) {
        opentree::TreeEntry entry;
        entry.kind = opentree::TreeEntryKind::Folder;
        entry.name = QString::fromLatin1(item.name);
        const QString parentPath = root + QLatin1Char('/') + QString::fromLatin1(item.parent);
        entry.path = parentPath + QLatin1Char('/') + entry.name;
        entry.parentPath = parentPath;
        entry.size = qint64(item.mb) * 1024LL * 1024LL;
        entry.parentSize = sizeByPath.value(parentPath, rootEntry.size);
        entry.fileCount = 1 + (qint64(item.mb) % 9);
        entry.folderCount = qint64(item.mb) % 4;
        result.treeEntries.push_back(entry);
    }

    for (const DemoItem &item : files) {
        opentree::FileEntry file;
        file.name = QString::fromLatin1(item.name);
        file.path = root + QLatin1Char('/') + file.name;
        file.parentPath = root;
        file.size = qint64(item.mb) * 1024LL * 1024LL;

        opentree::TreeEntry entry;
        entry.kind = opentree::TreeEntryKind::File;
        entry.name = file.name;
        entry.path = file.path;
        entry.parentPath = file.parentPath;
        entry.size = file.size;
        entry.parentSize = rootEntry.size;
        result.treeEntries.push_back(entry);
        result.files.push_back(file);
    }

    return result;
}

// Renders a synthetic chart (pie/bars/treemap) to a PNG so label placement can be
// inspected without a display.
// Usage: OpenTree.exe --render-chart-preview <out.png> [pie|bars|treemap]
//        (set QT_QPA_PLATFORM=offscreen)
int runChartPreviewMode(const QString &outputPath, const QString &rawMode)
{
    QApplication app(__argc, __argv);
    opentree::Logger::initialize();

    // Mode may carry a "+freespace" suffix so the drive-root free-space slice can be
    // previewed (which needs the active folder to actually be a volume root).
    QStringList modeParts = rawMode.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    const QString mode = modeParts.isEmpty() ? QString() : modeParts.first().trimmed();
    const bool includeFreeSpace = rawMode.contains(QStringLiteral("freespace"), Qt::CaseInsensitive);

    const QString root = includeFreeSpace ? QStringLiteral("C:/") : QStringLiteral("C:/ChartDemo");
    const opentree::ScanResult result = buildPreviewScanResult(root);

    opentree::ChartPanel panel;
    panel.resize(1200, 760);
    panel.setScanResult(result);
    panel.setActiveFolderPath(root);
    panel.setIncludeFreeSpace(includeFreeSpace);

    opentree::ChartPanel::ViewMode view = opentree::ChartPanel::ViewMode::Pie;
    int treemapDepth = 1;
    if (mode.compare(QStringLiteral("bars"), Qt::CaseInsensitive) == 0) {
        view = opentree::ChartPanel::ViewMode::Bars;
    } else if (mode.startsWith(QStringLiteral("treemap"), Qt::CaseInsensitive)) {
        view = opentree::ChartPanel::ViewMode::Treemap;
        if (mode.size() > 7) {
            treemapDepth = std::clamp(mode.mid(7).toInt(), 1, 3);
        }
    }
    panel.setTreemapDepth(treemapDepth);
    panel.setActiveViewMode(view);
    panel.show();
    QCoreApplication::processEvents();

    const bool saved = panel.grab().save(outputPath);
    qInfo().noquote() << QStringLiteral("Chart preview %1: %2")
                             .arg(saved ? QStringLiteral("written to") : QStringLiteral("FAILED for"), outputPath);
    return saved ? 0 : 1;
}

// Renders the sortable details table to a PNG.
// Usage: OpenTree.exe --render-details-preview <out.png>
int runDetailsPreviewMode(const QString &outputPath, const QString &mode)
{
    QApplication app(__argc, __argv);
    opentree::Logger::initialize();

    const QString root = QStringLiteral("C:/ChartDemo");
    const opentree::ScanResult result = buildPreviewScanResult(root);

    opentree::DetailsTablePanel panel;
    panel.resize(1200, 700);
    panel.setScanResult(result);
    panel.setActiveFolderPath(root);
    if (mode.contains(QStringLiteral("flat"), Qt::CaseInsensitive)) {
        panel.setFlatMode(true);
    }
    panel.show();
    QCoreApplication::processEvents();

    const bool saved = panel.grab().save(outputPath);
    return saved ? 0 : 1;
}

// Exports the preview details table to CSV so the export path can be verified
// without a desktop session. Usage: OpenTree.exe --export-details-csv <out.csv>
int runExportDetailsPreviewMode(const QString &outputPath)
{
    QApplication app(__argc, __argv);
    opentree::Logger::initialize();

    const QString root = QStringLiteral("C:/ChartDemo");
    const opentree::ScanResult result = buildPreviewScanResult(root);

    opentree::DetailsTablePanel panel;
    panel.setScanResult(result);
    panel.setActiveFolderPath(root);

    QString error;
    return panel.exportCsv(outputPath, &error) ? 0 : 1;
}

// Scans a folder and writes an HTML or PDF report. Usage:
//   OpenTree.exe --export-report <scanPath> <outFile.html|outFile.pdf>
int runExportReportMode(const QString &path, const QString &outputPath)
{
    // QPrinter/QTextDocument need a GUI application instance for font handling.
    QApplication app(__argc, __argv);
    opentree::Logger::initialize();

    opentree::ConfigService configService;
    const QString normalized = opentree::PathUtils::normalizePath(path);
    const opentree::ScanResult scan = opentree::ScanService::performFilesystemScan(normalized, configService.excludedPatterns());

    QString error;
    const bool pdf = outputPath.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);
    const bool ok = pdf
        ? opentree::ReportService::writePdfReport(outputPath, scan, {}, &error)
        : opentree::ReportService::writeHtmlReport(outputPath, scan, {}, &error);

    QStringList report;
    report << QStringLiteral("Report %1: %2").arg(ok ? QStringLiteral("written") : QStringLiteral("FAILED"), outputPath);
    report << QStringLiteral("Root: %1").arg(normalized);
    report << QStringLiteral("Files: %1  Folders: %2").arg(scan.files.size()).arg(scan.folders.size());
    report << QStringLiteral("PDF support: %1").arg(opentree::ReportService::printSupportAvailable() ? QStringLiteral("yes") : QStringLiteral("no"));
    if (!ok) {
        report << QStringLiteral("Error: %1").arg(error);
    }

    const QString reportPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("opentree-report-test.txt"));
    QFile reportFile(reportPath);
    if (reportFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&reportFile);
        for (const QString &line : report) {
            stream << line << '\n';
        }
    }
    return ok ? 0 : 1;
}

// Exercises the virtual trash staging bookkeeping without touching the file system.
// Usage: OpenTree.exe --test-trash <path>
int runTrashTestMode(const QString &path)
{
    QCoreApplication app(__argc, __argv);
    opentree::Logger::initialize();

    opentree::DatabaseManager databaseManager;
    if (!databaseManager.initialize()) {
        return 1;
    }

    opentree::VirtualTrashService trash(databaseManager.database());
    const QString normalized = opentree::PathUtils::normalizePath(path);
    const QFileInfo info(normalized);
    const bool isFolder = info.isDir();

    QString error;
    QStringList report;
    report << QStringLiteral("Virtual trash staging test for %1").arg(normalized);

    if (!trash.stage(normalized, info.size(), isFolder, QString(), QStringLiteral("dry-run staging"), &error)) {
        report << QStringLiteral("stage failed: %1").arg(error);
    } else {
        report << QStringLiteral("staged ok (%1, %2)")
                      .arg(isFolder ? QStringLiteral("folder") : QStringLiteral("file"),
                           opentree::SizeFormatter::formatBytes(info.size()));
    }

    const QVector<opentree::TrashItem> items = trash.stagedItems(&error);
    report << QStringLiteral("staged items: %1").arg(items.size());
    for (const opentree::TrashItem &item : items) {
        report << QStringLiteral("  - %1 (%2)").arg(item.path, opentree::SizeFormatter::formatBytes(item.size));
    }
    report << QStringLiteral("projected reclaim: %1").arg(opentree::SizeFormatter::formatBytes(trash.stagedBytes(&error)));

    QString reason;
    const bool eligible = opentree::VirtualTrashService::canMoveToRecycleBin(normalized, &reason);
    report << QStringLiteral("recycle-bin eligible: %1%2")
                  .arg(eligible ? QStringLiteral("yes") : QStringLiteral("no"),
                       eligible ? QString() : QStringLiteral(" (%1)").arg(reason));

    // Leave the staged list as it was found.
    trash.clearStaged(&error);
    report << QStringLiteral("staged list cleared (no files were touched)");

    const QString reportPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("opentree-trash-test.txt"));
    QFile reportFile(reportPath);
    if (reportFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&reportFile);
        for (const QString &line : report) {
            stream << line << '\n';
        }
    }
    return 0;
}

// Scans a folder and reports duplicate files. Usage:
//   OpenTree.exe --find-duplicates <path> [minSizeMB]
int runFindDuplicatesMode(const QString &path, int minimumMB, bool includeSystem)
{
    QCoreApplication app(__argc, __argv);
    opentree::Logger::initialize();

    opentree::ConfigService configService;
    const QString normalized = opentree::PathUtils::normalizePath(path);
    const opentree::ScanResult scan = opentree::ScanService::performFilesystemScan(normalized, configService.excludedPatterns());

    const qint64 minimumBytes = minimumMB > 0
        ? qint64(minimumMB) * 1024LL * 1024LL
        : configService.dedupMinimumBytes();

    opentree::DedupService service;
    const opentree::DedupResult result = service.findDuplicates(scan.files, minimumBytes, !includeSystem);

    QStringList report;
    report << QStringLiteral("Duplicates under %1 (min %2, system folders %3)")
                  .arg(normalized,
                       opentree::SizeFormatter::formatBytes(minimumBytes),
                       includeSystem ? QStringLiteral("included") : QStringLiteral("skipped"));
    int index = 0;
    for (const opentree::DuplicateGroup &group : result.groups) {
        ++index;
        report << QStringLiteral("Group %1: %2 files x %3 (wasted %4)")
                      .arg(index)
                      .arg(group.files.size())
                      .arg(opentree::SizeFormatter::formatBytes(group.size))
                      .arg(opentree::SizeFormatter::formatBytes(group.wastedBytes()));
        for (const opentree::DuplicateFile &file : group.files) {
            report << QStringLiteral("  - %1").arg(file.path);
        }
    }
    report << QStringLiteral("Summary: %1 groups | %2 reclaimable | %3 candidates | %4 hashed | %5 skipped | %6 system excluded | %7 files scanned")
                  .arg(result.groups.size())
                  .arg(opentree::SizeFormatter::formatBytes(result.wastedBytes))
                  .arg(result.candidateFiles)
                  .arg(result.hashedFiles)
                  .arg(result.skippedFiles)
                  .arg(result.filteredFiles)
                  .arg(scan.files.size());

    const QString reportPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("opentree-duplicates.txt"));
    QFile reportFile(reportPath);
    if (reportFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&reportFile);
        for (const QString &line : report) {
            stream << line << '\n';
        }
    }

    return result.groups.isEmpty() ? 0 : 0;
}

// Opens a real folder, waits for the scan to finish, then grabs the whole window so the
// layout (toolbar, drive selector, tabs) can be inspected without a desktop session.
// Usage: OpenTree.exe --render-window-preview <out.png> [path] [tabIndex]
int runWindowPreviewMode(const QString &outputPath, const QString &path, int tabIndex)
{
    QApplication app(__argc, __argv);
    opentree::Logger::initialize();

    opentree::ConfigService configService;
    opentree::DatabaseManager databaseManager;
    if (!databaseManager.initialize()) {
        return 1;
    }

    opentree::AppController controller;
    opentree::MainWindow window;
    controller.attachWindow(&window);

    window.resize(1500, 900);
    window.show();

    controller.openPath(path);

    QEventLoop loop;
    QTimer::singleShot(7000, &loop, &QEventLoop::quit);
    loop.exec();

    window.setCurrentTabIndex(tabIndex);
    if (qEnvironmentVariableIsSet("OPENTREE_PREVIEW_BUSY")) {
        window.setBusy(true);
        window.setProgress(65);
    }
    if (qEnvironmentVariableIsSet("OPENTREE_PREVIEW_DEDUP") && window.duplicatesPanel()) {
        window.duplicatesPanel()->startScan();
        QEventLoop dedupLoop;
        QTimer::singleShot(2500, &dedupLoop, &QEventLoop::quit);
        dedupLoop.exec();
    }
    if (qEnvironmentVariableIsSet("OPENTREE_PREVIEW_EXPANDALL")) {
        window.treePanel()->expandAll();
        QEventLoop expandLoop;
        QTimer::singleShot(4000, &expandLoop, &QEventLoop::quit);
        expandLoop.exec();
    }
    QCoreApplication::processEvents();

    const bool saved = window.grab().save(outputPath);
    return saved ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setApplicationName("OpenTree");
    QApplication::setOrganizationName("OpenTree");
    QApplication::setApplicationVersion(QStringLiteral("0.6.0"));

    QString startupPath;
    QString scanTestPath;
    QString chartPreviewPath;
    QString chartPreviewMode;
    QString detailsPreviewPath;
    QString detailsPreviewMode;
    QString windowPreviewPath;
    QString windowPreviewScanPath;
    int windowPreviewTab = -1;
    QString exportDetailsCsvPath;
    QString findDuplicatesPath;
    int findDuplicatesMinMB = 0;
    bool findDuplicatesIncludeSystem = false;
    QString trashTestPath;
    QString reportScanPath;
    QString reportOutputPath;

    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == "--background-snapshot") {
            return runBackgroundSnapshotMode();
        }
        if (argument == "--test-scan" && index + 1 < argc) {
            scanTestPath = QString::fromLocal8Bit(argv[++index]);
            continue;
        }
        if (argument == "--render-chart-preview" && index + 1 < argc) {
            chartPreviewPath = QString::fromLocal8Bit(argv[++index]);
            if (index + 1 < argc) {
                const QString maybeMode = QString::fromLocal8Bit(argv[index + 1]);
                if (!maybeMode.startsWith('-')) {
                    chartPreviewMode = maybeMode;
                    ++index;
                }
            }
            continue;
        }
        if (argument == "--render-details-preview" && index + 1 < argc) {
            detailsPreviewPath = QString::fromLocal8Bit(argv[++index]);
            if (index + 1 < argc && !QString::fromLocal8Bit(argv[index + 1]).startsWith('-')) {
                detailsPreviewMode = QString::fromLocal8Bit(argv[++index]);
            }
            continue;
        }
        if (argument == "--export-report" && index + 2 < argc) {
            reportScanPath = QString::fromLocal8Bit(argv[++index]);
            reportOutputPath = QString::fromLocal8Bit(argv[++index]);
            continue;
        }
        if (argument == "--test-trash" && index + 1 < argc) {
            trashTestPath = QString::fromLocal8Bit(argv[++index]);
            continue;
        }
        if (argument == "--find-duplicates" && index + 1 < argc) {
            findDuplicatesPath = QString::fromLocal8Bit(argv[++index]);
            if (index + 1 < argc && !QString::fromLocal8Bit(argv[index + 1]).startsWith('-')) {
                findDuplicatesMinMB = QString::fromLocal8Bit(argv[++index]).toInt();
            }
            if (index + 1 < argc && QString::fromLocal8Bit(argv[index + 1]).compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
                findDuplicatesIncludeSystem = true;
                ++index;
            }
            continue;
        }
        if (argument == "--export-details-csv" && index + 1 < argc) {
            exportDetailsCsvPath = QString::fromLocal8Bit(argv[++index]);
            continue;
        }
        if (argument == "--render-window-preview" && index + 1 < argc) {
            windowPreviewPath = QString::fromLocal8Bit(argv[++index]);
            if (index + 1 < argc && !QString::fromLocal8Bit(argv[index + 1]).startsWith('-')) {
                windowPreviewScanPath = QString::fromLocal8Bit(argv[++index]);
            }
            if (index + 1 < argc && !QString::fromLocal8Bit(argv[index + 1]).startsWith('-')) {
                windowPreviewTab = QString::fromLocal8Bit(argv[++index]).toInt();
            }
            continue;
        }
        if (argument == "--open-path" && index + 1 < argc) {
            startupPath = QString::fromLocal8Bit(argv[++index]);
        }
    }

    if (!reportOutputPath.isEmpty()) {
        return runExportReportMode(reportScanPath, reportOutputPath);
    }

    if (!trashTestPath.isEmpty()) {
        return runTrashTestMode(trashTestPath);
    }

    if (!findDuplicatesPath.isEmpty()) {
        return runFindDuplicatesMode(findDuplicatesPath, findDuplicatesMinMB, findDuplicatesIncludeSystem);
    }

    if (!exportDetailsCsvPath.isEmpty()) {
        return runExportDetailsPreviewMode(exportDetailsCsvPath);
    }

    if (!windowPreviewPath.isEmpty()) {
        const QString scanPath = windowPreviewScanPath.isEmpty() ? QStringLiteral("C:/") : windowPreviewScanPath;
        return runWindowPreviewMode(windowPreviewPath, scanPath, windowPreviewTab);
    }

    if (!detailsPreviewPath.isEmpty()) {
        return runDetailsPreviewMode(detailsPreviewPath, detailsPreviewMode);
    }

    if (!chartPreviewPath.isEmpty()) {
        return runChartPreviewMode(chartPreviewPath, chartPreviewMode);
    }

    if (!scanTestPath.isEmpty()) {
        return runScanTestMode(scanTestPath);
    }

    try {
        QApplication app(argc, argv);
        QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.ico")));
        opentree::Logger::initialize();
        opentree::Logger::info("Application startup begin");

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
