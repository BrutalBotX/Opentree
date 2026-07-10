#pragma once

#include <QMainWindow>

#include <QMap>

#include <memory>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QAction)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QMenu)
QT_FORWARD_DECLARE_CLASS(QProgressBar)
QT_FORWARD_DECLARE_CLASS(QTabWidget)
QT_FORWARD_DECLARE_CLASS(QToolBar)
QT_FORWARD_DECLARE_CLASS(QSplitter)
QT_FORWARD_DECLARE_CLASS(QWidget)

namespace opentree {

class DetailsPanel;
class ChartPanel;
class ExtensionsPanel;
class GraphPanel;
class HeatmapPanel;
class TimelinePanel;
class TreePanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    QAction *createSnapshotAction() const;
    QAction *compareSnapshotAction() const;
    QAction *viewBySizeAction() const;
    QAction *viewByPercentageAction() const;
    QAction *viewByFilesAction() const;
    QAction *treemapDepth1Action() const;
    QAction *treemapDepth2Action() const;
    QAction *treemapDepth3Action() const;
    QAction *rescanCurrentRootAction() const;
    QAction *refreshAction() const;
    TreePanel *treePanel() const;
    DetailsPanel *detailsPanel() const;
    ChartPanel *chartPanel() const;
    ExtensionsPanel *extensionsPanel() const;
    GraphPanel *existingGraphPanel() const;
    GraphPanel *graphPanel();
    HeatmapPanel *heatmapPanel() const;
    TimelinePanel *timelinePanel() const;
    int currentTabIndex() const;
    void showGraphTab();
    void showChartTab();
    void showExtensionsTab();
    void showHeatmapTab();
    void showTimelineTab();
    void setAvailableThemes(const QMap<QString, QString> &themes, const QString &selectedThemeId);
    void setRecentRoots(const QStringList &roots);
    void setBusy(bool busy);
    void setProgress(int percent);
    void setStatusText(const QString &text);
    void setTimelineMode(bool active);

signals:
    void scanRequested();
    void createSnapshotRequested();
    void compareSnapshotRequested();
    void snapshotManagementRequested();
    void everythingLocationRequested();
    void snapshotSettingsRequested();
    void othersThresholdRequested();
    void rescanCurrentRootRequested();
    void clearAllRootsRequested();
    void openCurrentInExplorerRequested();
    void openTerminalHereRequested();
    void copyCurrentPathRequested();
    void openConfigFolderRequested();
    void openLogFileRequested();
    void recentRootRequested(const QString &path);
    void expandAllRequested();
    void collapseAllRequested();
    void treemapDepthChanged(int depth);
    void themeSelected(const QString &themeId);
    void reloadThemesRequested();
    void graphTabActivated();
    void tabModeChanged(bool timelineCompareMode);
    void graphNodeActivated(const QString &path);
    void navigateToEntryRequested(const TreeEntry &entry, const QString &targetTab);

private:
    void ensureGraphPanel();
    void rebuildThemeMenu();
    void rebuildRecentRootsMenu();

    TreePanel *m_treePanel;
    DetailsPanel *m_detailsPanel;
    ChartPanel *m_chartPanel;
    ExtensionsPanel *m_extensionsPanel;
    GraphPanel *m_graphPanel;
    HeatmapPanel *m_heatmapPanel;
    TimelinePanel *m_timelinePanel;
    QAction *m_scanAction;
    QAction *m_createSnapshotAction;
    QAction *m_compareSnapshotAction;
    QAction *m_rescanCurrentRootAction;
    QAction *m_refreshAction;
    QAction *m_clearAllRootsAction;
    QAction *m_openCurrentInExplorerAction;
    QAction *m_openTerminalHereAction;
    QAction *m_copyCurrentPathAction;
    QAction *m_openConfigFolderAction;
    QAction *m_openLogFileAction;
    QAction *m_showGraphTabAction;
    QAction *m_showChartTabAction;
    QAction *m_showExtensionsTabAction;
    QAction *m_showHeatmapTabAction;
    QAction *m_showTimelineTabAction;
    QAction *m_expandAllAction;
    QAction *m_collapseAllAction;
    QAction *m_treemapDepth1Action;
    QAction *m_treemapDepth2Action;
    QAction *m_treemapDepth3Action;
    QAction *m_viewBySizeAction;
    QAction *m_viewByPercentageAction;
    QAction *m_viewByFilesAction;
    QAction *m_sizeUnitsAdaptiveAction;
    QAction *m_setOthersThresholdAction;
    QAction *m_exitAction;
    QAction *m_locateEverythingAction;
    QAction *m_snapshotSettingsAction;
    QAction *m_reloadThemesAction;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QTabWidget *m_tabs;
    QToolBar *m_mainToolBar;
    QWidget *m_graphPlaceholder;
    QWidget *m_centralContainer;
    QSplitter *m_mainSplitter;
    QSplitter *m_rightSplitter;
    QMenu *m_recentRootsMenu = nullptr;
    QMenu *m_treemapMenu = nullptr;
    QMenu *m_themeMenu = nullptr;
    QMap<QString, QString> m_themeNames;
    QString m_selectedThemeId;
    QStringList m_recentRoots;
    QList<int> m_savedRightSplitterSizes;
};

}
