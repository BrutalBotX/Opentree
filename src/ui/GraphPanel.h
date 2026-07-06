#pragma once

#include <QWidget>

#include "services/SnapshotService.h"
#include "domain/ScanTypes.h"

#include <QVector>

QT_FORWARD_DECLARE_CLASS(QCompleter)
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

signals:
    void entryActivated(const TreeEntry &entry);
    void entryOpened(const TreeEntry &entry);
    void pathEntered(const QString &path);
    void viewInTabRequested(const TreeEntry &entry, const QString &tabName);

public slots:
    Q_INVOKABLE
    void activateNode(const QString &path);
    Q_INVOKABLE
    void openNode(const QString &path);

private slots:
    void handleAddressSubmitted();

private:
    void showNodeContextMenu(const QString &nodeId, int screenX, int screenY);
    const TreeEntry *findEntryByPath(const QString &path) const;
    QString buildEmptyHtml() const;
    QString buildHtml() const;
    QString buildGraphPayload(const QString &rootPath, const QVector<TreeEntry> &entries, const QVector<SnapshotCompareRow> &compareRows) const;
    void renderGraph();

    QLineEdit *m_addressBar;
    QLabel *m_summaryLabel;

#if defined(OPENTREE_HAVE_WEBENGINE)
    GraphBridge *m_bridge;
    QWebChannel *m_channel;
    QWebEngineView *m_view;
#else
    QTextBrowser *m_view;
#endif

    QString m_currentRootPath;
    QVector<TreeEntry> m_currentEntries;
    QVector<SnapshotCompareRow> m_currentCompareRows;
    QString m_graphRootPath;
    QString m_selectedPath;
    QStringList m_visiblePaths;
    bool m_suspendRender = false;
    NodeSizeMode m_nodeSizeMode = NodeSizeMode::Size;
    double m_otherThresholdPercent = 1.0;
};

}
