#include "app/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
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
    , m_graphPlaceholder(nullptr)
    , m_centralContainer(nullptr)
    , m_mainSplitter(nullptr)
    , m_rightSplitter(nullptr)
{
    m_scanAction = new QAction("Scan Folder", this);
    m_createSnapshotAction = new QAction("Create Snapshot", this);
    m_compareSnapshotAction = new QAction("Compare Snapshot", this);
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
    resize(1400, 900);
    setMinimumSize(1100, 720);

    auto *fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(m_scanAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);

    auto *snapshotMenu = menuBar()->addMenu("Snapshots");
    snapshotMenu->addAction(m_createSnapshotAction);
    snapshotMenu->addAction(m_compareSnapshotAction);
    snapshotMenu->addSeparator();
    snapshotMenu->addAction(m_snapshotSettingsAction);

    auto *toolsMenu = menuBar()->addMenu("Tools");
    toolsMenu->addAction(m_locateEverythingAction);

    auto *viewMenu = menuBar()->addMenu("View");
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
    connect(m_reloadThemesAction, &QAction::triggered, this, &MainWindow::reloadThemesRequested);

    auto *viewMetricGroup = new QActionGroup(this);
    viewMetricGroup->setExclusive(true);
    m_viewBySizeAction->setCheckable(true);
    m_viewByPercentageAction->setCheckable(true);
    m_viewByFilesAction->setCheckable(true);
    viewMetricGroup->addAction(m_viewBySizeAction);
    viewMetricGroup->addAction(m_viewByPercentageAction);
    viewMetricGroup->addAction(m_viewByFilesAction);
    m_viewBySizeAction->setChecked(true);

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
