#include "app/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QInputDialog>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/DetailsPanel.h"
#include "ui/ChartPanel.h"
#include "ui/ExtensionsPanel.h"
#include "ui/GraphPanel.h"
#include "ui/HeatmapPanel.h"
#include "ui/TimelinePanel.h"
#include "ui/TreePanel.h"
namespace opentree {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_treePanel(nullptr)
    , m_detailsPanel(nullptr)
    , m_chartPanel(nullptr)
    , m_extensionsPanel(nullptr)
    , m_graphPanel(nullptr)
    , m_heatmapPanel(nullptr)
    , m_timelinePanel(nullptr)
    , m_scanAction(nullptr)
    , m_createSnapshotAction(nullptr)
    , m_compareSnapshotAction(nullptr)
    , m_rescanCurrentRootAction(nullptr)
    , m_refreshAction(nullptr)
    , m_clearAllRootsAction(nullptr)
    , m_openCurrentInExplorerAction(nullptr)
    , m_openTerminalHereAction(nullptr)
    , m_copyCurrentPathAction(nullptr)
    , m_openConfigFolderAction(nullptr)
    , m_openLogFileAction(nullptr)
    , m_showGraphTabAction(nullptr)
    , m_showChartTabAction(nullptr)
    , m_showExtensionsTabAction(nullptr)
    , m_showHeatmapTabAction(nullptr)
    , m_showTimelineTabAction(nullptr)
    , m_expandAllAction(nullptr)
    , m_collapseAllAction(nullptr)
    , m_treemapDepth1Action(nullptr)
    , m_treemapDepth2Action(nullptr)
    , m_treemapDepth3Action(nullptr)
    , m_viewBySizeAction(nullptr)
    , m_viewByPercentageAction(nullptr)
    , m_viewByFilesAction(nullptr)
    , m_setOthersThresholdAction(nullptr)
    , m_exitAction(nullptr)
    , m_locateEverythingAction(nullptr)
    , m_snapshotSettingsAction(nullptr)
    , m_reloadThemesAction(nullptr)
    , m_statusLabel(nullptr)
    , m_progressBar(nullptr)
    , m_tabs(nullptr)
    , m_mainToolBar(nullptr)
    , m_graphPlaceholder(nullptr)
    , m_centralContainer(nullptr)
    , m_mainSplitter(nullptr)
    , m_rightSplitter(nullptr)
{
    m_scanAction = new QAction("Scan Folder", this);
    m_createSnapshotAction = new QAction("Create Snapshot", this);
    m_compareSnapshotAction = new QAction("Compare Snapshot", this);
    m_rescanCurrentRootAction = new QAction("Rescan Current Root", this);
    m_refreshAction = new QAction("Refresh", this);
    m_clearAllRootsAction = new QAction("Clear All Roots", this);
    m_openCurrentInExplorerAction = new QAction("Open in Explorer", this);
    m_openTerminalHereAction = new QAction("Open Terminal Here", this);
    m_copyCurrentPathAction = new QAction("Copy Current Path", this);
    m_openConfigFolderAction = new QAction("Open Config Folder", this);
    m_openLogFileAction = new QAction("Open Log File", this);
    m_showGraphTabAction = new QAction("Graph", this);
    m_showChartTabAction = new QAction("Chart", this);
    m_showExtensionsTabAction = new QAction("Extensions", this);
    m_showHeatmapTabAction = new QAction("Heatmap", this);
    m_showTimelineTabAction = new QAction("Timeline", this);
    m_expandAllAction = new QAction("Expand All", this);
    m_collapseAllAction = new QAction("Collapse All", this);
    m_treemapDepth1Action = new QAction("Depth 1", this);
    m_treemapDepth2Action = new QAction("Depth 2", this);
    m_treemapDepth3Action = new QAction("Depth 3", this);
    m_viewBySizeAction = new QAction("View by Size", this);
    m_viewByPercentageAction = new QAction("View by Percentage", this);
    m_viewByFilesAction = new QAction("View by File Count", this);
    m_setOthersThresholdAction = new QAction("Set Others Threshold...", this);
    m_exitAction = new QAction("Exit", this);
    m_locateEverythingAction = new QAction("Locate Everything SDK", this);
    m_snapshotSettingsAction = new QAction("Snapshot Settings", this);
    m_reloadThemesAction = new QAction("Reload Themes", this);
    m_statusLabel = new QLabel(this);
    m_progressBar = new QProgressBar(this);
    m_tabs = new QTabWidget(this);
    m_mainToolBar = new QToolBar(this);
    m_graphPlaceholder = new QWidget(this);
    m_centralContainer = new QWidget(this);
    m_mainSplitter = new QSplitter(this);
    m_rightSplitter = new QSplitter(this);
    m_treePanel = new TreePanel(this);
    m_detailsPanel = new DetailsPanel(this);
    m_chartPanel = new ChartPanel(this);
    m_extensionsPanel = new ExtensionsPanel(this);
    m_heatmapPanel = new HeatmapPanel(this);
    m_timelinePanel = new TimelinePanel(this);

    setWindowTitle("OpenTree");
    setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.ico")));
    resize(1400, 900);
    setMinimumSize(1100, 720);

    auto *fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(m_scanAction);
    m_recentRootsMenu = fileMenu->addMenu("Recent Roots");
    fileMenu->addAction(m_rescanCurrentRootAction);
    fileMenu->addAction(m_clearAllRootsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);

    auto *snapshotMenu = menuBar()->addMenu("Snapshots");
    snapshotMenu->addAction(m_createSnapshotAction);
    snapshotMenu->addAction(m_compareSnapshotAction);
    snapshotMenu->addSeparator();
    snapshotMenu->addAction(m_snapshotSettingsAction);

    auto *toolsMenu = menuBar()->addMenu("Tools");
    toolsMenu->addAction(m_locateEverythingAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_openCurrentInExplorerAction);
    toolsMenu->addAction(m_openTerminalHereAction);
    toolsMenu->addAction(m_copyCurrentPathAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_openConfigFolderAction);
    toolsMenu->addAction(m_openLogFileAction);

    auto *viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(m_refreshAction);
    viewMenu->addAction(m_expandAllAction);
    viewMenu->addAction(m_collapseAllAction);
    viewMenu->addSeparator();
    auto *tabsMenu = viewMenu->addMenu("Tabs");
    tabsMenu->addAction(m_showGraphTabAction);
    tabsMenu->addAction(m_showChartTabAction);
    tabsMenu->addAction(m_showExtensionsTabAction);
    tabsMenu->addAction(m_showHeatmapTabAction);
    tabsMenu->addAction(m_showTimelineTabAction);
    m_treemapMenu = viewMenu->addMenu("Treemap Depth");
    m_treemapMenu->addAction(m_treemapDepth1Action);
    m_treemapMenu->addAction(m_treemapDepth2Action);
    m_treemapMenu->addAction(m_treemapDepth3Action);
    viewMenu->addSeparator();
    viewMenu->addAction(m_viewBySizeAction);
    viewMenu->addAction(m_viewByPercentageAction);
    viewMenu->addAction(m_viewByFilesAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_setOthersThresholdAction);
    m_themeMenu = viewMenu->addMenu("Theme");
    m_themeMenu->addAction(m_reloadThemesAction);

    auto *helpMenu = menuBar()->addMenu("Help");
    helpMenu->addAction("About OpenTree", this, [this]() {
        QMessageBox::information(this, "About OpenTree", "OpenTree\nDisk explorer and snapshot viewer.");
    });

    connect(m_scanAction, &QAction::triggered, this, &MainWindow::scanRequested);
    connect(m_rescanCurrentRootAction, &QAction::triggered, this, &MainWindow::rescanCurrentRootRequested);
    connect(m_refreshAction, &QAction::triggered, this, &MainWindow::rescanCurrentRootRequested);
    connect(m_clearAllRootsAction, &QAction::triggered, this, &MainWindow::clearAllRootsRequested);
    connect(m_createSnapshotAction, &QAction::triggered, this, [this]() {
        setStatusText("Create Snapshot clicked");
        emit createSnapshotRequested();
    });
    connect(m_compareSnapshotAction, &QAction::triggered, this, [this]() {
        setStatusText("Compare Snapshot clicked");
        emit compareSnapshotRequested();
    });
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);
    connect(m_locateEverythingAction, &QAction::triggered, this, &MainWindow::everythingLocationRequested);
    connect(m_snapshotSettingsAction, &QAction::triggered, this, &MainWindow::snapshotSettingsRequested);
    connect(m_setOthersThresholdAction, &QAction::triggered, this, &MainWindow::othersThresholdRequested);
    connect(m_openCurrentInExplorerAction, &QAction::triggered, this, &MainWindow::openCurrentInExplorerRequested);
    connect(m_openTerminalHereAction, &QAction::triggered, this, &MainWindow::openTerminalHereRequested);
    connect(m_copyCurrentPathAction, &QAction::triggered, this, &MainWindow::copyCurrentPathRequested);
    connect(m_openConfigFolderAction, &QAction::triggered, this, &MainWindow::openConfigFolderRequested);
    connect(m_openLogFileAction, &QAction::triggered, this, &MainWindow::openLogFileRequested);
    connect(m_expandAllAction, &QAction::triggered, this, &MainWindow::expandAllRequested);
    connect(m_collapseAllAction, &QAction::triggered, this, &MainWindow::collapseAllRequested);
    connect(m_treemapDepth1Action, &QAction::triggered, this, [this]() { emit treemapDepthChanged(1); });
    connect(m_treemapDepth2Action, &QAction::triggered, this, [this]() { emit treemapDepthChanged(2); });
    connect(m_treemapDepth3Action, &QAction::triggered, this, [this]() { emit treemapDepthChanged(3); });
    connect(m_reloadThemesAction, &QAction::triggered, this, &MainWindow::reloadThemesRequested);
    connect(m_showGraphTabAction, &QAction::triggered, this, [this]() { showGraphTab(); });
    connect(m_showChartTabAction, &QAction::triggered, this, [this]() { showChartTab(); });
    connect(m_showExtensionsTabAction, &QAction::triggered, this, [this]() { showExtensionsTab(); });
    connect(m_showHeatmapTabAction, &QAction::triggered, this, [this]() { showHeatmapTab(); });
    connect(m_showTimelineTabAction, &QAction::triggered, this, [this]() { showTimelineTab(); });

    auto *viewMetricGroup = new QActionGroup(this);
    viewMetricGroup->setExclusive(true);
    m_viewBySizeAction->setCheckable(true);
    m_viewByPercentageAction->setCheckable(true);
    m_viewByFilesAction->setCheckable(true);
    viewMetricGroup->addAction(m_viewBySizeAction);
    viewMetricGroup->addAction(m_viewByPercentageAction);
    viewMetricGroup->addAction(m_viewByFilesAction);
    m_viewBySizeAction->setChecked(true);

    auto *treemapDepthGroup = new QActionGroup(this);
    treemapDepthGroup->setExclusive(true);
    m_treemapDepth1Action->setCheckable(true);
    m_treemapDepth2Action->setCheckable(true);
    m_treemapDepth3Action->setCheckable(true);
    treemapDepthGroup->addAction(m_treemapDepth1Action);
    treemapDepthGroup->addAction(m_treemapDepth2Action);
    treemapDepthGroup->addAction(m_treemapDepth3Action);
    m_treemapDepth1Action->setChecked(true);

    m_scanAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_copyCurrentPathAction->setShortcut(QKeySequence::Copy);
    m_showGraphTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    m_showChartTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    m_showExtensionsTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    m_showHeatmapTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+4")));
    m_showTimelineTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+5")));

    addToolBar(Qt::TopToolBarArea, m_mainToolBar);
    m_mainToolBar->setMovable(false);
    m_mainToolBar->addAction(m_scanAction);
    m_mainToolBar->addAction(m_rescanCurrentRootAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_createSnapshotAction);
    m_mainToolBar->addAction(m_compareSnapshotAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_openCurrentInExplorerAction);
    m_mainToolBar->addAction(m_copyCurrentPathAction);

    m_tabs->setContentsMargins(8, 8, 8, 8);
    m_tabs->addTab(m_graphPlaceholder, "Graph");
    m_tabs->addTab(m_chartPanel, "Chart");
    m_tabs->addTab(m_extensionsPanel, "Extensions");
    m_tabs->addTab(m_heatmapPanel, "Heatmap");
    m_tabs->addTab(m_timelinePanel, "Timeline");
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0) {
            ensureGraphPanel();
            QTimer::singleShot(0, this, [this]() {
                emit graphTabActivated();
            });
        }
    });

    m_mainSplitter->addWidget(m_treePanel);

    m_rightSplitter->addWidget(m_tabs);
    m_rightSplitter->addWidget(m_detailsPanel);
    m_rightSplitter->setStretchFactor(0, 3);
    m_rightSplitter->setStretchFactor(1, 1);
    m_rightSplitter->setChildrenCollapsible(false);

    m_mainSplitter->addWidget(m_rightSplitter);
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setChildrenCollapsible(false);

    auto *centralLayout = new QHBoxLayout(m_centralContainer);
    centralLayout->setContentsMargins(14, 10, 14, 10);
    centralLayout->setSpacing(10);
    centralLayout->addWidget(m_mainSplitter);
    setCentralWidget(m_centralContainer);

    m_mainSplitter->setHandleWidth(8);
    m_rightSplitter->setHandleWidth(8);

    m_treePanel->setMinimumWidth(260);
    m_tabs->setMinimumWidth(420);
    m_detailsPanel->setMinimumWidth(260);
    m_mainSplitter->setSizes({320, 1080});
    m_rightSplitter->setSizes({820, 260});

    m_treePanel->setContentsMargins(0, 8, 0, 0);
    m_detailsPanel->setContentsMargins(0, 22, 0, 0);

    m_progressBar->setRange(0, 100);
    m_progressBar->setMinimumWidth(240);
    m_progressBar->setTextVisible(true);
    m_progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_progressBar->setVisible(false);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progressBar);
    setStatusText("Ready");
}

void MainWindow::ensureGraphPanel()
{
    if (m_graphPanel) {
        return;
    }

    m_graphPanel = new GraphPanel(this);
    connect(m_graphPanel, &GraphPanel::viewInTabRequested, this, [this](const TreeEntry &entry, const QString &tabName) {
        emit navigateToEntryRequested(entry, tabName);
    });
    m_tabs->removeTab(0);
    m_tabs->insertTab(0, m_graphPanel, "Graph");
    if (m_graphPlaceholder) {
        m_graphPlaceholder->deleteLater();
        m_graphPlaceholder = nullptr;
    }
}

TreePanel *MainWindow::treePanel() const
{
    return m_treePanel;
}

QAction *MainWindow::createSnapshotAction() const
{
    return m_createSnapshotAction;
}

QAction *MainWindow::compareSnapshotAction() const
{
    return m_compareSnapshotAction;
}

QAction *MainWindow::viewBySizeAction() const
{
    return m_viewBySizeAction;
}

QAction *MainWindow::viewByPercentageAction() const
{
    return m_viewByPercentageAction;
}

QAction *MainWindow::viewByFilesAction() const
{
    return m_viewByFilesAction;
}

QAction *MainWindow::treemapDepth1Action() const
{
    return m_treemapDepth1Action;
}

QAction *MainWindow::treemapDepth2Action() const
{
    return m_treemapDepth2Action;
}

QAction *MainWindow::treemapDepth3Action() const
{
    return m_treemapDepth3Action;
}


DetailsPanel *MainWindow::detailsPanel() const
{
    return m_detailsPanel;
}

ChartPanel *MainWindow::chartPanel() const
{
    return m_chartPanel;
}

ExtensionsPanel *MainWindow::extensionsPanel() const
{
    return m_extensionsPanel;
}

GraphPanel *MainWindow::existingGraphPanel() const
{
    return m_graphPanel;
}

GraphPanel *MainWindow::graphPanel()
{
    ensureGraphPanel();
    return m_graphPanel;
}

HeatmapPanel *MainWindow::heatmapPanel() const
{
    return m_heatmapPanel;
}

TimelinePanel *MainWindow::timelinePanel() const
{
    return m_timelinePanel;
}

int MainWindow::currentTabIndex() const
{
    return m_tabs ? m_tabs->currentIndex() : -1;
}

void MainWindow::showGraphTab()
{
    ensureGraphPanel();
    m_tabs->setCurrentWidget(m_graphPanel);
}

void MainWindow::showChartTab()
{
    m_tabs->setCurrentWidget(m_chartPanel);
}

void MainWindow::showExtensionsTab()
{
    m_tabs->setCurrentWidget(m_extensionsPanel);
}

void MainWindow::showHeatmapTab()
{
    m_tabs->setCurrentWidget(m_heatmapPanel);
}

void MainWindow::showTimelineTab()
{
    m_tabs->setCurrentWidget(m_timelinePanel);
}

void MainWindow::setRecentRoots(const QStringList &roots)
{
    m_recentRoots = roots;
    rebuildRecentRootsMenu();
}

void MainWindow::rebuildRecentRootsMenu()
{
    if (!m_recentRootsMenu) {
        return;
    }

    m_recentRootsMenu->clear();
    for (const QString &root : m_recentRoots) {
        QAction *action = m_recentRootsMenu->addAction(root);
        connect(action, &QAction::triggered, this, [this, root]() {
            emit recentRootRequested(root);
        });
    }
    if (m_recentRoots.isEmpty()) {
        QAction *emptyAction = m_recentRootsMenu->addAction("(none)");
        emptyAction->setEnabled(false);
    }
}

void MainWindow::setAvailableThemes(const QMap<QString, QString> &themes, const QString &selectedThemeId)
{
    m_themeNames = themes;
    m_selectedThemeId = selectedThemeId;
    rebuildThemeMenu();
}

void MainWindow::rebuildThemeMenu()
{
    if (!m_themeMenu) {
        return;
    }

    m_themeMenu->clear();
    auto *themeGroup = new QActionGroup(m_themeMenu);
    themeGroup->setExclusive(true);
    for (auto it = m_themeNames.cbegin(); it != m_themeNames.cend(); ++it) {
        QAction *action = m_themeMenu->addAction(it.value());
        action->setCheckable(true);
        action->setChecked(it.key() == m_selectedThemeId);
        action->setData(it.key());
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, id = it.key()]() {
            emit themeSelected(id);
        });
    }
    m_themeMenu->addSeparator();
    m_themeMenu->addAction(m_reloadThemesAction);
}

void MainWindow::setBusy(bool busy)
{
    m_progressBar->setVisible(busy);
    m_scanAction->setEnabled(!busy);
    m_createSnapshotAction->setEnabled(!busy);
    m_compareSnapshotAction->setEnabled(!busy);
    if (busy) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
    } else {
        m_progressBar->setValue(0);
    }
}

void MainWindow::setProgress(int percent)
{
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(percent);
}

void MainWindow::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
}

}
