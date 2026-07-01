#pragma once

#include <QMainWindow>

#include <QMap>

#include <memory>

QT_FORWARD_DECLARE_CLASS(QAction)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QProgressBar)
QT_FORWARD_DECLARE_CLASS(QTabWidget)
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
    void setAvailableThemes(const QMap<QString, QString> &themes, const QString &selectedThemeId);
    void setBusy(bool busy);
    void setProgress(int percent);
    void setStatusText(const QString &text);

signals:
    void scanRequested();
    void createSnapshotRequested();
    void compareSnapshotRequested();
    void everythingLocationRequested();
    void snapshotSettingsRequested();
    void othersThresholdRequested();
    void themeSelected(const QString &themeId);
    void reloadThemesRequested();
    void graphTabActivated();

private:
    void ensureGraphPanel();
    void rebuildThemeMenu();

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
    QAction *m_viewBySizeAction;
    QAction *m_viewByPercentageAction;
    QAction *m_viewByFilesAction;
    QAction *m_setOthersThresholdAction;
    QAction *m_exitAction;
    QAction *m_locateEverythingAction;
    QAction *m_snapshotSettingsAction;
    QAction *m_reloadThemesAction;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QTabWidget *m_tabs;
    QWidget *m_graphPlaceholder;
    QWidget *m_centralContainer;
    QSplitter *m_mainSplitter;
    QSplitter *m_rightSplitter;
    QMenu *m_themeMenu = nullptr;
    QMap<QString, QString> m_themeNames;
    QString m_selectedThemeId;
};

}
