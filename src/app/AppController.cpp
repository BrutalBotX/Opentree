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
#include <QPushButton>
#include <QGuiApplication>
#include <QTimer>
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
#include "services/ReportService.h"
#include "services/AnalysisService.h"
#include "services/VirtualTrashService.h"
#include "ui/DetailsPanel.h"
#include "ui/ChartPanel.h"
#include "ui/DetailsTablePanel.h"
#include "ui/DriveSelector.h"
#include "ui/DuplicatesPanel.h"
#include "ui/ExtensionsPanel.h"
#include "ui/InsightsPanel.h"
#include "ui/TrashPanel.h"
#include "ui/GraphPanel.h"
#include "ui/HeatmapPanel.h"
#include "ui/SnapshotSettingsDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/SnapshotManagerDialog.h"
#include "ui/ThemeManager.h"
#include "ui/TimelinePanel.h"
#include "ui/TreePanel.h"
#include "utils/Logger.h"
#include "utils/TaskSchedulerUtils.h"
#include "utils/PathUtils.h"

namespace opentree {

namespace {

QString normalizeRootKey(const QString &path)
{
    return PathUtils::normalizePath(path);
}

bool isSameOrDescendantPath(const QString &path, const QString &rootPath)
{
    return PathUtils::isSameOrDescendant(path, rootPath);
}

template <typename Fn>
void updateGraphPanel(GraphPanel *graph, Fn &&fn)
{
    if (!graph) {
        return;
    }

    graph->beginBatchUpdate();
    fn(graph);
    graph->endBatchUpdate();
}

GraphPanel *ensureGraphPanel(MainWindow *window)
{
    return window ? window->graphPanel() : nullptr;
}

}

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

    if (m_window->useEverythingAction()) {
        m_window->setUseEverythingChecked(m_configService->useEverything());
    }
    m_window->setCloseToTrayEnabled(m_configService->closeToTray());

    connect(m_window, &MainWindow::scanRequested, this, [this]() {
        handleScanRequest();
    });
    connect(m_window->createSnapshotAction(), &QAction::triggered, this, [this]() {
        handleCreateSnapshotRequest();
    });
    connect(m_window->compareSnapshotAction(), &QAction::triggered, this, [this]() {
        handleCompareSnapshotMenuRequest();
    });
    connect(m_window, &MainWindow::snapshotManagementRequested, this, [this]() {
        handleSnapshotManagementRequest();
    });
    connect(m_window, &MainWindow::graphPanelCreated, this, [this](GraphPanel *graph) {
        if (!graph || !m_configService) {
            return;
        }
        graph->setMaxNodes(m_configService->graphMaxNodes());
        graph->setFollowTreeExpansion(m_configService->graphFollowTreeExpansion());
        connect(graph, &GraphPanel::followTreeExpansionChanged, this, [this](bool follow) {
            m_configService->setGraphFollowTreeExpansion(follow);
            if (m_window) {
                m_window->setStatusText(follow
                    ? QStringLiteral("Graph now follows the tree expansion")
                    : QStringLiteral("Graph now shows the whole subtree"));
            }
        });
    });
    connect(m_window, &MainWindow::settingsRequested, this, [this]() {
        handleSettingsRequest();
    });
    connect(m_window, &MainWindow::everythingLocationRequested, this, [this]() {
        handleLocateEverythingRequest();
    });
    connect(m_window, &MainWindow::exportDetailsCsvRequested, this, [this]() {
        handleExportDetailsCsvRequest();
    });
    connect(m_window, &MainWindow::exportReportRequested, this, [this](const QString &format) {
        handleExportReportRequest(format);
    });
    connect(m_window->detailsTablePanel(), &DetailsTablePanel::exportRequested, this, [this]() {
        handleExportDetailsCsvRequest();
    });
    connect(m_window, &MainWindow::useEverythingToggled, this, [this](bool enabled) {
        m_configService->setUseEverything(enabled);
        m_window->setStatusText(enabled
            ? QStringLiteral("Everything scan engine enabled")
            : QStringLiteral("Everything scan engine disabled; filesystem scanning will be used"));
    });
    connect(m_window, &MainWindow::testEverythingRequested, this, [this]() {
        handleTestEverythingRequest();
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
    connect(m_window, &MainWindow::tabModeChanged, this, [this](bool timelineCompareMode) {
        if (!m_window) {
            return;
        }

        if (!timelineCompareMode) {
            if (const TreeEntry *entry = findTreeEntry(m_activeFolderPath)) {
                m_window->detailsPanel()->setEntry(*entry);
            } else {
                m_window->detailsPanel()->clear();
            }
        } else {
            m_window->detailsPanel()->clear();
        }
    });
    connect(m_window->treePanel(), &TreePanel::entryActivated, this, &AppController::handleEntryActivated);
    connect(m_window->treePanel(), &TreePanel::entryOpened, this, &AppController::handleGraphEntryOpened);
    connect(m_window->chartPanel(), &ChartPanel::entryActivated, this, &AppController::handleChartEntryActivated);
    connect(m_window->chartPanel(), &ChartPanel::entryOpenInGraphRequested, this, &AppController::handleChartOpenInGraphRequested);
    connect(m_window->detailsTablePanel(), &DetailsTablePanel::entryActivated, this, &AppController::handleChartEntryActivated);
    m_duplicatesPanel = new DuplicatesPanel(m_configService, m_window);
    m_window->setDuplicatesPanel(m_duplicatesPanel);
    connect(m_duplicatesPanel, &DuplicatesPanel::entryActivated, this, &AppController::handleChartEntryActivated);

    if (m_databaseManager && m_databaseManager->database().isValid()) {
        m_trashService = new VirtualTrashService(m_databaseManager->database());
    }
    m_trashPanel = new TrashPanel(m_trashService, m_window);
    m_window->setTrashPanel(m_trashPanel);

    if (m_databaseManager && m_databaseManager->database().isValid()) {
        m_analysisService = new AnalysisService(m_databaseManager->database());
    }
    m_insightsPanel = new InsightsPanel(m_analysisService, m_trashService, m_window);
    m_window->setInsightsPanel(m_insightsPanel);
    connect(m_window->driveSelector(), &DriveSelector::driveActivated, this, &AppController::handleRecentRootRequested);
    connect(m_window->chartPanel(), &ChartPanel::entryOpenRequested, this, &AppController::handleChartOpenRequested);
    connect(m_window->chartPanel(), &ChartPanel::entryShowInExplorerRequested, this, &AppController::handleChartShowInExplorerRequested);
    connect(m_window->chartPanel(), &ChartPanel::entryCopyPathRequested, this, &AppController::handleChartCopyPathRequested);
    connect(m_window->chartPanel(), &ChartPanel::pathEntered, this, [this](const QString &path) {
        handleNavigatePath(path, false);
    });
    connect(m_window->treePanel(), &TreePanel::visiblePathsChanged, this, [this](const QStringList &paths) {
        // Expanding/collapsing many folders fires this for every node. Coalesce the burst
        // so the graph is rebuilt once instead of thousands of times (Expand All used to
        // lock up or crash the app on large trees).
        m_pendingGraphPaths = paths;
        if (!m_graphPathTimer) {
            m_graphPathTimer = new QTimer(this);
            m_graphPathTimer->setSingleShot(true);
            m_graphPathTimer->setInterval(200);
            connect(m_graphPathTimer, &QTimer::timeout, this, [this]() {
                if (!m_window) {
                    return;
                }
                updateGraphPanel(m_window->existingGraphPanel(), [&](GraphPanel *graph) {
                    graph->beginBatchUpdate();
                    graph->setVisiblePaths(m_pendingGraphPaths);
                    graph->endBatchUpdate();
                });
            });
        }
        m_graphPathTimer->start();
    });
    connect(m_window->timelinePanel(), &TimelinePanel::snapshotSelected, this, [this](int snapshotId) {
        Q_UNUSED(snapshotId);
    });
    connect(m_window->timelinePanel(), &TimelinePanel::compareSnapshotRequested, this, &AppController::handleCompareSnapshotRequest);
    connect(m_window, &MainWindow::graphNodeActivated, this, &AppController::handleGraphNodeActivated);
    connect(m_window, &MainWindow::graphTabActivated, this, [this]() {
        if (!m_window) {
            return;
        }

        Logger::info(QStringLiteral("graph-debug graphTabActivated currentTab=%1 root=%2 active=%3")
                         .arg(m_window->currentTabIndex())
                         .arg(m_currentResult.rootPath)
                         .arg(m_activeFolderPath));

        GraphPanel *graph = m_window->graphPanel();
        connect(graph, &GraphPanel::entryActivated, this, &AppController::handleGraphEntryActivated, Qt::UniqueConnection);
        connect(graph, &GraphPanel::entryOpened, this, &AppController::handleGraphEntryOpened, Qt::UniqueConnection);
        connect(graph, &GraphPanel::pathEntered, this, &AppController::handleGraphPathEntered, Qt::UniqueConnection);
        updateGraphPanel(graph, [&](GraphPanel *panel) {
            panel->setVisiblePaths(m_window->treePanel()->visibleFolderPaths());
            panel->setOtherThresholdPercent(m_otherThresholdPercent);
            panel->setGraphData(m_currentResult.rootPath, m_currentResult.treeEntries, {});
            if (!m_activeFolderPath.isEmpty()) {
                panel->setSelectedPath(m_activeFolderPath);
                panel->setGraphRootPath(m_activeFolderPath);
            }
        });
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

    const QString normalizedFolder = normalizeRootKey(folder);

    m_lastRequestedRootPath = normalizedFolder;
    refreshRecentRoots();
    m_window->timelinePanel()->setCurrentRootPath(normalizedFolder);

    ScanResult cachedResult;
    QString cacheError;
    if (loadCachedRootResult(normalizedFolder, &cachedResult, &cacheError)) {
        m_currentResult = cachedResult;
        m_activeFolderPath = cachedResult.rootPath;
        bool replaced = false;
        for (RootSession &session : m_rootSessions) {
            if (session.rootPath.compare(cachedResult.rootPath, Qt::CaseInsensitive) == 0) {
                session.result = cachedResult;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_rootSessions.push_back({cachedResult.rootPath, cachedResult});
        }
        syncActiveResultUi(cachedResult, m_activeFolderPath, {}, true);
        m_window->setStatusText(QStringLiteral("Showing cached result for %1, refreshing...").arg(normalizedFolder));
    } else if (!cacheError.isEmpty()) {
        m_window->setStatusText(QStringLiteral("Cache load failed, scanning %1").arg(normalizedFolder));
    }

    m_window->setBusy(true);
    m_window->setProgress(0);
    if (cachedResult.rootPath.isEmpty()) {
        m_window->setStatusText(QStringLiteral("Scanning %1").arg(normalizedFolder));
    }
    m_scanService->scanPath(normalizedFolder);
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

    const QString normalizedRoot = normalizeRootKey(rootPath);

    for (const RootSession &session : m_rootSessions) {
        if (session.rootPath.compare(normalizedRoot, Qt::CaseInsensitive) == 0 && !session.result.rootPath.isEmpty()) {
            m_currentResult = session.result;
            m_activeFolderPath = session.result.rootPath;
            syncActiveResultUi(session.result, m_activeFolderPath, {}, false);
            if (showGraphTab) {
                m_window->showGraphTab();
            }
            m_window->setStatusText(QStringLiteral("Switched to %1").arg(normalizedRoot));
            return;
        }
    }

    m_lastRequestedRootPath = normalizedRoot;
    m_window->timelinePanel()->setCurrentRootPath(normalizedRoot);
    m_window->driveSelector()->setRootPath(normalizedRoot);

    ScanResult cachedResult;
    QString cacheError;
    if (loadCachedRootResult(normalizedRoot, &cachedResult, &cacheError)) {
        m_currentResult = cachedResult;
        m_activeFolderPath = cachedResult.rootPath;
        bool replaced = false;
        for (RootSession &session : m_rootSessions) {
            if (session.rootPath.compare(cachedResult.rootPath, Qt::CaseInsensitive) == 0) {
                session.result = cachedResult;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_rootSessions.push_back({cachedResult.rootPath, cachedResult});
        }
        syncActiveResultUi(cachedResult, m_activeFolderPath, {}, true);
        m_window->setStatusText(QStringLiteral("Showing cached result for %1, refreshing...").arg(normalizedRoot));
    } else if (!cacheError.isEmpty()) {
        m_window->setStatusText(QStringLiteral("Cache load failed, scanning %1").arg(normalizedRoot));
    }

    m_window->setBusy(true);
    m_window->setProgress(0);
    if (cachedResult.rootPath.isEmpty()) {
        m_window->setStatusText(QStringLiteral("Scanning %1").arg(normalizedRoot));
    }
    m_scanService->scanPath(normalizedRoot);
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
    syncActiveResultUi(result, m_activeFolderPath, {}, true);
    refreshTimeline();
    if (m_folderRepository && m_fileRepository) {
        QString error;
        if (!m_folderRepository->replaceAll(result.folders, result.rootPath, &error)
            || !m_fileRepository->replaceAll(result.files, result.rootPath, &error)) {
            Logger::warning("Failed to persist scan: " + error);
        }
    }

    m_window->setBusy(false);
    m_window->setProgress(100);
    m_window->setStatusText(QStringLiteral("Scanned %1 folders and %2 files via %3")
                                .arg(result.folders.size())
                                .arg(result.files.size())
                                .arg(result.usedEverything ? "Everything" : "filesystem"));
    m_window->notify(QStringLiteral("Scan complete"),
                     QStringLiteral("%1 folders and %2 files via %3")
                         .arg(result.folders.size())
                         .arg(result.files.size())
                         .arg(result.usedEverything ? QStringLiteral("Everything") : QStringLiteral("filesystem")));
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

    // Selecting an item that belongs to another scanned root has to switch the active
    // root first; otherwise the panels stay scoped to the old root and can show nothing.
    if (const RootSession *session = findRootSessionForPath(entry.path)) {
        if (m_currentResult.rootPath.compare(session->result.rootPath, Qt::CaseInsensitive) != 0) {
            activateRootSession(session->result.rootPath, false);
        }
    }

    m_activeFolderPath = entry.kind == TreeEntryKind::Folder ? entry.path : entry.parentPath;
    m_window->detailsPanel()->setEntry(entry);
    m_window->chartPanel()->setActiveFolderPath(m_activeFolderPath);
    m_window->detailsTablePanel()->setActiveFolderPath(m_activeFolderPath);
    m_window->extensionsPanel()->setActiveFolderPath(m_activeFolderPath);
    m_window->heatmapPanel()->setActiveFolderPath(m_activeFolderPath);
    updateGraphPanel(m_window->existingGraphPanel(), [&](GraphPanel *graph) {
        graph->setSelectedPath(entry.path);
    });
    updateTrashSelection();
}

void AppController::handleGraphEntryActivated(const TreeEntry &entry)
{
    Logger::info(QStringLiteral("graph-debug graph activate kind=%1 path=%2 parent=%3")
                     .arg(entry.kind == TreeEntryKind::Folder ? QStringLiteral("folder") : QStringLiteral("file"))
                     .arg(entry.path)
                     .arg(entry.parentPath));
    if (const RootSession *session = findRootSessionForPath(entry.path)) {
        if (m_currentResult.rootPath.compare(session->result.rootPath, Qt::CaseInsensitive) != 0) {
            activateRootSession(session->result.rootPath, true);
        }
    }

    if (entry.kind == TreeEntryKind::Folder) {
        if (!m_window) {
            return;
        }

        m_window->detailsPanel()->setEntry(entry);
        updateGraphPanel(m_window->existingGraphPanel(), [&](GraphPanel *graph) {
            graph->setSelectedPath(entry.path);
        });
        return;
    }

    if (!m_window) {
        return;
    }

    syncFileSelectionUi(entry);
}

void AppController::handleGraphNodeActivated(const QString &path)
{
    if (!m_window || !m_snapshotService || path.isEmpty()) {
        return;
    }

    // Highlight the matching snapshot for this graph node, but never switch tabs:
    // clicking a graph node must not yank the user out of the Graph view.
    const QVector<SnapshotSummary> snapshots = m_snapshotService->listSnapshots(nullptr);
    for (const SnapshotSummary &snapshot : snapshots) {
        const QString snapshotRoot = snapshot.rootPath;
        if (snapshotRoot.isEmpty()) {
            continue;
        }
        if (snapshotRoot.compare(path, Qt::CaseInsensitive) == 0 || path.startsWith(snapshotRoot, Qt::CaseInsensitive)) {
            m_window->timelinePanel()->selectSnapshotId(snapshot.id);
            break;
        }
    }
}

void AppController::handleGraphEntryOpened(const TreeEntry &entry)
{
    Logger::info(QStringLiteral("graph-debug graph open kind=%1 path=%2 parent=%3")
                     .arg(entry.kind == TreeEntryKind::Folder ? QStringLiteral("folder") : QStringLiteral("file"))
                     .arg(entry.path)
                     .arg(entry.parentPath));
    if (entry.kind == TreeEntryKind::Folder) {
        focusFolderPath(entry.path, true);
        return;
    }

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

    if (!entry.parentPath.isEmpty()) {
        focusFolderPath(entry.parentPath, false);
    }
    m_window->treePanel()->selectEntryPath(entry.path);
    syncFileSelectionUi(entry);
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

    if (!entry.parentPath.isEmpty()) {
        focusFolderPath(entry.parentPath, true);
    }
    m_window->treePanel()->selectEntryPath(entry.path);
    syncFileSelectionUi(entry);
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
    if (m_window && !m_rootSessions.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            m_window,
            "Clear All Roots",
            QStringLiteral("Close all %1 scanned roots? Cached data stays on disk, but the current session is cleared.").arg(m_rootSessions.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    m_rootSessions.clear();
    m_currentResult = {};
    m_activeFolderPath.clear();
    if (m_window) {
        m_window->treePanel()->setRootSessions({});
        m_window->detailsPanel()->clear();
        m_window->chartPanel()->setScanResult({});
        m_window->extensionsPanel()->setScanResult({});
        m_window->detailsTablePanel()->setScanResult({});
        if (m_duplicatesPanel) {
            m_duplicatesPanel->setScanResult({});
        }
        m_window->driveSelector()->setRootPath(QString());
        m_window->heatmapPanel()->setHeatmapData({}, {});
        m_window->heatmapPanel()->setActiveFolderPath(QString());
        m_window->timelinePanel()->setCurrentRootPath(QString());
        m_window->timelinePanel()->setSnapshots({});
        m_window->timelinePanel()->resetCompareState();
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

void AppController::updateTrashSelection()
{
    if (!m_window || !m_window->trashPanel()) {
        return;
    }

    if (const TreeEntry *entry = findTreeEntry(m_activeFolderPath)) {
        m_window->trashPanel()->setSelection(*entry);
    } else {
        TreeEntry empty;
        m_window->trashPanel()->setSelection(empty);
    }
}

void AppController::handleExpandAllRequest()
{
    if (!m_window || !m_window->treePanel()) {
        return;
    }

    // Expanding walks the whole model and re-lays out the view, so a whole drive root
    // (hundreds of thousands of folders) would freeze or crash the app. Warn first, and
    // refuse outright above a hard ceiling.
    const int folderCount = m_currentResult.folders.size();
    constexpr int kExpandAllWarnThreshold = 200;
    constexpr int kExpandAllBlockThreshold = 5000;

    if (folderCount > kExpandAllBlockThreshold) {
        QMessageBox::warning(
            m_window,
            QStringLiteral("Expand All"),
            QStringLiteral("This root has %1 folders, which is far too many to expand at once.\n\n"
                           "Doing it would very likely freeze or crash the app. Expand the folders you "
                           "need one at a time instead, or scan a smaller root.")
                .arg(folderCount));
        m_window->setStatusText(QStringLiteral("Expand All skipped: %1 folders is too many").arg(folderCount));
        return;
    }

    if (folderCount > kExpandAllWarnThreshold) {
        QMessageBox box(m_window);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Expand All"));
        box.setText(QStringLiteral("Expand every folder in the tree?"));
        box.setInformativeText(QStringLiteral(
            "The current root has %1 folders under it. Expanding all of them is very "
            "expensive and can freeze or crash the app, especially when several roots are "
            "open.\n\nExpand folders a few at a time if you only need part of the tree.")
                .arg(folderCount));
        QPushButton *expandButton = box.addButton(QStringLiteral("Expand All Anyway"), QMessageBox::AcceptRole);
        QPushButton *cancelButton = box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
        box.setDefaultButton(cancelButton);
        box.exec();
        if (box.clickedButton() != expandButton) {
            if (m_window) {
                m_window->setStatusText(QStringLiteral("Expand All cancelled"));
            }
            return;
        }
    }

    m_window->treePanel()->expandAll();
    m_window->setStatusText(QStringLiteral("Expanded all folders"));
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
    m_window->detailsTablePanel()->setViewMetric(metric);
}

RootSession *AppController::findRootSessionForPath(const QString &path)
{
    for (RootSession &session : m_rootSessions) {
        if (session.result.rootPath.isEmpty()) {
            continue;
        }
        if (isSameOrDescendantPath(path, session.result.rootPath)) {
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
        if (isSameOrDescendantPath(path, session.result.rootPath)) {
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
    syncFolderFocusUi(*entry, showGraphTab);
}

void AppController::syncActiveResultUi(const ScanResult &result, const QString &activeFolderPath, const QVector<SnapshotCompareRow> &compareRows, bool resetCompare)
{
    if (!m_window) {
        return;
    }

    Logger::info(QStringLiteral("graph-debug syncActiveResultUi root=%1 active=%2 entries=%3 compareRows=%4 resetCompare=%5")
                     .arg(result.rootPath)
                     .arg(activeFolderPath)
                     .arg(result.treeEntries.size())
                     .arg(compareRows.size())
                     .arg(resetCompare ? QStringLiteral("true") : QStringLiteral("false")));

    m_window->treePanel()->setRootSessions(m_rootSessions);
    m_window->treePanel()->selectEntryPath(activeFolderPath);
    m_window->chartPanel()->setScanResult(result);
    m_window->chartPanel()->setOtherThresholdPercent(m_otherThresholdPercent);
    m_window->chartPanel()->setActiveFolderPath(activeFolderPath);
    m_window->extensionsPanel()->setScanResult(result);
    m_window->extensionsPanel()->setActiveFolderPath(activeFolderPath);
    m_window->detailsTablePanel()->setScanResult(result);
    m_window->detailsTablePanel()->setActiveFolderPath(activeFolderPath);
    if (m_duplicatesPanel) {
        m_duplicatesPanel->setScanResult(result);
    }
    applyViewMetric(m_viewMetric);
    if (!m_window->isTimelineTabVisible()) {
        if (const TreeEntry *rootEntry = findTreeEntry(activeFolderPath)) {
            m_window->detailsPanel()->setEntry(*rootEntry);
        } else {
            m_window->detailsPanel()->clear();
        }
    }
    updateGraphPanel(ensureGraphPanel(m_window), [&](GraphPanel *graph) {
        graph->beginBatchUpdate();
        graph->setVisiblePaths(m_window->treePanel()->visibleFolderPaths());
        graph->setOtherThresholdPercent(m_otherThresholdPercent);
        graph->setGraphData(result.rootPath, result.treeEntries, compareRows);
        graph->setSelectedPath(activeFolderPath);
        graph->setGraphRootPath(activeFolderPath);
        graph->endBatchUpdate();
    });
    m_window->heatmapPanel()->setActiveFolderPath(activeFolderPath);
    m_window->heatmapPanel()->setHeatmapData(result.treeEntries, compareRows);
    if (m_insightsPanel) {
        m_insightsPanel->setScanResult(result);
    }
    updateTrashSelection();
    m_window->timelinePanel()->setCurrentRootPath(result.rootPath);
    if (resetCompare) {
        m_window->timelinePanel()->resetCompareState();
    }
    Q_UNUSED(resetCompare);
}

void AppController::syncFolderFocusUi(const TreeEntry &entry, bool showGraphTab)
{
    if (!m_window) {
        return;
    }

    Logger::info(QStringLiteral("graph-debug syncFolderFocusUi path=%1 showGraphTab=%2")
                     .arg(entry.path)
                     .arg(showGraphTab ? QStringLiteral("true") : QStringLiteral("false")));

    m_activeFolderPath = entry.path;
    m_window->treePanel()->selectEntryPath(entry.path);
    m_window->detailsPanel()->setEntry(entry);
    m_window->chartPanel()->setActiveFolderPath(entry.path);
    m_window->detailsTablePanel()->setActiveFolderPath(entry.path);
    m_window->extensionsPanel()->setActiveFolderPath(entry.path);
    m_window->heatmapPanel()->setActiveFolderPath(entry.path);
    updateGraphPanel(ensureGraphPanel(m_window), [&](GraphPanel *graph) {
        graph->beginBatchUpdate();
        graph->setVisiblePaths(m_window->treePanel()->visibleFolderPaths());
        graph->setSelectedPath(entry.path);
        graph->setGraphRootPath(entry.path);
        graph->endBatchUpdate();
    });
    if (showGraphTab) {
        m_window->showGraphTab();
    }
}

void AppController::syncFileSelectionUi(const TreeEntry &entry)
{
    if (!m_window) {
        return;
    }

    Logger::info(QStringLiteral("graph-debug syncFileSelectionUi path=%1 parent=%2")
                     .arg(entry.path)
                     .arg(entry.parentPath));

    m_window->detailsPanel()->setEntry(entry);
    updateGraphPanel(ensureGraphPanel(m_window), [&](GraphPanel *graph) {
        graph->setSelectedPath(entry.path);
    });
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
        m_configService->resolvedEverythingExecutablePath(),
        "Everything executable (Everything.exe);;All files (*.*)");
    if (filePath.isEmpty()) {
        return;
    }

    m_configService->setEverythingExecutablePath(filePath);

    QString error;
    if (m_everythingClient && m_everythingClient->ensureEverythingRunning(filePath, &error)) {
        m_window->setStatusText(QStringLiteral("Everything executable set to %1 and service is reachable").arg(filePath));
        QMessageBox::information(m_window, "Everything",
            QStringLiteral("Everything is available.\nExecutable: %1").arg(filePath));
    } else {
        m_window->setStatusText(QStringLiteral("Everything executable set to %1 (not reachable)").arg(filePath));
        QMessageBox::warning(m_window, "Everything",
            error.isEmpty() ? QStringLiteral("Everything is not reachable.") : error);
    }
}

void AppController::handleTestEverythingRequest()
{
    if (!m_window || !m_everythingClient) {
        return;
    }

    const QString libraryPath = m_everythingClient->libraryPath();
    if (!m_everythingClient->isLibraryLoaded()) {
        const QString message = QStringLiteral("Everything SDK library could not be loaded.\n%1\n%2")
                                    .arg(libraryPath, m_everythingClient->availabilityError());
        m_window->setStatusText(QStringLiteral("Everything unavailable: %1").arg(m_everythingClient->availabilityError()));
        QMessageBox::warning(m_window, "Everything", message);
        return;
    }

    QString error;
    if (m_everythingClient->testConnection(&error)) {
        m_window->setStatusText(QStringLiteral("Everything is available (%1)").arg(libraryPath));
        QMessageBox::information(m_window, "Everything",
            QStringLiteral("Everything is available.\nSDK: %1\nService: reachable").arg(libraryPath));
        return;
    }

    const QString executablePath = m_configService->resolvedEverythingExecutablePath();
    if (!executablePath.isEmpty() && m_everythingClient->ensureEverythingRunning(executablePath, &error)) {
        m_window->setStatusText(QStringLiteral("Everything is available (started %1)").arg(executablePath));
        QMessageBox::information(m_window, "Everything",
            QStringLiteral("Everything was started and is now available.\nExecutable: %1").arg(executablePath));
        return;
    }

    const QString message = QStringLiteral("Everything is not reachable.\nSDK: %1\n%2")
                                .arg(libraryPath, error.isEmpty() ? QStringLiteral("Everything service is not running.") : error);
    m_window->setStatusText(QStringLiteral("Everything unavailable: %1").arg(error));
    QMessageBox::warning(m_window, "Everything", message);
}

void AppController::handleExportDetailsCsvRequest()
{
    if (!m_window || !m_window->detailsTablePanel()) {
        return;
    }

    const QString suggested = QDir(QDir(m_activeFolderPath.isEmpty() ? m_currentResult.rootPath : m_activeFolderPath).absolutePath())
                                  .filePath(QStringLiteral("opentree-details.csv"));
    QString filePath = QFileDialog::getSaveFileName(m_window, "Export details as CSV", suggested, "CSV files (*.csv)");
    if (filePath.isEmpty()) {
        return;
    }
    if (!filePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        filePath += QStringLiteral(".csv");
    }

    QString error;
    if (m_window->detailsTablePanel()->exportCsv(filePath, &error)) {
        m_window->setStatusText(QStringLiteral("Exported details table to %1").arg(filePath));
    } else {
        QMessageBox::warning(m_window, "Export CSV", error.isEmpty() ? QStringLiteral("Export failed.") : error);
    }
}

void AppController::handleExportReportRequest(const QString &format)
{
    if (!m_window) {
        return;
    }

    if (m_currentResult.rootPath.isEmpty()) {
        QMessageBox::information(m_window, QStringLiteral("Export report"), QStringLiteral("Scan a folder first."));
        return;
    }

    const bool pdf = format.compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
    const QString extension = pdf ? QStringLiteral("pdf") : QStringLiteral("html");
    const QString suggested = QDir(m_currentResult.rootPath)
                                  .filePath(QStringLiteral("opentree-report.") + extension);
    const QString filter = pdf ? QStringLiteral("PDF files (*.pdf)") : QStringLiteral("HTML files (*.html)");

    QString filePath = QFileDialog::getSaveFileName(m_window, QStringLiteral("Export report"), suggested, filter);
    if (filePath.isEmpty()) {
        return;
    }
    if (!filePath.endsWith(QLatin1Char('.') + extension, Qt::CaseInsensitive)) {
        filePath += QLatin1Char('.') + extension;
    }

    QString error;
    const bool ok = pdf
        ? ReportService::writePdfReport(filePath, m_currentResult, {}, &error)
        : ReportService::writeHtmlReport(filePath, m_currentResult, {}, &error);

    if (ok) {
        m_window->setStatusText(QStringLiteral("Report exported to %1").arg(filePath));
    } else {
        QMessageBox::warning(m_window, QStringLiteral("Export report"),
                             error.isEmpty() ? QStringLiteral("Export failed.") : error);
    }
}

void AppController::handleSettingsRequest()
{
    if (!m_window) {
        return;
    }

    SettingsDialog dialog(m_configService, m_window->availableThemes(), m_window);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Push the new settings into the running UI.
    applyCurrentTheme();
    applyViewMetric(m_configService->viewMetric());
    m_otherThresholdPercent = m_configService->othersThresholdPercent();
    if (GraphPanel *graph = m_window->existingGraphPanel()) {
        graph->setMaxNodes(m_configService->graphMaxNodes());
        graph->setFollowTreeExpansion(m_configService->graphFollowTreeExpansion());
        graph->setOtherThresholdPercent(m_otherThresholdPercent);
    }
    m_window->setCloseToTrayEnabled(m_configService->closeToTray());
    m_window->setUseEverythingChecked(m_configService->useEverything());

    // Keep the scheduled task in sync with the snapshot settings.
    QString error;
    if (!TaskSchedulerUtils::syncSnapshotTask(
            QCoreApplication::applicationFilePath(),
            m_configService->snapshotScheduleEnabled(),
            m_configService->snapshotScheduleMode(),
            m_configService->snapshotScheduleTime(),
            &error)) {
        QMessageBox::warning(m_window, QStringLiteral("Settings"),
                             error.isEmpty() ? QStringLiteral("Failed to update the scheduled task.") : error);
    }

    m_window->setStatusText(QStringLiteral("Settings updated"));
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

void AppController::handleSnapshotManagementRequest()
{
    if (!m_window || !m_snapshotService) {
        return;
    }

    SnapshotManagerDialog dialog(m_snapshotService, m_window);
    dialog.exec();
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

    if (m_window->isTimelineTabVisible()) {
        m_window->timelinePanel()->setCompareResult(compare, rows, events);
        return;
    }

    updateGraphPanel(m_window->existingGraphPanel(), [&](GraphPanel *graph) {
        graph->beginBatchUpdate();
        graph->setGraphData(m_currentResult.rootPath, m_currentResult.treeEntries, rows);
        graph->setSelectedPath(m_activeFolderPath.isEmpty() ? m_currentResult.rootPath : m_activeFolderPath);
        if (!m_activeFolderPath.isEmpty()) {
            graph->setGraphRootPath(m_activeFolderPath);
        }
        graph->endBatchUpdate();
    });
    m_window->heatmapPanel()->setActiveFolderPath(m_activeFolderPath);
    m_window->heatmapPanel()->setHeatmapData(m_currentResult.treeEntries, rows);
    m_window->setStatusText(QStringLiteral("Compared snapshot from %1").arg(compare.snapshotCreatedAt));
}

bool AppController::loadCachedRootResult(const QString &rootPath, ScanResult *result, QString *errorMessage) const
{
    if (!result) {
        return false;
    }
    *result = {};
    const QString normalizedRoot = normalizeRootKey(rootPath);
    if (!m_folderRepository || !m_fileRepository || normalizedRoot.isEmpty()) {
        return false;
    }

    QString folderError;
    const QVector<FolderEntry> folders = m_folderRepository->loadByRoot(normalizedRoot, &folderError);
    if (!folderError.isEmpty()) {
        if (errorMessage) {
            *errorMessage = folderError;
        }
        return false;
    }

    if (folders.isEmpty()) {
        return false;
    }

    *result = ScanService::buildTreeResult(normalizedRoot, folders, {});
    result->usedEverything = false;
    return true;
}

}
