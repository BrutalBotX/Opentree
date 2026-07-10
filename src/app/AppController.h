#pragma once

#include <QObject>

#include <QMap>

#include "domain/ScanTypes.h"
#include "ui/ThemeManager.h"

namespace opentree {

class ConfigService;
class DatabaseManager;
class EverythingClient;
class FileRepository;
class FolderRepository;
class FolderTreeModel;
class MainWindow;
class ScanService;
class SnapshotService;
struct SnapshotCompareRow;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    void attachWindow(MainWindow *window);
    void openPath(const QString &path);

private:
    void handleScanRequest();
    void handleCreateSnapshotRequest();
    void handleCompareSnapshotMenuRequest();
    void handleScanFinished();
    void handleScanFailed(const QString &message);
    void handleEntryActivated(const TreeEntry &entry);
    void handleChartEntryActivated(const TreeEntry &entry);
    void handleChartOpenInGraphRequested(const TreeEntry &entry);
    void handleChartOpenRequested(const TreeEntry &entry);
    void handleChartShowInExplorerRequested(const TreeEntry &entry);
    void handleChartCopyPathRequested(const TreeEntry &entry);
    void handleGraphEntryActivated(const TreeEntry &entry);
    void handleGraphEntryOpened(const TreeEntry &entry);
    void handleGraphPathEntered(const QString &path);
    void handleGraphNodeActivated(const QString &path);
    void handleNavigateToEntryRequested(const TreeEntry &entry, const QString &targetTab);
    void handleOthersThresholdRequest();
    void handleRescanCurrentRootRequest();
    void handleClearAllRootsRequest();
    void handleRecentRootRequested(const QString &path);
    void handleOpenCurrentInExplorerRequest();
    void handleOpenTerminalHereRequest();
    void handleCopyCurrentPathRequest();
    void handleOpenConfigFolderRequest();
    void handleOpenLogFileRequest();
    void handleExpandAllRequest();
    void handleCollapseAllRequest();
    void handleTreemapDepthChanged(int depth);
    void handleThemeSelected(const QString &themeId);
    void reloadThemes();
    void applyCurrentTheme();
    void applyViewMetric(ViewMetric metric);
    void syncActiveResultUi(const ScanResult &result, const QString &activeFolderPath, const QVector<SnapshotCompareRow> &compareRows, bool resetCompare);
    void syncFolderFocusUi(const TreeEntry &entry, bool showGraphTab);
    void syncFileSelectionUi(const TreeEntry &entry);
    void handleNavigatePath(const QString &path, bool showGraphTab);
    RootSession *findRootSessionForPath(const QString &path);
    const RootSession *findRootSessionForPath(const QString &path) const;
    void activateRootSession(const QString &rootPath, bool showGraphTab);
    void focusFolderPath(const QString &path, bool showGraphTab);
    const TreeEntry *findTreeEntry(const QString &path) const;
    void handleLocateEverythingRequest();
    void handleSnapshotSettingsRequest();
    void handleSnapshotManagementRequest();
    void handleCompareSnapshotRequest(int snapshotId);
    void refreshTimeline();
    void refreshRecentRoots();
    bool loadCachedRootResult(const QString &rootPath, ScanResult *result, QString *errorMessage = nullptr) const;

    QString m_activeFolderPath;
    QString m_lastRequestedRootPath;
    ScanResult m_currentResult;
    QVector<RootSession> m_rootSessions;
    double m_otherThresholdPercent = 1.0;
    ViewMetric m_viewMetric = ViewMetric::Size;
    QMap<QString, ThemeDefinition> m_themes;

    MainWindow *m_window = nullptr;
    ConfigService *m_configService;
    DatabaseManager *m_databaseManager;
    EverythingClient *m_everythingClient;
    ScanService *m_scanService;
    SnapshotService *m_snapshotService = nullptr;
    FolderRepository *m_folderRepository = nullptr;
    FileRepository *m_fileRepository = nullptr;
    FolderTreeModel *m_treeModel;
};

}
