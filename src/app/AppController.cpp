#include "app/AppController.h"

#include <QCoreApplication>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QGuiApplication>
#include <QUrl>

#include "app/MainWindow.h"
#include "data/DatabaseManager.h"
#include "data/FileRepository.h"
#include "data/FolderRepository.h"
#include "integrations/EverythingClient.h"
#include "models/FolderTreeModel.h"
#include "services/ConfigService.h"
#include "services/ScanService.h"
#include "services/SnapshotService.h"
#include "ui/DetailsPanel.h"
#include "ui/ChartPanel.h"
#include "ui/ExtensionsPanel.h"
#include "ui/GraphPanel.h"
#include "ui/HeatmapPanel.h"
#include "ui/SnapshotSettingsDialog.h"
#include "ui/ThemeManager.h"
#include "ui/TimelinePanel.h"
#include "ui/TreePanel.h"
#include "utils/Logger.h"
#include "utils/TaskSchedulerUtils.h"

namespace opentree {

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_configService(new ConfigService())
    , m_databaseManager(new DatabaseManager())
    , m_everythingClient(new EverythingClient())
    , m_scanService(new ScanService(m_configService, m_everythingClient, this))
    , m_treeModel(new FolderTreeModel(this))
{
    if (!m_databaseManager->initialize()) {
        Logger::error("Database initialization failed: " + m_databaseManager->lastError());
    } else {
        m_snapshotService = new SnapshotService(m_databaseManager->database());
        m_folderRepository = new FolderRepository(m_databaseManager->database());
        m_fileRepository = new FileRepository(m_databaseManager->database());
    }

    connect(m_scanService, &ScanService::scanFinished, this, &AppController::handleScanFinished);
    connect(m_scanService, &ScanService::scanFailed, this, &AppController::handleScanFailed);
    connect(m_scanService, &ScanService::scanProgress, this, [this](int percent, const QString &message) {
        if (!m_window) {
            return;
        }
        m_window->setProgress(percent);
        m_window->setStatusText(message);
    });
}

AppController::~AppController()
{
    delete m_fileRepository;
    delete m_folderRepository;
    delete m_snapshotService;
    delete m_everythingClient;
    delete m_databaseManager;
    delete m_configService;
}

void AppController::attachWindow(MainWindow *window)
{
    m_window = window;
    m_window->treePanel()->setModel(m_treeModel);
    
    if (!m_snapshotService) {
        m_window->setStatusText(QStringLiteral("WARNING: Database initialization failed - snapshots disabled"));
        QMessageBox::warning(
            m_window,
            "Database Error",
            QStringLiteral("Failed to initialize database:\n%1\n\nSnapshot features will be unavailable this session.")
                .arg(m_databaseManager->lastError()));
    } else {
        m_window->setStatusText(QStringLiteral("Config: %1").arg(m_configService->configPath()));
    }
    
    m_window->timelinePanel()->setCompareResult({}, {}, {});

    connect(m_window, &MainWindow::scanRequested, this, [this]() {
        handleScanRequest();
    });
    connect(m_window->createSnapshotAction(), &QAction::triggered, this, [this]() {
        handleCreateSnapshotRequest();
    });
    connect(m_window->compareSnapshotAction(), &QAction::triggered, this, [this]() {
        handleCompareSnapshotMenuRequest();
    });
    connect(m_window, &MainWindow::everythingLocationRequested, this, [this]() {
        handleLocateEverythingRequest();
    });
    connect(m_window, &MainWindow::snapshotSettingsRequested, this, [this]() {
        handleSnapshotSettingsRequest();
    });
    connect(m_window, &MainWindow::othersThresholdRequested, this, [this]() {
        handleOthersThresholdRequest();
    });
    connect(m_window, &MainWindow::rescanCurrentRootRequested, this, &AppController::handleRescanCurrentRootRequest);
    connect(m_window, &MainWindow::clearAllRootsRequested, this, &AppController::handleClearAllRootsRequest);
    connect(m_window, &MainWindow::recentRootRequested, this, &AppController::handleRecentRootRequested);
    connect(m_window, &MainWindow::openCurrentInExplorerRequested, this, &AppController::handleOpenCurrentInExplorerRequest);
    connect(m_window, &MainWindow::openTerminalHereRequested, this, &AppController::handleOpenTerminalHereRequest);
    connect(m_window, &MainWindow::copyCurrentPathRequested, this, &AppController::handleCopyCurrentPathRequest);
    connect(m_window, &MainWindow::openConfigFolderRequested, this, &AppController::handleOpenConfigFolderRequest);
    connect(m_window, &MainWindow::openLogFileRequested, this, &AppController::handleOpenLogFileRequest);
    connect(m_window, &MainWindow::expandAllRequested, this, &AppController::handleExpandAllRequest);
    connect(m_window, &MainWindow::collapseAllRequested, this, &AppController::handleCollapseAllRequest);
    connect(m_window, &MainWindow::treemapDepthChanged, this, &AppController::handleTreemapDepthChanged);
    connect(m_window, &MainWindow::themeSelected, this, &AppController::handleThemeSelected);
    connect(m_window, &MainWindow::reloadThemesRequested, this, [this]() {
        reloadThemes();
    });
    connect(m_window->treePanel(), &TreePanel::entryActivated, this, &AppController::handleEntryActivated);
    connect(m_window->chartPanel(), &ChartPanel::entryActivated, this, &AppController::handleChartEntryActivated);
    connect(m_window->chartPanel(), &ChartPanel::entryOpenInGraphRequested, this, &AppController::handleChartOpenInGraphRequested);
    connect(m_window->chartPanel(), &ChartPanel::entryOpenRequested, this, &AppController::handleChartOpenRequested);
    connect(m_window->chartPanel(), &ChartPanel::entryShowInExplorerRequested, this, &AppController::handleChartShowInExplorerRequested);
    connect(m_window->chartPanel(), &ChartPanel::entryCopyPathRequested, this, &AppController::handleChartCopyPathRequested);
    connect(m_window->chartPanel(), &ChartPanel::pathEntered, this, [this](const QString &path) {
        handleNavigatePath(path, false);
    });
    connect(m_window->treePanel(), &TreePanel::visiblePathsChanged, this, [this](const QStringList &paths) {
        if (m_window && m_window->existingGraphPanel()) {
            m_window->existingGraphPanel()->setVisiblePaths(paths);
        }
    });
    connect(m_window->timelinePanel(), &TimelinePanel::compareSnapshotRequested, this, &AppController::handleCompareSnapshotRequest);
    connect(m_window, &MainWindow::graphTabActivated, this, [this]() {
        if (!m_window) {
            return;
        }

        GraphPanel *graph = m_window->graphPanel();
        connect(graph, &GraphPanel::entryActivated, this, &AppController::handleGraphEntryActivated, Qt::UniqueConnection);
        connect(graph, &GraphPanel::entryOpened, this, &AppController::handleGraphEntryOpened, Qt::UniqueConnection);
        connect(graph, &GraphPanel::pathEntered, this, &AppController::handleGraphPathEntered, Qt::UniqueConnection);
        graph->setVisiblePaths(m_window->treePanel()->visibleFolderPaths());
        graph->setOtherThresholdPercent(m_otherThresholdPercent);
        graph->setGraphData(m_currentResult.rootPath, m_currentResult.treeEntries, {});
        if (!m_activeFolderPath.isEmpty()) {
            graph->setSelectedPath(m_activeFolderPath);
            graph->setGraphRootPath(m_activeFolderPath);
        }
    });
    connect(m_window, &MainWindow::navigateToEntryRequested, this, &AppController::handleNavigateToEntryRequested);
    connect(m_window->viewBySizeAction(), &QAction::triggered, this, [this]() {
        applyViewMetric(ViewMetric::Size);
    });
    connect(m_window->viewByPercentageAction(), &QAction::triggered, this, [this]() {
        applyViewMetric(ViewMetric::Percentage);
    });
    connect(m_window->viewByFilesAction(), &QAction::triggered, this, [this]() {
        applyViewMetric(ViewMetric::Files);
    });

    m_otherThresholdPercent = m_configService->othersThresholdPercent();
    m_viewMetric = m_configService->viewMetric();
    reloadThemes();
    applyCurrentTheme();
    applyViewMetric(m_viewMetric);
    refreshRecentRoots();
    refreshTimeline();
}

void AppController::handleScanRequest()
{
    if (!m_window) {
        return;
    }

    const QString folder = QFileDialog::getExistingDirectory(m_window, "Select folder to scan");
    if (folder.isEmpty()) {
        return;
    }

    m_lastRequestedRootPath = folder;
    refreshRecentRoots();
    m_window->timelinePanel()->setCurrentRootPath(folder);

    m_window->setBusy(true);
    m_window->setProgress(0);
    m_window->setStatusText(QStringLiteral("Scanning %1").arg(folder));
    m_scanService->scanPath(folder);
}

void AppController::openPath(const QString &path)
{
    if (!m_window || path.isEmpty()) {
        return;
    }

    activateRootSession(path, false);
}

void AppController::refreshRecentRoots()
{
    if (!m_window) {
        return;
    }

    QStringList roots = m_configService->recentRoots();
    if (!m_lastRequestedRootPath.isEmpty()) {
        roots.removeAll(m_lastRequestedRootPath);
        roots.prepend(m_lastRequestedRootPath);
    }
    while (roots.size() > 10) {
        roots.removeLast();
    }
    m_configService->setRecentRoots(roots);
    m_window->setRecentRoots(roots);
}

void AppController::activateRootSession(const QString &rootPath, bool showGraphTab)
{
    if (!m_window || rootPath.isEmpty()) {
        return;
    }

    for (const RootSession &session : m_rootSessions) {
        if (session.rootPath.compare(rootPath, Qt::CaseInsensitive) == 0 && !session.result.rootPath.isEmpty()) {
            m_currentResult = session.result;
            m_activeFolderPath = session.result.rootPath;
            m_window->treePanel()->setRootSessions(m_rootSessions);
            m_window->treePanel()->selectEntryPath(m_activeFolderPath);
            m_window->chartPanel()->setScanResult(session.result);
            m_window->chartPanel()->setOtherThresholdPercent(m_otherThresholdPercent);
            m_window->chartPanel()->setActiveFolderPath(m_activeFolderPath);
            m_window->extensionsPanel()->setScanResult(session.result);
            m_window->extensionsPanel()->setActiveFolderPath(m_activeFolderPath);
            m_window->heatmapPanel()->setHeatmapData(session.result.treeEntries, {});
            m_window->timelinePanel()->setCurrentRootPath(session.result.rootPath);
            if (m_window->existingGraphPanel()) {
                m_window->existingGraphPanel()->setVisiblePaths(m_window->treePanel()->visibleFolderPaths());
                m_window->existingGraphPanel()->setOtherThresholdPercent(m_otherThresholdPercent);
                m_window->existingGraphPanel()->setGraphData(session.result.rootPath, session.result.treeEntries, {});
                m_window->existingGraphPanel()->setSelectedPath(m_activeFolderPath);
                m_window->existingGraphPanel()->setGraphRootPath(m_activeFolderPath);
            }
            if (showGraphTab) {
                m_window->showGraphTab();
            }
            m_window->setStatusText(QStringLiteral("Switched to %1").arg(rootPath));
            return;
        }
    }

    m_lastRequestedRootPath = rootPath;
    m_window->timelinePanel()->setCurrentRootPath(rootPath);
    m_window->setBusy(true);
    m_window->setProgress(0);
    m_window->setStatusText(QStringLiteral("Scanning %1").arg(rootPath));
    m_scanService->scanPath(rootPath);
}

void AppController::refreshTimeline()
{
    if (!m_window || !m_snapshotService) {
        return;
    }

    QString error;
    const QVector<SnapshotSummary> snapshots = m_snapshotService->listSnapshots(&error);
    m_window->timelinePanel()->setSnapshots(snapshots);
    QString backgroundError;
    const BackgroundRunSummary latestRun = m_snapshotService->latestBackgroundRun(&backgroundError);
    const QString cadence = m_configService->snapshotScheduleMode() == SnapshotScheduleMode::Weekly
        ? QStringLiteral("Weekly")
        : (m_configService->snapshotScheduleMode() == SnapshotScheduleMode::Monthly ? QStringLiteral("Monthly") : QStringLiteral("Daily"));
    TimelinePanel::DiagnosticsState diagnostics;
    diagnostics.scheduleEnabled = m_configService->snapshotScheduleEnabled();
    diagnostics.cadence = cadence;
    diagnostics.runAt = m_configService->snapshotScheduleTime().toString(QStringLiteral("HH:mm"));
    diagnostics.whitelistCount = m_configService->snapshotWhitelist().size();
    diagnostics.latestRun = latestRun;
    m_window->timelinePanel()->setDiagnosticsState(diagnostics);
    if (!error.isEmpty()) {
        m_window->setStatusText(QStringLiteral("Snapshot list error: %1").arg(error));
    } else if (!backgroundError.isEmpty()) {
        m_window->setStatusText(QStringLiteral("Background run status error: %1").arg(backgroundError));
    }
}

void AppController::handleCreateSnapshotRequest()
{
    if (!m_window || !m_snapshotService) {
        return;
    }

    Logger::info("Create snapshot action triggered");
    m_window->setStatusText("Create Snapshot requested in controller");

    if (m_currentResult.rootPath.isEmpty()) {
        QMessageBox::information(
            m_window,
            "Create Snapshot",
            QStringLiteral("No active scan result is loaded yet.\nScan a folder first.\n\nDatabase: %1")
                .arg(m_databaseManager->databasePath()));
        return;
    }

    QString error;
    const SnapshotCreateResult createResult = m_snapshotService->createSnapshot(
        m_currentResult,
        m_configService->snapshotThresholdBytes(),
        &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(m_window, "Create Snapshot", error.isEmpty() ? "Failed to create snapshot." : error);
        return;
    }

    QString listError;
    const QVector<SnapshotSummary> snapshots = m_snapshotService->listSnapshots(&listError);
    m_window->timelinePanel()->setCurrentRootPath(m_currentResult.rootPath);
    m_window->timelinePanel()->setSnapshots(snapshots);
    if (!listError.isEmpty()) {
        QMessageBox::warning(m_window, "Create Snapshot", QStringLiteral("Snapshot was processed, but timeline refresh failed: %1").arg(listError));
    }

    refreshTimeline();
    if (createResult.created) {
        const int visibleCount = m_window->timelinePanel()->visibleSnapshotCount();
        if (visibleCount == 0) {
            QMessageBox::warning(
                m_window,
                "Create Snapshot",
                QStringLiteral("Snapshot write reported success, but no snapshots are visible for the current root.\n\nRoot: %1\nDatabase: %2")
                    .arg(m_currentResult.rootPath)
                    .arg(m_databaseManager->databasePath()));
        } else {
            QMessageBox::information(
                m_window,
                "Create Snapshot",
                QStringLiteral("%1\n\nVisible snapshots for this root: %2\nDatabase: %3")
                    .arg(createResult.message)
                    .arg(visibleCount)
                    .arg(m_databaseManager->databasePath()));
        }
        m_window->setStatusText(createResult.message);
    } else {
        QMessageBox::information(m_window, "Create Snapshot", createResult.message);
        m_window->setStatusText(createResult.message);
    }
}

void AppController::handleCompareSnapshotMenuRequest()
{
    if (!m_window) {
        return;
    }

    const int snapshotId = m_window->timelinePanel()->selectedSnapshotId();
    if (snapshotId == 0) {
        QMessageBox::information(m_window, "Compare Snapshot", "Select a snapshot in the Timeline tab first.");
        return;
    }

    handleCompareSnapshotRequest(snapshotId);
}

void AppController::handleScanFinished()
{
    if (!m_window) {
        return;
    }

    const ScanResult result = m_scanService->lastResult();
    m_currentResult = result;
    m_activeFolderPath = result.rootPath;
    bool replaced = false;
    for (RootSession &session : m_rootSessions) {
        if (session.rootPath.compare(result.rootPath, Qt::CaseInsensitive) == 0) {
            session.result = result;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_rootSessions.push_back({result.rootPath, result});
    }
    m_window->treePanel()->setRootSessions(m_rootSessions);
    m_window->treePanel()->selectEntryPath(m_activeFolderPath);
    m_window->chartPanel()->setScanResult(result);
    m_window->chartPanel()->setOtherThresholdPercent(m_otherThresholdPercent);
    m_window->chartPanel()->setActiveFolderPath(m_activeFolderPath);
    m_window->extensionsPanel()->setScanResult(result);
    m_window->extensionsPanel()->setActiveFolderPath(m_activeFolderPath);
    applyViewMetric(m_viewMetric);
    if (const TreeEntry *rootEntry = findTreeEntry(m_activeFolderPath)) {
        m_window->detailsPanel()->setEntry(*rootEntry);
    }
    if (m_window->existingGraphPanel()) {
        GraphPanel *graph = m_window->existingGraphPanel();
        graph->setVisiblePaths(m_window->treePanel()->visibleFolderPaths());
        graph->setOtherThresholdPercent(m_otherThresholdPercent);
        graph->setGraphData(result.rootPath, result.treeEntries, {});
        graph->setSelectedPath(m_activeFolderPath);
        graph->setGraphRootPath(m_activeFolderPath);
    }
    m_window->heatmapPanel()->setHeatmapData(result.treeEntries, {});
    m_window->timelinePanel()->setCurrentRootPath(result.rootPath);
    m_window->timelinePanel()->setCompareResult({}, {}, {});
    refreshTimeline();
    if (m_folderRepository && m_fileRepository) {
        QString error;
        if (!m_folderRepository->replaceAll(result.folders, result.rootPath, &error)
            || !m_fileRepository->replaceAll(result.files, &error)) {
            Logger::warning("Failed to persist scan: " + error);
        }
    }

    m_window->setBusy(false);
    m_window->setProgress(100);
    m_window->setStatusText(QStringLiteral("Scanned %1 folders and %2 files via %3")
                                .arg(result.folders.size())
                                .arg(result.files.size())
                                .arg(result.usedEverything ? "Everything" : "filesystem"));
}

void AppController::handleScanFailed(const QString &message)
{
    if (!m_window) {
        return;
    }

    m_window->setBusy(false);
    m_window->setStatusText(message);
    QMessageBox::warning(m_window, "Scan failed", message);
}

void AppController::handleEntryActivated(const TreeEntry &entry)
{
    if (!m_window) {
        return;
    }

    if (const RootSession *session = findRootSessionForPath(entry.path)) {
        if (m_currentResult.rootPath.compare(session->result.rootPath, Qt::CaseInsensitive) != 0) {
            activateRootSession(session->result.rootPath, false);
        }
    }

    if (entry.kind != TreeEntryKind::Folder) {
        if (!entry.parentPath.isEmpty()) {
            focusFolderPath(entry.parentPath, false);
            m_window->treePanel()->selectEntryPath(entry.path);
        }
        m_window->detailsPanel()->setEntry(entry);
        if (m_window->existingGraphPanel()) {
            m_window->existingGraphPanel()->setSelectedPath(entry.path);
        }
        return;
    }

    focusFolderPath(entry.path, false);
}

void AppController::handleGraphEntryActivated(const TreeEntry &entry)
{
    if (const RootSession *session = findRootSessionForPath(entry.path)) {
        if (m_currentResult.rootPath.compare(session->result.rootPath, Qt::CaseInsensitive) != 0) {
            activateRootSession(session->result.rootPath, true);
        }
    }

    if (entry.kind == TreeEntryKind::Folder) {
        focusFolderPath(entry.path, true);
        return;
    }

    if (!m_window) {
        return;
    }

    m_window->detailsPanel()->setEntry(entry);
    if (!entry.parentPath.isEmpty()) {
        focusFolderPath(entry.parentPath, true);
    }
    m_window->treePanel()->selectEntryPath(entry.path);
    m_window->detailsPanel()->setEntry(entry);
    if (m_window->existingGraphPanel()) {
        m_window->existingGraphPanel()->setSelectedPath(entry.path);
    }
}

void AppController::handleGraphEntryOpened(const TreeEntry &entry)
{
    handleGraphEntryActivated(entry);
}

void AppController::handleGraphPathEntered(const QString &path)
{
    handleNavigatePath(path, true);
}

void AppController::handleChartEntryActivated(const TreeEntry &entry)
{
    if (const RootSession *session = findRootSessionForPath(entry.path)) {
        if (m_currentResult.rootPath.compare(session->result.rootPath, Qt::CaseInsensitive) != 0) {
            activateRootSession(session->result.rootPath, false);
        }
    }

    if (entry.kind == TreeEntryKind::Folder) {
        focusFolderPath(entry.path, false);
        return;
    }

    if (!m_window) {
        return;
    }

    m_window->detailsPanel()->setEntry(entry);
    if (!entry.parentPath.isEmpty()) {
        focusFolderPath(entry.parentPath, false);
    }
    m_window->treePanel()->selectEntryPath(entry.path);
    m_window->detailsPanel()->setEntry(entry);
    if (m_window->existingGraphPanel()) {
        m_window->existingGraphPanel()->setSelectedPath(entry.path);
    }
}

void AppController::handleNavigateToEntryRequested(const TreeEntry &entry, const QString &targetTab)
{
    auto *session = findRootSessionForPath(entry.path);
    const QString rootPath = session ? session->rootPath : entry.path;
    if (entry.kind == TreeEntryKind::Folder) {
        activateRootSession(rootPath, false);
        focusFolderPath(entry.path, false);
    }
    if (!m_window) {
        return;
    }
    if (targetTab == QStringLiteral("pie")) {
        m_window->showChartTab();
        m_window->chartPanel()->setActiveViewMode(ChartPanel::ViewMode::Pie);
    } else if (targetTab == QStringLiteral("bars")) {
        m_window->showChartTab();
        m_window->chartPanel()->setActiveViewMode(ChartPanel::ViewMode::Bars);
    } else if (targetTab == QStringLiteral("treemap")) {
        m_window->showChartTab();
        m_window->chartPanel()->setActiveViewMode(ChartPanel::ViewMode::Treemap);
    } else if (targetTab == QStringLiteral("extensions")) {
        m_window->showExtensionsTab();
    } else if (targetTab == QStringLiteral("heatmap")) {
        m_window->showHeatmapTab();
    }
}

void AppController::handleChartOpenInGraphRequested(const TreeEntry &entry)
{
    if (const RootSession *session = findRootSessionForPath(entry.path)) {
        if (m_currentResult.rootPath.compare(session->result.rootPath, Qt::CaseInsensitive) != 0) {
            activateRootSession(session->result.rootPath, true);
        }
    }

    if (entry.kind == TreeEntryKind::Folder) {
        focusFolderPath(entry.path, true);
        return;
    }

    if (!m_window) {
        return;
    }

    m_window->detailsPanel()->setEntry(entry);
    if (!entry.parentPath.isEmpty()) {
        focusFolderPath(entry.parentPath, true);
    }
    m_window->treePanel()->selectEntryPath(entry.path);
    m_window->detailsPanel()->setEntry(entry);
    if (m_window->existingGraphPanel()) {
        m_window->existingGraphPanel()->setSelectedPath(entry.path);
    }
}

void AppController::handleChartOpenRequested(const TreeEntry &entry)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(entry.path));
}

void AppController::handleChartShowInExplorerRequested(const TreeEntry &entry)
{
    const QString argument = QFileInfo(entry.path).isDir()
        ? entry.path
        : QStringLiteral("/select,") + QDir::toNativeSeparators(entry.path);
    QProcess::startDetached("explorer.exe", {argument});
}

void AppController::handleChartCopyPathRequested(const TreeEntry &entry)
{
    QGuiApplication::clipboard()->setText(entry.path);
}

void AppController::handleOthersThresholdRequest()
{
    if (!m_window) {
        return;
    }

    bool ok = false;
    const double value = QInputDialog::getDouble(
        m_window,
        "Others Threshold",
        "Send folders/files below this % to Others:",
        m_otherThresholdPercent,
        0.0,
        100.0,
        1,
        &ok);
    if (!ok) {
        return;
    }

    m_otherThresholdPercent = value;
    m_configService->setOthersThresholdPercent(m_otherThresholdPercent);
    m_window->chartPanel()->setOtherThresholdPercent(m_otherThresholdPercent);
    if (m_window->existingGraphPanel()) {
        m_window->existingGraphPanel()->setOtherThresholdPercent(m_otherThresholdPercent);
    }
    m_window->setStatusText(QStringLiteral("Others threshold set to %1%").arg(QString::number(m_otherThresholdPercent, 'f', 1)));
}

void AppController::handleRescanCurrentRootRequest()
{
    if (!m_currentResult.rootPath.isEmpty()) {
        openPath(m_currentResult.rootPath);
    }
}

void AppController::handleClearAllRootsRequest()
{
    m_rootSessions.clear();
    m_currentResult = {};
    m_activeFolderPath.clear();
    if (m_window) {
        m_window->treePanel()->setRootSessions({});
        m_window->detailsPanel()->clear();
        m_window->chartPanel()->setScanResult({});
        m_window->extensionsPanel()->setScanResult({});
        m_window->heatmapPanel()->setHeatmapData({}, {});
        m_window->timelinePanel()->setCurrentRootPath(QString());
        m_window->timelinePanel()->setCompareResult({}, {}, {});
        m_window->setStatusText("Cleared all roots");
    }
}

void AppController::handleRecentRootRequested(const QString &path)
{
    openPath(path);
}

void AppController::handleOpenCurrentInExplorerRequest()
{
    if (!m_activeFolderPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_activeFolderPath));
    }
}

void AppController::handleOpenTerminalHereRequest()
{
    if (!m_activeFolderPath.isEmpty()) {
        QProcess::startDetached("cmd.exe", {QStringLiteral("/K"), QStringLiteral("cd /d %1").arg(m_activeFolderPath)});
    }
}

void AppController::handleCopyCurrentPathRequest()
{
    if (!m_activeFolderPath.isEmpty()) {
        QGuiApplication::clipboard()->setText(m_activeFolderPath);
    }
}

void AppController::handleOpenConfigFolderRequest()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_configService->configPath()).absolutePath()));
}

void AppController::handleOpenLogFileRequest()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::current().absoluteFilePath("opentree.log")));
}

void AppController::handleExpandAllRequest()
{
    if (m_window && m_window->treePanel()) {
        m_window->treePanel()->expandAll();
    }
}

void AppController::handleCollapseAllRequest()
{
    if (m_window && m_window->treePanel()) {
        m_window->treePanel()->collapseAll();
    }
}

void AppController::handleTreemapDepthChanged(int depth)
{
    if (m_window && m_window->chartPanel()) {
        m_window->chartPanel()->setTreemapDepth(depth);
        m_window->setStatusText(QStringLiteral("Treemap depth: %1").arg(depth));
    }
}

void AppController::handleThemeSelected(const QString &themeId)
{
    m_configService->setThemeId(themeId);
    applyCurrentTheme();
}

void AppController::reloadThemes()
{
    m_themes = ThemeManager::loadThemes(m_configService->themesDirectory());
    if (m_window) {
        QMap<QString, QString> themeNames;
        for (auto it = m_themes.cbegin(); it != m_themes.cend(); ++it) {
            themeNames.insert(it.key(), it.value().name);
        }
        m_window->setAvailableThemes(themeNames, m_configService->themeId());
    }
}

void AppController::applyCurrentTheme()
{
    if (!m_window) {
        return;
    }

    QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) {
        return;
    }

    const QString themeId = m_configService->themeId();
    const ThemeDefinition theme = m_themes.value(themeId, m_themes.value("dark"));
    ThemeManager::applyTheme(*app, theme);
    QMap<QString, QString> themeNames;
    for (auto it = m_themes.cbegin(); it != m_themes.cend(); ++it) {
        themeNames.insert(it.key(), it.value().name);
    }
    m_window->setAvailableThemes(themeNames, theme.id);
    m_window->setStatusText(QStringLiteral("Theme: %1").arg(theme.name));
}

void AppController::applyViewMetric(ViewMetric metric)
{
    m_viewMetric = metric;
    m_configService->setViewMetric(metric);

    if (!m_window) {
        return;
    }

    if (metric == ViewMetric::Size) {
        m_window->viewBySizeAction()->setChecked(true);
    } else if (metric == ViewMetric::Percentage) {
        m_window->viewByPercentageAction()->setChecked(true);
    } else {
        m_window->viewByFilesAction()->setChecked(true);
    }

    if (m_window->existingGraphPanel()) {
        if (metric == ViewMetric::Files) {
            m_window->existingGraphPanel()->setNodeSizeMode(GraphPanel::NodeSizeMode::Files);
        } else {
            m_window->existingGraphPanel()->setNodeSizeMode(GraphPanel::NodeSizeMode::Size);
        }
    }
    m_window->chartPanel()->setViewMetric(metric);
    m_window->extensionsPanel()->setViewMetric(metric);
    m_window->heatmapPanel()->setViewMetric(metric);
}

RootSession *AppController::findRootSessionForPath(const QString &path)
{
    for (RootSession &session : m_rootSessions) {
        if (session.result.rootPath.isEmpty()) {
            continue;
        }
        if (path.compare(session.result.rootPath, Qt::CaseInsensitive) == 0
            || path.startsWith(session.result.rootPath + QLatin1Char('\\'), Qt::CaseInsensitive)
            || path.startsWith(session.result.rootPath + QLatin1Char('/'), Qt::CaseInsensitive)) {
            return &session;
        }
    }
    return nullptr;
}

const RootSession *AppController::findRootSessionForPath(const QString &path) const
{
    for (const RootSession &session : m_rootSessions) {
        if (session.result.rootPath.isEmpty()) {
            continue;
        }
        if (path.compare(session.result.rootPath, Qt::CaseInsensitive) == 0
            || path.startsWith(session.result.rootPath + QLatin1Char('\\'), Qt::CaseInsensitive)
            || path.startsWith(session.result.rootPath + QLatin1Char('/'), Qt::CaseInsensitive)) {
            return &session;
        }
    }
    return nullptr;
}

void AppController::handleNavigatePath(const QString &path, bool showGraphTab)
{
    if (!m_window) {
        return;
    }

    const QString cleaned = QDir::fromNativeSeparators(path.trimmed());
    if (cleaned.isEmpty()) {
        return;
    }

    QFileInfo info(QDir::toNativeSeparators(cleaned));
    if (!info.exists() || !info.isDir()) {
        m_window->setStatusText(QStringLiteral("Invalid folder path: %1").arg(path));
        return;
    }

    const QString targetPath = QDir::toNativeSeparators(info.absoluteFilePath());
    if (const RootSession *session = findRootSessionForPath(targetPath)) {
        activateRootSession(session->result.rootPath, showGraphTab);
        focusFolderPath(targetPath, showGraphTab);
        return;
    }

    if (QFileInfo(targetPath).exists()) {
        activateRootSession(targetPath, showGraphTab);
        return;
    }
}

void AppController::focusFolderPath(const QString &path, bool showGraphTab)
{
    if (!m_window || path.isEmpty()) {
        return;
    }

    if (const RootSession *session = findRootSessionForPath(path)) {
        if (m_currentResult.rootPath.compare(session->result.rootPath, Qt::CaseInsensitive) != 0) {
            activateRootSession(session->result.rootPath, showGraphTab);
        }
    }

    const TreeEntry *entry = findTreeEntry(path);
    if (!entry) {
        return;
    }

    m_activeFolderPath = entry->path;
    m_window->treePanel()->selectEntryPath(entry->path);
    m_window->detailsPanel()->setEntry(*entry);
    m_window->chartPanel()->setActiveFolderPath(entry->path);
    m_window->extensionsPanel()->setActiveFolderPath(entry->path);
    if (showGraphTab) {
        m_window->showGraphTab();
    }
    if (m_window->existingGraphPanel()) {
        m_window->existingGraphPanel()->setSelectedPath(entry->path);
        m_window->existingGraphPanel()->setGraphRootPath(entry->path);
    }
}

const TreeEntry *AppController::findTreeEntry(const QString &path) const
{
    for (const TreeEntry &entry : m_currentResult.treeEntries) {
        if (entry.path.compare(path, Qt::CaseInsensitive) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

void AppController::handleLocateEverythingRequest()
{
    if (!m_window) {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        m_window,
        "Locate Everything executable",
        m_configService->everythingExecutablePath(),
        "Everything executable (Everything.exe);;All files (*.*)");
    if (filePath.isEmpty()) {
        return;
    }

    m_configService->setEverythingExecutablePath(filePath);
    m_window->setStatusText(QStringLiteral("Everything executable set to %1").arg(filePath));
}

void AppController::handleSnapshotSettingsRequest()
{
    if (!m_window) {
        return;
    }

    SnapshotSettingsDialog dialog(m_configService, m_window);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString error;
    if (!TaskSchedulerUtils::syncSnapshotTask(
            QCoreApplication::applicationFilePath(),
            m_configService->snapshotScheduleEnabled(),
            m_configService->snapshotScheduleMode(),
            m_configService->snapshotScheduleTime(),
            &error)) {
        QMessageBox::warning(m_window, "Snapshot Settings", error.isEmpty() ? "Failed to update scheduled task." : error);
        return;
    }

    m_window->setStatusText("Snapshot settings updated");
}

void AppController::handleCompareSnapshotRequest(int snapshotId)
{
    if (!m_window || !m_snapshotService) {
        return;
    }

    if (m_currentResult.rootPath.isEmpty()) {
        QMessageBox::information(m_window, "Compare Snapshot", "Scan a folder first.");
        return;
    }

    QString error;
    const SnapshotCompareResult compare = m_snapshotService->compareSnapshotToCurrent(snapshotId, m_currentResult, &error);
    const QVector<SnapshotCompareRow> rows = m_snapshotService->compareSnapshotRows(snapshotId, m_currentResult, &error);
    const QVector<SnapshotFileEvent> events = m_snapshotService->snapshotFileEvents(snapshotId, &error);
    if (!compare.found) {
        QMessageBox::warning(m_window, "Compare Snapshot", error.isEmpty() ? "Nothing to compare." : error);
        return;
    }

    m_window->timelinePanel()->setCompareResult(compare, rows, events);
    if (m_window->existingGraphPanel()) {
        m_window->existingGraphPanel()->setGraphData(m_currentResult.rootPath, m_currentResult.treeEntries, rows);
        m_window->existingGraphPanel()->setSelectedPath(m_activeFolderPath.isEmpty() ? m_currentResult.rootPath : m_activeFolderPath);
        if (!m_activeFolderPath.isEmpty()) {
            m_window->existingGraphPanel()->setGraphRootPath(m_activeFolderPath);
        }
    }
    m_window->heatmapPanel()->setHeatmapData(m_currentResult.treeEntries, rows);
    m_window->setStatusText(QStringLiteral("Compared snapshot from %1").arg(compare.snapshotCreatedAt));
}

}
