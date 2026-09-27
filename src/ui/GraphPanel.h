#pragma once

#include <QColor>
#include <QWidget>

#include "services/SnapshotService.h"
#include "domain/ScanTypes.h"

#include <QVector>

QT_FORWARD_DECLARE_CLASS(QCompleter)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QFileSystemModel)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QLineEdit)

#if defined(OPENTREE_HAVE_WEBENGINE)
QT_FORWARD_DECLARE_CLASS(QWebChannel)
QT_FORWARD_DECLARE_CLASS(QWebEngineView)
#else
QT_FORWARD_DECLARE_CLASS(QTextBrowser)
#endif

namespace opentree {

class GraphBridge : public QObject {
    Q_OBJECT

public:
    explicit GraphBridge(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    std::function<void(const QString &)> onActivate;
    std::function<void(const QString &)> onOpen;
    std::function<void(const QString &, int, int)> onContextMenu;

    Q_INVOKABLE void activateNode(const QString &path)
    {
        if (onActivate) {
            onActivate(path);
        }
    }

    Q_INVOKABLE void openNode(const QString &path)
    {
        if (onOpen) {
            onOpen(path);
        }
    }

    Q_INVOKABLE void contextMenuNode(const QString &path, int screenX, int screenY)
    {
        if (onContextMenu) {
            onContextMenu(path, screenX, screenY);
        }
    }
};

class GraphPanel : public QWidget {
    Q_OBJECT

public:
    enum class NodeSizeMode {
        Size,
        Files,
        Folders,
    };

    explicit GraphPanel(QWidget *parent = nullptr);

    void setGraphData(const QString &rootPath, const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows);
    void setNodeSizeMode(NodeSizeMode mode);
    void setGraphRootPath(const QString &path);
    void setSelectedPath(const QString &path);
    void setVisiblePaths(const QStringList &paths);
    void setOtherThresholdPercent(double percent);
    void setFollowTreeExpansion(bool follow);
    bool followTreeExpansion() const;
    void setMaxNodes(int maxNodes);
    void beginBatchUpdate();
    void endBatchUpdate();
    // Applies the application theme to the graph page (chrome colours, labels, edges).
    void setThemePalette(const QPalette &palette);
    // "neutral" (plain discs, the default) or "planets" (the space theme's spheres).
    void setGraphStyle(const QString &style);
    // Full graph page (HTML + payload); used by renderGraph and the --dump-graph-html tool.
    QString debugHtml() const;

signals:
    void entryActivated(const TreeEntry &entry);
    void entryOpened(const TreeEntry &entry);
    void pathEntered(const QString &path);
    void viewInTabRequested(const TreeEntry &entry, const QString &tabName);
    void nodeActivated(const QString &path);
    void followTreeExpansionChanged(bool follow);

public slots:    Q_INVOKABLE
    void activateNode(const QString &path);
    Q_INVOKABLE
    void openNode(const QString &path);

private slots:
    void handleAddressSubmitted();

protected:
    // The WebEngine view is created only when the Graph tab is actually shown, and its
    // renderer is discarded again when the tab is left. Loading Chromium costs a few hundred
    // megabytes, so it must not happen for users who never open the graph.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void showNodeContextMenu(const QString &nodeId, int screenX, int screenY);
    void ensureView();
    void releaseView();
    const TreeEntry *findEntryByPath(const QString &path) const;
    QString buildEmptyHtml() const;
    QString buildHtml() const;
    QString nodeSizeModeLabel() const;
    QString buildGraphPayload(const QString &rootPath, const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows) const;
    void markGraphDirty();
    void renderGraph();
    void refreshSelectionVisuals();
    void updateSelectionInView();

    QLineEdit *m_addressBar;
    QCheckBox *m_followTreeCheck;
    QLabel *m_summaryLabel;

#if defined(OPENTREE_HAVE_WEBENGINE)
    GraphBridge *m_bridge;
    QWebChannel *m_channel;
    QWebEngineView *m_view = nullptr;   // created lazily on first show
#else
    QTextBrowser *m_view = nullptr;     // created lazily on first show
#endif

    QString m_currentRootPath;
    QVector<TreeEntry> m_currentEntries;
    QVector<SnapshotCompareRow> m_currentCompareRows;
    QString m_graphRootPath;
    QString m_selectedPath;
    QStringList m_visiblePaths;
    bool m_suspendRender = false;
    int m_batchDepth = 0;
    bool m_renderDirty = false;
    bool m_followTreeExpansion = true;
    int m_maxNodes = 120;
    QString m_renderedRoot;
    int m_renderedEntryCount = -1;
    NodeSizeMode m_nodeSizeMode = NodeSizeMode::Size;
    // Defaults match the neutral dark theme; AppController pushes the real palette in.
    QColor m_themeWindow = QColor("#1e1f22");
    QColor m_themeBase = QColor("#26272b");
    QColor m_themeText = QColor("#e6e6e8");
    QColor m_themeAccent = QColor("#4c5d73");
    QColor m_themeBorder = QColor("#3a3c42");
    QString m_graphStyle = QStringLiteral("neutral");
    double m_otherThresholdPercent = 1.0;
};

}
